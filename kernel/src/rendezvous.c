#include <unos/rendezvous.h>
#include <unos/fs_root_provider.h>
#include <unos/heap.h>
#include <unos/scheduler.h>
#include <unos/events.h>
#include <unos/message_ipc.h>
#include <string.h>
typedef struct offer {struct offer *next;fs_root_t *root;vex_web_fd_object_t *resource;
    unsigned rights,limit,label_size,traits_size,claimed,linked,transfer,refs;
    uint64_t generation;
    uint8_t label[512],traits[64];} offer;
typedef struct waiter {struct waiter *next;uint32_t tid;uint64_t scope;
    unsigned size;uint8_t label[512];volatile uint8_t woken;} waiter;
static offer *offers;static waiter *waiters;
typedef struct {offer *entry;unsigned committed;uint64_t generation;} claim_receipt;
static kernel_gate_t gate=KERNEL_GATE_INIT;
static int matches(offer *o,uint64_t scope,const uint8_t *label,unsigned size) {
    return fs_root_identity(o->root)==scope && o->label_size==size && !memcmp(o->label,label,size);
}
static void wake_scope(offer *o) {
    for(waiter *w=waiters;w;w=w->next)if(w->scope==fs_root_identity(o->root) && w->size==o->label_size && !memcmp(w->label,o->label,w->size)) {
        __atomic_store_n(&w->woken,1,__ATOMIC_RELEASE);process_wake(w->tid);
    }
}
static void destroy_offer(offer *o) {ladybird_fd_release(o->resource);fs_root_release(o->root);kfree(o);}
static void claim_close(void *opaque) {
    claim_receipt *c=opaque;offer *o=c->entry;unsigned dead;
    {KERNEL_GUARD(gate);if(!c->committed && o->linked && c->generation==o->generation){o->claimed=0;wake_scope(o);}dead=!--o->refs;}
    kfree(c);if(dead)destroy_offer(o);
}
static int lease_close(void *opaque) {
    offer *o=opaque;
    unsigned dead;
    {KERNEL_GUARD(gate);if(o->linked) {
        offer **p=&offers;while(*p && *p!=o)p=&(*p)->next;if(*p)*p=o->next;wake_scope(o);
        o->linked=0;
    }dead=!--o->refs;}
    if(dead)destroy_offer(o);return 0;
}
static const vex_web_fd_ops_t lease_ops={.close=lease_close};
int vex_rendezvous_transfer_allowed(vex_web_fd_object_t *o) {
    return !o || o->ops!=&lease_ops || ((offer *)o->ctx)->transfer;
}
void vex_rendezvous_thread_stopped(uint32_t tid) {
    KERNEL_GUARD(gate);waiter **p=&waiters;
    while(*p){if((*p)->tid==tid)*p=(*p)->next;else p=&(*p)->next;}
}
int64_t vex_rendezvous_call(vex_rendezvous_req_t *r,const uint8_t *label) {
    if(r->reserved || r->traits_size>64)return -EINVAL;
    if(r->operation==VEX_RENDEZVOUS_CANCEL_ENDPOINT || r->operation==VEX_RENDEZVOUS_LEASE_STATE || r->operation==VEX_RENDEZVOUS_REVOKE_ENDPOINT) {
        if(r->scope_fd || r->label_size || r->rights || r->flags || r->limit || r->traits_size || r->timeout_ns)return -EINVAL;
        VEX_FD_BORROW(ref);vex_web_fd_object_t *o=vex_fd_borrow_acquire(&ref,r->resource_fd);if(!o)return -EBADF;
        if(r->operation==VEX_RENDEZVOUS_CANCEL_ENDPOINT)return vex_message_fd_cancel(o);
        if(r->operation==VEX_RENDEZVOUS_REVOKE_ENDPOINT)return vex_message_fd_revoke(o);
        if(o->ops!=&lease_ops)return -EBADF;KERNEL_GUARD(gate);return ((offer *)o->ctx)->claimed;
    }
    if(r->operation==VEX_RENDEZVOUS_COMMIT) {
        if(r->scope_fd || r->label_size || r->rights || r->flags || r->limit || r->traits_size || r->timeout_ns)return -EINVAL;
        VEX_FD_BORROW(claim);vex_web_fd_object_t *o=vex_fd_borrow_acquire(&claim,r->resource_fd);
        claim_receipt *c;
        {KERNEL_GUARD(gate);c=vex_message_fd_hook_context(o,claim_close);if(!c)return -EBADF;
         if(!c->entry->linked || !c->entry->claimed || c->generation!=c->entry->generation)return -ECANCELED;
         c->committed=1;vex_message_fd_close_hook(o,NULL,NULL);}
        claim_close(c);return 0;
    }
    if(r->operation==VEX_RENDEZVOUS_ATTENUATE) {
        if(r->scope_fd || r->label_size || r->flags || r->limit || r->traits_size || r->timeout_ns)return -EINVAL;
        VEX_FD_BORROW(source);vex_web_fd_object_t *o=vex_fd_borrow_acquire(&source,r->resource_fd);if(!o)return -EBADF;
        int error;vex_web_fd_object_t *copy=vex_message_fd_clone(o,r->rights,&error);
        if(!copy)return error;int fd=ladybird_fd_install_flags(copy,-1,1);return fd<0?-*ladybird_errno_location():fd;
    }
    if(r->operation==VEX_RENDEZVOUS_REARM) {
        if(r->scope_fd || r->label_size || r->rights || r->flags || r->limit || r->traits_size || r->timeout_ns)return -EINVAL;
        VEX_FD_BORROW(lease);vex_web_fd_object_t *o=vex_fd_borrow_acquire(&lease,r->resource_fd);
        if(!o || o->ops!=&lease_ops)return -EBADF;
        KERNEL_GUARD(gate);offer *entry=o->ctx;if(entry->generation==UINT64_MAX)return -EINVAL;
        ++entry->generation;entry->claimed=0;wake_scope(entry);return 0;
    }
    if(!r->label_size || r->label_size>512 || !label ||
       (r->operation!=VEX_RENDEZVOUS_WAIT && r->timeout_ns))return -EINVAL;
    if(r->operation<VEX_RENDEZVOUS_PUBLISH || r->operation>VEX_RENDEZVOUS_WAIT)return -EINVAL;
    int error=0;VEX_FD_BORROW(scope);
    unsigned needed=r->operation==VEX_RENDEZVOUS_PUBLISH?FS_ROOT_WRITE|FS_ROOT_CREATE:FS_ROOT_READ|FS_ROOT_ENUM;
    fs_root_t *root=vex_fs_root_borrow(&scope,r->scope_fd,needed,&error);if(!root)return error;
    uint64_t identity=fs_root_identity(root);
    if(r->operation==VEX_RENDEZVOUS_PUBLISH) {
        if(!r->limit || r->limit>4096 || (r->flags&~VEX_RENDEZVOUS_EXCLUSIVE))return -EINVAL;
        VEX_FD_BORROW(source);vex_web_fd_object_t *o=vex_fd_borrow_acquire(&source,r->resource_fd);
        if(!o)return -EBADF;if(!vex_fd_transfer_allowed(o))return -EACCES;
        offer *entry=kmalloc(sizeof(*entry));if(!entry)return -ENOMEM;memset(entry,0,sizeof(*entry));
        entry->refs=1;
        entry->resource=vex_message_fd_clone(o,r->rights,&error);
        if(!entry->resource){kfree(entry);return error;}
        entry->root=fs_root_retain(root);if(!entry->root){ladybird_fd_release(entry->resource);kfree(entry);return -ENOMEM;}
        vex_fs_root_info_t info;if(vex_fs_root_query(r->scope_fd,&info)) {lease_close(entry);return -EBADF;}
        entry->transfer=(info.rights&FS_ROOT_TRANSFER)!=0;
        entry->rights=r->rights;entry->limit=r->limit;entry->label_size=r->label_size;entry->traits_size=r->traits_size;
        memcpy(entry->label,label,r->label_size);memcpy(entry->traits,r->traits,r->traits_size);
        vex_web_fd_object_t *lease=ladybird_fd_object_create(entry,&lease_ops);if(!lease){lease_close(entry);return -ENOMEM;}
        int fd=-1;
        {KERNEL_GUARD(gate);unsigned count=0;
         for(offer *a=offers;a;a=a->next)if(matches(a,identity,label,r->label_size)) {
             count++;
             if(r->flags&VEX_RENDEZVOUS_EXCLUSIVE){error=-EEXIST;break;}
             if(a->limit!=entry->limit || a->traits_size!=entry->traits_size || a->rights!=entry->rights ||
                memcmp(a->traits,entry->traits,entry->traits_size)){error=-EINVAL;break;}
         }
         if(!error && count>=r->limit)error=-EAGAIN;
         if(!error) {
             fd=ladybird_fd_install_flags(lease,-1,1);lease=NULL;
             if(fd<0)error=-*ladybird_errno_location();
             else {entry->linked=1;entry->next=offers;offers=entry;wake_scope(entry);}
         }}
        if(lease)ladybird_fd_release(lease);return error?error:fd;
    }
    if(r->flags || r->limit || r->resource_fd ||
       (r->operation!=VEX_RENDEZVOUS_CLAIM && r->rights))return -EINVAL;
    if(r->operation==VEX_RENDEZVOUS_CLAIM && (!r->rights || (r->rights&~7u)))return -EINVAL;
    uint64_t now=scheduler_time_ns(),deadline=r->timeout_ns>UINT64_MAX-now?UINT64_MAX:now+r->timeout_ns;
    for(;;) {
        waiter w={.tid=scheduler_current_tid(),.scope=identity,.size=r->label_size};memcpy(w.label,label,r->label_size);
        {KERNEL_GUARD(gate);offer *first=NULL,*ready=NULL;
         for(offer *a=offers;a;a=a->next)if(matches(a,identity,label,r->label_size)) {if(!first)first=a;if(!a->claimed){ready=a;break;}}
         if(!first)return -ENOENT;
         if(r->operation==VEX_RENDEZVOUS_QUERY) {
             memcpy(r->traits,first->traits,first->traits_size);r->traits_size=first->traits_size;r->limit=first->limit;return ready?1:2;
         }
         if(r->operation==VEX_RENDEZVOUS_CLAIM) {
             if(!ready)return -EAGAIN;
             if(r->traits_size!=ready->traits_size || memcmp(r->traits,ready->traits,r->traits_size))return -EINVAL;
             if(r->rights&~ready->rights)return -EACCES;
             if(ready->generation==UINT64_MAX)return -EINVAL;
             vex_web_fd_object_t *copy=vex_message_fd_clone(ready->resource,r->rights,&error);if(!copy)return error;
             claim_receipt *receipt=kmalloc(sizeof(*receipt));
             if(!receipt || ready->refs==UINT32_MAX){kfree(receipt);ladybird_fd_release(copy);return -ENOMEM;}
             *receipt=(claim_receipt){ready,0,ready->generation+1};ready->refs++;vex_message_fd_close_hook(copy,claim_close,receipt);
             int fd=ladybird_fd_install_flags(copy,-1,1);if(fd<0)return -*ladybird_errno_location();
             ready->generation=receipt->generation;ready->claimed=1;return fd;
         }
         if(ready)return 0;
         if(r->timeout_ns && scheduler_time_ns()>=deadline)return -ETIMEDOUT;
         w.next=waiters;waiters=&w;
        }
        if(r->timeout_ns) {
            now=scheduler_time_ns();if(now<deadline) {
                uint64_t left=deadline-now;
                process_wait_event_timeout(&w.woken,left/10000000ull+(left%10000000ull!=0));
            }
        }else process_wait_event_interruptible(&w.woken);
        {KERNEL_GUARD(gate);waiter **p=&waiters;while(*p && *p!=&w)p=&(*p)->next;if(*p)*p=w.next;}
        if(vex_event_interrupt_pending())return -EINTR;
    }
}
