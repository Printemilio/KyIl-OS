#include <unos/event_counter.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/heap.h>
#include <unos/random.h>
#include <unos/scheduler.h>
#ifndef EVENT_COUNTER_TESTING
#include <unos/smp.h>
#endif

#define EVENT_COUNTER_MAGIC 0x564558434f554e54ull
#define EVENT_COUNTER_MAX_VALUE (UINT64_MAX - 1ull)

typedef struct event_counter event_counter_t;

typedef struct event_counter_waiter {
    event_counter_t *counter;
    uint32_t pid;
    uint32_t tid;
    volatile uint8_t woken;
    uint8_t cancelled;
    struct event_counter_waiter *next;
} event_counter_waiter_t;

struct event_counter {
    uint64_t magic;
    uint64_t capability;
    uint64_t value;
    uint32_t flags;
    uint32_t handle_refs;
    uint32_t wait_refs;
    uint32_t closed;
    uint32_t owner_pid;
    uint8_t owner_handle_active;
    uint8_t reserved[3];
    event_counter_waiter_t *waiters;
    event_counter_t *next;
};

static event_counter_t *event_counters;
static uint8_t counter_lock_word;
typedef struct {uint64_t flags;uint8_t held;} counter_guard_t;
static counter_guard_t counter_lock(void) {
    counter_guard_t g={0};
#ifndef EVENT_COUNTER_TESTING
    __asm__ volatile("pushfq; popq %0; cli":"=r"(g.flags)::"memory");
    if(smp_cpu_count()==1u)return g;
#endif
    while(__atomic_test_and_set(&counter_lock_word,__ATOMIC_ACQUIRE)) {
#ifdef EVENT_COUNTER_TESTING
        __asm__ volatile("pause":::"memory");
#else
        smp_relax();
#endif
    }
    g.held=1u;return g;
}
static void counter_unlock(counter_guard_t *g) {
    if(g->held)__atomic_clear(&counter_lock_word,__ATOMIC_RELEASE);
#ifndef EVENT_COUNTER_TESTING
    __asm__ volatile("pushq %0; popfq"::"r"(g->flags):"memory","cc");
#endif
}
#define COUNTER_LOCK counter_guard_t counter_guard __attribute__((cleanup(counter_unlock)))=counter_lock()
/* This gate covers only counter inventory/value/refcount/waiter metadata.
   It is NEVER held across a scheduler wait or execution of a consumer. Lock
   order is counter -> scheduler metadata; task retirement calls us outside
   the scheduler gate, after execution leases are gone. */

static event_counter_t *event_counter_lookup(uint64_t capability) {
    if (capability == 0u) {
        return NULL;
    }
    for (event_counter_t *counter = event_counters; counter != NULL;
         counter = counter->next) {
        if (counter->capability == capability && counter->magic == EVENT_COUNTER_MAGIC) {
            return counter;
        }
    }
    return NULL;
}

static uint64_t event_counter_new_capability(void) {
    uint64_t capability;
    do {
        /* Syscall return values reserve the signed-negative half for errors.
           Keep opaque capabilities in the positive half so a valid random
           token cannot be mistaken for an errno by userspace. */
        if (kernel_random_try_fill(&capability,sizeof(capability)) != 0) return 0;
        capability &= (uint64_t)INT64_MAX;
    } while (capability == 0u ||
             event_counter_lookup(capability) != NULL);
    return capability;
}

static void event_counter_unlink_waiter(event_counter_t *counter,
                                        event_counter_waiter_t *waiter) {
    event_counter_waiter_t **link;
    if (counter == NULL || waiter == NULL) {
        return;
    }
    link = &counter->waiters;
    while (*link != NULL && *link != waiter) {
        link = &(*link)->next;
    }
    if (*link == waiter) {
        *link = waiter->next;
        waiter->next = NULL;
        if (counter->wait_refs != 0u) {
            --counter->wait_refs;
        }
    }
}

static void event_counter_maybe_free(event_counter_t *counter) {
    event_counter_t **link;
    if (counter == NULL || counter->handle_refs != 0u || counter->wait_refs != 0u) {
        return;
    }
    link = &event_counters;
    while (*link != NULL && *link != counter) {
        link = &(*link)->next;
    }
    if (*link == counter) {
        *link = counter->next;
        counter->magic = 0u;
        kfree(counter);
    }
}

