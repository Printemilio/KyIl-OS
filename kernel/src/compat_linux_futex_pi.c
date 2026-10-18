#include <unos/compat_linux_futex_pi.h>
#include <unos/scheduler.h>
#include <unos/vex_syscall.h>
#include <unos/heap.h>
#include <unos/process_memory.h>
#include <string.h>
/* Private PI futex ledger, shared across CPUs. Ordinary WAIT
 * words are NEVER parsed as owner IDs. This encoding applies only when the
 * Linux caller explicitly selected its PI protocol. */
#define PI_WAITERS 0x80000000u
#define PI_DIED 0x40000000u
#define PI_TID_MASK 0x3fffffffu
typedef struct pi_waiter {
    struct pi_waiter *next;
    uint32_t tid;
    volatile uint8_t woken;
    uint8_t granted;
    uint8_t cancelled;
} pi_waiter_t;
typedef struct pi_object {
    struct pi_object *next;
    uint64_t cr3,address;
    uint32_t owner;
    uint8_t dead, invalid;
    pi_waiter_t *waiters;
} pi_object_t;
static pi_object_t *objects;
static kernel_gate_t pi_gate=KERNEL_GATE_INIT;
#ifdef FUTEX_PI_TESTING
/* Hosted tests replace CPU privilege and scheduler adapters, not PI state. */
void pi_test_lock(void);
void pi_test_unlock(void *);
void pi_test_before_cas(uint32_t value);
#define LOCK unsigned char test_guard __attribute__((cleanup(pi_test_unlock)))=(pi_test_lock(),0)
#define BEFORE_CAS(value) pi_test_before_cas(value)
#else
#define LOCK KERNEL_GUARD(pi_gate)
#define BEFORE_CAS(value) ((void)(value))
#endif
/* All user-word accesses pin the mapping transaction BEFORE the PI ledger.
   Unmap holds process_memory and then invalidates PI objects in that order. */
