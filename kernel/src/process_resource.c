#include <unos/process_resource.h>
#include <unos/fs_root_provider.h>
#include <unos/fd_borrow.h>
#include <unos/ora.h>
#include <unos/elf.h>
#include <unos/scheduler.h>
#include <unos/usercopy.h>
#include <unos/vex_legacy_compat.h>
#include <unos/heap.h>
#include <unos/message_ipc.h>
#include <unos/rendezvous.h>
#include <unos/fs_root.h>
#include <unos/events.h>
#include <string.h>
#define GRANTS_MAX 64u
#define ARGS_MAX 128u
#define STRING_MAX 4096u
#ifndef E2BIG
#define E2BIG 7
#endif
#ifndef ENOEXEC
#define ENOEXEC 8
#endif
typedef struct {uint32_t refs;scheduler_process_observer_t observer;} process_backing;
typedef struct {process_backing *backing;unsigned rights;} process_cap;
static void backing_release(process_backing *b) {
    if(__atomic_sub_fetch(&b->refs,1,__ATOMIC_ACQ_REL))return;
    if(b->observer.info.pid)scheduler_resource_release(&b->observer);kfree(b);
}
static int cap_close(void *opaque){process_cap *c=opaque;backing_release(c->backing);kfree(c);return 0;}
static const vex_web_fd_ops_t process_ops={.close=cap_close};
int vex_process_resource_transfer_allowed(void *opaque) {
    vex_web_fd_object_t *o=opaque;return o->ops!=&process_ops || (((process_cap *)o->ctx)->rights&VEX_PROCESS_RIGHT_TRANSFER);
}
static int install(process_backing *b,unsigned rights) {
    process_cap *c=kmalloc(sizeof(*c));if(!c)return -ENOMEM;
    uint32_t refs=__atomic_load_n(&b->refs,__ATOMIC_ACQUIRE);
    do {if(!refs || refs==UINT32_MAX){kfree(c);return -ENOMEM;}}
    while(!__atomic_compare_exchange_n(&b->refs,&refs,refs+1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
    *c=(process_cap){b,rights};vex_web_fd_object_t *o=vex_web_fd_object_create(c,&process_ops);
    if(!o){cap_close(c);return -ENOMEM;}
    int fd=vex_web_fd_install_flags(o,-1,1);return fd<0?-*vex_web_errno_location():fd;
}
typedef struct {vex_fd_borrow_t source,grants[GRANTS_MAX];ora_view_t image;process_backing *backing;
    vex_process_grant_t slots[GRANTS_MAX];unsigned count,suspended,ultra;int fd,error;} launch;
static void cleanup(launch *l) {
    vex_fd_borrow_release(&l->source);for(unsigned i=0;i<GRANTS_MAX;i++)vex_fd_borrow_release(&l->grants[i]);
    ora_view_release(&l->image);if(l->backing)backing_release(l->backing);
}
static int prepare(uint32_t pid,void *opaque) {
    launch *l=opaque;
    if(l->ultra && scheduler_isolate_prepared(pid)){l->error=-EPERM;return -1;}
    for(unsigned i=0;i<l->count;i++) {
        int e=vex_fd_install_prepared(pid,l->grants[i].object,l->slots[i].slot,(uint8_t)l->slots[i].flags);
        if(e){l->error=e;return -1;}
    }
    if(scheduler_resource_observe(&l->backing->observer,pid,l->suspended)){l->error=-ENOMEM;return -1;}
    l->fd=install(l->backing,VEX_PROCESS_RIGHT_ALL);
    if(l->fd<0){l->error=l->fd;return -1;}return 0;
}
static int vector(const char *const *user,char **out,char *strings,unsigned *count) {
    *count=0;if(!user)return 0;
    for(unsigned i=0;i<ARGS_MAX;i++) {
        const char *s;if(vex_copy_from_user(&s,(uintptr_t)(user+i),sizeof(s)))return -EFAULT;
        if(!s)return 0;
        if(vex_user_string_copy(strings+i*STRING_MAX,STRING_MAX,(uintptr_t)s))return -EFAULT;
        out[i]=strings+i*STRING_MAX;(*count)++;
    }return -E2BIG;
}
#include "ultra_space.inc"
int64_t vex_process_resource_call(const vex_process_resource_req_t *r) {
    if(r->size!=sizeof(*r) || r->reserved)return -EINVAL;
    if(r->operation==VEX_PROCESS_ULTRA_OPEN || r->operation==VEX_PROCESS_ULTRA_QUERY || r->operation==VEX_PROCESS_ULTRA_OBSERVE)return ultra_call(r);
    if(r->operation==VEX_PROCESS_LAUNCH) {
        process_t *current=scheduler_current_process();if(current && current->group && current->group->ultra_space)return -EACCES;
        if((r->flags&~VEX_PROCESS_START_SUSPENDED) || r->grant_count>GRANTS_MAX || r->rights || r->timeout_ns || r->out || r->status)return -EINVAL;
        launch l __attribute__((cleanup(cleanup)))={.count=r->grant_count,.suspended=r->flags!=0,.fd=-1};
        vex_web_fd_object_t *image=vex_fd_borrow_acquire(&l.source,r->fd);if(!image)return -EBADF;
        int e=vex_fd_executable_snapshot(image,&l.image);if(e)return e;
        if(l.count && vex_copy_from_user(l.slots,(uintptr_t)r->grants,l.count*sizeof(l.slots[0])))return -EFAULT;
        for(unsigned i=0;i<l.count;i++) {
            if(l.slots[i].reserved || l.slots[i].flags>1 || l.slots[i].slot<0 || l.slots[i].slot>=128)return -EINVAL;
            for(unsigned j=0;j<i;j++)if(l.slots[i].slot==l.slots[j].slot)return -EINVAL;
            vex_web_fd_object_t *o=vex_fd_borrow_acquire(&l.grants[i],l.slots[i].fd);if(!o)return -EBADF;
            if(!vex_fd_transfer_allowed(o))return -EACCES;
        }
        VEX_SCRATCH(storage);char *strings=vex_scratch_allocate(&storage,ARGS_MAX*STRING_MAX*2u);if(!strings)return -ENOMEM;
        char *argv[ARGS_MAX+1]={0},*envp[ARGS_MAX+1]={0};unsigned argc,envc;
        if((e=vector(r->argv,argv,strings,&argc)) || (e=vector(r->envp,envp,strings+ARGS_MAX*STRING_MAX,&envc)))return e;
        l.backing=kmalloc(sizeof(*l.backing));if(!l.backing)return -ENOMEM;memset(l.backing,0,sizeof(*l.backing));l.backing->refs=1;
        uint32_t pid=0;
        if(elf_spawn_native_snapshot(ora_view_data(&l.image),l.image.length,(const char *const *)argv,argc,
            (const char *const *)envp,envc,prepare,&l,&pid)) {
            if(l.fd>=0)close(l.fd);return l.error?l.error:-ENOEXEC;
        }
        return l.fd;
    }
    if(r->argv || r->envp || r->grants || r->grant_count)return -EINVAL;
    if(r->operation==VEX_PROCESS_SELF) {
        if(r->fd || r->flags || r->rights || r->timeout_ns || r->out || r->status)return -EINVAL;
        process_backing *b=kmalloc(sizeof(*b));if(!b)return -ENOMEM;memset(b,0,sizeof(*b));b->refs=1;
        if(scheduler_resource_observe(&b->observer,scheduler_current_process_id(),0)){kfree(b);return -ESRCH;}
        int fd=install(b,VEX_PROCESS_RIGHT_ALL);backing_release(b);return fd;
    }
    VEX_FD_BORROW(borrow);vex_web_fd_object_t *o=vex_fd_borrow_acquire(&borrow,r->fd);
    if(!o || o->ops!=&process_ops)return -EBADF;process_cap *c=o->ctx;
    unsigned needed=(r->operation==VEX_PROCESS_QUERY || r->operation==VEX_PROCESS_USAGE)?VEX_PROCESS_RIGHT_QUERY:
        r->operation==VEX_PROCESS_WAIT?VEX_PROCESS_RIGHT_WAIT:
        r->operation==VEX_PROCESS_TERMINATE?VEX_PROCESS_RIGHT_TERMINATE:
        r->operation==VEX_PROCESS_RESUME?VEX_PROCESS_RIGHT_RESUME:0;
    if(r->operation==VEX_PROCESS_EXIT_ORDER)needed=VEX_PROCESS_RIGHT_RESUME;
    if(needed&~c->rights)return -EACCES;
    if(r->operation==VEX_PROCESS_EXIT_ORDER){
        if(r->flags>1 || r->rights || r->out || r->status || (r->flags && r->timeout_ns))return -EINVAL;
        vex_process_resource_info_t info;if(scheduler_resource_info(&c->backing->observer,&info) || info.exited)return -ESRCH;
        return scheduler_process_exit_order(info.pid,r->timeout_ns,r->flags!=0);
    }
    if(r->operation==VEX_PROCESS_USAGE) {
        if(r->flags || r->rights || r->timeout_ns || r->status || !r->out)return -EINVAL;
        vex_process_usage_t info;int e=scheduler_resource_usage(&c->backing->observer,&info);if(e)return e;
        return vex_copy_to_user((uintptr_t)r->out,&info,sizeof(info))?-EFAULT:0;
    }
    if(r->operation==VEX_PROCESS_QUERY) {
        if(r->flags || r->rights || r->timeout_ns || r->status || !r->out)return -EINVAL;
        vex_process_resource_info_t info;int e=scheduler_resource_info(&c->backing->observer,&info);if(e)return e;
        return vex_copy_to_user((uintptr_t)r->out,&info,sizeof(info))?-EFAULT:0;
    }
    if(r->out || (r->flags && (r->operation!=VEX_PROCESS_WAIT || (r->flags&~(VEX_PROCESS_WAIT_THREAD|VEX_PROCESS_WAIT_POLL)))) || (r->operation!=VEX_PROCESS_WAIT && r->timeout_ns) ||
       (r->operation!=VEX_PROCESS_TERMINATE && r->status))return -EINVAL;
    if(r->operation==VEX_PROCESS_VIEW) {
        if(!r->rights || (r->rights&~c->rights))return -EACCES;return install(c->backing,r->rights);
    }
    if(r->rights)return -EINVAL;
    if(r->operation==VEX_PROCESS_WAIT) {
        if((r->flags&VEX_PROCESS_WAIT_POLL) && r->timeout_ns)return -EINVAL;
        return scheduler_resource_wait(&c->backing->observer,r->timeout_ns,(int)r->flags);
    }
    if(r->operation==VEX_PROCESS_TERMINATE || r->operation==VEX_PROCESS_RESUME) {
        vex_process_resource_info_t info;scheduler_resource_info(&c->backing->observer,&info);if(info.exited)return -ESRCH;
        return r->operation==VEX_PROCESS_TERMINATE?scheduler_resource_terminate(info.pid,r->status):scheduler_resource_resume(info.pid);
    }
    return -EINVAL;
}
