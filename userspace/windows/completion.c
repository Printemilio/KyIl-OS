#include "windows.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
/* Windows packet format, FIFO and handle/thread association live entirely
 * here. The native execution gate only observes real runnable/blocking state.
 * Native FD work is carried out by the userspace file worker. */
typedef struct packet {struct packet *next;unsigned bytes,error;uintptr_t key;void *overlapped;} packet;
typedef struct completion_waiter {struct completion_waiter *next;uint32_t woken;} completion_waiter;
typedef struct completion_port {
    uint64_t admission;
    unsigned refs,closed;
    packet *first,*last;
    completion_waiter *waiting;
} completion_port;
typedef struct completion_call {struct completion_call *next;completion_port *port;completion_waiter *waiter;} completion_call;
static struct {completion_port *port;uint32_t generation;} handles[256];
static wr_mutex gate;
unsigned wr_completion_handle_count(void){wr_lock(&gate);unsigned count=0;for(unsigned i=0;i<256;i++)if(handles[i].port)count++;wr_unlock(&gate);return count;}
static int64_t admission(unsigned op,uint64_t cap,unsigned capacity,uint64_t ns) {
    vex_execution_req_t r={sizeof(r),op,capacity,0,cap,ns};
    return (int64_t)vex_syscall1(VEX_SYS_EXECUTION_GATE,(uintptr_t)&r);
}
static completion_port *lookup(void *h,unsigned *slot) {
    uintptr_t v=(uintptr_t)h;unsigned i=(unsigned)(wr_handle_slot(v));
    if((wr_handle_tag(v))!=0x56 ||
       wr_handle_generation(v)!=handles[i].generation || !handles[i].port)return 0;
    if(slot)*slot=i;return handles[i].port;
}
static void release(completion_port *p) {
    if(--p->refs)return;
    while(p->first){packet *n=p->first;p->first=n->next;wr_native_free(n);}
    wr_native_free(p);
}
void *wr_completion_acquire(void *h) {
    wr_lock(&gate);completion_port *p=lookup(h,0);
    if(p && p->refs==UINT32_MAX)p=0;if(p)p->refs++;
    wr_unlock(&gate);return p;
}
void wr_completion_release(void *p) {if(p){wr_lock(&gate);release(p);wr_unlock(&gate);}}
void *wr_completion_prepare(uintptr_t key,void *overlapped) {
    packet *n=wr_native_alloc(sizeof(*n));if(n)*n=(packet){.key=key,.overlapped=overlapped};return n;
}
void wr_completion_publish(void *opaque,void **prepared,unsigned bytes,unsigned error) {
    completion_port *p=opaque;packet *n=*prepared;n->bytes=bytes;n->error=error;
    wr_lock(&gate);*prepared=0;if(p->last)p->last->next=n;else p->first=n;p->last=n;
    completion_waiter *w=p->waiting;while(w && __atomic_load_n(&w->woken,__ATOMIC_RELAXED))w=w->next;
    if(w){__atomic_store_n(&w->woken,1,__ATOMIC_RELEASE);wr_futex(&w->woken,1,1,0);}wr_unlock(&gate);
}
void wr_completion_thread_exit(wr_thread *t) {
    wr_lock(&gate);
    for(completion_call *call=t->completion_calls,*next;call;call=next) {
        next=call->next;
        if(call->waiter) {
            completion_waiter **link=&call->port->waiting;
            while(*link && *link!=call->waiter)link=&(*link)->next;
            if(*link)*link=call->waiter->next;
        }
        release(call->port);
    }
    t->completion_calls=0;completion_port *p=t->completion_port;t->completion_port=0;
    if(p)release(p);wr_unlock(&gate);
    /* A remotely stopped thread has already lost its native membership at
     * PROC_ZOMBIE. Never detach the reaper's own membership on its behalf. */
    if(t==(wr_thread *)wr_current_teb())admission(VEX_EXECUTION_LEAVE,0,0,0);
}
void wr_completion_quiesce(int begin) {if(begin)wr_lock(&gate);else wr_unlock(&gate);}
int wr_completion_close(void *h) {
    if((wr_handle_tag((uintptr_t)h))!=0x56)return -1;
    wr_lock(&gate);unsigned i;completion_port *p=lookup(h,&i);uint64_t cap=0;
    if(p) {
        handles[i].port=0;p->closed=1;cap=p->admission;
        for(completion_waiter *w=p->waiting;w;w=w->next) {
            __atomic_store_n(&w->woken,1,__ATOMIC_RELEASE);wr_futex(&w->woken,1,1,0);
        }
        release(p);
    }
    wr_unlock(&gate);
    if(!p){wr_current_teb()->last_error=6;return 0;}
    int64_t r=admission(VEX_EXECUTION_CLOSE,cap,0,0);
    if(r<0)wr_current_teb()->last_error=r==-9?6:50;return r==0;
}
static void *WR_WIN CreateIoCompletionPort(void *file,void *existing,uintptr_t key,unsigned concurrency) {
    (void)key;
    if(file!=(void *)-1) {
        if((wr_handle_tag((uintptr_t)file))==0x55){wr_current_teb()->last_error=50;return 0;}
        void *h=existing?existing:CreateIoCompletionPort((void *)-1,0,0,concurrency);
        if(!h)return 0;completion_port *p=wr_completion_acquire(h);
        unsigned error=p?wr_file_associate(file,p,key):6;
        if(error){wr_completion_release(p);if(!existing)wr_completion_close(h);wr_current_teb()->last_error=error;return 0;}
        return h; /* Successful file association owns the borrowed reference. */
    }
    if(existing){wr_current_teb()->last_error=87;return 0;}
    completion_port *p=wr_native_alloc(sizeof(*p));
    if(!p){wr_current_teb()->last_error=8;return 0;}
    memset(p,0,sizeof(*p));p->refs=1;
    int64_t cap=admission(VEX_EXECUTION_CREATE,0,concurrency,0);
    if(cap<0){wr_native_free(p);wr_current_teb()->last_error=8;return 0;}
    p->admission=(uint64_t)cap;wr_lock(&gate);
    unsigned i=0;while(i<256 && (handles[i].port || handles[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    if(i==256){wr_unlock(&gate);admission(VEX_EXECUTION_CLOSE,p->admission,0,0);wr_native_free(p);
        wr_current_teb()->last_error=8;return 0;}
    if(!++handles[i].generation)++handles[i].generation;handles[i].port=p;
    void *h=(void *)(wr_handle_pack(0x56,handles[i].generation,i));
    wr_unlock(&gate);return h;
}
static int WR_WIN PostQueuedCompletionStatus(void *h,unsigned bytes,uintptr_t key,void *overlapped) {
    packet *n=wr_native_alloc(sizeof(*n));if(!n){wr_current_teb()->last_error=8;return 0;}
    *n=(packet){.bytes=bytes,.key=key,.overlapped=overlapped};wr_lock(&gate);completion_port *p=lookup(h,0);
    if(p) {
        if(p->last)p->last->next=n;else p->first=n;p->last=n;
        /* Newest blocked packet waiter first. Already notified registrations
         * are skipped so multiple posts can wake distinct consumers. */
        completion_waiter *w=p->waiting;
        while(w && __atomic_load_n(&w->woken,__ATOMIC_RELAXED))w=w->next;
        if(w){__atomic_store_n(&w->woken,1,__ATOMIC_RELEASE);wr_futex(&w->woken,1,1,0);}
    }
    wr_unlock(&gate);if(!p){wr_native_free(n);wr_current_teb()->last_error=6;}return p!=0;
}
static int WR_WIN GetQueuedCompletionStatus(void *h,unsigned *bytes,uintptr_t *key,void **overlapped,unsigned timeout) {
    if(!bytes || !key || !overlapped){wr_current_teb()->last_error=87;return 0;}
    *overlapped=0;wr_thread *t=(void *)wr_current_teb();
    wr_lock(&gate);completion_port *p=lookup(h,0);
    if(!p){wr_unlock(&gate);t->teb.last_error=6;return 0;}
    p->refs++; /* This call, separately from persistent thread association. */
    completion_call call={t->completion_calls,p,0};t->completion_calls=&call;
    completion_port *old=t->completion_port;
    if(old!=p){p->refs++;t->completion_port=p;if(old)release(old);}
    wr_unlock(&gate);
    /* Relinquish the preceding active slot before every dequeue attempt.
     * An oversubscribed resumed consumer must not admit another packet solely
     * because it already has a persistent port association. */
    admission(VEX_EXECUTION_LEAVE,0,0,0);
    uint64_t start=wr_ticks();unsigned error=0;int success=0;
    for(;;) {
        uint64_t elapsed=wr_ticks()-start;
        uint64_t ns=timeout==UINT32_MAX?0:(elapsed>=timeout?1:((uint64_t)timeout-elapsed)*1000000);
        int64_t r=admission(VEX_EXECUTION_ENTER,p->admission,0,ns);
        if(r<0){error=r==-110?258:(r==-9 || r==-125?735:50);break;}
        wr_lock(&gate);
        if(p->closed){wr_unlock(&gate);error=735;break;}
        if(p->first) {
            packet *n=p->first;p->first=n->next;if(!p->first)p->last=0;
            *bytes=n->bytes;*key=n->key;*overlapped=n->overlapped;
            error=n->error;wr_native_free(n);wr_unlock(&gate);success=!error;break;
        }
        elapsed=wr_ticks()-start;
        if(timeout!=UINT32_MAX && elapsed>=timeout){wr_unlock(&gate);error=258;break;}
        completion_waiter w={p->waiting,0};p->waiting=&w;
        call.waiter=&w;
        /* A packet waiter is not an active consumer. Drop admission before
         * sleeping; on packet wake, re-enter through the real native quota. */
        admission(VEX_EXECUTION_LEAVE,0,0,0);wr_unlock(&gate);
        while(!__atomic_load_n(&w.woken,__ATOMIC_ACQUIRE)) {
            elapsed=wr_ticks()-start;
            if(timeout!=UINT32_MAX && elapsed>=timeout)break;
            wr_futex(&w.woken,0,0,timeout==UINT32_MAX?0:((uint64_t)timeout-elapsed)*1000000);
        }
        wr_lock(&gate);completion_waiter **link=&p->waiting;
        while(*link!=&w)link=&(*link)->next;*link=w.next;
        call.waiter=0;
        if(p->closed)error=735;
        else if(!__atomic_load_n(&w.woken,__ATOMIC_ACQUIRE))error=258;
        wr_unlock(&gate);if(error)break;
    }
    /* A timeout resumes associated ordinary execution, not new packet work.
     * Restore activity even when that temporarily oversubscribes capacity. */
    if(error==258)admission(VEX_EXECUTION_RESUME,p->admission,0,0);
    wr_lock(&gate);t->completion_calls=call.next;release(p);wr_unlock(&gate);
    if(!success)t->teb.last_error=error;return success;
}
void *wr_completion_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(CreateIoCompletionPort);E(PostQueuedCompletionStatus);E(GetQueuedCompletionStatus);
#undef E
    return 0;
}