#define MEMORY_LOCK kernel_guard_t memory_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update()
static pi_object_t *lookup(uint64_t cr3,uint64_t address) {
    for(pi_object_t *o=objects;o;o=o->next)if(!o->invalid && o->cr3==cr3 && o->address==address)return o;return NULL;
}
static void prune(pi_object_t *o) {
    if(o->owner || o->waiters)return;
    pi_object_t **p=&objects;while(*p && *p!=o)p=&(*p)->next;if(*p)*p=o->next;kfree(o);
}
static pi_waiter_t *best_waiter(pi_object_t *o) {
    pi_waiter_t *best=NULL;
    for(pi_waiter_t *w=o->waiters;w;w=w->next)
        if(!w->granted && !w->cancelled && (!best || scheduler_waiter_precedes(w->tid,best->tid)))best=w;
    return best;
}
static void donors(pi_object_t *o) {
    for(pi_waiter_t *w=o->waiters;w;w=w->next)
        if(!w->granted)(void)scheduler_dependency_set(w->tid,o->owner);
}
static void undo_waiters_bit(uint64_t address,uint32_t before) {
    if(!(before&PI_WAITERS)) {
        uint32_t expected=before|PI_WAITERS;
        (void)__atomic_compare_exchange_n((uint32_t *)(uintptr_t)address,&expected,
            before,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    }
}
int linux_futex_pi_lock(uint64_t address,int try_only) {
    if(address&3u)return -22;
    if(!vex_syscall_user_range_valid(address,4,1))return -14;
    process_t *self=scheduler_current_process();if(!self || !self->pid || self->pid>PI_TID_MASK)return -22;
    pi_waiter_t waiter={0};pi_object_t *o;
    for(;;) {
        MEMORY_LOCK;LOCK;
        if(!vex_syscall_user_range_valid(address,4,1))return -14;
        uint32_t value=__atomic_load_n((uint32_t *)(uintptr_t)address,__ATOMIC_ACQUIRE);
        o=lookup(self->cr3,address);
        uint32_t owner=o && o->owner?o->owner:(value&PI_TID_MASK);
        if(owner==self->pid)return -35;
        if(owner) {
            SCHEDULER_TASK_REF(owner_ref);
            process_t *thread=scheduler_task_acquire(&owner_ref,owner,0);
            if(!thread || thread->state==PROC_ZOMBIE || thread->cr3!=self->cr3)return -3;
            if(try_only)return -11;
        }
        if(!owner) {
            /* Uncontended glibc unlock can be a userspace CAS. Do not retain
               a kernel owner record for that fast path. */
            uint32_t expected=value;
            BEFORE_CAS(value);
            if(!__atomic_compare_exchange_n((uint32_t *)(uintptr_t)address,&expected,
                self->pid|(value&PI_DIED)|(o?PI_WAITERS:0),0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))continue;
            if(o)o->owner=self->pid;
            return 0;
        }
        /* glibc can fast-unlock with a userspace CAS, independently of our
           ledger lock. Pin WAITERS atomically before recording its owner;
           otherwise unlock between the load and fetch_or strands the waiter. */
        uint32_t expected=value;
        BEFORE_CAS(value);
        if(!__atomic_compare_exchange_n((uint32_t *)(uintptr_t)address,&expected,
            value|PI_WAITERS,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))continue;
        int created=0;
        if(!o) {
            o=kmalloc(sizeof(*o));if(!o){undo_waiters_bit(address,value);return -12;}memset(o,0,sizeof(*o));
            o->cr3=self->cr3;o->address=address;o->owner=owner;o->dead=(value&PI_DIED)!=0;
            o->next=objects;objects=o;
            created=1;
        }
        if(o->invalid)return -14;
        o->owner=owner;
        if(scheduler_wait_dependency(owner)) {if(created){o->owner=0;prune(o);undo_waiters_bit(address,value);}return -35;}
        waiter.tid=self->pid;waiter.next=o->waiters;o->waiters=&waiter;
        break;
    }
    while(!__atomic_load_n(&waiter.woken,__ATOMIC_ACQUIRE))process_wait_event(&waiter.woken);
    {
        MEMORY_LOCK;LOCK;pi_waiter_t **p=&o->waiters;while(*p && *p!=&waiter)p=&(*p)->next;if(*p)*p=waiter.next;
        scheduler_wait_dependency_clear();
        if(!waiter.granted) {int error=o->invalid?-14:-4;prune(o);return error;}
        if(o->invalid || !vex_syscall_user_range_valid(address,4,1)) {
            o->invalid=1;o->owner=0;
            for(pi_waiter_t *w=o->waiters;w;w=w->next){w->granted=0;__atomic_store_n(&w->woken,1u,__ATOMIC_RELEASE);process_wake(w->tid);}
            prune(o);return -14;
        }
        /* Owner death is published from the resumed waiter's own CR3; the
           killer never dereferences a foreign userspace pointer. */
        /* Keep the kernel ledger authoritative until UNLOCK_PI; WAITERS
           prevents the glibc userspace fast-unlock from bypassing teardown. */
        uint32_t value=self->pid|(o->dead?PI_DIED:0)|PI_WAITERS;
        __atomic_store_n((uint32_t *)(uintptr_t)address,value,__ATOMIC_RELEASE);
        return 0;
    }
}
int linux_futex_pi_unlock(uint64_t address) {
    if(address&3u)return -22;
    if(!vex_syscall_user_range_valid(address,4,1))return -14;
    process_t *self=scheduler_current_process();if(!self)return -22;
    MEMORY_LOCK;LOCK;
    if(!vex_syscall_user_range_valid(address,4,1))return -14;
    uint32_t value=__atomic_load_n((uint32_t *)(uintptr_t)address,__ATOMIC_ACQUIRE);
    pi_object_t *o=lookup(self->cr3,address);
    if((value&PI_TID_MASK)!=self->pid || (o && o->owner!=self->pid))return -1;
    if(!o){__atomic_store_n((uint32_t *)(uintptr_t)address,0,__ATOMIC_RELEASE);return 0;}
    pi_waiter_t *next=best_waiter(o);o->dead=0;
    if(!next) {
        o->owner=0;__atomic_store_n((uint32_t *)(uintptr_t)address,0,__ATOMIC_RELEASE);prune(o);return 0;
    }
    o->owner=next->tid;next->granted=1;donors(o);
    __atomic_store_n((uint32_t *)(uintptr_t)address,next->tid|PI_WAITERS,__ATOMIC_RELEASE);
    __atomic_store_n(&next->woken,1u,__ATOMIC_RELEASE);process_wake(next->tid);return 0;
}
void linux_futex_pi_cancel_tid(uint32_t tid) {
    LOCK;
    /* A remote kill request is not proof that a linked stack waiter or the
       current lock owner is quiescent. Keep the object pinned until unlink. */
    for(pi_object_t *o=objects;o;o=o->next)for(pi_waiter_t *w=o->waiters;w;w=w->next)
        if(w->tid==tid){w->cancelled=1u;w->granted=0;__atomic_store_n(&w->woken,1u,__ATOMIC_RELEASE);process_wake(tid);}
}
void linux_futex_pi_thread_stopped(uint32_t tid) {
    LOCK;pi_object_t *o=objects;
    while(o) {
        pi_object_t *next_object=o->next;pi_waiter_t **p=&o->waiters;
        while(*p){pi_waiter_t *w=*p;if(w->tid==tid){*p=w->next;__atomic_store_n(&w->woken,1u,__ATOMIC_RELEASE);}else p=&w->next;}
        if(o->owner==tid) {
            o->owner=0;o->dead=1;pi_waiter_t *next=best_waiter(o);
            if(next){o->owner=next->tid;next->granted=1;__atomic_store_n(&next->woken,1u,__ATOMIC_RELEASE);donors(o);process_wake(next->tid);}
        }
        prune(o);o=next_object;
    }
}
void linux_futex_pi_cancel_range(uint64_t cr3,uint64_t address,uint64_t length) {
    LOCK;pi_object_t *o=objects;
    while(o) {
        pi_object_t *next=o->next;
        if(o->cr3==cr3 && o->address>=address && o->address-address<length) {
            o->invalid=1;o->owner=0;
            for(pi_waiter_t *w=o->waiters;w;w=w->next){w->granted=0;__atomic_store_n(&w->woken,1u,__ATOMIC_RELEASE);process_wake(w->tid);}
            prune(o);
        }
        o=next;
    }
}