static void event_counter_wake_waiters(event_counter_t *counter) {
    if (counter == NULL) {
        return;
    }
    for (event_counter_waiter_t *waiter = counter->waiters; waiter != NULL;
         waiter = waiter->next) {
        __atomic_store_n(&waiter->woken,1u,__ATOMIC_RELEASE);
        process_wake(waiter->tid);
    }
}

static int counter_create(uint32_t owner,uint64_t initial,uint32_t flags,
                          uint64_t *capability_out) {
    event_counter_t *counter;
    if (capability_out == NULL || (flags & ~(VEX_EVENT_COUNTER_SEMAPHORE |
                                              VEX_EVENT_COUNTER_NONBLOCK)) != 0u ||
        initial > EVENT_COUNTER_MAX_VALUE) {
        return -1;
    }
    counter = kmalloc(sizeof(*counter));
    if (counter == NULL) {
        return -1;
    }
    memset(counter, 0, sizeof(*counter));
    counter->magic = EVENT_COUNTER_MAGIC;
    counter->value = initial;
    counter->flags = flags;
    counter->handle_refs = 1u;
    counter->owner_pid=owner;counter->owner_handle_active=owner!=0u;
    COUNTER_LOCK;
    counter->capability = event_counter_new_capability();
    if (!counter->capability) { kfree(counter); return -1; }
    counter->next = event_counters;
    event_counters = counter;
    *capability_out = counter->capability;
    return 0;
}
int vex_event_counter_create(uint64_t initial,uint32_t flags,uint64_t *out) {
    return counter_create(0u,initial,flags,out);
}

int vex_event_counter_create_owned(uint32_t owner_pid, uint64_t initial,
                                   uint32_t flags, uint64_t *capability_out) {
    return owner_pid?counter_create(owner_pid,initial,flags,capability_out):-1;
}

int vex_event_counter_authorize_owner(uint32_t owner_pid,
                                      uint64_t capability) {
    COUNTER_LOCK;
    event_counter_t *counter = event_counter_lookup(capability);
    return counter != NULL && counter->closed == 0u &&
           counter->owner_pid == owner_pid &&
           counter->owner_handle_active != 0u ? 0 : -1;
}

static int counter_release_locked(event_counter_t *counter) {
    if(!counter || !counter->handle_refs)return -1;
    if(!--counter->handle_refs) {
        counter->closed=1u;event_counter_wake_waiters(counter);
    }
    event_counter_maybe_free(counter);return 0;
}
int vex_event_counter_release_owned(uint32_t owner_pid,uint64_t capability) {
    COUNTER_LOCK;
    event_counter_t *counter = event_counter_lookup(capability);
    if (counter == NULL || counter->owner_pid != owner_pid ||
        counter->owner_handle_active == 0u) return -1;
    counter->owner_handle_active = 0u;
    return counter_release_locked(counter);
}

int vex_event_counter_retain(uint64_t capability) {
    COUNTER_LOCK;
    event_counter_t *counter = event_counter_lookup(capability);
    if (counter == NULL || counter->closed != 0u || counter->handle_refs == UINT32_MAX) {
        return -1;
    }
    ++counter->handle_refs;
    return 0;
}

int vex_event_counter_release(uint64_t capability) {
    COUNTER_LOCK;
    return counter_release_locked(event_counter_lookup(capability));
}

int vex_event_counter_signal(uint64_t capability, uint64_t value) {
    COUNTER_LOCK;
    event_counter_t *counter = event_counter_lookup(capability);
    if (counter == NULL || counter->closed != 0u || value > EVENT_COUNTER_MAX_VALUE ||
        counter->value > EVENT_COUNTER_MAX_VALUE - value) {
        return -1;
    }
    counter->value += value;
    if (value != 0u) {
        event_counter_wake_waiters(counter);
    }
    return 0;
}

int vex_event_counter_read(uint64_t capability, uint64_t *value_out,
                           uint32_t flags) {
    return vex_event_counter_read_timed(capability, value_out, flags, 0u);
}

