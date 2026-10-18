#define FUTEX_PI_TESTING
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "../kernel/src/compat_linux_futex_pi.c"

/* Privileged mapping, task-registry and wait adapters. PI ledger, user-word
   atomics, ownership handoff, cancellation and rollback are production C. */
static SRWLOCK ledger=SRWLOCK_INIT,wake=SRWLOCK_INIT;
static CONDITION_VARIABLE changed=CONDITION_VARIABLE_INIT;
static CRITICAL_SECTION memory;
static process_t tasks[16];
static _Thread_local uint32_t tid=1;
static _Atomic unsigned allocations,inside,checks,fast_locks,fast_unlocks;
static _Atomic int mapped=1,fail_alloc,force_unlock;
static uint32_t word;
static HANDLE start,wait_entered,owner_ready,owner_leave,cas_entered,cas_continue;
static uint64_t root=0x12345000u;
void pi_test_lock(void){AcquireSRWLockExclusive(&ledger);}
void pi_test_unlock(void *unused){(void)unused;ReleaseSRWLockExclusive(&ledger);}
kernel_guard_t process_memory_begin_update(void){EnterCriticalSection(&memory);return (kernel_guard_t){0};}
void process_memory_end_update(kernel_guard_t *unused){(void)unused;LeaveCriticalSection(&memory);}
int vex_syscall_user_range_valid(uint64_t address,size_t bytes,int writable){(void)writable;return mapped && address==(uintptr_t)&word && bytes==4;}
process_t *scheduler_current_process(void){return &tasks[tid];}
uint32_t scheduler_current_tid(void){return tid;}
process_t *scheduler_task_acquire(scheduler_task_ref_t *ref,uint32_t id,int identity){(void)identity;if(id>=16)return NULL;ref->target=&tasks[id];return ref->target;}
void scheduler_task_release(scheduler_task_ref_t *ref){ref->target=NULL;}
int scheduler_wait_dependency(uint32_t owner){return owner==tid?-1:0;}
int scheduler_dependency_set(uint32_t waiter,uint32_t owner){(void)waiter;(void)owner;return 0;}
void scheduler_wait_dependency_clear(void){}
int scheduler_waiter_precedes(uint32_t a,uint32_t b){return a<b;}
void process_wake(uint32_t id){(void)id;AcquireSRWLockExclusive(&wake);WakeAllConditionVariable(&changed);ReleaseSRWLockExclusive(&wake);}
void process_wait_event(const volatile uint8_t *woken){
    AcquireSRWLockExclusive(&wake);
    if(wait_entered)assert(SetEvent(wait_entered));
    while(!__atomic_load_n(woken,__ATOMIC_ACQUIRE))assert(SleepConditionVariableSRW(&changed,&wake,10000,0));
    ReleaseSRWLockExclusive(&wake);
}
void *kmalloc(size_t bytes){if(fail_alloc)return NULL;void *p=malloc(bytes);if(p)++allocations;return p;}
void kfree(void *p){if(p){--allocations;free(p);}}
void pi_test_before_cas(uint32_t value){
    if(tid==2 && (value&PI_TID_MASK)==1 && atomic_exchange(&force_unlock,0)) {
        assert(SetEvent(cas_entered));
        assert(WaitForSingleObject(cas_continue,10000)==WAIT_OBJECT_0);
    }
}
static DWORD WINAPI contender(void *argument){
    tid=(uint32_t)(uintptr_t)argument;
    assert(WaitForSingleObject(start,10000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<10000u;++i){
        uint32_t expected=0;
        if(__atomic_compare_exchange_n(&word,&expected,tid,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))++fast_locks;
        else assert(linux_futex_pi_lock((uintptr_t)&word,0)==0);
        assert(atomic_fetch_add(&inside,1)==0);
        ++checks;
        assert(atomic_fetch_sub(&inside,1)==1);
        expected=tid;
        if(__atomic_compare_exchange_n(&word,&expected,0,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED))++fast_unlocks;
        else assert(linux_futex_pi_unlock((uintptr_t)&word)==0);
    }
    return 0;
}
static DWORD WINAPI single_waiter(void *argument){
    tid=2;
    int expected=(int)(intptr_t)argument;
    assert(linux_futex_pi_lock((uintptr_t)&word,0)==expected);
    if(!expected){assert(linux_futex_pi_unlock((uintptr_t)&word)==0);}
    return 0;
}
static DWORD WINAPI dead_owner(void *unused){
    (void)unused;tid=9;
    assert(linux_futex_pi_lock((uintptr_t)&word,0)==0);
    assert(SetEvent(owner_ready));assert(WaitForSingleObject(owner_leave,10000)==WAIT_OBJECT_0);
    return 0; /* Real owner thread stops before the owner-death callback. */
}
static DWORD WINAPI death_waiter(void *unused){
    (void)unused;tid=2;
    assert(linux_futex_pi_lock((uintptr_t)&word,0)==0);
    assert((__atomic_load_n(&word,__ATOMIC_ACQUIRE)&(PI_TID_MASK|PI_DIED))==(2u|PI_DIED));
    assert(linux_futex_pi_unlock((uintptr_t)&word)==0);return 0;
}
static HANDLE event(void){HANDLE h=CreateEventW(NULL,TRUE,FALSE,NULL);assert(h);return h;}
static void join(HANDLE h){assert(WaitForSingleObject(h,20000)==WAIT_OBJECT_0);assert(CloseHandle(h));}
int main(void){
    InitializeCriticalSection(&memory);
    for(unsigned i=0;i<16;++i){tasks[i].pid=i;tasks[i].cr3=root;tasks[i].user_mode=1;tasks[i].state=PROC_RUNNING;}
    start=event();HANDLE threads[8];
    for(unsigned i=0;i<8;++i){threads[i]=CreateThread(NULL,0,contender,(void *)(uintptr_t)(i+1),0,NULL);assert(threads[i]);}
    assert(SetEvent(start));assert(WaitForMultipleObjects(8,threads,TRUE,60000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<8;++i)join(threads[i]);assert(CloseHandle(start));
    assert(checks==80000u && !inside && !word && !objects && !allocations);
    /* Force a userspace fast unlock precisely between the contender's read
       and its WAITERS publication. It must retry/acquire, without a wake. */
    word=1;force_unlock=1;cas_entered=event();cas_continue=event();
    HANDLE t=CreateThread(NULL,0,single_waiter,(void *)0,0,NULL);assert(t);
    assert(WaitForSingleObject(cas_entered,10000)==WAIT_OBJECT_0);
    uint32_t expected=1;assert(__atomic_compare_exchange_n(&word,&expected,0,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED));
    assert(SetEvent(cas_continue));join(t);assert(!word && !objects && !allocations);
    assert(CloseHandle(cas_entered));assert(CloseHandle(cas_continue));
    /* OOM must not strand WAITERS on an otherwise userspace-owned lock. */
    word=1;fail_alloc=1;t=CreateThread(NULL,0,single_waiter,(void *)(intptr_t)-12,0,NULL);assert(t);join(t);
    assert(word==1 && !objects && !allocations);word=0;fail_alloc=0;
    wait_entered=event();owner_ready=event();owner_leave=event();
    HANDLE owner=CreateThread(NULL,0,dead_owner,NULL,0,NULL);assert(owner);
    assert(WaitForSingleObject(owner_ready,10000)==WAIT_OBJECT_0);
    t=CreateThread(NULL,0,death_waiter,NULL,0,NULL);assert(t);
    assert(WaitForSingleObject(wait_entered,10000)==WAIT_OBJECT_0);
    assert(SetEvent(owner_leave));join(owner);tasks[9].state=PROC_ZOMBIE;
    linux_futex_pi_thread_stopped(9);join(t);assert(!word && !objects && !allocations);
    assert(CloseHandle(owner_ready));assert(CloseHandle(owner_leave));
    /* Mapping invalidation wakes the waiter with a real EFAULT. */
    assert(ResetEvent(wait_entered));word=1;
    t=CreateThread(NULL,0,single_waiter,(void *)(intptr_t)-14,0,NULL);assert(t);
    assert(WaitForSingleObject(wait_entered,10000)==WAIT_OBJECT_0);
    mapped=0;linux_futex_pi_cancel_range(root,(uintptr_t)&word,4);join(t);
    assert(!objects && !allocations);mapped=1;word=0;
    assert(CloseHandle(wait_entered));wait_entered=NULL;
    DeleteCriticalSection(&memory);
    printf("SMP PI: 80000 critical sections, fast CAS paths=%u/%u, forced fast-unlock race, OOM rollback, stopped-owner death, invalidation PASS\n",(unsigned)fast_locks,(unsigned)fast_unlocks);
}
