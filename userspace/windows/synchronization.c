#include "windows.h"
/* SRWLOCK is pointer-sized, initially zero. One writer bit and a bounded
 * reader count are process-local state; native futexes supply real sleep/wake.
 * Windows SRW locks are nonrecursive and do not promise FIFO fairness. */
static void WR_WIN InitializeSRWLock(uint64_t *lock) {__atomic_store_n(lock,0,__ATOMIC_RELEASE);}
static unsigned WR_WIN TryAcquireSRWLockExclusive(uint64_t *lock) {
    uint64_t old=0;return __atomic_compare_exchange_n(lock,&old,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void WR_WIN AcquireSRWLockExclusive(uint64_t *lock) {
    for(;;) {uint64_t old=0;
        if(__atomic_compare_exchange_n(lock,&old,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
        wr_futex((uint32_t *)lock,0,(uint32_t)old,0);
    }
}
static void WR_WIN ReleaseSRWLockExclusive(uint64_t *lock) {
    if(__atomic_exchange_n(lock,0,__ATOMIC_RELEASE)!=1)wr_exit(0xc000000du);
    wr_futex((uint32_t *)lock,1,0x7fffffff,0);
}
static unsigned WR_WIN TryAcquireSRWLockShared(uint64_t *lock) {
    uint64_t old=__atomic_load_n(lock,__ATOMIC_RELAXED);
    while(!(old&1) && old<=0xfffffffcull)
        if(__atomic_compare_exchange_n(lock,&old,old+2,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 1;
    return 0;
}
static void WR_WIN AcquireSRWLockShared(uint64_t *lock) {
    for(;;) {
        uint64_t old=__atomic_load_n(lock,__ATOMIC_RELAXED);
        if(!(old&1) && old<=0xfffffffcull && __atomic_compare_exchange_n(lock,&old,old+2,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return;
        wr_futex((uint32_t *)lock,0,(uint32_t)old,0);
    }
}
static void WR_WIN ReleaseSRWLockShared(uint64_t *lock) {
    uint64_t old=__atomic_fetch_sub(lock,2,__ATOMIC_RELEASE);
    if((old&1) || old<2)wr_exit(0xc000000du);
    if(old==2)wr_futex((uint32_t *)lock,1,0x7fffffff,0);
}
/* CRITICAL_SECTION has the public x64 layout but execution is a native futex
 * protocol. Owner is published with acquire/release; LockCount counts every
 * recursion and waiter minus one. An unfair hand-off is permitted by Win32. */
typedef wr_critical_section critical;
_Static_assert(sizeof(critical)==40,"x64 CRITICAL_SECTION");
typedef struct debug_link {struct debug_link *next,*previous;} debug_link;
typedef struct {uint16_t type,creator;uint32_t pad;critical *section;debug_link links;
    uint32_t entries,contentions,flags;uint16_t creator_high,spare;} critical_debug;
_Static_assert(sizeof(critical_debug)==48 && offsetof(critical_debug,links)==16,"AMD64 RTL_CRITICAL_SECTION_DEBUG");
static debug_link debug_sections={&debug_sections,&debug_sections};static wr_mutex debug_gate;
static int critical_initialize(critical *c,unsigned spin,unsigned flags){
    if(flags&~0x11000000u){wr_current_teb()->last_error=50;return 0;}
    if((flags&0x11000000u)==0x11000000u){wr_current_teb()->last_error=87;return 0;}
    critical_debug *d=NULL;if(flags&0x10000000u){d=wr_native_alloc(sizeof(*d));if(!d){wr_current_teb()->last_error=8;return 0;}
        memset(d,0,sizeof(*d));d->section=c;d->flags=0;}
    *c=(critical){.count=-1,.spin=spin,.debug=d};
    if(d){wr_lock(&debug_gate);d->links.next=&debug_sections;d->links.previous=debug_sections.previous;
        debug_sections.previous->next=&d->links;debug_sections.previous=&d->links;wr_unlock(&debug_gate);}
    return 1;
}
static void WR_WIN InitializeCriticalSection(critical *c) {
    (void)critical_initialize(c,0,0);
}
static int WR_WIN InitializeCriticalSectionAndSpinCount(critical *c,unsigned spin) {
    return critical_initialize(c,spin,0);
}
static int WR_WIN InitializeCriticalSectionEx(critical *c,unsigned spin,unsigned flags) {
    return critical_initialize(c,spin,flags);
}
void wr_critical_enter(wr_critical_section *c,uintptr_t self) {
    __atomic_fetch_add(&c->count,1,__ATOMIC_ACQ_REL);
    if(__atomic_load_n(&c->owner,__ATOMIC_ACQUIRE)==self){c->recursion++;return;}
    unsigned contended=0;for(;;) {uintptr_t owner=0;
        if(__atomic_compare_exchange_n(&c->owner,&owner,self,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))break;
        if(!contended && c->debug)__atomic_add_fetch(&((critical_debug *)c->debug)->contentions,1,__ATOMIC_RELAXED);contended=1;
        wr_futex((uint32_t *)&c->owner,0,(uint32_t)owner,0);
    }
    c->recursion=1;if(c->debug)__atomic_add_fetch(&((critical_debug *)c->debug)->entries,1,__ATOMIC_RELAXED);
}
int wr_critical_try(wr_critical_section *c,uintptr_t self) {
    uintptr_t owner=0;
    if(__atomic_load_n(&c->owner,__ATOMIC_ACQUIRE)!=self &&
       !__atomic_compare_exchange_n(&c->owner,&owner,self,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
    __atomic_fetch_add(&c->count,1,__ATOMIC_RELAXED);c->recursion++;return 1;
}
void wr_critical_leave(wr_critical_section *c,uintptr_t self) {
    if(__atomic_load_n(&c->owner,__ATOMIC_RELAXED)!=self || c->recursion<=0)wr_exit(0xc000000du);
    if(--c->recursion){__atomic_fetch_sub(&c->count,1,__ATOMIC_RELEASE);return;}
    __atomic_store_n(&c->owner,0,__ATOMIC_RELEASE);
    if(__atomic_fetch_sub(&c->count,1,__ATOMIC_ACQ_REL)>0)wr_futex((uint32_t *)&c->owner,1,1,0);
}
static void WR_WIN EnterCriticalSection(critical *c){wr_critical_enter(c,wr_current_teb()->tid);}
static int WR_WIN TryEnterCriticalSection(critical *c){return wr_critical_try(c,wr_current_teb()->tid);}
static void WR_WIN LeaveCriticalSection(critical *c){wr_critical_leave(c,wr_current_teb()->tid);}
static void WR_WIN DeleteCriticalSection(critical *c) {
    /* Deletion while owned/contended is invalid, not a successful teardown. */
    if(__atomic_load_n(&c->owner,__ATOMIC_ACQUIRE) || __atomic_load_n(&c->count,__ATOMIC_ACQUIRE)!=-1)wr_exit(0xc000000du);
    critical_debug *d=c->debug;
    if(d){wr_lock(&debug_gate);d->links.previous->next=d->links.next;d->links.next->previous=d->links.previous;wr_unlock(&debug_gate);wr_native_free(d);}
    c->debug=0;c->semaphore=0;c->spin=0;
}
static unsigned WR_WIN SetCriticalSectionSpinCount(critical *c,unsigned spin) {
    uintptr_t old=c->spin;c->spin=spin;return (unsigned)old;
}
/* A condition variable stores a wake generation, not a raw waiter pointer.
 * Capture before releasing the lock and futex-compare before sleeping: a wake
 * in that interval is observed without a lost wakeup. Spurious wakeups are part
 * of the Win32 contract, so predicates remain under the caller's SRW/CS lock. */
static void WR_WIN InitializeConditionVariable(uint64_t *condition) {__atomic_store_n(condition,0,__ATOMIC_RELEASE);}
static void WR_WIN WakeConditionVariable(uint64_t *condition) {
    __atomic_add_fetch((uint32_t *)condition,1,__ATOMIC_RELEASE);wr_futex((uint32_t *)condition,1,1,0);
}
static void WR_WIN WakeAllConditionVariable(uint64_t *condition) {
    __atomic_add_fetch((uint32_t *)condition,1,__ATOMIC_RELEASE);wr_futex((uint32_t *)condition,1,0x7fffffff,0);
}
static int condition_wait(uint64_t *condition,uint32_t generation,unsigned timeout) {
    if(!timeout){wr_current_teb()->last_error=1460;return 0;}
    int r=wr_futex((uint32_t *)condition,0,generation,timeout==UINT32_MAX?0:(uint64_t)timeout*1000000);
    if(r==-110){wr_current_teb()->last_error=1460;return 0;}
    if(r<0 && r!=-11 && r!=-4){wr_current_teb()->last_error=r==-14?998:87;return 0;}
    return 1;
}
static int WR_WIN SleepConditionVariableSRW(uint64_t *condition,uint64_t *lock,unsigned timeout,unsigned flags) {
    if(flags&~1u){wr_current_teb()->last_error=87;return 0;}
    uint32_t generation=__atomic_load_n((uint32_t *)condition,__ATOMIC_ACQUIRE);
    if(flags&1)ReleaseSRWLockShared(lock);else ReleaseSRWLockExclusive(lock);
    int result=condition_wait(condition,generation,timeout);unsigned error=wr_current_teb()->last_error;
    if(flags&1)AcquireSRWLockShared(lock);else AcquireSRWLockExclusive(lock);
    wr_current_teb()->last_error=error;return result;
}
static int WR_WIN SleepConditionVariableCS(uint64_t *condition,critical *lock,unsigned timeout) {
    if(lock->owner!=wr_current_teb()->tid || lock->recursion!=1){wr_current_teb()->last_error=87;return 0;}
    uint32_t generation=__atomic_load_n((uint32_t *)condition,__ATOMIC_ACQUIRE);
    LeaveCriticalSection(lock);int result=condition_wait(condition,generation,timeout);unsigned error=wr_current_teb()->last_error;
    EnterCriticalSection(lock);wr_current_teb()->last_error=error;return result;
}
/* INIT_ONCE's two reserved low bits encode unpublished/pending/completed.
 * Immutable context publication is release/acquire; synchronous contenders
 * sleep in native futexes. No side table, allocation, polling or kernel ABI. */
static void WR_WIN InitOnceInitialize(uint64_t *once){__atomic_store_n(once,0,__ATOMIC_RELEASE);}
static int WR_WIN InitOnceBeginInitialize(uint64_t *once,unsigned flags,int *pending,void **context) {
    if(!pending || (flags&~3u) || flags==3){wr_current_teb()->last_error=87;return 0;}
    for(;;) {
        uint64_t old=__atomic_load_n(once,__ATOMIC_ACQUIRE);unsigned state=old&3;
        if(state==2){*pending=0;if(context)*context=(void *)(uintptr_t)(old&~3ull);return 1;}
        if(flags&1){wr_current_teb()->last_error=31;return 0;}
        unsigned target=(flags&2)?3:1;
        if(!state) {
            if(!__atomic_compare_exchange_n(once,&old,target,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))continue;
            *pending=1;return 1;
        }
        if(state!=target){wr_current_teb()->last_error=87;return 0;}
        if(target==3){*pending=1;return 1;}
        int r=wr_futex((uint32_t *)once,0,1,0);
        if(r<0 && r!=-11 && r!=-4){wr_current_teb()->last_error=87;return 0;}
    }
}
static int WR_WIN InitOnceComplete(uint64_t *once,unsigned flags,void *context) {
    if((flags&~6u) || flags==6 || ((uintptr_t)context&3) || ((flags&4) && context)) {
        wr_current_teb()->last_error=87;return 0;
    }
    uint64_t expected=(flags&2)?3:1,value=(flags&4)?0:((uintptr_t)context|2);
    if(!__atomic_compare_exchange_n(once,&expected,value,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED)) {
        wr_current_teb()->last_error=31;return 0;
    }
    wr_futex((uint32_t *)once,1,0x7fffffff,0);return 1;
}
static int WR_WIN InitOnceExecuteOnce(uint64_t *once,int (WR_WIN *callback)(uint64_t *,void *,void **),void *parameter,void **context) {
    if(!callback){wr_current_teb()->last_error=87;return 0;}
    int pending;if(!InitOnceBeginInitialize(once,0,&pending,context))return 0;
    if(!pending)return 1;
    void *published=0;int result=callback(once,parameter,&published);unsigned error=wr_current_teb()->last_error;
    if(!result){InitOnceComplete(once,4,0);wr_current_teb()->last_error=error;return 0;}
    if(!InitOnceComplete(once,0,published)) {
        error=wr_current_teb()->last_error;InitOnceComplete(once,4,0);wr_current_teb()->last_error=error;return 0;
    }
    if(context)*context=published;return 1;
}
void *wr_sync_exports(const char *name) {
    void *list=wr_slist_exports(name);if(list)return list;
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(InitializeSRWLock);E(AcquireSRWLockExclusive);E(ReleaseSRWLockExclusive);
    E(TryAcquireSRWLockExclusive);E(AcquireSRWLockShared);E(ReleaseSRWLockShared);E(TryAcquireSRWLockShared);
    E(InitializeCriticalSection);E(InitializeCriticalSectionAndSpinCount);E(InitializeCriticalSectionEx);
    E(EnterCriticalSection);E(TryEnterCriticalSection);E(LeaveCriticalSection);E(DeleteCriticalSection);E(SetCriticalSectionSpinCount);
    E(InitializeConditionVariable);E(WakeConditionVariable);E(WakeAllConditionVariable);E(SleepConditionVariableSRW);E(SleepConditionVariableCS);
    E(InitOnceInitialize);E(InitOnceBeginInitialize);E(InitOnceComplete);E(InitOnceExecuteOnce);
#undef E
    return 0;
}