int vex_event_counter_read_timed(uint64_t capability, uint64_t *value_out,
                                 uint32_t flags, uint64_t timeout_ticks) {
    if(!value_out || (flags&~VEX_EVENT_COUNTER_NONBLOCK))return -1;
    event_counter_waiter_t waiter={.pid=scheduler_current_pid(),.tid=scheduler_current_tid()};
    event_counter_t *counter;
    const uint64_t start=scheduler_ticks_now();
    {
        COUNTER_LOCK;
        counter=event_counter_lookup(capability);
        if(!counter || counter->closed || counter->wait_refs==UINT32_MAX)return -1;
        /* A registered operation retains its object even if the last handle
           closes. Cancellation marks it but does not revoke a reference from
           underneath a consumer still executing on another CPU. */
        waiter.counter=counter;waiter.next=counter->waiters;
        counter->waiters=&waiter;++counter->wait_refs;
    }
    for (;;) {
        uint64_t remaining=0u,value=0u;int result=1;
        {
            COUNTER_LOCK;
            if(waiter.cancelled)result=-3;
            else if(counter->closed)result=-1;
            else if(counter->value) {
                if(counter->flags&VEX_EVENT_COUNTER_SEMAPHORE){value=1u;--counter->value;}
                else {value=counter->value;counter->value=0u;}
                result=0;
            } else if((flags|counter->flags)&VEX_EVENT_COUNTER_NONBLOCK)result=-2;
            else if(timeout_ticks) {
                uint64_t elapsed=scheduler_ticks_now()-start;
                if(elapsed>=timeout_ticks)result=-4;
                else remaining=timeout_ticks-elapsed;
            }
            if(result!=1) {
                event_counter_unlink_waiter(counter,&waiter);
                event_counter_maybe_free(counter);
            } else __atomic_store_n(&waiter.woken,0u,__ATOMIC_RELEASE);
        }
        if(result!=1){if(!result)*value_out=value;return result;}
        /* Reset/check/register is one counter transaction; the scheduler
           pairs this predicate with remote wake under its own short gate. */
        if(timeout_ticks)process_wait_event_timeout(&waiter.woken,remaining);
        else process_wait_event(&waiter.woken);
    }
}

int vex_event_counter_poll(uint64_t capability, short events, short *revents) {
    COUNTER_LOCK;
    event_counter_t *counter = event_counter_lookup(capability);
    if (counter == NULL || revents == NULL) {
        return -1;
    }
    *revents = 0;
    if ((events & (short)0x0001) != 0 && counter->value != 0u) {
        *revents |= (short)0x0001;
    }
    if ((events & (short)0x0004) != 0 && counter->closed == 0u &&
        counter->value < EVENT_COUNTER_MAX_VALUE) {
        *revents |= (short)0x0004;
    }
    if (counter->closed != 0u) {
        *revents |= (short)0x0010;
    }
    return 0;
}

void vex_event_counter_cancel_pid(uint32_t pid) {
    if (pid == 0u) {
        return;
    }
    COUNTER_LOCK;
    for (event_counter_t *counter = event_counters; counter != NULL;
         counter = counter->next) {
        event_counter_waiter_t **link = &counter->waiters;
        while (*link != NULL) {
            event_counter_waiter_t *waiter = *link;
            if (waiter->pid == pid || waiter->tid == pid) {
                waiter->cancelled = 1u;
                __atomic_store_n(&waiter->woken,1u,__ATOMIC_RELEASE);
                process_wake(waiter->tid);
            }
            link = &waiter->next;
        }
    }
}

/* Only the scheduler's stopped-task retirement path may revoke abandoned
   stack references. A remote kill request alone does not imply CPU quiescence. */
void vex_event_counter_thread_stopped(uint32_t tid) {
    if(!tid)return;
    COUNTER_LOCK;
    for(event_counter_t *counter=event_counters,*next;counter;counter=next) {
        next=counter->next;
        event_counter_waiter_t **link=&counter->waiters;
        while(*link) {
            event_counter_waiter_t *w=*link;
            if(w->tid!=tid){link=&w->next;continue;}
            *link=w->next;w->next=NULL;--counter->wait_refs;
        }
        event_counter_maybe_free(counter);
    }
}

void vex_event_counter_reap_pid(uint32_t pid) {
    if (pid == 0u) return;
    COUNTER_LOCK;
    event_counter_t *counter = event_counters;
    while (counter != NULL) {
        event_counter_t *next = counter->next;
        if (counter->owner_pid == pid && counter->owner_handle_active != 0u) {
            counter->owner_handle_active = 0u;
            if (counter->handle_refs != 0u) --counter->handle_refs;
            if (counter->handle_refs == 0u) {
                counter->closed = 1u;
                event_counter_wake_waiters(counter);
            }
            event_counter_maybe_free(counter);
        }
        counter = next;
    }
}
