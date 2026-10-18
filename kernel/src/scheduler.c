#include <unos/scheduler.h>
#include <stdio.h>
#include <unos/compat_linux_futex_pi.h>
#include <unos/kernel_mapping_stats.h>
#include <unos/sched_clock.h>
#include <unos/smp.h>
#include <unos/usercopy.h>
#include <unos/network_state.h>
#include <unos/ipc.h>
#include <unos/irq.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/app_surface.h>
#include <unos/boot_requests.h>
#include <unos/compositor_surface.h>
#include <unos/debug_log.h>
#include <unos/event_counter.h>
#include <unos/random.h>
#include <unos/vex_execution_abi.h>
#include <unos/fd_borrow.h>
#include <unos/message_ipc.h>
#include <unos/rendezvous.h>
#include <unos/gdt.h>
#include <unos/gpu.h>
#include <unos/heap.h>
#include <unos/compat.h>
#include <unos/elevation.h>
#include <unos/kernel_allocator.h>
#include <unos/versioned_store.h>
#include <unos/pmm.h>
#include <unos/process_memory.h>
#include <unos/security.h>
#include <unos/vex_syscall.h>
#include <unos/vex_process_abi.h>
#include <unos/file_times.h>
#include <unos/vmm.h>
#include <unos/events.h>
#include <unos/vex_legacy_compat.h>

#define PROCESS_STACK_SIZE (32ull * 1024ull)
#define PROCESS_STACK_MAX_SIZE (256ull * 1024ull)
#define PROCESS_USER_PAGE_SIZE 4096ull

#define PAGE_ADDR_MASK 0x000ffffffffff000ull
#define USER_ALLOC_BASE 0x0000000100000000ull
#define MSR_FS_BASE 0xc0000100u
#define MSR_USER_GS_SLOT 0xc0000102u

/* Size in bytes of the register frame pushed by syscall_entry.S: one saved
   user RSP below 14 register slots and a 24-byte scratch area. */
#define SYSCALL_FRAME_SIZE 144ull
#define SYSCALL_FRAME_RSP_OFFSET 136ull
#define FORK_RESUME_FRAME_OFFSET 256ull

_Static_assert(_Alignof(process_t) >= 64u, "process state must preserve XSAVE alignment");
_Static_assert((offsetof(process_t, fpu_state) & 63u) == 0u,
               "per-thread FPU state must be 64-byte aligned");

int ladybird_fd_clone_process(uint32_t parent_pid, uint32_t child_pid);
void ladybird_fd_close_process(uint32_t pid);
void ladybird_fd_close_exec(uint32_t pid);

/* Futex wait queue. The nodes live on the waiting thread's own (kernel) stack
   because the syscall runs on that stack and is preserved across the blocking
   context switch, mirroring how process_wait_pid() blocks in place. A futex is
   uniquely identified by (cr3, uaddr): CLONE_VM threads share the same cr3, so
   the userspace address alone disambiguates the futex within a process. */
typedef struct futex_waiter {
    uint64_t cr3;
    uint64_t uaddr;
    uint32_t pid;
    volatile uint8_t woken;
    struct futex_waiter *next;
} futex_waiter_t;

static futex_waiter_t *futex_waiters;
static kernel_gate_t futex_gate=KERNEL_GATE_INIT;
static void futex_cancel_pid(uint32_t pid);
static void scheduler_log_u64_dec(uint64_t value);
static void debug_log_u64_hex(uint64_t value);

/* Futex diagnostics have a boot-wide budget. The trace records
   the exact private key and the waitqueue transition, so a Linux personality
   can be audited without changing futex semantics or flooding the serial
   console during normal WebKit startup. */
static uint32_t futex_trace_count;
#define FUTEX_TRACE_LIMIT 512u

static void futex_trace(const char *event, uint64_t uaddr, uint64_t cr3,
                        uint32_t expected, uint32_t observed,
                        uint32_t waiters, uint32_t matched, int result) {
    if (!VEX_COMPAT_TRACE) return;
    const uint32_t owner = scheduler_current_pid();
    if (futex_trace_count >= FUTEX_TRACE_LIMIT) {
        return;
    }
    ++futex_trace_count;
    debug_log_puts("futex-trace: ");
    debug_log_puts(event);
    debug_log_puts(" pid=");
    scheduler_log_u64_dec(owner);
    debug_log_puts(" tid=");
    scheduler_log_u64_dec(scheduler_current_tid());
    debug_log_puts(" addr=0x");
    debug_log_u64_hex(uaddr);
    debug_log_puts(" cr3=0x");
    debug_log_u64_hex(cr3);
    debug_log_puts(" arg3=");
    scheduler_log_u64_dec(expected);
    debug_log_puts(" observed=");
    scheduler_log_u64_dec(observed);
    debug_log_puts(" waiters=");
    scheduler_log_u64_dec(waiters);
    debug_log_puts(" matched=");
    scheduler_log_u64_dec(matched);
    debug_log_puts(" result=");
    scheduler_log_u64_dec((uint64_t)(uint32_t)result);
    debug_log_puts("\n");
}

extern void scheduler_context_switch(uint64_t *old_rsp, uint64_t new_rsp, uint64_t new_cr3,
                                     void *old_fpu, const void *new_fpu,
                                     uint64_t xsave_mask, uint64_t new_fs_base, uint64_t new_user_gs_base);
extern void resume_syscall_return(uint64_t frame_rsp, uint64_t return_value);
extern void user_enter(uint64_t entry, uint64_t user_rsp);

/* CPU-local execution state, with a static allocation-free BSP fast path. */
typedef struct scheduler_cpu {
    process_t idle;
    process_t *current,*switch_from;
    flow_cpu_t flow;
    uint64_t account_at,switch_at,kernel_enter_ns,kernel_max_ns,account_tick;
    uint64_t kernel_entry_pc,kernel_max_pc,idle_ns,dispatch_max_ns;
    unsigned nonpreempt_depth;
    uint64_t cycles_at;
    volatile uint8_t pending;
} scheduler_cpu_t;
static scheduler_cpu_t scheduler_boot;
static inline scheduler_cpu_t *scheduler_local(void) {
    vex_cpu_t *cpu=smp_current_cpu();
    return cpu && cpu->scheduler ? cpu->scheduler : &scheduler_boot;
}
#define kernel_process (scheduler_local()->idle)
#define current_process (scheduler_local()->current)
#define flow_local (scheduler_local()->flow)
#define flow_account_at (scheduler_local()->account_at)
#define flow_switch_at (scheduler_local()->switch_at)
#define flow_kernel_enter_ns (scheduler_local()->kernel_enter_ns)
#define flow_kernel_max_ns (scheduler_local()->kernel_max_ns)
#define flow_kernel_entry_pc (scheduler_local()->kernel_entry_pc)
#define flow_kernel_max_pc (scheduler_local()->kernel_max_pc)
#define flow_idle_ns (scheduler_local()->idle_ns)
#define flow_dispatch_max_ns (scheduler_local()->dispatch_max_ns)
#define flow_nonpreempt_depth (scheduler_local()->nonpreempt_depth)
#define preempt_pending (scheduler_local()->pending)
static process_t *process_list;
static uint32_t next_pid;
static volatile uint64_t scheduler_ticks;
static volatile uint64_t scheduler_idle_ticks;
static process_t *pid_index[256];
static process_t *zombie_list;
static void scheduler_set_state(process_t *p, unsigned state);
static void scheduler_account(uint64_t now);
static void wake_child_waiters(process_t *child);
static void scheduler_request_stop(process_t *p,int status);
static void scheduler_finish_stop(process_t *p);
static int scheduler_stop_at_boundary(void);
static uint32_t context_waiter_count;
static void context_notify(uint32_t tid);
static void futex_wake_key(uint64_t cr3,uint64_t uaddr);
static void futex_block(const volatile uint8_t *flag,uint64_t deadline_ns);
typedef struct scheduler_irq_guard {uint64_t flags;} scheduler_irq_guard_t;
static scheduler_irq_guard_t scheduler_lock(void) {
    scheduler_irq_guard_t g;__asm__ volatile("pushfq; pop %0; cli":"=r"(g.flags)::"memory");return g;
}
static void scheduler_unlock(scheduler_irq_guard_t *g) {
    __asm__ volatile("push %0; popfq"::"r"(g->flags):"memory","cc");
}
#define SCHED_LOCK scheduler_irq_guard_t sched_guard __attribute__((cleanup(scheduler_unlock)))=scheduler_lock()
/* Shared FlowSched metadata only: never held through execution, waiting,
 * resource destruction, or a stack switch. UP uses IRQ exclusion directly. */
static flow_domain_t scheduling_domain;
static uint8_t flow_lock_word;
static uint8_t automatic_dispatch;
static uint8_t shutdown_order_active;
static uint32_t flow_lock_owner=UINT32_MAX,flow_lock_depth;
static uint32_t watched_tid,watch_count;
static const volatile uint8_t *watched_flag;
typedef struct {uint64_t site;uint32_t cpu,before,after,flag;} wait_watch_entry_t;
static wait_watch_entry_t wait_watch[64];
typedef struct scheduler_flow_guard {uint64_t flags;uint8_t active,held;} scheduler_flow_guard_t;
static scheduler_flow_guard_t scheduler_flow_lock(void) {
    scheduler_flow_guard_t g={.active=1u};
    __asm__ volatile("pushfq; popq %0; cli":"=r"(g.flags)::"memory");
    if(smp_cpu_count()>1u) {
        uint32_t cpu=smp_current_index();
        if(__atomic_load_n(&flow_lock_owner,__ATOMIC_ACQUIRE)!=cpu) {
            while(__atomic_test_and_set(&flow_lock_word,__ATOMIC_ACQUIRE))smp_relax();
            __atomic_store_n(&flow_lock_owner,cpu,__ATOMIC_RELEASE);
        }
        ++flow_lock_depth;g.held=1u;
    }
    return g;
}
static void scheduler_flow_unlock(scheduler_flow_guard_t *g) {
    if(!g->active)return;g->active=0u;
    if(g->held && !--flow_lock_depth) {
        __atomic_store_n(&flow_lock_owner,UINT32_MAX,__ATOMIC_RELEASE);
        __atomic_clear(&flow_lock_word,__ATOMIC_RELEASE);
    }
    __asm__ volatile("pushq %0; popfq"::"r"(g->flags):"memory","cc");
}
#define FLOW_LOCK scheduler_flow_guard_t flow_guard __attribute__((cleanup(scheduler_flow_unlock)))=scheduler_flow_lock()
#include "execution_gate.inc"
void scheduler_watch_wait(uint32_t tid,const volatile uint8_t *flag) {
    FLOW_LOCK;watched_tid=tid;watched_flag=flag;watch_count=0u;
}
void scheduler_dump_wait_watch(void) {
    wait_watch_entry_t copy[64];unsigned count;
    {FLOW_LOCK;count=watch_count<64u?watch_count:64u;
     for(unsigned i=0;i<count;++i)copy[i]=wait_watch[(watch_count-count+i)%64u];}
    for(unsigned i=0;i<count;++i) {
        char line[160];snprintf(line,sizeof(line),"smp-wait-state: cpu=%u before=%u after=%u flag=%u site=%08x%08x\n",copy[i].cpu,copy[i].before,copy[i].after,copy[i].flag,(unsigned)(copy[i].site>>32u),(unsigned)copy[i].site);
        debug_log_puts(line);
    }
}
static void record_wait_watch(uint32_t before,uint32_t after,uint32_t flag,uint64_t site) {
    wait_watch[watch_count++%64u]=(wait_watch_entry_t){site,smp_current_index(),before,after,flag};
}
static void scheduler_flow_rebind(process_t *p,process_group_t *g) {
    FLOW_LOCK;
    SCHED_LOCK; scheduler_account(sched_clock_ns());
    flow_rebind(&flow_local,&p->scheduling,g?&g->scheduling:NULL,sched_clock_ns());
}
static void scheduler_account(uint64_t now) {
    FLOW_LOCK;
    if(now<flow_account_at)return;
    uint64_t cycles=sched_clock_cycles();
    if(scheduler_local()->cycles_at && current_process && current_process->pid)
        current_process->cpu_cycles+=cycles-scheduler_local()->cycles_at;
    scheduler_local()->cycles_at=cycles;
    if(current_process && !current_process->pid)flow_idle_ns+=now-flow_account_at;
    if(current_process && current_process->pid && current_process->execution_per_cpu)
        current_process->execution_per_cpu[smp_current_index()]+=now-flow_account_at;
    if(current_process && current_process->group && current_process->scheduling.runnable) {
        if(current_process->accounting_user)current_process->group->user_cpu_ns+=now-flow_account_at;
        else current_process->group->kernel_cpu_ns+=now-flow_account_at;
    }
    flow_charge(&flow_local,current_process && current_process->pid?&current_process->scheduling:NULL,now-flow_account_at,now);
    flow_account_at=now;
}
void scheduler_execution_mode(int user) {
    FLOW_LOCK;
    if(!current_process || !current_process->pid || !current_process->group)return;
    scheduler_account(sched_clock_ns());current_process->accounting_user=user!=0;
}
static void scheduler_set_state(process_t *p,unsigned state) {
    FLOW_LOCK;
    if(!p)return;SCHED_LOCK;uint64_t now=sched_clock_ns();scheduler_account(now);
    if(p->pid && p->scheduling.entity.owner) {
        if(state==PROC_RUNNING || state==PROC_READY) {
            flow_timer_cancel(&flow_local,&p->scheduling);
            if(!p->scheduling.runnable)flow_runnable(&flow_local,&p->scheduling,now);
        } else {
            flow_block(&flow_local,&p->scheduling);
            if(state==PROC_SLEEPING && p->sleep_deadline_ns) {
                flow_timer_arm(&flow_local,&p->scheduling,p->sleep_deadline_ns);
            } else if(state==PROC_SLEEPING) {
                uint64_t ticks=p->sleep_until_tick>scheduler_ticks?p->sleep_until_tick-scheduler_ticks:0;
                uint64_t delta=ticks>UINT64_MAX/10000000ull?UINT64_MAX:ticks*10000000ull;
                flow_timer_arm(&flow_local,&p->scheduling,now>UINT64_MAX-delta?UINT64_MAX:now+delta);
            } else flow_timer_cancel(&flow_local,&p->scheduling);
            if(state==PROC_ZOMBIE)flow_forget(&flow_local,&p->scheduling,now);
        }
    }
    if(state==PROC_ZOMBIE && p->pid && !p->zombie_queued){p->zombie_queued=1;p->zombie_next=zombie_list;zombie_list=p;}
    if(p->pid==watched_tid && watched_flag)
        record_wait_watch((uint32_t)p->state,state,__atomic_load_n(watched_flag,__ATOMIC_ACQUIRE),(uint64_t)__builtin_return_address(0));
    p->state=state;
    if(context_waiter_count && (state==PROC_WAITING || state==PROC_SLEEPING))context_notify(p->pid);
    execution_transition(p);
    if(state==PROC_ZOMBIE)wake_child_waiters(p);
    if(p->pid && state==PROC_READY && p->scheduling.cpu &&
       p->scheduling.cpu->cpu_id!=smp_current_index())smp_reschedule(p->scheduling.cpu->cpu_id);
}
static uint64_t scheduler_xsave_mask;
static uint8_t scheduler_initial_fpu[SCHEDULER_FPU_STATE_SIZE] __attribute__((aligned(64)));

static void scheduler_log_u64_dec(uint64_t value) {
    char digits[21];
    size_t count = 0u;
    if (value == 0u) {
        debug_log_puts("0");
        return;
    }
    while (value != 0u && count < sizeof(digits) - 1u) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u) {
        const char text[2] = { digits[--count], '\0' };
        debug_log_puts(text);
    }
}

static void debug_log_u64_hex(uint64_t value) {
    static const char hex[] = "0123456789abcdef";
    char digits[17];
    for (unsigned int i = 0u; i < 16u; ++i) {
        digits[15u - i] = hex[value & 0xfu];
        value >>= 4u;
    }
    digits[16] = '\0';
    debug_log_puts(digits);
}

#define PROCESS_WAIT_ANY (-1)
#define PROCESS_WAIT_NOHANG 0x1u

static process_t *find_process(uint32_t pid);
static process_t *find_process_identity(uint32_t process_id);

static void scheduler_cpuid(uint32_t leaf, uint32_t subleaf,
                            uint32_t *eax, uint32_t *ebx,
                            uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
}

static void scheduler_fpu_save(void *area) {
    if (scheduler_xsave_mask != 0u) {
        const uint32_t eax = (uint32_t)scheduler_xsave_mask;
        const uint32_t edx = (uint32_t)(scheduler_xsave_mask >> 32u);
        __asm__ volatile ("xsave64 (%0)" :: "r"(area), "a"(eax), "d"(edx) : "memory");
    } else {
        __asm__ volatile ("fxsave64 (%0)" :: "r"(area) : "memory");
    }
}

void scheduler_capture_fpu_state(uint8_t *state) {
    if (state == NULL || ((uintptr_t)state & 63u) != 0u) {
        return;
    }
    scheduler_fpu_save(state);
}

void scheduler_restore_fpu_state(const uint8_t *state) {
    if (state == NULL || ((uintptr_t)state & 63u) != 0u) {
        return;
    }
    if (scheduler_xsave_mask != 0u) {
        const uint32_t eax = (uint32_t)scheduler_xsave_mask;
        const uint32_t edx = (uint32_t)(scheduler_xsave_mask >> 32u);
        __asm__ volatile ("xrstor64 (%0)" :: "r"(state), "a"(eax), "d"(edx) : "memory");
    } else {
        __asm__ volatile ("fxrstor64 (%0)" :: "r"(state) : "memory");
    }
}

static void scheduler_fpu_init(void) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t max_leaf;

    scheduler_xsave_mask = 0u;
    scheduler_cpuid(0u, 0u, &max_leaf, &ebx, &ecx, &edx);
    scheduler_cpuid(1u, 0u, &eax, &ebx, &ecx, &edx);

    /* Enable AVX state only when both hardware XSAVE and AVX are present and
       the architectural state mask fits our fixed per-thread buffer. */
    if (max_leaf >= 0xdu && (ecx & (1u << 26u)) != 0u &&
        (ecx & (1u << 28u)) != 0u) {
        uint64_t cr4;
        uint32_t xsave_size;
        uint32_t supported_lo;

        scheduler_cpuid(0xdu, 0u, &eax, &xsave_size, &ecx, &edx);
        supported_lo = eax;
        if (xsave_size <= SCHEDULER_FPU_STATE_SIZE &&
            (supported_lo & 0x7u) == 0x7u) {
            __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
            cr4 |= 1ull << 18u; /* OSXSAVE */
            __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4) : "memory");
            __asm__ volatile ("xsetbv" :: "c"(0u), "a"(0x7u), "d"(0u) : "memory");
            scheduler_xsave_mask = 0x7u;
        }
    }

    scheduler_fpu_save(scheduler_initial_fpu);
    debug_log_puts("fpu-initial-state: fcw=");
    debug_log_u64_hex(*(const uint16_t *)(const void *)(scheduler_initial_fpu + 0u));
    debug_log_puts(" fsw=");
    debug_log_u64_hex(*(const uint16_t *)(const void *)(scheduler_initial_fpu + 2u));
    debug_log_puts(" fip=");
    debug_log_u64_hex(*(const uint64_t *)(const void *)(scheduler_initial_fpu + 8u));
    debug_log_puts(" mxcsr=");
    debug_log_u64_hex(*(const uint32_t *)(const void *)(scheduler_initial_fpu + 24u));
    debug_log_puts("\n");
}
int scheduler_fpstate_valid(const void *legacy_state) {
    uint32_t value,mask;
    memcpy(&value,(const uint8_t *)legacy_state+24u,4u);
    memcpy(&mask,scheduler_initial_fpu+28u,4u);
    if(!mask)mask=0xffbfu;
    return (value&~mask)==0u;
}

static uint32_t scheduler_fpu_copy_trace_count;
/* Temporary passive probes used to compare live x87 state immediately before
 * IRETQ and immediately after the next SYSCALL entry. */
volatile uint16_t scheduler_user_iret_fcw_probe;
static void __attribute__((noinline)) scheduler_fpu_copy(void *dst, const void *src) {
    uint16_t source_cw;
    uint16_t destination_cw;
    uint16_t copied_cw;
    uint64_t source_bv = 0u;
    uint64_t copied_bv = 0u;
    uint8_t trace = VEX_FPU_TRACE && scheduler_fpu_copy_trace_count < 96u;
    if (trace != 0u) {
        memcpy(&source_cw, src, sizeof(source_cw));
        memcpy(&destination_cw, dst, sizeof(destination_cw));
    }
    memcpy(dst, src, SCHEDULER_FPU_STATE_SIZE);
    if (trace != 0u) {
        ++scheduler_fpu_copy_trace_count;
        memcpy(&copied_cw, dst, sizeof(copied_cw));
        if (scheduler_xsave_mask != 0u) {
            memcpy(&source_bv, (const uint8_t *)src + 512u, sizeof(source_bv));
            memcpy(&copied_bv, (const uint8_t *)dst + 512u, sizeof(copied_bv));
        }
        debug_log_puts("fpu-copy: dst="); debug_log_u64_hex((uintptr_t)dst);
        debug_log_puts(" src="); debug_log_u64_hex((uintptr_t)src);
        debug_log_puts(" ret="); debug_log_u64_hex((uintptr_t)__builtin_return_address(0));
        debug_log_puts(" src-cw="); debug_log_u64_hex(source_cw);
        debug_log_puts(" old-dst-cw="); debug_log_u64_hex(destination_cw);
        debug_log_puts(" new-cw="); debug_log_u64_hex(copied_cw);
        debug_log_puts(" src-bv="); debug_log_u64_hex(source_bv);
        debug_log_puts(" dst-bv="); debug_log_u64_hex(copied_bv);
        debug_log_puts("\n");
    }
}

/* Temporary browser bring-up diagnostic: report the first scheduler edge
 * carrying a non-default x87 control word.  Saving here mirrors the XSAVE
 * performed by scheduler_context_switch(), so this observes the actual live
 * outgoing state without changing it. */
static uint32_t scheduler_fpu_trace_count;
static void scheduler_trace_fpu_switch(const char *site,
                                       process_t *previous,
                                       process_t *next) {
    if(!VEX_FPU_TRACE)return;
    uint16_t previous_cw;
    uint16_t next_cw;
    uint16_t previous_sw;
    uint16_t next_sw;
    uint64_t previous_bv = 0u;
    uint64_t next_bv = 0u;

    if (previous == NULL || next == NULL) {
        return;
    }

    scheduler_fpu_save(previous->fpu_state);
    memcpy(&previous_cw, previous->fpu_state, sizeof(previous_cw));
    memcpy(&previous_sw, previous->fpu_state + 2u, sizeof(previous_sw));
    memcpy(&next_cw, next->fpu_state, sizeof(next_cw));
    memcpy(&next_sw, next->fpu_state + 2u, sizeof(next_sw));
    if (scheduler_xsave_mask != 0u) {
        memcpy(&previous_bv, previous->fpu_state + 512u, sizeof(previous_bv));
        memcpy(&next_bv, next->fpu_state + 512u, sizeof(next_bv));
    }
    if (previous_cw == 0x037fu && next_cw == 0x037fu) {
        return;
    }
    if (scheduler_fpu_trace_count >= 128u) {
        return;
    }
    ++scheduler_fpu_trace_count;
    debug_log_puts("fpu-switch: site="); debug_log_puts(site);
    debug_log_puts(" from="); scheduler_log_u64_dec(previous->pid);
    debug_log_puts(" to="); scheduler_log_u64_dec(next->pid);
    debug_log_puts(" from-cw="); debug_log_u64_hex(previous_cw);
    debug_log_puts(" from-sw="); debug_log_u64_hex(previous_sw);
    debug_log_puts(" to-cw="); debug_log_u64_hex(next_cw);
    debug_log_puts(" to-sw="); debug_log_u64_hex(next_sw);
    debug_log_puts(" from-bv="); debug_log_u64_hex(previous_bv);
    debug_log_puts(" to-bv="); debug_log_u64_hex(next_bv);
    debug_log_puts("\n");
}

static uint32_t process_owner_pid(const process_t *process) {
    if (process == NULL) {
        return 0u;
    }
    return process->group == NULL ? process->pid : process->group->process_id;
}

typedef struct resource_waiter {struct resource_waiter *next;uint32_t tid,pid;volatile uint8_t woken;} resource_waiter;
static resource_waiter *resource_waiters;
static scheduler_process_observer_t *process_observers;
static void resource_publish_completion(void);
static void wake_child_waiters(process_t *child) {
    /* Called only inside the metadata transaction publishing the exit status
       and zombie state. A Process owns children; any of its threads may wait. */
    if(!child)return;
    for(resource_waiter *w=resource_waiters;w;w=w->next)if(w->pid==process_owner_pid(child)) {
        __atomic_store_n(&w->woken,1,__ATOMIC_RELEASE);process_wake(w->tid);
    }
    if(!child->parent_pid)return;
    for(process_t *p=process_list;p;p=p->next) {
        if(p->state==PROC_ZOMBIE || process_owner_pid(p)!=child->parent_pid ||
           !p->wait_child_active ||
           (p->wait_child_pid>0 && (uint32_t)p->wait_child_pid!=child->pid))continue;
        process_wake(p->pid);
    }
}

/* Bounded diagnostics for the temporary clone/vfork-style child used by the
 * Linux compatibility layer.  These messages expose lifecycle ordering only;
 * they do not participate in scheduling or process ownership. */
static uint32_t scheduler_spawn_trace_count;

static void scheduler_spawn_trace(const char *event, const process_t *process,
                                  uint64_t a, uint64_t b) {
    if (!VEX_COMPAT_TRACE) return;
    if (scheduler_spawn_trace_count >= 128u) {
        return;
    }
    ++scheduler_spawn_trace_count;
    debug_log_puts("spawn-life: ");
    debug_log_puts(event);
    debug_log_puts(" pid=");
    scheduler_log_u64_dec(process == NULL ? 0u : process->pid);
    debug_log_puts(" owner=");
    scheduler_log_u64_dec(process == NULL ? 0u : process_owner_pid(process));
    debug_log_puts(" tid=");
    scheduler_log_u64_dec(process == NULL ? 0u : process->pid);
    debug_log_puts(" a=");
    scheduler_log_u64_dec(a);
    debug_log_puts(" b=");
    scheduler_log_u64_dec(b);
    debug_log_puts("\n");
}

static void scheduler_cap_trace(const char *event, const process_t *process) {
    if (process == NULL) return;
    debug_log_puts("cap-trace: "); debug_log_puts(event);
    debug_log_puts(" pid="); scheduler_log_u64_dec(process->pid);
    debug_log_puts(" owner="); scheduler_log_u64_dec(process_owner_pid(process));
    debug_log_puts(" ppid="); scheduler_log_u64_dec(process->parent_pid);
    debug_log_puts(" caps="); scheduler_log_u64_dec(process->capabilities | process->elevated_capabilities);
    debug_log_puts(" user="); scheduler_log_u64_dec(process->user_mode);
    debug_log_puts(" abi="); scheduler_log_u64_dec(process->abi);
    debug_log_puts("\n");
}

static int process_is_group_thread(const process_t *process) {
    return process != NULL && process->group != NULL &&
           process->pid != process->group->process_id;
}

static process_group_t *process_group_create(uint32_t process_id, uint64_t cr3,
                                             uint64_t user_alloc_base,
                                             uint64_t user_alloc_next,
                                             uint8_t owns_address_space,
                                             uint8_t abi) {
    process_group_t *group = kmalloc(sizeof(*group));

    if (group == NULL) {
        return NULL;
    }
    memset(group, 0, sizeof(*group));
    group->process_id = process_id;
    group->exit_order=0x80000000u;
    group->birth_monotonic_ns=sched_clock_ns();
    group->birth_time_valid=vex_realtime_ns(&group->birth_realtime_ns)==0;
    group->refcount = 1u;
    group->storage_refs = 1u;
    if(smp_cpu_count()>1u) {
        flow_group_view_t *views=kmalloc((size_t)smp_cpu_count()*sizeof(*views));
        if(!views){kfree(group);return NULL;}
        if(flow_group_bind_views(&group->scheduling,views,smp_cpu_count())){kfree(views);kfree(group);return NULL;}
    }
    group->cr3 = cr3;
    group->user_alloc_base = user_alloc_base;
    group->user_alloc_next = user_alloc_next;
    group->owns_address_space = owns_address_space;
    group->user_mode = 1u;
    group->abi = abi;
    if(current_process && current_process->group)
        group->fs_sealed=__atomic_load_n(&current_process->group->fs_sealed,__ATOMIC_ACQUIRE);
    memcpy(group->name, "user-task", sizeof("user-task"));
    return group;
}

static void scheduler_copy_task_name(char destination[SCHEDULER_TASK_NAME_SIZE],
                                     const char *source) {
    const char *base = source;
    size_t out = 0u;
    if (destination == NULL) return;
    memset(destination, 0, SCHEDULER_TASK_NAME_SIZE);
    if (source == NULL) source = "task";
    for (const char *cursor = source; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
    }
    while (base[out] != '\0' && out + 1u < SCHEDULER_TASK_NAME_SIZE) {
        const unsigned char value = (unsigned char)base[out];
        destination[out] = (value >= 0x20u && value <= 0x7eu) ? (char)value : '_';
        ++out;
    }
    if (out == 0u) memcpy(destination, "task", sizeof("task"));
}

int scheduler_set_process_name(uint32_t pid, const char *name) {
    FLOW_LOCK;
    process_t *process = find_process(pid);
    if (process == NULL || name == NULL || name[0] == '\0') return -1;
    if (process->group != NULL) scheduler_copy_task_name(process->group->name, name);
    else scheduler_copy_task_name(process->name, name);
    return 0;
}

static int process_group_attach(process_t *thread, process_group_t *group) {
    if (thread == NULL || group == NULL) {
        return -1;
    }
    {
    FLOW_LOCK;
    if(group->terminating)return -1;
    uint32_t refs=__atomic_load_n(&group->refcount,__ATOMIC_ACQUIRE);
    do {
        if(!refs || refs==UINT32_MAX)return -1;
    } while(!__atomic_compare_exchange_n(&group->refcount,&refs,refs+1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
    scheduler_flow_rebind(thread,group);
    thread->group = group;
    thread->cr3 = group->cr3;
    thread->user_alloc_base = group->user_alloc_base;
    thread->user_alloc_next = group->user_alloc_next;
    thread->shares_address_space = 1u;
    }
    /* create_process() briefly registers every schedulable object under its
       thread id.  Once this object becomes a process-group thread, retire
       that transient record and keep only the process identity visible to
       the watchdog. */
    security_watchdog_reap_pid(thread->pid);
    security_watchdog_note_process_state(group->process_id, thread->uid,
                                         thread->user_mode, thread->capabilities,
                                         thread->elevated_capabilities,
                                         thread->elevated_capabilities_expires_tick);
    return 0;
}

static void process_group_sync_alloc(process_group_t *group, uint64_t next) {
    if (group == NULL) {
        return;
    }
    if (next < group->user_alloc_base) {
        next = group->user_alloc_base;
    }
    group->user_alloc_next = next;
    for (process_t *process = process_list; process != NULL; process = process->next) {
        if (process->group == group) {
            process->user_alloc_next = next;
            process->user_alloc_base = group->user_alloc_base;
            process->cr3 = group->cr3;
        }
    }
}

static void scheduler_refresh_process_capabilities(process_t *process) {
    if (process == NULL) {
        return;
    }

    if (process->elevated_capabilities != 0u &&
        process->elevated_capabilities_expires_tick != 0u &&
        scheduler_ticks >= process->elevated_capabilities_expires_tick) {
        process->elevated_capabilities = 0u;
        process->elevated_capabilities_expires_tick = 0u;
    }

    process->is_admin = (((process->capabilities | process->elevated_capabilities) & ~VEX_CAP_NET_CONNECT) != 0u) ? 1u : 0u;
}

static uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PAGE_ADDR_MASK;
}

static void write_msr(uint32_t msr, uint64_t value) {
    const uint32_t low = (uint32_t)value;
    const uint32_t high = (uint32_t)(value >> 32u);

    __asm__ volatile ("wrmsr" :: "c"(msr), "a"(low), "d"(high) : "memory");
}
static uint64_t read_user_gs_slot(void) {
    uint32_t lo,hi;
    __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(MSR_USER_GS_SLOT):"memory");
    return ((uint64_t)hi<<32)|lo;
}

static void scheduler_apply_current_tls_state(void) {
    if (current_process == NULL) {
        return;
    }

    write_msr(MSR_FS_BASE, current_process->fs_base);
}

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static uint64_t process_kernel_stack_top(const process_t *process) {
    return process == NULL ? 0u : (uint64_t)process->stack + process->kernel_stack_size;
}

static void process_trampoline(void (*entry)(void)) {
    if (current_process != NULL && current_process->user_mode != 0) {
        if (current_process->pid == 6u) {
            uint16_t live_cw;
            __asm__ volatile ("fnstcw %0" : "=m"(live_cw));
            debug_log_puts("fpu-user-entry: pid=6 cw=");
            debug_log_u64_hex(live_cw);
            debug_log_puts("\n");
        }
        scheduler_apply_current_tls_state();
        scheduler_execution_mode(1);
        user_enter((uint64_t)entry, current_process->user_rsp);
    }

    entry();
    process_exit();
    halt_forever();
}

static void fork_resume_trampoline(void) {
    if (current_process == NULL || current_process->resume_syscall_rsp == 0u) {
        process_exit_status(1);
    }

    current_process->user_mode = 1u;
    gdt_set_tss_rsp0(process_kernel_stack_top(current_process));
    scheduler_apply_current_tls_state();
    /* Native start-paused threads use the same user-boundary rendezvous as
       an already running thread; their constructor never executes in ring 3. */
    scheduler_user_return(current_process->resume_syscall_rsp,0u);
    resume_syscall_return(current_process->resume_syscall_rsp, 0u);
    halt_forever();
}

static void process_group_storage_release(process_group_t *group);
static void exec_resume_trampoline(void) {
    if (current_process == NULL || current_process->pending_user_entry == 0u || current_process->user_rsp == 0u) {
        process_exit_status(1);
    }

    if (current_process->pending_old_cr3 != 0u) {
        if (current_process->pending_old_cr3_owned != 0u) {
            vmm_destroy_address_space(vmm_cr3_to_address_space(current_process->pending_old_cr3));
        }
        current_process->pending_old_cr3 = 0u;
        current_process->pending_old_cr3_owned = 0u;
    }

    /* The switch has already activated the new root. Releasing the old
       storage lease earlier could free pages beneath our syscall stack. */
    process_group_t *borrowed=current_process->borrowed_memory_group;
    current_process->borrowed_memory_group=NULL;
    process_group_storage_release(borrowed);
    current_process->user_mode = 1u;
    gdt_set_tss_rsp0(process_kernel_stack_top(current_process));
    scheduler_apply_current_tls_state();
    scheduler_execution_mode(1);
    user_enter(current_process->pending_user_entry, current_process->user_rsp);
    process_exit_status(1);
}

static void append_process(process_t *process) {
    FLOW_LOCK;
    SCHED_LOCK; scheduler_account(sched_clock_ns());
    process->zombie_next=NULL;process->zombie_queued=0;
    process->next = NULL;
    process->pid_next=pid_index[process->pid&255u];
    pid_index[process->pid&255u]=process;
    flow_thread_init(&flow_local,&process->scheduling,process->group ? &process->group->scheduling : NULL,process);
    /* Device/desktop service contexts keep their IRQ locality. Ordinary user
       Processes are not pinned; placement and idle stealing share FlowSched's
       same group entitlement and exclusive context claim. */
    process->scheduling.pinned_cpu=process->user_mode?UINT32_MAX:0u;
    if(!process->group && process->private_flow_views &&
       flow_group_bind_views(&process->scheduling.private_group,process->private_flow_views,smp_cpu_count()))halt_forever();
    if(process->state==PROC_READY)
        flow_runnable(&flow_local,&process->scheduling,sched_clock_ns());
    if (current_process != NULL && current_process->pid != 0u) {
        flow_adopt(&flow_local, &process->scheduling,
                   current_process->scheduling.effective, sched_clock_ns());
    }

    if (process_list == NULL) {
        process_list = process;
        return;
    }

    process_t *tail = process_list;
    while (tail->next != NULL) {
        tail = tail->next;
    }

    tail->next = process;
}

/* Final storage retirement is separate from thread/group retirement. A spawn
 * child may still fault a parent's sparse backing after its final thread died. */
static void retirement_trace(uint32_t pid,const char *stage,uint64_t start) {
#if VEX_DESKTOP_TRACE
    uint64_t elapsed=sched_clock_ns()-start;
    char line[128];snprintf(line,sizeof(line),"retire-trace: pid=%u stage=%s us=%u\n",pid,stage,(unsigned)(elapsed/1000));
    debug_log_puts(line);
#else
    (void)pid;(void)stage;(void)start;
#endif
}
static void process_storage_reap(uint32_t owner_pid,uint64_t owner_cr3,uint8_t owns_address_space) {
        uint64_t mark=VEX_DESKTOP_TRACE?sched_clock_ns():0;
        if (owns_address_space != 0u && owner_cr3 != 0u) {
            vex_jit_reap_pid(owner_pid, owner_cr3);
            retirement_trace(owner_pid,"jit",mark);mark=VEX_DESKTOP_TRACE?sched_clock_ns():0;
            /* Include materialized but unpublished construction inventories:
               ORA/shared leaves must not be freed as anonymous VMM pages. */
            process_memory_reap_mappings(owner_pid,owner_cr3);
            retirement_trace(owner_pid,"mappings",mark);mark=VEX_DESKTOP_TRACE?sched_clock_ns():0;
            /* SHM capabilities belong to the process identity.  Revoke them
             * only after the last thread's mappings have been torn down so
             * backing refs are released exactly once. */
            vex_shm_reap_pid(owner_pid);
            vmm_destroy_address_space(vmm_cr3_to_address_space(owner_cr3));
            retirement_trace(owner_pid,"shm+tables",mark);mark=VEX_DESKTOP_TRACE?sched_clock_ns():0;
        }

        app_surface_reap_pid(owner_pid);
        compositor_surface_reap_pid(owner_pid);
        gpu_reap_pid(owner_pid);
        retirement_trace(owner_pid,"surfaces+gpu",mark);mark=VEX_DESKTOP_TRACE?sched_clock_ns():0;
        vex_event_counter_reap_pid(owner_pid);
        execution_reap_owner(owner_pid);
        versioned_reap(owner_pid);
        process_memory_reap(owner_pid);
        security_mrac_reap_pid(owner_pid);
        security_watchdog_reap_pid(owner_pid);
        elevation_process_exit(owner_pid);
        ladybird_fd_close_process(owner_pid);
        retirement_trace(owner_pid,"objects+fds",mark);mark=VEX_DESKTOP_TRACE?sched_clock_ns():0;
        kernel_allocator_reap(owner_pid);
        retirement_trace(owner_pid,"allocator",mark);

}
static int process_group_storage_retain(process_group_t *group) {
    if(!group)return -1;
    uint32_t refs=__atomic_load_n(&group->storage_refs,__ATOMIC_ACQUIRE);
    do {if(!refs || refs==UINT32_MAX)return -1;}
    while(!__atomic_compare_exchange_n(&group->storage_refs,&refs,refs+1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
    return 0;
}
static void process_group_storage_release(process_group_t *group) {
    if(!group)return;
    uint32_t refs=__atomic_fetch_sub(&group->storage_refs,1u,__ATOMIC_ACQ_REL);
    if(!refs)halt_forever();
    if(refs!=1u)return;
    process_storage_reap(group->process_id,group->cr3,group->owns_address_space);
    kfree(group->scheduling.views);kfree(group);
}

static void destroy_process(process_t *process) {
    process_group_t *group;

    if (process == NULL || process->pid == 0u) {
        return;
    }

    { SCHED_LOCK;FLOW_LOCK;flow_forget(&flow_local,&process->scheduling,sched_clock_ns()); }
    vex_event_thread_exit(process);
    execution_collect();

    group = process->group;
    /* A native thread owns only its private stack mapping. Release it while
       the shared process address space is still alive; the process-memory
       namespace remains owned by the group until its final thread exits. */
    if (process->owned_user_stack_base != 0u &&
        process->owned_user_stack_size != 0u) {
        (void)process_memory_remove_region(process_owner_pid(process),
            process->cr3, process->owned_user_stack_base,
            (size_t)process->owned_user_stack_size);
        process->owned_user_stack_base = 0u;
        process->owned_user_stack_size = 0u;
    }
    if (process->spawn_shared != 0u) {
        /* The temporary spawn child has a private descriptor table even
           while it borrows the parent's process group/address space. */
        ladybird_fd_close_process(process->pid);
    }
    if(group) {
        if(__atomic_fetch_sub(&group->refcount,1u,__ATOMIC_ACQ_REL)==1u)
            process_group_storage_release(group);
    } else process_storage_reap(process->pid,process->cr3,process->user_mode);
    /* This context is quiescent and cannot access its borrowed root again. */
    process_group_storage_release(process->borrowed_memory_group);
    process->borrowed_memory_group=NULL;

    if (process->stack != NULL && hhdm_request.response != NULL) {
        const uint64_t page_count = process->kernel_stack_size / 4096ull;
        pmm_free_contiguous_pages(
            (void *)((uint64_t)process->stack - hhdm_request.response->offset),
            page_count
        );
    }

    kfree(process->execution_per_cpu);
    kfree(process->scheduling.allowed_cpus);
    kfree(process->private_flow_views);kfree_aligned(process);
}

static int parent_alive(uint32_t pid) {
    return pid != 0u && find_process_identity(pid) != NULL;
}

static void scheduler_stopped_cleanup(process_t *p) {
    {FLOW_LOCK;resource_waiter **w=&resource_waiters;while(*w){if((*w)->tid==p->pid)*w=(*w)->next;else w=&(*w)->next;}}
    /* This context is no longer executing on any CPU. It can no longer read
       linked waiters on its abandoned stack. No scheduler metadata gate is
       held while subsystem locks/owner-death handoffs are processed. */
    process_spawn_child_ready(p->pid);
    futex_cancel_pid(p->pid);
    linux_futex_pi_thread_stopped(p->pid);
    vex_event_counter_thread_stopped(p->pid);
    vex_message_thread_stopped(p->pid);
    vex_rendezvous_thread_stopped(p->pid);
    compositor_surface_thread_stopped(p->pid);
    gpu_thread_stopped(p->pid);
    pipe_thread_stopped(p->pid);
    vex_file_range_thread_stopped(p->pid);
    vex_fd_borrow_thread_stopped(p);
    vex_scratch_thread_stopped(p);
    dns_thread_stopped(p->pid);
    /* This CPU can have a different CR3. Never dereference the old user's
       pointer here. Pin the inventory transaction and write its actual page. */
    if(p->clear_child_tid) {
        kernel_guard_t memory_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
        uint64_t address=p->clear_child_tid;
        void *root=vmm_cr3_to_address_space(p->cr3);
        uint64_t flags=vmm_get_page_flags(root,address);
        uint64_t physical=vmm_get_physical(root,address);
        if(address<VMM_USER_ADDRESS_LIMIT && !(address&3u) && physical &&
           (flags&(VMM_PAGE_PRESENT|VMM_PAGE_USER|VMM_PAGE_WRITABLE))==
             (VMM_PAGE_PRESENT|VMM_PAGE_USER|VMM_PAGE_WRITABLE)) {
            __atomic_store_n((uint32_t *)(uintptr_t)(hhdm_request.response->offset+physical),0u,__ATOMIC_RELEASE);
            futex_wake_key(p->group?p->group->cr3:p->cr3,address);
        }
        p->clear_child_tid=0u;
    }
    uint32_t close_owner=0u;
    {
        FLOW_LOCK;
        int last=1;
        for(process_t *q=process_list;q;q=q->next)
            if(q!=p && p->group && q->group==p->group &&
               (q->state!=PROC_ZOMBIE || q->scheduling.running_cpu!=UINT32_MAX ||
                !q->stopped_cleanup_done)){last=0;break;}
        if(last && (!p->group || !p->group->descriptors_closed)) {
            close_owner=process_owner_pid(p);
            if(p->group)p->group->descriptors_closed=1u;
            for(process_t *q=process_list;q;q=q->next)
                if(q->parent_pid==close_owner)q->parent_pid=0u;
        }
    }
    if(close_owner)ladybird_fd_close_process(close_owner);
    {
        FLOW_LOCK;
        p->stopped_cleanup_done=2u;
        resource_publish_completion();
        /* Publish completion after waiters/operation refs/FD closure. A parent
           woken by the initial zombie transition may have registered again. */
        for(process_t *q=process_list;q;q=q->next)
            if(q==p || (p->group && q->group==p->group))wake_child_waiters(q);
        if(p->join_waiter_tid)process_wake(p->join_waiter_tid);
    }
}

static uint64_t pending_retirements;
uint64_t scheduler_pending_retirements(void) {
    return __atomic_load_n(&pending_retirements,__ATOMIC_ACQUIRE);
}
static void reap_zombies(process_t *skip) {
    /* Only retirements are visited. Live tasks never participate in dispatch
       scanning. Cleanup can synchronously wake tasks and retire more objects. */
    for(;;) {
        process_t *retired=NULL,*stopped=NULL;
        {
            FLOW_LOCK;
            process_t **pending=&zombie_list;
            while(*pending) {
                process_t *p=*pending;
                if(p==skip || p->scheduling.running_cpu!=UINT32_MAX){pending=&p->zombie_next;continue;}
                /* A killed thread will never unwind the RAII guards on its
                   abandoned kernel stack. Cancel them before testing incoming
                   leases, so mutually dead borrowers cannot retain a cycle. */
                if(p->context_wait_tid){p->context_wait_tid=0;--context_waiter_count;}
                while(p->outgoing_refs)scheduler_task_release(p->outgoing_refs);
                if(!p->stopped_cleanup_done) {
                    if(p->borrowed_refs==UINT32_MAX)halt_forever();
                    p->stopped_cleanup_done=1u;++p->borrowed_refs;
                    stopped=p;break;
                }
                int aborted_builder=0;
                for(process_t *child=process_list;child;child=child->next) {
                    if(child->state!=PROC_CONSTRUCTING || child->construction_tid!=p->pid)continue;
                    child->construction_tid=0u;child->construction_cpu=0u;
                    child->wait_collected=1u;child->parent_pid=0u;
                    scheduler_set_state(child,PROC_ZOMBIE);aborted_builder=1;
                }
                /* Marking a builder prepends to zombie_list. Restart instead
                   of overwriting its new head while detaching its creator. */
                if(aborted_builder){pending=&zombie_list;continue;}
                if(p->borrowed_refs || (!p->wait_collected && parent_alive(p->parent_pid))) {pending=&p->zombie_next;continue;}
                *pending=p->zombie_next;p->zombie_queued=0;
                process_t **link=&process_list;while(*link && *link!=p)link=&(*link)->next;
                if(*link)*link=p->next;
                process_t **pid_link=&pid_index[p->pid&255u];
                while(*pid_link && *pid_link!=p)pid_link=&(*pid_link)->pid_next;
                if(*pid_link)*pid_link=p->pid_next;
                __atomic_add_fetch(&pending_retirements,1u,__ATOMIC_ACQ_REL);
                retired=p;break;
            }
        }
        if(stopped) {
            scheduler_stopped_cleanup(stopped);
            {FLOW_LOCK;--stopped->borrowed_refs;}
            continue;
        }
        if(!retired)break;
        destroy_process(retired);
        __atomic_sub_fetch(&pending_retirements,1u,__ATOMIC_RELEASE);
    }
}

static uint64_t hhdm_stack_top(uint64_t physical_stack_base, uint64_t stack_size) {
    return hhdm_request.response->offset + physical_stack_base + stack_size;
}

static uint64_t initial_process_rsp(uint64_t stack_top, void (*entry)(void)) {
    uint64_t *sp = (uint64_t *)(stack_top - sizeof(uint64_t));

    /* The assembly switch restores r15..rax, RFLAGS, then ret. This frame makes the
       first switch enter process_trampoline(entry) with a clean register set. */
    *--sp = (uint64_t)process_trampoline;
    *--sp = 0x202u;           /* RFLAGS: reserved bit + interrupts enabled */
    *--sp = 0;                /* rax */
    *--sp = 0;                /* rbx */
    *--sp = 0;                /* rcx */
    *--sp = 0;                /* rdx */
    *--sp = 0;                /* rbp */
    *--sp = 0;                /* rsi */
    *--sp = (uint64_t)entry;  /* rdi */
    *--sp = 0;                /* r8 */
    *--sp = 0;                /* r9 */
    *--sp = 0;                /* r10 */
    *--sp = 0;                /* r11 */
    *--sp = 0;                /* r12 */
    *--sp = 0;                /* r13 */
    *--sp = 0;                /* r14 */
    *--sp = 0;                /* r15 */

    return (uint64_t)sp;
}

static process_t *find_process(uint32_t pid) {
    if(!pid)return &kernel_process;
    for(process_t *p=pid_index[pid&255u];p;p=p->pid_next)if(p->pid==pid)return p;
    return NULL;
}

/* Process IDs remain valid while any thread in the group is alive.  Keep
   exact-TID lookup above for thread operations, and use this fallback only
   for APIs whose contract names the process identity. */
static process_t *find_process_identity(uint32_t process_id) {
    process_t *process = find_process(process_id);

    if (process != NULL) {
        return process;
    }

    for (process = process_list; process != NULL; process = process->next) {
        if (process_owner_pid(process) == process_id) {
            return process;
        }
    }

    return NULL;
}

static process_t *select_next_ready(void) {
    FLOW_LOCK;
    flow_thread_t *t=flow_pick(&flow_local);return t?t->entity.owner:NULL;
}

uint32_t scheduler_current_pid(void) {
    return process_owner_pid(current_process);
}

uint32_t scheduler_current_process_id(void) {
    return process_owner_pid(current_process);
}

uint32_t scheduler_current_memory_owner_pid(void) {
    if (current_process == NULL) {
        return 0u;
    }
    if(current_process->borrowed_memory_group &&
       current_process->cr3==current_process->borrowed_memory_group->cr3)
        return current_process->borrowed_memory_group->process_id;
    return current_process->spawn_parent_pid != 0u
        ? current_process->spawn_parent_pid
        : process_owner_pid(current_process);
}

uint32_t scheduler_current_tid(void) {
    return current_process == NULL ? 0u : current_process->pid;
}

uint32_t scheduler_current_ppid(void) {
    return current_process == NULL ? 0u : current_process->parent_pid;
}

uint8_t scheduler_process_accepts_input(uint32_t pid) {
    FLOW_LOCK;
    process_t *process = find_process_identity(pid);

    if (process == NULL) {
        return 0;
    }

    if (process->state != PROC_READY && process->state != PROC_RUNNING) {
        return 0;
    }

    return 1;
}

void scheduler_init(void) {
    vex_cpu_t *bsp=smp_cpu_at(0u);
    bsp->scheduler=&scheduler_boot;
    bsp->scheduler_enabled=1u;
    scheduler_fpu_init();
    flow_cpu_init(&flow_local);
    memset(pid_index,0,sizeof(pid_index));
    zombie_list=NULL;

    kernel_process.pid = 0;
    kernel_process.stack = NULL;
    kernel_process.kernel_stack_size = 0u;
    kernel_process.rsp = 0;
    kernel_process.cr3 = read_cr3();
    kernel_process.state = PROC_RUNNING;
    kernel_process.priority = 0;
    kernel_process.uid = 0;
    kernel_process.is_admin = 1;
    kernel_process.capabilities = VEX_CAP_ALL;
    kernel_process.elevated_capabilities = 0u;
    kernel_process.user_mode = 0;
    kernel_process.in_user_syscall = 0;
    kernel_process.abi = VEX_ABI_NATIVE;
    kernel_process.user_rsp = 0;
    kernel_process.user_alloc_base = 0;
    kernel_process.user_alloc_next = 0;
    kernel_process.sleep_until_tick = 0;
    kernel_process.parent_pid = 0;
    kernel_process.exit_status = 0;
    kernel_process.wait_collected = 0;
    kernel_process.syscall_frame_rsp = 0;
    kernel_process.resume_syscall_rsp = 0;
    kernel_process.pending_user_entry = 0;
    kernel_process.pending_old_cr3 = 0;
    kernel_process.pending_old_cr3_owned = 0u;
    kernel_process.fs_base = 0;
    kernel_process.user_gs_base = 0;
    kernel_process.shares_address_space = 0;
    kernel_process.clear_child_tid = 0;
    kernel_process.elevated_capabilities_expires_tick = 0u;
    kernel_process.group = NULL;
    scheduler_fpu_copy(kernel_process.fpu_state, scheduler_initial_fpu);
    kernel_process.next = NULL;

    process_list = &kernel_process;
    current_process = &kernel_process;
    next_pid = 1;
    scheduler_ticks = 0;
    scheduler_idle_ticks = 0;
    for(uint32_t i=1;i<smp_cpu_count();++i) {
        scheduler_cpu_t *state=kmalloc_aligned(sizeof(*state),64u);
        if(!state) {debug_log_puts("smp: scheduler state OOM\n");for(;;)__asm__ volatile("cli; hlt");}
        memset(state,0,sizeof(*state));
        memcpy(&state->idle,&kernel_process,sizeof(process_t));
        state->idle.next=NULL;state->current=&state->idle;
        flow_cpu_init(&state->flow);state->flow.cpu_id=i;
        smp_cpu_at(i)->scheduler=state;
    }
    if(smp_cpu_count()>1u) {
        flow_cpu_t **index=kmalloc((size_t)smp_cpu_count()*sizeof(*index));
        if(!index){debug_log_puts("smp: flow domain OOM\n");halt_forever();}
        flow_domain_init(&scheduling_domain,index,smp_cpu_count());
        for(uint32_t i=0;i<smp_cpu_count();++i) {
            scheduler_cpu_t *state=smp_cpu_at(i)->scheduler;
            if(flow_cpu_join(&state->flow,&scheduling_domain,i))halt_forever();
        }
    }
}

int scheduler_init_current_cpu(void) {
    if(scheduler_xsave_mask) {
        uint32_t a,b,c,d;
        scheduler_cpuid(1u,0u,&a,&b,&c,&d);
        if(!(c&(1u<<26u)) || !(c&(1u<<28u)))return -1;
        scheduler_cpuid(0xdu,0u,&a,&b,&c,&d);
        if((a&scheduler_xsave_mask)!=scheduler_xsave_mask || b>SCHEDULER_FPU_STATE_SIZE)return -1;
        uint64_t cr4;__asm__ volatile("mov %%cr4,%0":"=r"(cr4));cr4|=1ull<<18u;
        __asm__ volatile("mov %0,%%cr4"::"r"(cr4):"memory");
        __asm__ volatile("xsetbv"::"c"(0u),"a"((uint32_t)scheduler_xsave_mask),"d"((uint32_t)(scheduler_xsave_mask>>32u)):"memory");
    }
    scheduler_restore_fpu_state(kernel_process.fpu_state);
    flow_account_at=sched_clock_ns();
    scheduler_local()->account_tick=flow_account_at/10000000ull;
    return 0;
}

static uint32_t create_process(void (*entry)(void), uint8_t priority, uint64_t cr3,
                               uint8_t user_mode, uint64_t user_rsp, uint8_t abi,
                               uint64_t requested_stack_size, uint32_t user_caps, int publish) {
    uint64_t stack_size;

    if (entry == NULL || hhdm_request.response == NULL || current_process == NULL) {
        return 0;
    }

    stack_size = requested_stack_size < PROCESS_STACK_SIZE
        ? PROCESS_STACK_SIZE : requested_stack_size;
    if (stack_size > PROCESS_STACK_MAX_SIZE) {
        return 0;
    }
    stack_size = (stack_size + 4095ull) & ~4095ull;

    process_t *process = kmalloc_aligned(sizeof(process_t), 64u);
    if (process == NULL) {
        return 0;
    }
    memset(process,0,sizeof(*process));

    void *physical_stack = pmm_alloc_contiguous_pages(stack_size / 4096ull);
    if (physical_stack == NULL) {
        kfree_aligned(process);
        return 0;
    }

    const uint64_t stack_base = (uint64_t)physical_stack;
    const uint64_t stack_top = hhdm_stack_top(stack_base, stack_size);

    uint32_t allocated_pid=__atomic_load_n(&next_pid,__ATOMIC_ACQUIRE);
    do {
        if(!allocated_pid || allocated_pid==UINT32_MAX) {
            pmm_free_contiguous_pages(physical_stack,stack_size/4096u);
            kfree_aligned(process);return 0u;
        }
    } while(!__atomic_compare_exchange_n(&next_pid,&allocated_pid,allocated_pid+1u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
    process->pid = allocated_pid;
    process->stack = (uint64_t *)(stack_top - stack_size);
    process->kernel_stack_size = stack_size;
    process->rsp = initial_process_rsp(stack_top, entry);
    process->cr3 = cr3 == 0 ? read_cr3() : cr3;
    process->state = PROC_CONSTRUCTING;
    process->construction_tid=scheduler_current_tid();
    process->construction_cpu=smp_current_index();
    process->wait_child_pid=0;process->wait_child_active=0u;
    process->stopped_cleanup_done=0u;
    process->terminate_requested=0u;process->terminate_status=0;
    process->compat_stdio_ensuring=0u;
    process->fd_borrows=NULL;
    process->priority = priority;
    scheduler_copy_task_name(process->name, user_mode != 0u ? "user-task" : "kernel-task");
    process->linux_sched_policy = 0u;
    process->linux_sched_priority = 0;
    process->linux_sched_reset_on_fork = 0u;
    process->uid = current_process->uid;
    process->capabilities = user_mode != 0 ? security_cap_sanitize(user_caps) : current_process->capabilities;
    process->elevated_capabilities = 0u;
    process->is_admin = (process->capabilities & ~VEX_CAP_NET_CONNECT) != 0u;
    process->user_mode = user_mode != 0;
    process->in_user_syscall = 0u;
    process->abi = user_mode != 0 ? abi : VEX_ABI_NATIVE;
    process->user_rsp = user_rsp;
    process->user_alloc_base = user_mode != 0 ? USER_ALLOC_BASE : 0;
    process->user_alloc_next = user_mode != 0 ? USER_ALLOC_BASE : 0;
    process->sleep_until_tick = 0;
    process->cpu_ticks = 0u;
    process->parent_pid = process_owner_pid(current_process);
    process->exit_status = 0;
    process->wait_collected = 0;
    process->syscall_frame_rsp = 0;
    process->resume_syscall_rsp = 0;
    process->pending_user_entry = 0;
    process->pending_old_cr3 = 0;
    process->pending_old_cr3_owned = 0u;
    process->fs_base = 0u;
    process->user_gs_base = 0u;
    process->shares_address_space = 0u;
    process->spawn_shared = 0u;
    process->spawn_parent_pid = 0u;
    process->borrowed_memory_group = NULL;
    process->spawn_wait_child_pid = 0u;
    process->spawn_wait_done = 0u;
    process->clear_child_tid = 0u;
    process->owned_user_stack_base = 0u;
    process->owned_user_stack_size = 0u;
    process->join_waiter_tid = 0u;
    process->native_thread_joinable = 0u;
    memset(process->reserved_thread, 0, sizeof(process->reserved_thread));
    process->elevated_capabilities_expires_tick = 0u;
    process->event_pending = 0u;
    process->event_mask = 0u;
    process->event_alt_stack_base = 0u;
    process->event_alt_stack_size = 0u;
    memset(process->event_sender_pid, 0, sizeof(process->event_sender_pid));
    process->linux_signal_mask = 0u;
    process->event_active = 0u;
    process->event_saved_event = 0u;
    process->event_reserved = 0u;
    process->event_saved_mask = 0u;
    process->event_user_context_addr = 0u;
    process->event_return_rax = 0u;
    process->event_context_kind = VEX_EVENT_CONTEXT_NONE;
    process->event_return_rax_valid = 0u;
    memset(process->event_saved_frame, 0, sizeof(process->event_saved_frame));
    process->event_saved_fpu_valid = 0u;
    memset(process->event_saved_fpu_state, 0, sizeof(process->event_saved_fpu_state));
    process->group = NULL;
    process->borrowed_refs=0u;process->outgoing_refs=NULL;
    process->kernel_errno=0;process->private_flow_views=NULL;
    scheduler_fpu_copy(process->fpu_state, scheduler_initial_fpu);
    process->next = NULL;

    if (user_mode != 0u) {
        process->group = process_group_create(process->pid, process->cr3,
                                              process->user_alloc_base,
                                              process->user_alloc_next,
                                              1u, abi);
        if (process->group == NULL) {
            pmm_free_contiguous_pages(physical_stack, stack_size / 4096ull);
            kfree_aligned(process);
            return 0u;
        }
    }

    if(smp_cpu_count()>1u && !process->group) {
        process->private_flow_views=kmalloc((size_t)smp_cpu_count()*sizeof(*process->private_flow_views));
        if(!process->private_flow_views){pmm_free_contiguous_pages(physical_stack,stack_size/4096u);kfree_aligned(process);return 0u;}
    }
    if (process->cr3 != 0u) {
        vmm_sync_kernel_mappings(vmm_cr3_to_address_space(process->cr3));
    }
    if(smp_cpu_count()>1u) {
        process->execution_per_cpu=kmalloc((size_t)smp_cpu_count()*sizeof(uint64_t));
        if(!process->execution_per_cpu) {
            if(process->group){kfree(process->group->scheduling.views);kfree(process->group);}
            kfree(process->private_flow_views);
            pmm_free_contiguous_pages(physical_stack,stack_size/4096u);kfree_aligned(process);return 0;
        }
        memset(process->execution_per_cpu,0,(size_t)smp_cpu_count()*sizeof(uint64_t));
    }

    append_process(process);
    security_watchdog_note_process_state(process_owner_pid(process), process->uid, process->user_mode,
                                         process->capabilities, process->elevated_capabilities,
                                         process->elevated_capabilities_expires_tick);
    if ((process->capabilities & VEX_CAP_NET_CONNECT) != 0u ||
        (current_process != NULL &&
         (current_process->capabilities & VEX_CAP_NET_CONNECT) != 0u)) {
        scheduler_cap_trace("create", process);
    }
    /* Publishing can immediately run and retire the child on another CPU.
       Do not access its object after this release of construction ownership. */
    if(publish && scheduler_publish_process(allocated_pid)!=0) {
        (void)scheduler_abort_process(allocated_pid);return 0u;
    }
    return allocated_pid;
}

uint32_t process_create_in_address_space(void (*entry)(void), uint8_t priority, uint64_t cr3) {
    return create_process(entry, priority, cr3, 0, 0, VEX_ABI_NATIVE, PROCESS_STACK_SIZE, 0u, 1);
}

uint32_t process_prepare_in_address_space(void (*entry)(void), uint8_t priority, uint64_t cr3) {
    return create_process(entry,priority,cr3,0,0,VEX_ABI_NATIVE,PROCESS_STACK_SIZE,0u,0);
}

static int construction_owned(process_t *p) {
    return p && p->state==PROC_CONSTRUCTING &&
        p->construction_tid==scheduler_current_tid() &&
        (p->construction_tid!=0u || p->construction_cpu==smp_current_index());
}
int scheduler_construction_owned(uint32_t pid) {FLOW_LOCK;return construction_owned(find_process(pid));}
int scheduler_resource_observe(scheduler_process_observer_t *o,uint32_t pid,int suspended) {
    FLOW_LOCK;process_t *p=find_process(pid);
    if(!p || (!construction_owned(p) && pid!=scheduler_current_process_id()) || p->borrowed_refs==UINT32_MAX)return -1;
    ++p->borrowed_refs;if(construction_owned(p)){p->wait_collected=1;p->resource_owned=1;p->resource_suspended=suspended!=0;}
    o->registered=1;o->info.pid=o->info.tid=pid;o->next=process_observers;process_observers=o;return 0;
}
void scheduler_resource_release(scheduler_process_observer_t *o) {
    {FLOW_LOCK;if(o->registered) {
        scheduler_process_observer_t **link=&process_observers;while(*link && *link!=o)link=&(*link)->next;
        if(!*link)halt_forever();*link=o->next;o->registered=0;
        process_t *p=find_process(o->info.pid);if(!p || !p->borrowed_refs)halt_forever();--p->borrowed_refs;
    }}
    reap_zombies(current_process);
}
static int resource_update(scheduler_process_observer_t *o) {
    process_t *p=find_process(o->info.pid);if(!p)return -ESRCH;
    int primary=p->state==PROC_ZOMBIE && p->stopped_cleanup_done==2u,exited=primary;
    for(process_t *q=process_list;exited && q;q=q->next)
        if(p->group && q->group==p->group && (q->state!=PROC_ZOMBIE || q->stopped_cleanup_done!=2u))exited=0;
    o->info=(vex_process_resource_info_t){o->info.pid,o->info.tid,(uint32_t)exited,p->resource_suspended,
        p->group && p->group->resource_status_valid?p->group->resource_exit_status:p->exit_status,(uint32_t)primary,p->exit_status,0};return 0;
}
static void resource_usage_update(scheduler_process_observer_t *o,uint64_t now) {
    process_t *p=find_process(o->info.pid);process_group_t *g=p?p->group:NULL;if(!g)return;
    uint64_t user=g->user_cpu_ns,kernel=g->kernel_cpu_ns;
    for(uint32_t i=0;i<smp_cpu_count();i++){
        scheduler_cpu_t *cpu=smp_cpu_at(i)->scheduler;process_t *running=cpu?cpu->current:NULL;
        if(running && running->group==g && running->scheduling.runnable && now>=cpu->account_at){
            if(running->accounting_user)user+=now-cpu->account_at;else kernel+=now-cpu->account_at;
        }
    }
    if(o->info.exited && !g->exit_realtime_ns && g->birth_time_valid)g->exit_realtime_ns=g->birth_realtime_ns+(now-g->birth_monotonic_ns);
    o->usage=(vex_process_usage_t){g->birth_realtime_ns,g->exit_realtime_ns,user,kernel,(g->birth_time_valid?1u:0u)|(o->info.exited?2u:0u),0};
}
static void resource_publish_completion(void) {
    scheduler_process_observer_t **link=&process_observers;
    while(*link) {
        scheduler_process_observer_t *o=*link;
        if(resource_update(o))halt_forever();
        if(!o->info.exited){link=&o->next;continue;}
        resource_usage_update(o,sched_clock_ns());
        process_t *p=find_process(o->info.pid);if(!p || !p->borrowed_refs)halt_forever();
        --p->borrowed_refs;*link=o->next;o->next=NULL;o->registered=0;
    }
}
int scheduler_resource_info(scheduler_process_observer_t *o,void *out) {
    FLOW_LOCK;if(o->registered && resource_update(o))return -ESRCH;
    *(vex_process_resource_info_t *)out=o->info;return 0;
}
int scheduler_resource_usage(scheduler_process_observer_t *o,vex_process_usage_t *out) {
    FLOW_LOCK;uint64_t now=sched_clock_ns();scheduler_account(now);
    if(o->registered){if(resource_update(o))return -ESRCH;resource_usage_update(o,now);}
    if(!(o->usage.flags&1u))return -ENOTSUP;*out=o->usage;return 0;
}
int64_t scheduler_process_exit_order(uint32_t pid,uint64_t rank,int query) {
    FLOW_LOCK;process_group_t *g=current_process?current_process->group:NULL;
    if(!g || g->process_id!=pid)return -EPERM;
    if(query)return g->exit_order;
    if(rank>UINT32_MAX)return -EINVAL;if(shutdown_order_active)return -EBUSY;
    g->exit_order=(uint32_t)rank;return 0;
}
void scheduler_shutdown_order_begin(int begin) {
    FLOW_LOCK;shutdown_order_active=begin!=0;
    for(process_t *p=process_list;p;p=p->next)if(p->group)p->group->shutdown_notified=0;
}
uint32_t scheduler_shutdown_next(uint32_t *tid) {
    FLOW_LOCK;process_t *best=NULL;
    for(process_t *p=process_list;p;p=p->next)if(p->user_mode && p->group &&
        p->state!=PROC_ZOMBIE && p->state!=PROC_CONSTRUCTING && !p->group->shutdown_notified &&
        (!best || p->group->exit_order>best->group->exit_order ||
         (p->group->exit_order==best->group->exit_order && p->group->process_id<best->group->process_id)))best=p;
    if(!best)return 0;best->group->shutdown_notified=1;*tid=best->pid;return best->group->process_id;
}
int scheduler_process_active(uint32_t pid) {
    FLOW_LOCK;for(process_t *p=process_list;p;p=p->next)if(process_owner_pid(p)==pid && (p->state!=PROC_ZOMBIE || p->stopped_cleanup_done!=2u))return 1;return 0;
}
void scheduler_inventory_counts(uint32_t *processes,uint32_t *threads){
    FLOW_LOCK;*processes=*threads=0;
    for(process_t *p=process_list;p;p=p->next)if(p->state!=PROC_ZOMBIE && p->state!=PROC_CONSTRUCTING){
        ++*threads;int seen=0;uint32_t owner=process_owner_pid(p);
        for(process_t *q=process_list;q!=p;q=q->next)if(q->state!=PROC_ZOMBIE && q->state!=PROC_CONSTRUCTING && process_owner_pid(q)==owner){seen=1;break;}
        if(!seen)++*processes;
    }
}
int scheduler_resource_wait(scheduler_process_observer_t *o,uint64_t timeout,int mode) {
    uint64_t now=scheduler_time_ns(),deadline=timeout>UINT64_MAX-now?UINT64_MAX:now+timeout;
    for(;;) {
        resource_waiter w={.tid=scheduler_current_tid(),.pid=o->info.pid};vex_process_resource_info_t info;
        {FLOW_LOCK;if(scheduler_resource_info(o,&info))return -ESRCH;if((mode&VEX_PROCESS_WAIT_THREAD)?info.primary_exited:info.exited)return 0;
         if(mode&VEX_PROCESS_WAIT_POLL)return -ETIMEDOUT;
         if(timeout && scheduler_time_ns()>=deadline)return -ETIMEDOUT;w.next=resource_waiters;resource_waiters=&w;}
        if(timeout){now=scheduler_time_ns();if(now<deadline){uint64_t left=deadline-now;process_wait_event_timeout(&w.woken,left/10000000ull+(left%10000000ull!=0));}}
        else process_wait_event_interruptible(&w.woken);
        {FLOW_LOCK;resource_waiter **link=&resource_waiters;while(*link && *link!=&w)link=&(*link)->next;if(*link)*link=w.next;}
        if(vex_event_interrupt_pending())return -EINTR;
    }
}
int scheduler_resource_resume(uint32_t pid) {
    FLOW_LOCK;process_t *p=find_process(pid);if(!p || p->state==PROC_ZOMBIE || p->terminate_requested)return -ESRCH;
    int previous=p->resource_suspended;p->resource_suspended=0;if(previous)scheduler_set_state(p,PROC_READY);return previous;
}

static process_t *find_constructing_process(uint32_t pid) {
    FLOW_LOCK;
    process_t *p=find_process(pid);
    /* The construction owner keeps this object non-runnable/non-retirable.
       Only the PID registry traversal needs the metadata gate here. */
    return construction_owned(p)?p:NULL;
}

void scheduler_enable_smp_dispatch(void) {
    {FLOW_LOCK;
     automatic_dispatch=1u;
     for(uint32_t i=1;i<smp_cpu_count();++i)
         __atomic_store_n(&smp_cpu_at(i)->scheduler_enabled,1u,__ATOMIC_RELEASE);
     for(process_t *p=process_list;p;p=p->next)
         if(p->group && p->group->user_mode && !p->placement_explicit)
             p->scheduling.pinned_cpu=UINT32_MAX;
    }
    for(uint32_t i=1;i<smp_cpu_count();++i)smp_reschedule(i);
}
void scheduler_dump_cpu_distribution(uint32_t process_id) {
    uint64_t *times=kmalloc((size_t)smp_cpu_count()*sizeof(*times));
    if(!times)return;
    uint32_t cursor=0;
    for(;;) {
        uint32_t tid=0,pid=0,migrations=0;char name[SCHEDULER_TASK_NAME_SIZE];
        {FLOW_LOCK;
         scheduler_account(sched_clock_ns());
         process_t *best=NULL;
         for(process_t *p=process_list;p;p=p->next)
             if(p->pid>cursor && (!process_id || process_owner_pid(p)==process_id) &&
                (!best || p->pid<best->pid))best=p;
         if(best){tid=best->pid;pid=process_owner_pid(best);migrations=best->migrations;
             memcpy(name,best->group?best->group->name:best->name,sizeof(name));
             for(uint32_t i=0;i<smp_cpu_count();++i)times[i]=best->execution_per_cpu?
                 best->execution_per_cpu[i]:(i?0:best->scheduling.cpu_ns);}
        }
        if(!tid)break;
        for(uint32_t i=0;i<smp_cpu_count();++i) {
            if(!times[i])continue;
            char line[180];snprintf(line,sizeof(line),"smp-app: pid=%u tid=%u cpu=%u ns=%08x%08x migrations=%u name=%s\n",pid,tid,i,(unsigned)(times[i]>>32u),(unsigned)times[i],migrations,name);debug_log_puts(line);
        }
        cursor=tid;
    }
    kfree(times);
    vex_jit_report_execution_samples();
}
int scheduler_allow_migration(uint32_t tid) {
    FLOW_LOCK;
    process_t *p=find_process(tid);
    if(!p || p->terminate_requested)return -1;
    p->scheduling.pinned_cpu=UINT32_MAX;p->placement_explicit=0u;
    return 0;
}

int scheduler_bind_prepared_task(uint32_t pid,uint32_t index) {
    FLOW_LOCK;
    process_t *p=find_process(pid);
    if(!construction_owned(p) || index>=smp_cpu_count())return -1;
    scheduler_cpu_t *cpu=smp_cpu_at(index)->scheduler;
    if(!cpu)return -1;
    if(p->scheduling.cpu!=&cpu->flow && flow_migrate(p->scheduling.cpu,&cpu->flow,&p->scheduling))return -1;
    p->scheduling.pinned_cpu=index;p->placement_explicit=1u;
    return 0;
}

int scheduler_bind_blocked_task(uint32_t tid,uint32_t index) {
    FLOW_LOCK;
    process_t *p=find_process(tid);
    if(!p || index>=smp_cpu_count() || p->terminate_requested ||
       (p->state!=PROC_WAITING && p->state!=PROC_SLEEPING) ||
       p->scheduling.running_cpu!=UINT32_MAX)return -1;
    scheduler_cpu_t *cpu=smp_cpu_at(index)->scheduler;
    if(!cpu || !smp_cpu_at(index)->scheduler_enabled)return -1;
    if(p->scheduling.cpu!=&cpu->flow && flow_migrate(p->scheduling.cpu,&cpu->flow,&p->scheduling))return -1;
    p->scheduling.pinned_cpu=index;p->placement_explicit=1u;
    return 0;
}

int scheduler_relocate_current(uint32_t index) {
    vex_cpu_t *cpu=smp_current_cpu();
    if(!current_process || !current_process->pid || index>=smp_cpu_count() ||
       (cpu && cpu->nonpreempt_depth) || !smp_cpu_at(index)->scheduler_enabled)return -1;
    {
        FLOW_LOCK;
        if(current_process->terminate_requested ||
           (current_process->scheduling.allowed_cpus && !flow_cpu_allowed(&current_process->scheduling,index)))return -1;
        current_process->scheduling.pinned_cpu=index;current_process->placement_explicit=1u;
    }
    if(index!=smp_current_index())schedule();
    return smp_current_index()==index?0:-1;
}

int scheduler_publish_process(uint32_t pid) {
    FLOW_LOCK;
    process_t *p=find_process(pid);
    if(!construction_owned(p) || p->terminate_requested || (p->group && p->group->terminating))return -1;
    if(automatic_dispatch && smp_cpu_count()>1u && p->group && p->group->user_mode && !p->placement_explicit) {
        p->scheduling.pinned_cpu=UINT32_MAX;
        uint32_t loads[smp_cpu_count()];memset(loads,0,sizeof(loads));
        for(process_t *q=process_list;q;q=q->next)
            if(q!=p && q->scheduling.cpu && (q->state==PROC_READY || q->state==PROC_RUNNING))
                ++loads[q->scheduling.cpu->cpu_id];
        uint32_t best=p->scheduling.cpu->cpu_id;
        for(uint32_t i=0;i<smp_cpu_count();++i)
            if(flow_cpu_allowed(&p->scheduling,i) && (!flow_cpu_allowed(&p->scheduling,best) || loads[i]<loads[best]))best=i;
        scheduler_cpu_t *destination=smp_cpu_at(best)->scheduler;
        if(destination && p->scheduling.cpu!=&destination->flow &&
           flow_migrate(p->scheduling.cpu,&destination->flow,&p->scheduling))return -1;
    }
    p->construction_tid=0u;p->construction_cpu=0u;
    /* The metadata gate is a release/acquire publication barrier for every
       initialized resource and for remote queue/context selection. */
    scheduler_set_state(p,p->resource_suspended?PROC_WAITING:PROC_READY);
    return 0;
}

int scheduler_abort_process(uint32_t pid) {
    {
        FLOW_LOCK;
        process_t *p=find_process(pid);
        if(!construction_owned(p))return -1;
        p->construction_tid=0u;p->construction_cpu=0u;
        p->wait_collected=1u;p->parent_pid=0u;
        scheduler_set_state(p,PROC_ZOMBIE);
    }
    reap_zombies(current_process);
    return 0;
}

uint32_t process_prepare_user_abi_caps(void (*entry)(void), uint8_t priority, uint64_t cr3,
                                       uint64_t user_rsp, uint8_t abi, uint32_t caps) {
    if(!user_rsp || (caps & ~scheduler_current_capabilities()))return 0;
    return create_process(entry,priority,cr3,1,user_rsp,abi,PROCESS_STACK_SIZE,caps,0);
}

uint32_t process_create_user(void (*entry)(void), uint8_t priority, uint64_t cr3, uint64_t user_rsp) {
    return process_create_user_abi(entry, priority, cr3, user_rsp, VEX_ABI_NATIVE);
}

uint32_t process_create_user_abi(void (*entry)(void), uint8_t priority, uint64_t cr3, uint64_t user_rsp, uint8_t abi) {
    if (user_rsp == 0) {
        return 0;
    }

    return create_process(entry, priority, cr3, 1, user_rsp, abi, PROCESS_STACK_SIZE, 0u, 1);
}

uint32_t process_create_user_abi_caps(void (*entry)(void), uint8_t priority, uint64_t cr3,
                                      uint64_t user_rsp, uint8_t abi, uint32_t caps) {
    /* Kernel launch API, never a grant-from-userspace API. */
    if(!user_rsp || (caps & ~scheduler_current_capabilities()))return 0;
    return create_process(entry,priority,cr3,1,user_rsp,abi,PROCESS_STACK_SIZE,caps,1);
}

uint32_t process_create(void (*entry)(void), uint8_t priority) {
    return process_create_in_address_space(entry, priority, 0);
}

uint32_t process_create_with_stack(void (*entry)(void), uint8_t priority, uint64_t stack_size) {
    return create_process(entry, priority, 0u, 0u, 0u, VEX_ABI_NATIVE, stack_size, 0u, 1);
}

void scheduler_record_syscall_frame(uint64_t rsp) {
    scheduler_execution_mode(0);
    if (current_process != NULL) {
        current_process->syscall_frame_rsp = rsp;
    }
}

process_t *scheduler_task_acquire(scheduler_task_ref_t *ref,uint32_t id,int process_identity) {
    if(!ref || ref->target || !id)return NULL;
    FLOW_LOCK;
    process_t *p=process_identity?find_process_identity(id):find_process(id);
    if(!p || p->borrowed_refs==UINT32_MAX)return NULL;
    ++p->borrowed_refs;ref->target=p;ref->owner=current_process;
    ref->previous=NULL;ref->next=ref->owner?ref->owner->outgoing_refs:NULL;
    if(ref->next)ref->next->previous=ref;
    if(ref->owner)ref->owner->outgoing_refs=ref;
    return p;
}
void scheduler_task_release(scheduler_task_ref_t *ref) {
    if(!ref || !ref->target)return;
    FLOW_LOCK;
    if(ref->previous)ref->previous->next=ref->next;
    else if(ref->owner)ref->owner->outgoing_refs=ref->next;
    if(ref->next)ref->next->previous=ref->previous;
    if(!ref->target->borrowed_refs)halt_forever();
    --ref->target->borrowed_refs;
    ref->target=NULL;ref->owner=NULL;ref->previous=ref->next=NULL;
}

#include "thread_control.inc"

uint32_t scheduler_current_uid(void) {
    return current_process == NULL ? 0 : current_process->uid;
}

uint32_t scheduler_current_capabilities(void) {
    if (current_process == NULL) {
        return 0u;
    }
    scheduler_refresh_process_capabilities(current_process);
    return current_process->capabilities | current_process->elevated_capabilities;
}

uint8_t scheduler_current_is_admin(void) {
    return (scheduler_current_capabilities() & ~VEX_CAP_NET_CONNECT) != 0u;
}

uint8_t scheduler_current_has_capability(uint32_t capabilities) {
    capabilities = security_cap_sanitize(capabilities);
    if (capabilities == 0u) {
        return 1u;
    }
    return (scheduler_current_capabilities() & capabilities) == capabilities;
}

uint8_t scheduler_current_user_mode(void) {
    return current_process != NULL && current_process->user_mode != 0;
}

uint8_t scheduler_current_abi(void) {
    return current_process == NULL ? VEX_ABI_NATIVE : current_process->abi;
}

int scheduler_process_alive(uint32_t pid) {
    FLOW_LOCK;
    process_t *process = find_process_identity(pid);

    return process != NULL && process->state != PROC_ZOMBIE;
}

int scheduler_can_control_process(uint32_t pid) {
    SCHEDULER_TASK_REF(ref);
    process_t *process = scheduler_task_acquire(&ref,pid,1);

    if (process == NULL || process->state == PROC_ZOMBIE || process->state == PROC_CONSTRUCTING) {
        return 0;
    }
    if (current_process == NULL || current_process->user_mode == 0u ||
        scheduler_current_has_capability(VEX_CAP_PROC_ADMIN)) {
        return 1;
    }
    return process->uid == current_process->uid;
}

void scheduler_detach_process(uint32_t pid) {
    FLOW_LOCK;
    process_t *process = find_process_identity(pid);

    if (process == NULL || process->pid == 0u) {
        return;
    }

    process->parent_pid = 0u;
}

int scheduler_set_process_admin(uint32_t pid, uint8_t is_admin) {
    return scheduler_set_process_capabilities(pid, security_default_caps_for_admin(is_admin));
}

int scheduler_grant_process_capabilities(uint32_t pid, uint32_t capabilities, uint64_t duration_ticks) {
    SCHEDULER_TASK_REF(ref);
    process_t *process = scheduler_task_acquire(&ref,pid,1);
    const uint32_t granted = security_cap_sanitize(capabilities);

    if (process == NULL || process->state == PROC_ZOMBIE) {
        return -1;
    }

    scheduler_refresh_process_capabilities(process);
    if (granted == 0u || duration_ticks == 0u) {
        return 0;
    }

    process->elevated_capabilities |= granted;
    process->elevated_capabilities_expires_tick = scheduler_ticks + duration_ticks;
    process->is_admin = ((process->capabilities | process->elevated_capabilities) & ~VEX_CAP_NET_CONNECT) != 0u;
    security_watchdog_note_process_state(process_owner_pid(process), process->uid, process->user_mode,
                                         process->capabilities, process->elevated_capabilities,
                                         process->elevated_capabilities_expires_tick);
    return 0;
}

int scheduler_set_process_capabilities(uint32_t pid, uint32_t capabilities) {
    SCHEDULER_TASK_REF(ref);
    process_t *process = scheduler_task_acquire(&ref,pid,1);

    if (process == NULL || process->state == PROC_ZOMBIE) {
        return -1;
    }

    process->capabilities = security_cap_sanitize(capabilities);
    process->elevated_capabilities = 0u;
    process->elevated_capabilities_expires_tick = 0u;
    process->is_admin = (process->capabilities & ~VEX_CAP_NET_CONNECT) != 0u;
    security_watchdog_note_process_state(process_owner_pid(process), process->uid, process->user_mode,
                                         process->capabilities, process->elevated_capabilities,
                                         process->elevated_capabilities_expires_tick);
    return 0;
}

void scheduler_set_process_abi(uint32_t pid, uint8_t abi) {
    FLOW_LOCK;
    process_t *process = find_process_identity(pid);
    if (process == NULL) {
        return;
    }
    process->abi = abi;
}

void scheduler_set_process_user_alloc_next(uint32_t pid, uint64_t user_alloc_next) {
    FLOW_LOCK;
    process_t *process = find_process_identity(pid);

    if (process == NULL) {
        return;
    }

    if (process->group != NULL) {
        process_group_sync_alloc(process->group, user_alloc_next);
    } else {
        process->user_alloc_next = user_alloc_next < process->user_alloc_base
            ? process->user_alloc_base
            : user_alloc_next;
    }
}

void scheduler_set_process_fs_base(uint32_t pid, uint64_t fs_base) {
    FLOW_LOCK;
    process_t *process = find_process(pid);
    if (process != NULL) {
        process->fs_base = fs_base;
    }
}

void scheduler_set_current_credentials(uint32_t uid, uint32_t capabilities) {
    if (current_process == NULL) {
        return;
    }

    current_process->uid = uid;
    current_process->capabilities = security_cap_sanitize(capabilities);
    current_process->elevated_capabilities = 0u;
    current_process->elevated_capabilities_expires_tick = 0u;
    current_process->is_admin = (current_process->capabilities & ~VEX_CAP_NET_CONNECT) != 0u;
    security_watchdog_note_process_state(process_owner_pid(current_process), current_process->uid, current_process->user_mode,
                                         current_process->capabilities, current_process->elevated_capabilities,
                                         current_process->elevated_capabilities_expires_tick);
}

process_t *scheduler_current_process(void) {
    return current_process;
}

void scheduler_map_kernel_page_all(uint64_t virt, uint64_t phys, uint64_t flags) {
    __atomic_fetch_add(&kernel_mapping_stats.map_calls, 1u, __ATOMIC_RELAXED);
    /* Compatibility entry point. VMM owns the shared kernel tree and
     * publishes new root branches once per live address space, not thread. */
    vmm_map_kernel_page(virt, phys, flags);
}

void scheduler_unmap_kernel_page_all(uint64_t virt) {
    __atomic_fetch_add(&kernel_mapping_stats.unmap_calls, 1u, __ATOMIC_RELAXED);
    vmm_unmap_kernel_page(virt);
}

static uint32_t process_spawn_shared_current(uint64_t child_stack) {
    process_t *child;
    process_group_t *child_group;
    uint32_t child_pid;
    uint64_t child_frame;

    if (current_process == NULL || current_process->user_mode == 0u ||
        current_process->syscall_frame_rsp == 0u || child_stack == 0u ||
        current_process->group == NULL) {
        return 0u;
    }

    scheduler_spawn_trace("create.begin", current_process, child_stack, 1u);

    scheduler_fpu_save(current_process->fpu_state);
    child_pid = process_prepare_in_address_space(fork_resume_trampoline,
                                                current_process->priority,
                                                current_process->cr3);
    if (child_pid == 0u) {
        scheduler_spawn_trace("create.fail", current_process, child_stack, 0u);
        return 0u;
    }
    child = find_constructing_process(child_pid);
    if (child == NULL) {
        scheduler_spawn_trace("create.lookup-fail", current_process, child_pid, 0u);
        return 0u;
    }

    /* Keep a distinct process identity/group so exit_group and event
       ownership cannot terminate the parent.  The temporary group borrows
       the parent's CR3 until execve and therefore does not own it yet. */
    child_group = process_group_create(child_pid, current_process->cr3,
                                       current_process->group->user_alloc_base,
                                       current_process->group->user_alloc_next,
                                       0u, current_process->abi);
    if (child_group == NULL) {
        child->wait_collected = 1u;
        child->parent_pid = 0u;
        scheduler_set_state(child,PROC_ZOMBIE);
        reap_zombies(current_process);
        return 0u;
    }
    child_group->linux_brk = current_process->group->linux_brk;
    memcpy(child_group->name, current_process->group->name,
           sizeof(child_group->name));
    child_group->linux_sigpipe_handler = current_process->group->linux_sigpipe_handler;
    child_group->linux_sigpipe_flags = current_process->group->linux_sigpipe_flags;
    child_group->linux_sigpipe_restorer = current_process->group->linux_sigpipe_restorer;
    memcpy(child_group->linux_sigpipe_mask, current_process->group->linux_sigpipe_mask,
           sizeof(child_group->linux_sigpipe_mask));
    memcpy(child_group->linux_sigusr1_action_mask,
           current_process->group->linux_sigusr1_action_mask,
           sizeof(child_group->linux_sigusr1_action_mask));
    memcpy(child_group->linux_internal_action_mask,
           current_process->group->linux_internal_action_mask,
           sizeof(child_group->linux_internal_action_mask));
    memcpy(child_group->linux_sync_fault_handler,
           current_process->group->linux_sync_fault_handler,
           sizeof(child_group->linux_sync_fault_handler));
    memcpy(child_group->linux_sync_fault_flags,
           current_process->group->linux_sync_fault_flags,
           sizeof(child_group->linux_sync_fault_flags));
    memcpy(child_group->linux_sync_fault_restorer,
           current_process->group->linux_sync_fault_restorer,
           sizeof(child_group->linux_sync_fault_restorer));
    memcpy(child_group->linux_sync_fault_mask,
           current_process->group->linux_sync_fault_mask,
           sizeof(child_group->linux_sync_fault_mask));
    memcpy(child_group->event_action_mask, current_process->group->event_action_mask,
           sizeof(child_group->event_action_mask));
    memcpy(child_group->events, current_process->group->events,
           sizeof(child_group->events));
    scheduler_flow_rebind(child,child_group);
    child->group = child_group;
    child->cr3 = child_group->cr3;
    child->user_alloc_base = child_group->user_alloc_base;
    child->user_alloc_next = child_group->user_alloc_next;
    child->shares_address_space = 1u;
    process_group_t *storage=current_process->borrowed_memory_group
        ?current_process->borrowed_memory_group:current_process->group;
    if(process_group_storage_retain(storage)) {
        (void)scheduler_abort_process(child_pid);return 0u;
    }
    child->borrowed_memory_group=storage;

    child_frame = (uint64_t)child->stack + FORK_RESUME_FRAME_OFFSET;
    {
        const volatile uint8_t *source = (const volatile uint8_t *)current_process->syscall_frame_rsp;
        volatile uint8_t *destination = (volatile uint8_t *)child_frame;
        for (uint64_t index = 0u; index < SYSCALL_FRAME_SIZE; ++index) {
            destination[index] = source[index];
        }
        *(volatile uint64_t *)(child_frame + SYSCALL_FRAME_RSP_OFFSET) = child_stack;
    }

    child->abi = current_process->abi;
    child->linux_sched_policy = current_process->linux_sched_policy;
    child->linux_sched_priority = current_process->linux_sched_priority;
    child->linux_sched_reset_on_fork = current_process->linux_sched_reset_on_fork;
    child->user_mode = 0u;
    child->user_rsp = child_stack;
    child->resume_syscall_rsp = child_frame;
    child->syscall_frame_rsp = 0u;
    child->fs_base = current_process->fs_base;
    child->user_gs_base = scheduler_current_user_gs_base();
    child->linux_signal_mask = current_process->linux_signal_mask;
    child->spawn_shared = 1u;
    child->spawn_parent_pid = storage->process_id;
    scheduler_fpu_copy(child->fpu_state, current_process->fpu_state);
    if(ladybird_fd_clone_process(process_owner_pid(current_process),child_pid)!=0) {
        child->wait_collected=1u;child->parent_pid=0u;scheduler_set_state(child,PROC_ZOMBIE);
        reap_zombies(current_process);return 0u;
    }
    scheduler_spawn_trace("create.child", child, child_stack, child->parent_pid);
    if(scheduler_publish_process(child_pid)!=0) {
        (void)scheduler_abort_process(child_pid);return 0u;
    }
    return child_pid;
}

static uint32_t process_fork_current_impl(uint64_t child_stack, uint8_t use_child_stack, uint64_t child_tid, uint8_t set_tid, uint8_t clear_tid) {
    process_t *child;
    process_group_t *child_group;
    uint32_t child_pid;
    uint64_t child_cr3;
    void *child_pml4;

    if (current_process == NULL || current_process->user_mode == 0 || current_process->syscall_frame_rsp == 0u) {
        return 0u;
    }

    scheduler_cap_trace("fork.parent", current_process);

    /* glibc uses clone3(CLONE_VM|CLONE_VFORK|SIGCHLD) for posix_spawn.
       This child is guaranteed to exec or exit immediately; borrowing the
       existing process group avoids copying all WebKit mappings only to throw
       them away at execve. */
    if (use_child_stack != 0u) {
        return process_spawn_shared_current(child_stack);
    }

    scheduler_fpu_save(current_process->fpu_state);

    child_pml4 = vmm_new_address_space();
    if (child_pml4 == NULL) {
        return 0u;
    }

    child_cr3 = vmm_address_space_to_cr3(child_pml4);
    if (child_cr3 == 0u) {
        vmm_destroy_address_space(child_pml4);
        return 0u;
    }

    child_pid = process_prepare_in_address_space(fork_resume_trampoline, current_process->priority, child_cr3);
    if (child_pid == 0u) {
        vmm_destroy_address_space(child_pml4);
        return 0u;
    }

    child = find_constructing_process(child_pid);
    if (child == NULL) {
        vmm_destroy_address_space(child_pml4);
        return 0u;
    }
    scheduler_cap_trace("fork.child-created", child);

    /* The parent's syscall frame lives on its kernel stack and cannot be
       reused by the child: both tasks may be scheduled while the parent is
       still blocked in the fork syscall.  Copy the complete frame into the
       child's own stack before arranging the synthetic return. */
    {
        const uint64_t child_frame = (uint64_t)child->stack + FORK_RESUME_FRAME_OFFSET;
        const volatile uint8_t *source = (const volatile uint8_t *)current_process->syscall_frame_rsp;
        volatile uint8_t *destination = (volatile uint8_t *)child_frame;
        for (uint64_t index = 0u; index < SYSCALL_FRAME_SIZE; ++index) {
            destination[index] = source[index];
        }
        if (use_child_stack != 0u) {
            *(volatile uint64_t *)(child_frame + SYSCALL_FRAME_RSP_OFFSET) = child_stack;
        }
        child->resume_syscall_rsp = child_frame;
    }

    if (process_memory_clone_namespace(process_owner_pid(current_process), current_process->cr3,
                                       child_pid, child_cr3) != 0) {
        child->user_mode = 1u;
        child->wait_collected = 1u;
        child->parent_pid = 0u;
        scheduler_set_state(child,PROC_ZOMBIE);
        reap_zombies(current_process);
        return 0u;
    }

    child_group = process_group_create(child_pid, child_cr3,
                                       current_process->group->user_alloc_base,
                                       current_process->group->user_alloc_next,
                                       1u, current_process->abi);
    if (child_group == NULL) {
        /* The cloned namespace already owns its leaves/backings. Its teardown
           must precede page-table retirement, including this OOM path. */
        child->user_mode = 1u;
        child->wait_collected = 1u;
        child->parent_pid = 0u;
        scheduler_set_state(child,PROC_ZOMBIE);
        reap_zombies(current_process);
        return 0u;
    }
    child_group->linux_brk = current_process->group->linux_brk;
    memcpy(child_group->name, current_process->group->name,
           sizeof(child_group->name));
    child_group->linux_sigpipe_handler = current_process->group->linux_sigpipe_handler;
    child_group->linux_sigpipe_flags = current_process->group->linux_sigpipe_flags;
    child_group->linux_sigpipe_restorer = current_process->group->linux_sigpipe_restorer;
    memcpy(child_group->linux_sigpipe_mask, current_process->group->linux_sigpipe_mask,
           sizeof(child_group->linux_sigpipe_mask));
    memcpy(child_group->linux_sigusr1_action_mask,
           current_process->group->linux_sigusr1_action_mask,
           sizeof(child_group->linux_sigusr1_action_mask));
    memcpy(child_group->linux_internal_action_mask,
           current_process->group->linux_internal_action_mask,
           sizeof(child_group->linux_internal_action_mask));
    memcpy(child_group->event_action_mask, current_process->group->event_action_mask,
           sizeof(child_group->event_action_mask));
    memcpy(child_group->events, current_process->group->events,
           sizeof(child_group->events));

    child->abi = current_process->abi;
    child->linux_sched_policy = current_process->linux_sched_policy;
    child->linux_sched_priority = current_process->linux_sched_priority;
    child->linux_sched_reset_on_fork = current_process->linux_sched_reset_on_fork;
    child->user_rsp = use_child_stack != 0u ? child_stack : current_process->user_rsp;
    scheduler_flow_rebind(child,child_group);
    child->group = child_group;
    child->user_alloc_base = child_group->user_alloc_base;
    child->user_alloc_next = child_group->user_alloc_next;
    child->syscall_frame_rsp = 0u;
    child->user_mode = 0u;
    child->fs_base = current_process->fs_base;
    child->user_gs_base = scheduler_current_user_gs_base();
    child->linux_signal_mask = current_process->linux_signal_mask;
    child->event_mask = current_process->event_mask;
    child->event_alt_stack_base = current_process->event_alt_stack_base;
    child->event_alt_stack_size = current_process->event_alt_stack_size;
    scheduler_fpu_copy(child->fpu_state, current_process->fpu_state);
    if (child_tid && (set_tid || clear_tid)) {
        /* compat has validated/materialized the parent's word before fork;
         * clone copied those pages. Write into the CHILD mapping, not into
         * the still-current parent's address space, including split words. */
        const uint32_t value = child_pid;
        for (unsigned byte=0; set_tid && byte<sizeof(value); ++byte) {
            uint64_t pa=vmm_get_physical(child_pml4,child_tid+byte);
            if(!pa || !(vmm_get_page_flags(child_pml4,child_tid+byte)&VMM_PAGE_WRITABLE)) {
                child->wait_collected=1u;child->parent_pid=0u;scheduler_set_state(child,PROC_ZOMBIE);
                reap_zombies(current_process);return 0;
            }
            *(uint8_t *)(uintptr_t)(hhdm_request.response->offset+pa)=((const uint8_t *)&value)[byte];
        }
        if(clear_tid)child->clear_child_tid=child_tid;
    }
    if (vex_jit_clone_pid(process_owner_pid(current_process), child_pid) != 0) {
        /* The namespace clone is still authoritative and will be released by
           normal zombie reaping; do not expose a child whose native JIT
           control objects were only partially cloned. */
        child->wait_collected = 1u;
        child->parent_pid = 0u;
        scheduler_set_state(child,PROC_ZOMBIE);
        reap_zombies(current_process);
        return 0u;
    }
    if(ladybird_fd_clone_process(process_owner_pid(current_process),child_pid)!=0) {
        child->wait_collected=1u;child->parent_pid=0u;scheduler_set_state(child,PROC_ZOMBIE);
        reap_zombies(current_process);return 0u;
    }
    scheduler_cap_trace("fork.child-ready", child);
    if(scheduler_publish_process(child_pid)!=0) {
        (void)scheduler_abort_process(child_pid);return 0u;
    }
    return child_pid;
}

uint32_t process_fork_current(void) {
    return process_fork_current_impl(0u, 0u, 0u, 0u, 0u);
}

uint32_t process_fork_current_tid(uint64_t child_tid, uint8_t set_tid, uint8_t clear_tid) {
    return process_fork_current_impl(0u, 0u, child_tid, set_tid, clear_tid);
}

uint32_t process_fork_current_with_stack(uint64_t child_stack) {
    if (child_stack == 0u) {
        return 0u;
    }
    return process_fork_current_impl(child_stack, 1u, 0u, 0u, 0u);
}

/* CLONE flags relevant to thread creation (see linux/sched.h). */
#define CLONE_VM_FLAG            0x00000100ull
#define CLONE_SETTLS_FLAG        0x00080000ull
#define CLONE_PARENT_SETTID_FLAG 0x00100000ull
#define CLONE_CHILD_CLEARTID_FLAG 0x00200000ull
#define CLONE_CHILD_SETTID_FLAG  0x01000000ull

#define SYSCALL_FRAME_R10_OFFSET 56ull
#define SYSCALL_FRAME_RDI_OFFSET 80ull
#define SYSCALL_FRAME_RSI_OFFSET 88ull
#define SYSCALL_FRAME_RCX_OFFSET 128ull

uint32_t process_clone_thread(uint64_t child_stack, uint64_t flags, uint64_t parent_tid, uint64_t child_tid, uint64_t tls) {
    process_t *child;
    uint32_t child_pid;
    uint64_t child_frame;

    if (current_process == NULL || current_process->user_mode == 0 ||
        current_process->syscall_frame_rsp == 0u || child_stack == 0u) {
        return 0u;
    }

    scheduler_fpu_save(current_process->fpu_state);

    /* Place a synthetic syscall frame at the top of the new thread stack. After
       resume_syscall_return() pops it the user rsp will equal child_stack. */
    child_frame = child_stack - SYSCALL_FRAME_SIZE;
    /* The frame is written straight into the new user stack; anonymous mappings
       are demand-zero, so make sure its pages exist first. */
    if (!vex_syscall_user_range_valid(child_frame, SYSCALL_FRAME_SIZE, 1)) {
        return 0u;
    }

    /* Copy the parent's recorded frame so the child resumes at the same user RIP
       and rflags with identical registers (rax is forced to 0 on resume). The
       new thread shares the parent's cr3, so child_frame is already mapped. */
    {
        const volatile uint64_t *src = (const volatile uint64_t *)current_process->syscall_frame_rsp;
        volatile uint64_t *dst = (volatile uint64_t *)child_frame;
        for (uint64_t i = 0u; i < SYSCALL_FRAME_SIZE / sizeof(uint64_t); ++i) {
            dst[i] = src[i];
        }
        /* clone() supplies the top of a distinct user stack.  The copied
           syscall frame belongs to the parent, so replace only the saved
           return RSP; all other registers must retain the interrupted
           instruction context. */
        *(volatile uint64_t *)(child_frame + SYSCALL_FRAME_RSP_OFFSET) = child_stack;
    }

    child_pid = process_prepare_in_address_space(fork_resume_trampoline,
                                                current_process->priority,
                                                current_process->cr3);
    if (child_pid == 0u) {
        return 0u;
    }

    child = find_constructing_process(child_pid);
    if (child == NULL) {
        return 0u;
    }

    if (process_group_attach(child, current_process->group) != 0) {
        child->wait_collected = 1u;
        child->parent_pid = 0u;
        scheduler_set_state(child,PROC_ZOMBIE);
        reap_zombies(current_process);
        return 0u;
    }

    child->abi = current_process->abi;
    child->linux_sched_policy = current_process->linux_sched_policy;
    child->linux_sched_priority = current_process->linux_sched_priority;
    child->linux_sched_reset_on_fork = current_process->linux_sched_reset_on_fork;
    child->user_mode = 0u; /* fork_resume_trampoline flips this to 1 */
    child->user_rsp = child_stack;
    child->resume_syscall_rsp = child_frame;
    child->syscall_frame_rsp = 0u;
    child->fs_base = (flags & CLONE_SETTLS_FLAG) != 0u ? tls : current_process->fs_base;
    child->user_gs_base = scheduler_current_user_gs_base();
    child->linux_signal_mask = current_process->linux_signal_mask;
    scheduler_fpu_copy(child->fpu_state, current_process->fpu_state);

    if ((flags & CLONE_CHILD_CLEARTID_FLAG) != 0u && child_tid != 0u) {
        child->clear_child_tid = child_tid;
    }
    if ((flags & CLONE_CHILD_SETTID_FLAG) != 0u && child_tid != 0u) {
        *(volatile uint32_t *)child_tid = child_pid;
    }
    if ((flags & CLONE_PARENT_SETTID_FLAG) != 0u && parent_tid != 0u) {
        *(volatile uint32_t *)parent_tid = child_pid;
    }

    if(scheduler_publish_process(child_pid)!=0) {
        (void)scheduler_abort_process(child_pid);return 0u;
    }
    return child_pid;
}

/* Native Vex threads enter a SysV function directly.  Unlike the Linux
   clone paths above, the entry address and argument are supplied by the
   native thread-create request and the kernel owns the lazily mapped stack. */
uint32_t process_create_native_thread(uint64_t stack_base, uint64_t stack_size,
                                      uint64_t user_entry, uint64_t user_arg,
                                      uint64_t tls_base, uint64_t clear_tid,
                                      uint64_t tid_out, uint32_t flags) {
    process_t *child;
    uint32_t child_tid;
    uint64_t stack_top;
    uint64_t initial_user_rsp;
    uint64_t child_frame;
    uint64_t start_frame[SYSCALL_FRAME_SIZE/sizeof(uint64_t)];

    if (current_process == NULL || current_process->user_mode == 0u ||
        current_process->group == NULL || current_process->syscall_frame_rsp == 0u ||
        stack_base == 0u || stack_size < 2u * PROCESS_USER_PAGE_SIZE ||
        stack_base > UINT64_MAX - stack_size || user_entry == 0u ||
        stack_base + stack_size < 8u) {
        return 0u;
    }

    stack_top = stack_base + stack_size;
    initial_user_rsp = (stack_top & ~15ull) - sizeof(uint64_t);
    const uint64_t return_address=0u;
    if(vex_copy_to_user(initial_user_rsp,&return_address,sizeof(return_address)))return 0u;
    scheduler_fpu_save(current_process->fpu_state);

    /* The return frame belongs to the kernel, like process_fork_current.
       Another thread may unmap/protect the caller-owned user stack while a
       start-paused thread is parked. Never later read a SYSRET frame from it. */
    memcpy(start_frame,(void *)(uintptr_t)current_process->syscall_frame_rsp,sizeof(start_frame));
    start_frame[2]=0;
    start_frame[SYSCALL_FRAME_RSP_OFFSET/8]=initial_user_rsp;
    start_frame[SYSCALL_FRAME_RCX_OFFSET/8]=user_entry;
    start_frame[SYSCALL_FRAME_RDI_OFFSET/8]=user_arg;

    child_tid = process_prepare_in_address_space(fork_resume_trampoline,
        current_process->priority, current_process->cr3);
    if (child_tid == 0u) return 0u;
    child = find_constructing_process(child_tid);
    if (child == NULL || process_group_attach(child, current_process->group) != 0) {
        if (child != NULL) {
            child->wait_collected = 1u;
            child->parent_pid = 0u;
            scheduler_set_state(child,PROC_ZOMBIE);
            reap_zombies(current_process);
        }
        return 0u;
    }

    child->abi = current_process->abi;
    child->linux_sched_policy = current_process->linux_sched_policy;
    child->linux_sched_priority = current_process->linux_sched_priority;
    child->linux_sched_reset_on_fork = current_process->linux_sched_reset_on_fork;
    child->user_mode = 0u; /* fork_resume_trampoline transitions to ring 3 */
    child->user_rsp = initial_user_rsp;
    child_frame=(uint64_t)child->stack+FORK_RESUME_FRAME_OFFSET;
    memcpy((void *)(uintptr_t)child_frame,start_frame,sizeof(start_frame));
    child->resume_syscall_rsp = child_frame;
    child->syscall_frame_rsp = 0u;
    child->fs_base = tls_base != 0u ? tls_base : current_process->fs_base;
    child->user_gs_base = 0u; /* Native thread shim installs its own user pointer. */
    child->linux_signal_mask = current_process->linux_signal_mask;
    if ((flags & PROCESS_NATIVE_THREAD_CALLER_STACK) != 0u) {
        /* The C library owns this stack and joins through clear_tid; the
           kernel reaps the thread itself once it stops. */
        child->owned_user_stack_base = 0u;
        child->owned_user_stack_size = 0u;
        child->native_thread_joinable = 0u;
    } else {
        child->owned_user_stack_base = stack_base;
        child->owned_user_stack_size = stack_size;
        child->native_thread_joinable = 1u;
    }
    child->clear_child_tid = clear_tid;
    child->pause_count=(flags&PROCESS_NATIVE_THREAD_START_PAUSED)!=0u;
    scheduler_fpu_copy(child->fpu_state, current_process->fpu_state);
    /* Like CLONE_PARENT_SETTID, the id is visible before the thread runs. */
    if (tid_out != 0u) {
        const int32_t tid_value = (int32_t)child_tid;
        if (vex_copy_to_user(tid_out, &tid_value, sizeof(tid_value)) != 0) {
            (void)scheduler_abort_process(child_tid);return 0u;
        }
    }
    if(scheduler_publish_process(child_tid)!=0) {
        (void)scheduler_abort_process(child_tid);return 0u;
    }
    return child_tid;
}

void process_set_clear_child_tid(uint64_t addr) {
    if (current_process != NULL) {
        current_process->clear_child_tid = addr;
    }
}

void process_exit_group(int status) {
    {
        FLOW_LOCK;
        if(current_process && current_process->pid) {
            process_group_t *group=current_process->group;
            if(group){group->terminating=1u;group->resource_exit_status=status;group->resource_status_valid=1;}
            for(process_t *p=process_list;p;p=p->next)
                if(p==current_process || (group && p->group==group))
                    scheduler_request_stop(p,status);
        }
    }
    schedule();
    halt_forever();
}

static void futex_unlink(futex_waiter_t *node) {
    futex_waiter_t **link = &futex_waiters;

    while (*link != NULL) {
        if (*link == node) {
            *link = node->next;
            return;
        }
        link = &(*link)->next;
    }
}

static void futex_cancel_pid(uint32_t pid) {
    linux_futex_pi_cancel_tid(pid);
    KERNEL_GUARD(futex_gate);
    futex_waiter_t **link = &futex_waiters;

    while (*link != NULL) {
        futex_waiter_t *node = *link;
        if (node->pid != pid) {
            link = &node->next;
            continue;
        }
        *link = node->next;
        __atomic_store_n(&node->woken,1u,__ATOMIC_RELEASE);
    }
}

static void futex_wake_key(uint64_t cr3,uint64_t address) {
    KERNEL_GUARD(futex_gate);
    futex_waiter_t *best=NULL;
    for(futex_waiter_t *w=futex_waiters;w;w=w->next)
        if(w->cr3==cr3 && w->uaddr==address && !w->woken &&
           (!best || scheduler_waiter_precedes(w->pid,best->pid)))best=w;
    if(best){__atomic_store_n(&best->woken,1u,__ATOMIC_RELEASE);process_wake(best->pid);}
}

int futex_wait(uint64_t uaddr, uint32_t expected) {
    futex_waiter_t node;
    uint32_t observed;
    uint32_t queue_count = 0u;

    if (current_process == NULL || uaddr == 0u) {
        futex_trace("wait.invalid", uaddr, 0u, expected, 0u, 0u, 0u, -1);
        return -1;
    }

    {
    kernel_guard_t memory_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    KERNEL_GUARD(futex_gate);
    if((uaddr & 3u) || !vex_syscall_user_range_valid(uaddr,4,0))return -1;
    /* Check/register against WAKE on every CPU; the mapping transaction also
       prevents unmap/protection changes between validation and the load. */
    observed = __atomic_load_n((uint32_t *)(uintptr_t)uaddr,__ATOMIC_ACQUIRE);
    if (observed != expected) {
        futex_trace("wait.mismatch", uaddr,
                    current_process->group == NULL ? current_process->cr3
                        : current_process->group->cr3,
                    expected, observed, 0u, 0u, -1);
        return -1;
    }

    node.cr3 = current_process->group == NULL ? current_process->cr3 : current_process->group->cr3;
    node.uaddr = uaddr;
    node.pid = current_process->pid;
    node.woken = 0u;
    current_process->scheduling.wait_order=sched_clock_ns();
    node.next = futex_waiters;
    futex_waiters = &node;
    for (futex_waiter_t *waiter = futex_waiters; waiter != NULL; waiter = waiter->next) {
        ++queue_count;
    }
    futex_trace("wait.register", uaddr, node.cr3, expected, observed,
                queue_count, 0u, 0);
    }

    while (__atomic_load_n(&node.woken,__ATOMIC_ACQUIRE) == 0u) {
        process_wait_event(&node.woken);
    }

    {KERNEL_GUARD(futex_gate);futex_unlink(&node);}
    observed = 0u;
    {kernel_guard_t memory_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
      if(vex_syscall_user_range_valid(uaddr,sizeof(uint32_t),0))
        observed=__atomic_load_n((uint32_t *)(uintptr_t)uaddr,__ATOMIC_ACQUIRE);}
    futex_trace("wait.unregister", uaddr, node.cr3, expected,
                observed, queue_count, 1u, 0);
    return 0;
}

int futex_wake(uint64_t uaddr, uint32_t count) {
    kernel_guard_t memory_guard __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    KERNEL_GUARD(futex_gate);
    const uint64_t cr3 = current_process == NULL ? 0u
        : (current_process->group == NULL ? current_process->cr3 : current_process->group->cr3);
    int woken = 0;
    uint32_t queue_count = 0u;
    uint32_t observed = 0u;

    if (uaddr == 0u) {
        futex_trace("wake.invalid", uaddr, cr3, 0u, 0u, 0u, 0u, 0);
        return 0;
    }

    if (vex_syscall_user_range_valid(uaddr, sizeof(uint32_t), 0)) {
        observed = *(volatile uint32_t *)uaddr;
    }
    for (futex_waiter_t *waiter = futex_waiters; waiter != NULL; waiter = waiter->next) {
        ++queue_count;
    }
    futex_trace("wake.lookup", uaddr, cr3, count, observed,
                queue_count, 0u, 0);

    if(count>=queue_count) {
        /* Broadcast needs no ordering scan: FlowSched chooses who runs. */
        for(futex_waiter_t *w=futex_waiters;w;w=w->next)
            if(!w->woken && w->cr3==cr3 && w->uaddr==uaddr){__atomic_store_n(&w->woken,1u,__ATOMIC_RELEASE);process_wake(w->pid);++woken;}
    }
    while (count<queue_count && (uint32_t)woken < count) {
        futex_waiter_t *best=NULL;
        for(futex_waiter_t *w=futex_waiters;w;w=w->next)
            if(!w->woken && w->cr3==cr3 && w->uaddr==uaddr &&
               (!best || scheduler_waiter_precedes(w->pid,best->pid)))best=w;
        if(!best)break;
        __atomic_store_n(&best->woken,1u,__ATOMIC_RELEASE);process_wake(best->pid);++woken;
    }

    if (woken == 0 && queue_count != 0u) {
        uint32_t printed = 0u;
        for (futex_waiter_t *waiter = futex_waiters;
             waiter != NULL && printed < 4u; waiter = waiter->next, ++printed) {
            futex_trace("wake.candidate", waiter->uaddr, waiter->cr3,
                        count, waiter->woken, queue_count, 0u, -1);
        }
    }

    futex_trace("wake.result", uaddr, cr3, count, observed,
                queue_count, (uint32_t)woken, woken);
    return woken;
}

/* Sleep until *flag is set, a native event becomes deliverable or the
   scheduler tick `until_tick` passes (0: no bound). The checks and the
   READY -> WAITING/SLEEPING transition share FLOW_LOCK with process_wake()
   and vex_event_post(), so neither a wake nor an event can be missed. */
static void futex_block(const volatile uint8_t *flag, uint64_t deadline_ns) {
    {
        FLOW_LOCK;
        if (__atomic_load_n(flag, __ATOMIC_ACQUIRE)) return;
        if (__atomic_load_n(&current_process->event_pending, __ATOMIC_ACQUIRE) &
            ~current_process->event_mask) return;
        if (deadline_ns != 0u) {
            /* sleep_until_tick marks the thread as sleeping; the precise
               deadline arms the timer. */
            uint64_t now=sched_clock_ns();if(now>=deadline_ns)return;
            uint64_t left=deadline_ns-now;
            const uint64_t ticks = left/10000000u+(left%10000000u!=0);
            current_process->sleep_until_tick = scheduler_ticks + (ticks ? ticks : 1u);
            current_process->sleep_deadline_ns = deadline_ns;
            scheduler_set_state(current_process, PROC_SLEEPING);
            current_process->sleep_deadline_ns = 0u;
        } else {
            scheduler_set_state(current_process, PROC_WAITING);
        }
    }
    schedule();
}

/* The native futex wait used by the Vex C library. Unlike futex_wait(), it
   is bounded by an optional relative timeout and interrupted by deliverable
   native events. A wake that races with the timeout or the event wins: the
   waiter it selected reports success, so no wake is ever lost. */
int futex_wait_native(uint64_t uaddr, uint32_t expected, uint64_t timeout_ns) {
    futex_waiter_t node;
    uint64_t deadline = 0u;
    int result = 0;

    if (current_process == NULL || current_process->pid == 0u) return -EINVAL;
    if (uaddr == 0u || (uaddr & 3u) != 0u) return -EINVAL;
    if (timeout_ns != 0u) {
        const uint64_t now = sched_clock_ns();
        deadline = timeout_ns > UINT64_MAX - now ? UINT64_MAX : now + timeout_ns;
    }
    {
        kernel_guard_t memory_guard __attribute__((cleanup(process_memory_end_update))) =
            process_memory_begin_update();
        KERNEL_GUARD(futex_gate);
        if (!vex_syscall_user_range_valid(uaddr, sizeof(uint32_t), 0)) return -EFAULT;
        if (__atomic_load_n((uint32_t *)(uintptr_t)uaddr, __ATOMIC_ACQUIRE) != expected)
            return -EAGAIN;
        node.cr3 = current_process->group == NULL ? current_process->cr3
                                                  : current_process->group->cr3;
        node.uaddr = uaddr;
        node.pid = current_process->pid;
        node.woken = 0u;
        current_process->scheduling.wait_order = sched_clock_ns();
        node.next = futex_waiters;
        futex_waiters = &node;
    }

    while (__atomic_load_n(&node.woken, __ATOMIC_ACQUIRE) == 0u) {
        if (vex_event_interrupt_pending()) {
            result = -EINTR;
            break;
        }
        if (deadline != 0u && sched_clock_ns() >= deadline) {
            result = -ETIMEDOUT;
            break;
        }
        futex_block(&node.woken, deadline);
    }

    {
        KERNEL_GUARD(futex_gate);
        futex_unlink(&node);
        if (__atomic_load_n(&node.woken, __ATOMIC_ACQUIRE) != 0u) result = 0;
    }
    return result;
}

int scheduler_thread_name(uint32_t tid, char *out, size_t capacity, uint32_t *process_id) {
    FLOW_LOCK;
    process_t *p = find_process(tid);
    size_t i = 0u;
    if (p == NULL || out == NULL || capacity == 0u) return -1;
    for (; i + 1u < capacity && i < SCHEDULER_TASK_NAME_SIZE && p->name[i] != '\0'; ++i) out[i] = p->name[i];
    out[i] = '\0';
    if (process_id != NULL) *process_id = p->group != NULL ? p->group->process_id : p->pid;
    return 0;
}

static void scheduler_log_u64_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char text[19];
    text[0] = '0';
    text[1] = 'x';
    for (int i = 0; i < 16; ++i) text[2 + i] = digits[(value >> (60 - 4 * i)) & 15u];
    text[18] = '\0';
    debug_log_puts(text);
}

/* Diagnostics for a stuck user program: every user thread with its state,
   the user address it will return to from its current system call and,
   when it sleeps on a futex, the futex address. */
void scheduler_dump_user_threads(void) {
    FLOW_LOCK;
    debug_log_puts("task-dump: begin\n");
    for (process_t *p = process_list; p != NULL; p = p->next) {
        uint64_t rip = 0u, futex = 0u;
        if (p->user_mode == 0u && p->syscall_frame_rsp == 0u && p->resume_syscall_rsp == 0u) continue;
        if (p->syscall_frame_rsp != 0u)
            rip = *(const uint64_t *)(uintptr_t)(p->syscall_frame_rsp + SYSCALL_FRAME_RCX_OFFSET);
        {
            KERNEL_GUARD(futex_gate);
            for (futex_waiter_t *w = futex_waiters; w != NULL; w = w->next)
                if (w->pid == p->pid) futex = w->uaddr;
        }
        debug_log_puts("task-dump: tid=");
        scheduler_log_u64_dec(p->pid);
        debug_log_puts(" process=");
        scheduler_log_u64_dec(p->group != NULL ? p->group->process_id : p->pid);
        debug_log_puts(" state=");
        scheduler_log_u64_dec(p->state);
        debug_log_puts(" cpu_ticks=");
        scheduler_log_u64_dec(p->cpu_ticks);
        debug_log_puts(" user_rip=");
        scheduler_log_u64_hex(rip);
        debug_log_puts(" futex=");
        scheduler_log_u64_hex(futex);
        debug_log_puts(" events=");
        scheduler_log_u64_hex(p->event_pending);
        debug_log_puts("/");
        scheduler_log_u64_hex(p->event_mask);
        debug_log_puts("\n");
    }
    debug_log_puts("task-dump: end\n");
}

uint64_t scheduler_cpu_time_ns(int whole_process) {
    FLOW_LOCK;
    SCHED_LOCK;
    scheduler_account(sched_clock_ns());
    if (current_process == NULL) return 0u;
    return whole_process && current_process->group?
        current_process->group->scheduling.cpu_ns:current_process->scheduling.cpu_ns;
}

int futex_wake_native(uint64_t uaddr, uint32_t count) {
    if (uaddr == 0u || (uaddr & 3u) != 0u) return -EINVAL;
    return futex_wake(uaddr, count);
}

int process_exec_image(uint64_t new_cr3, uint64_t user_entry, uint64_t user_rsp, uint8_t abi, uint64_t user_alloc_next) {
    uint64_t abandoned_rsp = 0u;
    uint64_t bootstrap_rsp;

    if (current_process == NULL || new_cr3 == 0u || user_entry == 0u || user_rsp == 0u ||
        (current_process->group != NULL &&
         (__atomic_load_n(&current_process->group->refcount,__ATOMIC_ACQUIRE)!=1u ||
          __atomic_load_n(&current_process->group->storage_refs,__ATOMIC_ACQUIRE)!=1u))) {
        return -1;
    }

    bootstrap_rsp = initial_process_rsp(process_kernel_stack_top(current_process), exec_resume_trampoline);
    {FLOW_LOCK;execution_detach(current_process);}
    execution_collect();
    current_process->pending_old_cr3 = current_process->cr3;
    current_process->pending_old_cr3_owned = current_process->shares_address_space == 0u;
    current_process->pending_user_entry = user_entry;
    current_process->cr3 = new_cr3;
    current_process->abi = abi;
    current_process->user_gs_base = 0u; /* No pointer into the retired image. */
    current_process->user_rsp = user_rsp;
    current_process->user_alloc_base = USER_ALLOC_BASE;
    current_process->user_alloc_next = user_alloc_next < USER_ALLOC_BASE ? USER_ALLOC_BASE : user_alloc_next;
    if (current_process->group != NULL) {
        current_process->group->cr3 = new_cr3;
        current_process->group->user_alloc_base = USER_ALLOC_BASE;
        current_process->group->user_alloc_next = current_process->user_alloc_next;
        current_process->group->linux_brk = 0u;
        current_process->group->abi = abi;
        /* Handlers belong to the replaced image: like POSIX exec, a caught
           event reverts to its default action and an ignored one stays
           ignored. */
        current_process->event_alt_stack_base = 0u;
        current_process->event_alt_stack_size = 0u;
        for (unsigned i = 0u; i < VEX_EVENT_SLOT_COUNT; ++i) {
            vex_event_registration_t *event = &current_process->group->events[i];
            if (event->handler > VEX_EVENT_IGNORE_HANDLER) {
                event->handler = 0u;
                event->restorer = 0u;
                event->flags = 0u;
            }
            current_process->group->event_action_mask[i] = 0u;
        }
    }
    current_process->resume_syscall_rsp = 0u;
    current_process->syscall_frame_rsp = 0u;
    current_process->rsp = bootstrap_rsp;
    current_process->user_mode = 0u;
    scheduler_set_state(current_process,PROC_RUNNING);
    current_process->shares_address_space = 0u;
    if (current_process->group != NULL) {
        current_process->group->owns_address_space = 1u;
    }
    scheduler_spawn_trace("exec.ready", current_process, user_entry, user_rsp);
    process_spawn_child_ready(current_process->pid);

    /* Linux compatibility descriptors marked close-on-exec are discarded only
     * after the new image has been prepared, preserving the existing VexOS
     * process/resource ownership model. */
    ladybird_fd_close_exec(process_owner_pid(current_process));

    scheduler_trace_fpu_switch("exec", current_process, current_process);
    scheduler_context_switch(&abandoned_rsp, current_process->rsp, current_process->cr3,
                             current_process->fpu_state, current_process->fpu_state,
                             scheduler_xsave_mask, current_process->fs_base, 0u);
    halt_forever();
    return -1;
}

int process_prepare_exec_identity(void) {
    FLOW_LOCK;
    if(!current_process)return -1;
    if(current_process->group &&
       (__atomic_load_n(&current_process->group->refcount,__ATOMIC_ACQUIRE)!=1u ||
        __atomic_load_n(&current_process->group->storage_refs,__ATOMIC_ACQUIRE)!=1u))return -1;
    if(current_process->spawn_shared == 0u)return 0;
    if (current_process->group == NULL || current_process->group->refcount == 0u) {
        return -1;
    }
    /* The temporary child already owns a separate process group.  Drop only
       the address-space-owner alias once the new image is ready. */
    current_process->spawn_shared = 0u;
    current_process->spawn_parent_pid = 0u;
    return 0;
}

void process_spawn_child_ready(uint32_t child_pid) {
    FLOW_LOCK;
    process_t *child = find_process(child_pid);

    if (child == NULL || child->parent_pid == 0u) {
        return;
    }
    /* The spawning context can be a non-leader thread. Process identity is
       ownership, not the address of the thread actually waiting for exec. */
    for(process_t *parent=process_list;parent;parent=parent->next) {
        if(process_owner_pid(parent)!=child->parent_pid ||
           parent->spawn_wait_child_pid!=child_pid || parent->state==PROC_ZOMBIE)continue;
        parent->spawn_wait_done=1u;
        process_wake(parent->pid);
    }
}

void process_wait_spawn(uint32_t child_pid) {
    if (current_process == NULL || child_pid == 0u) {
        return;
    }
    {
        FLOW_LOCK;
        current_process->spawn_wait_child_pid=child_pid;
        current_process->spawn_wait_done=0u;
    }
    for(;;) {
        {
            FLOW_LOCK;
            process_t *child=find_process(child_pid);
            /* Persistent child state covers exec/exit before registration;
               the handshake and WAITING transition cover a concurrent exec. */
            if(current_process->spawn_wait_done || !child ||
               !child->spawn_shared || child->state==PROC_ZOMBIE)break;
            (void)scheduler_wait_dependency(child_pid);
            scheduler_set_state(current_process,PROC_WAITING);
        }
        schedule();
        if(current_process==&kernel_process && current_process->state==PROC_WAITING)
            scheduler_idle_wait();
    }
    {
        FLOW_LOCK;
        scheduler_wait_dependency_clear();
        current_process->spawn_wait_child_pid=0u;
        current_process->spawn_wait_done=0u;
    }
    scheduler_spawn_trace("wait.done", current_process, child_pid, 0u);
}

void scheduler_set_current_fs_base(uint64_t fs_base) {
    if (current_process == NULL) {
        return;
    }

    current_process->fs_base = fs_base;
    scheduler_apply_current_tls_state();
}

void scheduler_restore_current_fs_base(void) {
    scheduler_apply_current_tls_state();
}

uint64_t scheduler_current_fs_base(void) {
    return current_process == NULL ? 0u : current_process->fs_base;
}
void scheduler_set_current_user_gs_base(uint64_t base) {
    /* Pin this two-instruction state publication to the current CPU. */
    uint64_t flags;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    if(current_process) {current_process->user_gs_base=base;write_msr(MSR_USER_GS_SLOT,base);}
    __asm__ volatile("pushq %0; popfq"::"r"(flags):"memory","cc");
}
uint64_t scheduler_current_user_gs_base(void) {
    uint64_t flags,value;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    value=read_user_gs_slot();if(current_process)current_process->user_gs_base=value;
    __asm__ volatile("pushq %0; popfq"::"r"(flags):"memory","cc");return value;
}
uint64_t scheduler_current_user_rsp(void) {
    if(!current_process)return 0;
    return current_process->syscall_frame_rsp?
        *(const uint64_t *)(uintptr_t)(current_process->syscall_frame_rsp+136u):current_process->user_rsp;
}

uint64_t scheduler_current_user_alloc_next(void) {
    if (current_process == NULL) {
        return 0u;
    }
    return current_process->group == NULL
        ? current_process->user_alloc_next
        : current_process->group->user_alloc_next;
}

const char *scheduler_state_name(uint32_t pid) {
    FLOW_LOCK;
    process_t *process = find_process_identity(pid);
    if (process == NULL) {
        return "missing";
    }
    return scheduler_state_name_value((uint8_t)process->state);
}

const char *scheduler_state_name_value(uint8_t state) {
    switch (state) {
    case PROC_RUNNING:
        return "running";
    case PROC_READY:
        return "ready";
    case PROC_WAITING:
        return "waiting";
    case PROC_SLEEPING:
        return "sleeping";
    case PROC_ZOMBIE:
        return "zombie";
    case PROC_CONSTRUCTING:
        return "constructing";
    default:
        return "unknown";
    }
}

void scheduler_set_process_priority(uint32_t pid, uint8_t priority) {
    FLOW_LOCK;
    process_t *process = find_process(pid);
    if (process == NULL) {
        return;
    }
    process->priority = priority;
}

void process_yield(void) {
    if (current_process != NULL && current_process->state == PROC_RUNNING) {
        scheduler_set_state(current_process,PROC_READY);
    }


    schedule();
}

void process_yield_to(uint32_t pid) {
    /* Legacy hint carries urgency, never a way around group entitlement. */
    scheduler_wake_flow(pid,scheduler_flow_current());process_yield();
}

void process_sleep_ticks(uint64_t ticks) {
    SCHED_LOCK;
    if (current_process == NULL || current_process->pid == 0u) {
        process_yield();
        return;
    }

    if (ticks == 0u) {
        process_yield();
        return;
    }

    current_process->sleep_until_tick = ticks > UINT64_MAX - scheduler_ticks
        ? UINT64_MAX : scheduler_ticks + ticks;
    scheduler_set_state(current_process,PROC_SLEEPING);
    schedule();
}

void process_wait(void) {
    if (current_process != NULL) {
        scheduler_set_state(current_process,PROC_WAITING);
    }

    schedule();

    /* The bootstrap/kernel task is also used as the parent while boot smokes
       wait for a userspace child.  If every userspace task is asleep, schedule
       has no context to switch to and returns with the kernel task WAITING.
       Halt until the next IRQ instead of spinning in process_wait_pid(); the
       timer IRQ will mark timed sleepers READY and the caller will schedule
       them on its next iteration. */
    if (current_process == &kernel_process &&
        current_process->state == PROC_WAITING) {
        scheduler_idle_wait();
    }
}
int process_wait_event_interruptible(const volatile uint8_t *flag) {
    SCHED_LOCK;
    {FLOW_LOCK;
     if(flag && __atomic_load_n(flag,__ATOMIC_ACQUIRE))return 0;
     if(current_process && (__atomic_load_n(&current_process->event_pending,__ATOMIC_ACQUIRE)&~current_process->event_mask))return -1;
     if(!current_process || !current_process->pid)return -1;
     scheduler_set_state(current_process,PROC_WAITING);}
    schedule();return 0;
}

void process_wait_event(const volatile uint8_t *woken) {
    if(!woken || !current_process || current_process==&kernel_process){process_yield();return;}
    {
        /* Pair the predicate check with READY -> WAITING under the same
           short metadata transaction used by remote process_wake(). */
        FLOW_LOCK;
        uint8_t observed=__atomic_load_n(woken,__ATOMIC_ACQUIRE);
        if(current_process->pid==watched_tid)
            record_wait_watch(8u,observed,observed,(uint64_t)woken);
        if(observed)return;
        scheduler_set_state(current_process,PROC_WAITING);
    }
    schedule();
}
void process_wait_event_timeout(const volatile uint8_t *woken,uint64_t ticks) {
    if(!ticks){process_wait_event(woken);return;}
    if(!woken || !current_process || current_process==&kernel_process){process_yield();return;}
    {
        FLOW_LOCK;
        if(__atomic_load_n(woken,__ATOMIC_ACQUIRE))return;
        current_process->sleep_until_tick=ticks>UINT64_MAX-scheduler_ticks?UINT64_MAX:scheduler_ticks+ticks;
        scheduler_set_state(current_process,PROC_SLEEPING);
    }
    schedule();
}

void process_exit(void) {
    process_exit_status(0);
}

/* All calls below hold FLOW_LOCK. Requesting termination is distinct from
   stopping an executing context. No resource cleanup runs at request time. */
static void scheduler_finish_stop(process_t *p) {
    p->sleep_until_tick=0u;
    p->exit_status=p->terminate_status;
    p->wait_collected=p->resource_owned || (process_is_group_thread(p) && !p->native_thread_joinable);
    if(p->group && !p->group->resource_status_valid) {
        int last=1;for(process_t *q=process_list;q;q=q->next)if(q!=p && q->group==p->group && q->state!=PROC_ZOMBIE){last=0;break;}
        if(last){p->group->resource_exit_status=p->exit_status;p->group->resource_status_valid=1;}
    }
    scheduler_set_state(p,PROC_ZOMBIE);
    context_notify(p->pid);
    if(p->join_waiter_tid)process_wake(p->join_waiter_tid);
}
static void scheduler_request_stop(process_t *p,int status) {
    if(!p || !p->pid || p->state==PROC_ZOMBIE)return;
    if(!p->terminate_requested) {
        p->terminate_status=status;p->terminate_requested=1u;
    }
    /* A builder is still privately owned by its creator. Publish will refuse
       a closing group; its owner or its owner's stopped cleanup rolls back. */
    if(p->state==PROC_CONSTRUCTING)return;
    if(p->scheduling.running_cpu==UINT32_MAX)scheduler_finish_stop(p);
    else {
        /* Remove from selection without changing the running context's state.
           Its CPU stops it at user IRQ/return or a cooperative kernel boundary. */
        flow_block(&flow_local,&p->scheduling);
        smp_reschedule(p->scheduling.running_cpu);
        if(p==current_process)preempt_pending=1u;
    }
}
static int scheduler_stop_at_boundary(void) {
    FLOW_LOCK;
    if(!current_process || !current_process->pid || !current_process->terminate_requested)return 0;
    vex_cpu_t *cpu=smp_current_cpu();
    if(cpu?cpu->nonpreempt_depth:flow_nonpreempt_depth)return 0;
    scheduler_finish_stop(current_process);return 1;
}
/* Terminate the calling process once it reaches its next boundary (return
   to user mode or a cooperative kernel point). Safe while holding kernel
   locks, unlike process_exit_status(), which switches away immediately. */
void process_request_exit(int status) {
    FLOW_LOCK;
    scheduler_request_stop(current_process,status);
}

void process_exit_status(int status) {
    {
        FLOW_LOCK;
        scheduler_request_stop(current_process,status);
    }
    schedule();
    halt_forever();
}

void process_wake(uint32_t pid) {
    FLOW_LOCK;
    SCHED_LOCK;
    process_t *process = find_process_identity(pid);
    if(process && process->resource_suspended && !process->terminate_requested)return;
    if(process && process->pause_parked && process->pause_count && !process->terminate_requested)return;
    if (process != NULL && (process->state == PROC_WAITING || process->state == PROC_SLEEPING)) {
        flow_undepend(&flow_local,&process->scheduling,sched_clock_ns());
        if(current_process && current_process->pid)
            flow_adopt(&flow_local,&process->scheduling,current_process->scheduling.effective,sched_clock_ns());
        process->sleep_until_tick = 0u;
        scheduler_set_state(process,PROC_READY);
        preempt_pending=1;
        if(process->scheduling.cpu && process->scheduling.cpu->cpu_id!=smp_current_index())
            smp_reschedule(process->scheduling.cpu->cpu_id);
    }
}
int scheduler_wake_observed(uint32_t tid) {
    FLOW_LOCK;
    process_t *p=find_process(tid);if(!p)return -1;
    int before=(int)p->state;process_wake(tid);
    return (before<<4)|(int)p->state;
}

uint64_t scheduler_time_ns(void) {return sched_clock_ns();}
flow_context_t scheduler_flow_current(void) {
    FLOW_LOCK;
    SCHED_LOCK;
    if(!current_process || !current_process->pid)return (flow_context_t){0};
    flow_context_t f=current_process->scheduling.effective;
    if(f.id && sched_clock_ns()-f.born_ns>250000000ull)return (flow_context_t){0};
    return f;
}
void scheduler_flow_adopt(flow_context_t f) {
    FLOW_LOCK;
    SCHED_LOCK;if(current_process && current_process->pid)
        flow_adopt(&flow_local,&current_process->scheduling,f,sched_clock_ns());
}
void scheduler_wake_flow(uint32_t tid,flow_context_t f) {
    FLOW_LOCK;
    SCHED_LOCK;process_t *p=find_process(tid);
    if(!p || !p->pid || p->state==PROC_ZOMBIE)return;
    if(f.origin==FLOW_INPUT && !p->scheduling.input_at_ns)p->scheduling.input_at_ns=f.born_ns;
    if(p->state==PROC_READY && !p->scheduling.runnable_ns)p->scheduling.runnable_ns=sched_clock_ns();
    flow_adopt(&flow_local,&p->scheduling,f,sched_clock_ns());process_wake(tid);preempt_pending=1;
    if(p->scheduling.cpu)smp_reschedule(p->scheduling.cpu->cpu_id);
}
void scheduler_wake_origin(uint32_t tid,uint32_t origin,uint64_t budget) {
    FLOW_LOCK;
    SCHED_LOCK;
    /* Hardware input/I/O has no causative userspace thread. Do not attribute
       it to the unrelated task that happened to be interrupted. */
    uint32_t source=(origin==FLOW_INPUT || origin==FLOW_IO)?0:scheduler_current_tid();
    scheduler_wake_flow(tid,flow_new(&flow_local,origin,source,sched_clock_ns(),budget));
}
int scheduler_wait_dependency(uint32_t tid) {
    FLOW_LOCK;
    SCHED_LOCK;process_t *p=find_process(tid);
    if(!current_process || !current_process->pid || !p || !p->pid || p->state==PROC_ZOMBIE)return -1;
    current_process->scheduling.wait_order=sched_clock_ns();
    int result=flow_depend(&flow_local,&current_process->scheduling,&p->scheduling,sched_clock_ns());
    if(!result && p->scheduling.cpu)smp_reschedule(p->scheduling.cpu->cpu_id);
    return result;
}
int scheduler_dependency_set(uint32_t waiter_tid,uint32_t owner_tid) {
    FLOW_LOCK;
    SCHED_LOCK;process_t *w=find_process(waiter_tid),*o=owner_tid?find_process(owner_tid):NULL;
    if(!w || !w->pid || w->state==PROC_ZOMBIE || (owner_tid && (!o || !o->pid || o->state==PROC_ZOMBIE)))return -1;
    int result=flow_depend(&flow_local,&w->scheduling,o?&o->scheduling:NULL,sched_clock_ns());
    if(!result && o && o->scheduling.cpu)smp_reschedule(o->scheduling.cpu->cpu_id);
    return result;
}
void scheduler_wait_dependency_clear(void) {
    FLOW_LOCK;
    SCHED_LOCK;if(current_process && current_process->pid)
        flow_undepend(&flow_local,&current_process->scheduling,sched_clock_ns());
}
int scheduler_waiter_precedes(uint32_t a,uint32_t b) {
    FLOW_LOCK;
    SCHED_LOCK;process_t *first=find_process(a),*second=find_process(b);
    return first && (!second || flow_wait_precedes(&first->scheduling,&second->scheduling,sched_clock_ns()));
}
void scheduler_nonpreempt_enter(void) {
    vex_cpu_t *cpu=smp_current_cpu();
    if(cpu) {
        if(!cpu->nonpreempt_depth++) {
            cpu->guard_enter_ns=sched_clock_ns();
            cpu->guard_entry_pc=(uint64_t)__builtin_return_address(0);
        }
        return;
    }
    if(!flow_nonpreempt_depth++) {
        flow_kernel_enter_ns=sched_clock_ns();
        flow_kernel_entry_pc=(uint64_t)__builtin_return_address(0);
    }
}
void scheduler_nonpreempt_leave(void) {
    vex_cpu_t *cpu=smp_current_cpu();
    if(cpu) {
        if(cpu->nonpreempt_depth && !--cpu->nonpreempt_depth) {
            uint64_t elapsed=sched_clock_ns()-cpu->guard_enter_ns;
            if(elapsed>cpu->guard_max_ns){cpu->guard_max_ns=elapsed;cpu->guard_max_pc=cpu->guard_entry_pc;}
        }
        return;
    }
    if(flow_nonpreempt_depth && !--flow_nonpreempt_depth) {
        uint64_t elapsed=sched_clock_ns()-flow_kernel_enter_ns;
        if(elapsed>flow_kernel_max_ns) {
            flow_kernel_max_ns=elapsed;flow_kernel_max_pc=flow_kernel_entry_pc;
        }
    }
}
void scheduler_safe_point(void) {
    smp_stop_safe_point();
    uint64_t flags;__asm__ volatile("pushfq; pop %0":"=r"(flags));
    vex_cpu_t *cpu=smp_current_cpu();
    if((flags&512u) && !(cpu?cpu->nonpreempt_depth:flow_nonpreempt_depth))scheduler_preempt_if_pending();
}
void scheduler_flow_report(void) {
    typedef struct {
        uint32_t tid,owner,state;
        char name[SCHEDULER_TASK_NAME_SIZE];
        flow_thread_t thread;
        flow_group_t group;
    } report_task_t;
    size_t capacity=0,count=0,omitted=0;
    {FLOW_LOCK;for(process_t *p=process_list;p;p=p->next)capacity+=p->pid!=0u;}
    if(capacity>SIZE_MAX/sizeof(report_task_t))return;
    report_task_t *tasks=capacity?kmalloc(capacity*sizeof(*tasks)):NULL;
    if(capacity && !tasks){debug_log_puts("flowsched: snapshot allocation failed\n");return;}
    flow_cpu_t local;
    uint64_t at,guard_max,guard_pc,dispatch_max,fair;
    uint32_t cpu_index;int index_check;
    {
        FLOW_LOCK;
        at=sched_clock_ns();scheduler_account(at);
        vex_cpu_t *cpu=smp_current_cpu();cpu_index=smp_current_index();
        index_check=flow_validate(&flow_local);local=flow_local;
        guard_max=cpu?cpu->guard_max_ns:flow_kernel_max_ns;
        guard_pc=cpu?cpu->guard_max_pc:flow_kernel_max_pc;
        dispatch_max=flow_dispatch_max_ns;
        fair=flow_local.domain?flow_local.domain->fair_clock:flow_local.fair_clock;
        for(process_t *p=process_list;p;p=p->next)if(p->pid) {
            if(count==capacity){++omitted;continue;}
            report_task_t *r=&tasks[count++];
            r->tid=p->pid;r->owner=process_owner_pid(p);r->state=p->state;
            memcpy(r->name,p->group?p->group->name:p->name,sizeof(r->name));
            r->name[sizeof(r->name)-1u]=0;
            r->thread=p->scheduling;r->group=*p->scheduling.group;
        }
    }
    /* No serial I/O while holding runqueue/fairness metadata. The snapshot
       owns only values: it never dereferences copied task/group pointers. */
    {char line[96];snprintf(line,sizeof(line),"flowsched-index: cpu=%u check=%d\n",cpu_index,index_check);debug_log_puts(line);}
    debug_log_puts("flowsched: ns=");scheduler_log_u64_dec(at);
    debug_log_puts(" switches=");scheduler_log_u64_dec(local.switches);
    debug_log_puts(" wake_p50_ns<=");scheduler_log_u64_dec(flow_latency_percentile(&local,50));
    debug_log_puts(" p95<=");scheduler_log_u64_dec(flow_latency_percentile(&local,95));
    debug_log_puts(" p99<=");scheduler_log_u64_dec(flow_latency_percentile(&local,99));
    debug_log_puts(" max=");scheduler_log_u64_dec(local.latency_max_ns);
    debug_log_puts(" nonpreempt_max=");scheduler_log_u64_dec(guard_max);
    debug_log_puts(" nonpreempt_at=0x");debug_log_u64_hex(guard_pc);
    debug_log_puts(" selected_running_max=");scheduler_log_u64_dec(dispatch_max);
    debug_log_puts(" propagated=");scheduler_log_u64_dec(local.propagated);
    debug_log_puts(" inversions=");scheduler_log_u64_dec(local.inversions);
    debug_log_puts(" cycles_rejected=");scheduler_log_u64_dec(local.dependency_cycles);
    debug_log_puts(" selection_cycles=");scheduler_log_u64_dec(local.cycles);debug_log_puts("\n");
    for(size_t i=0;i<count;++i) {
        report_task_t *r=&tasks[i];flow_thread_t *t=&r->thread;flow_group_t *g=&r->group;
        debug_log_puts("flowsched-task: tid=");scheduler_log_u64_dec(r->tid);
        debug_log_puts(" group=");scheduler_log_u64_dec(r->owner);
        debug_log_puts(" cpu_ns=");scheduler_log_u64_dec(t->cpu_ns);
        debug_log_puts(" entitled_ns=");scheduler_log_u64_dec(t->entitlement_ns+(t->runnable?g->thread_clock-t->entitlement_stamp:0));
        debug_log_puts(" state=");debug_log_puts(scheduler_state_name_value((uint8_t)r->state));
        debug_log_puts(" runnable=");scheduler_log_u64_dec(t->runnable);
        debug_log_puts(" claimed=");scheduler_log_u64_dec(t->running_cpu);
        debug_log_puts(" service=");scheduler_log_u64_dec((uint64_t)g->entity.service);
        debug_log_puts(" group_cpu_ns=");scheduler_log_u64_dec(g->cpu_ns);
        debug_log_puts(" group_entitled_ns=");scheduler_log_u64_dec(g->entitlement_ns+(g->threads.count?fair-g->entitlement_stamp:0));
        flow_cpu_t histogram={0};memcpy(histogram.latency_hist,t->latency_hist,sizeof(histogram.latency_hist));
        debug_log_puts(" wake_p50_ns<=");scheduler_log_u64_dec(flow_latency_percentile(&histogram,50));
        debug_log_puts(" p95<=");scheduler_log_u64_dec(flow_latency_percentile(&histogram,95));
        debug_log_puts(" p99<=");scheduler_log_u64_dec(flow_latency_percentile(&histogram,99));
        debug_log_puts(" max=");scheduler_log_u64_dec(t->latency_max_ns);
        memcpy(histogram.latency_hist,t->input_hist,sizeof(histogram.latency_hist));
        debug_log_puts(" input_p50_ns<=");scheduler_log_u64_dec(flow_latency_percentile(&histogram,50));
        debug_log_puts(" p95<=");scheduler_log_u64_dec(flow_latency_percentile(&histogram,95));
        debug_log_puts(" p99<=");scheduler_log_u64_dec(flow_latency_percentile(&histogram,99));
        debug_log_puts(" input_max=");scheduler_log_u64_dec(t->input_max_ns);
        debug_log_puts(" name=");debug_log_puts(r->name);debug_log_puts("\n");
    }
    if(omitted){debug_log_puts("flowsched: tasks added during snapshot, omitted=");scheduler_log_u64_dec(omitted);debug_log_puts("\n");}
    for(uint32_t i=0;i<smp_cpu_count();++i) {
        uint32_t candidate=0,current=0,index=0,pending=0;uint64_t timers=0;
        {FLOW_LOCK;
         scheduler_cpu_t *state=smp_cpu_at(i)->scheduler;
         if(state){index=state->flow.cpu_id;current=state->current?state->current->pid:0;
             pending=state->pending;flow_thread_t *picked=flow_pick(&state->flow);
             candidate=picked?((process_t *)picked->entity.owner)->pid:0;}
         timers=smp_cpu_at(i)->timer_count;}
        char line[160];snprintf(line,sizeof(line),"smp-pick: cpu=%u index=%u current=%u candidate=%u pending=%u timers=%u\n",i,index,current,candidate,pending,(unsigned)timers);debug_log_puts(line);
    }
    kfree(tasks);
}
/* Called on the incoming kernel stack after CR3/FPU/FS_BASE restoration. */
void scheduler_running(void) {
    process_t *stopped=NULL;
    {
    FLOW_LOCK;
    process_t *old=scheduler_local()->switch_from;
    scheduler_local()->switch_from=NULL;
    if(old && old->pid) {
        flow_unclaim(&old->scheduling);
        if(old->pause_parked)context_notify(old->pid);
        if(old->terminate_requested && old->state!=PROC_ZOMBIE)scheduler_finish_stop(old);
        else if(old->state==PROC_READY && !flow_cpu_allowed(&old->scheduling,smp_current_index()))
            for(uint32_t i=0;i<smp_cpu_count();i++)if(flow_cpu_allowed(&old->scheduling,i)){smp_reschedule(i);break;}
    }
    if(old && old->pid && old->state==PROC_ZOMBIE && !old->stopped_cleanup_done) {
        if(old->borrowed_refs==UINT32_MAX)halt_forever();
        old->stopped_cleanup_done=1u;++old->borrowed_refs;stopped=old;
    }
    if(current_process && current_process->pid) {
        flow_thread_t *t=&current_process->scheduling;t->running_ns=sched_clock_ns();
        uint64_t elapsed=t->running_ns-t->selected_ns;
        if(elapsed>flow_dispatch_max_ns)flow_dispatch_max_ns=elapsed;
    }
    }
    if(stopped) {
        scheduler_stopped_cleanup(stopped);
        {FLOW_LOCK;--stopped->borrowed_refs;}
    }
}
uint64_t scheduler_idle_ns_now(void) {
    FLOW_LOCK;
    uint64_t now=sched_clock_ns(),total=0u;
    scheduler_account(now);
    for(uint32_t i=0;i<smp_cpu_count();++i) {
        vex_cpu_t *cpu=smp_cpu_at(i);
        scheduler_cpu_t *state=cpu->scheduler;
        if(!cpu->online || !state)continue;
        uint64_t idle=state->idle_ns;
        /* Read-only extrapolation: another CPU alone charges its own current
           execution. The metadata gate makes this a coherent snapshot. */
        if(state->current && !state->current->pid && now>=state->account_at)
            idle+=now-state->account_at;
        total=idle>UINT64_MAX-total?UINT64_MAX:total+idle;
    }
    return total;
}
uint64_t scheduler_thread_cpu_ns(uint32_t tid) {
    FLOW_LOCK;
    SCHED_LOCK;scheduler_account(sched_clock_ns());process_t *p=find_process(tid);return p?p->scheduling.cpu_ns:0;
}

int process_kill_group(uint32_t process_id,int status) {
    int caller_exits=0;
    SCHEDULER_TASK_REF(ref);
    process_t *target=scheduler_task_acquire(&ref,process_id,1);
    if(!target || !target->pid || !target->group || target->state==PROC_CONSTRUCTING)return -1;
    if(!scheduler_can_control_process(target->pid))return -2;
    {
        FLOW_LOCK;
        process_group_t *group=target->group;
        int found=0;
        for(process_t *p=process_list;p;p=p->next)
            if(p->group==group && p->state!=PROC_ZOMBIE)found=1;
        if(!found)return -1;
        group->terminating=1u;
        for(process_t *p=process_list;p;p=p->next) {
            if(p->group!=group)continue;
            scheduler_request_stop(p,status);
            if(p==current_process)caller_exits=1;
        }
    }
    security_audit_log_event("proc-kill-group","signal",scheduler_current_pid(),
        scheduler_current_uid(),scheduler_current_capabilities(),1u);
    if(caller_exits){schedule();halt_forever();}
    reap_zombies(current_process);
    return 0;
}
int scheduler_resource_terminate(uint32_t pid,int status) {
    FLOW_LOCK;process_t *p=find_process(pid);if(!p || !p->group || p->state==PROC_CONSTRUCTING)return -ESRCH;
    p->group->terminating=1;p->group->resource_exit_status=status;p->group->resource_status_valid=1;
    for(process_t *q=process_list;q;q=q->next)if(q->group==p->group)scheduler_request_stop(q,status);
    return 0;
}
int process_kill(uint32_t pid) {
    SCHEDULER_TASK_REF(ref);
    process_t *p=scheduler_task_acquire(&ref,pid,1);
    if(!p || !p->pid || p->state==PROC_ZOMBIE || p->state==PROC_CONSTRUCTING)return -1;
    if(!scheduler_can_control_process(p->pid))return -1;
    int caller_exits;
    {
        FLOW_LOCK;
        if(p->state==PROC_ZOMBIE)return -1;
        caller_exits=p==current_process;
        scheduler_request_stop(p,128+9);
    }
    security_audit_log_event("proc-kill","signal",scheduler_current_pid(),
        scheduler_current_uid(),scheduler_current_capabilities(),1u);
    if(caller_exits){schedule();halt_forever();}
    reap_zombies(current_process);
    return 0;
}
int scheduler_stop_own_thread(uint32_t tid,int status) {
    SCHEDULER_TASK_REF(ref);
    process_t *p=scheduler_task_acquire(&ref,tid,0);
    if(!p || !p->pid)return -ESRCH;
    FLOW_LOCK;
    if(!current_process || !current_process->group || p->group!=current_process->group)return -EPERM;
    if(p->state==PROC_ZOMBIE || p->state==PROC_CONSTRUCTING)return -ESRCH;
    scheduler_request_stop(p,status);
    return 0;
}

int process_wait_pid(int32_t pid, int *status, uint32_t flags, uint32_t *reaped_pid) {
    SCHED_LOCK;
    if (current_process == NULL) {
        return -1;
    }

    for (;;) {
        int has_matching_child = 0;
        uint32_t collected=0u;int collected_status=0;
        {
        FLOW_LOCK;
        current_process->wait_child_active=0u;
        for (process_t *process = process_list; process != NULL; process = process->next) {
            /* A CLONE_THREAD task has a TID for signalling/clear-child-tid,
               but it is not a waitpid child of its sibling threads. */
            if (current_process->group != NULL && process->group == current_process->group) {
                continue;
            }
            if (process->parent_pid != process_owner_pid(current_process)) {
                continue;
            }

            if (pid > 0 && process->pid != (uint32_t)pid) {
                continue;
            }

            has_matching_child = 1;
            if (process->state != PROC_ZOMBIE || process->stopped_cleanup_done!=2u) {
                continue;
            }
            int quiescent=1;
            for(process_t *q=process_list;q;q=q->next)
                if(process->group && q->group==process->group &&
                   (q->state!=PROC_ZOMBIE || q->stopped_cleanup_done!=2u)){quiescent=0;break;}
            if(!quiescent)continue;

            collected=process->pid;collected_status=process->exit_status;
            process->wait_collected = 1u;
            process->parent_pid = 0u;
            break;
        }
        if(!collected && has_matching_child && !(flags&PROCESS_WAIT_NOHANG)) {
            current_process->wait_child_pid=pid;
            current_process->wait_child_active=1u;
            if(pid>0)(void)scheduler_wait_dependency((uint32_t)pid);
            scheduler_set_state(current_process,PROC_WAITING);
        }
        }
        if(collected) {
            if(status)*status=collected_status;
            if(reaped_pid)*reaped_pid=collected;
            reap_zombies(current_process);return (int)collected;
        }
        if (!has_matching_child) {
            *ladybird_errno_location() = ECHILD;
            return -1;
        }
        if ((flags & PROCESS_WAIT_NOHANG) != 0u) {
            if (reaped_pid != NULL) {
                *reaped_pid = 0u;
            }
            return 0;
        }

        schedule();
        if(current_process==&kernel_process && current_process->state==PROC_WAITING)
            scheduler_idle_wait();
        scheduler_wait_dependency_clear();
    }
}

int process_reap_child_pid(uint32_t parent_pid, uint32_t child_pid,
                           int *status, uint32_t *reaped_pid) {
    SCHEDULER_TASK_REF(ref);
    process_t *child;

    if (parent_pid == 0u || child_pid == 0u) {
        return -1;
    }
    child = scheduler_task_acquire(&ref,child_pid,0);
    {
    FLOW_LOCK;
    if (child == NULL || child->parent_pid != parent_pid ||
        (current_process != NULL && child->group == current_process->group)) {
        return -1;
    }
    if (child->state != PROC_ZOMBIE) {
        return 0;
    }
    if (status != NULL) {
        *status = child->exit_status;
    }
    if (reaped_pid != NULL) {
        *reaped_pid = child->pid;
    }
    child->wait_collected = 1u;
    child->parent_pid = 0u;
    }
    scheduler_task_release(&ref);
    reap_zombies(current_process);
    return (int)child_pid;
}

int process_wait_thread(uint32_t tid, int *status_out) {
    SCHED_LOCK;
    SCHEDULER_TASK_REF(ref);
    if (current_process == NULL || current_process->group == NULL || tid == 0u ||
        tid == current_process->pid) return -1;
    process_t *target=scheduler_task_acquire(&ref,tid,0);
    if(!target)return -1;
    for(;;) {
        {
            FLOW_LOCK;
            if(target->group!=current_process->group || !target->native_thread_joinable ||
               (target->join_waiter_tid && target->join_waiter_tid!=current_process->pid))return -1;
            if(target->state==PROC_ZOMBIE && target->stopped_cleanup_done==2u) {
                if(status_out)*status_out=target->exit_status;
                target->join_waiter_tid=0u;target->parent_pid=0u;target->wait_collected=1u;
                break;
            }
            target->join_waiter_tid=current_process->pid;
            if(scheduler_wait_dependency(tid)) {target->join_waiter_tid=0u;return -1;}
            /* Completion and waiter registration use the same metadata
               transaction. No remote exit can wake before WAITING is visible. */
            scheduler_set_state(current_process,PROC_WAITING);
        }
        schedule();
        scheduler_wait_dependency_clear();
    }
    scheduler_task_release(&ref);
    reap_zombies(current_process);
    return 0;
}

uint64_t scheduler_ticks_now(void) {
    return scheduler_ticks;
}

uint64_t scheduler_idle_ticks_now(void) {
    return scheduler_idle_ticks;
}

uint64_t scheduler_process_count(void) {
    FLOW_LOCK;
    uint64_t count = 0u;
    for (process_t *process = process_list; process != NULL; process = process->next) {
        ++count;
    }
    return count;
}

int scheduler_process_info_at(uint64_t index, scheduler_process_info_t *out) {
    FLOW_LOCK;
    uint64_t current = 0u;

    if (out == NULL) {
        return -1;
    }

    for (process_t *process = process_list; process != NULL; process = process->next) {
        if (current == index) {
            scheduler_refresh_process_capabilities(process);
            out->pid = process->pid;
            out->process_id = process->group == NULL ? process->pid : process->group->process_id;
            out->parent_pid = process->parent_pid;
            out->state = (uint8_t)process->state;
            out->priority = process->priority;
            out->user_mode = process->user_mode;
            out->is_admin = process->is_admin;
            out->capabilities = process->capabilities | process->elevated_capabilities;
            out->base_capabilities = process->capabilities;
            out->elevated_capabilities = process->elevated_capabilities;
            out->uid = process->uid;
            out->sleep_until_tick = process->sleep_until_tick;
            out->elevated_capabilities_expires_tick = process->elevated_capabilities_expires_tick;
            out->cpu_ticks = __atomic_load_n(&process->cpu_ticks, __ATOMIC_RELAXED);
            memcpy(out->name, process->group == NULL ? process->name : process->group->name,
                   sizeof(out->name));
            return 0;
        }
        ++current;
    }

    return -1;
}

void scheduler_tick(void) {
    FLOW_LOCK;
    uint64_t now=sched_clock_ns();scheduler_account(now);
    uint64_t tick=now/10000000ull;
    uint64_t delta=tick>=scheduler_local()->account_tick?tick-scheduler_local()->account_tick:0;
    scheduler_local()->account_tick=tick;
    /* Wall-clock deadlines are not multiplied by the number of CPU timers. */
    if(smp_current_index()==0u)scheduler_ticks=tick;
    if(current_process==&kernel_process)scheduler_idle_ticks+=delta;
    else if(current_process && current_process->state==PROC_RUNNING)current_process->cpu_ticks+=delta;
    flow_thread_t *t;
    while((t=flow_timer_pop(&flow_local,now))) {
        process_t *p=t->entity.owner;p->sleep_until_tick=0;
        flow_adopt(&flow_local,t,flow_new(&flow_local,FLOW_TIMER,p->pid,now,FLOW_SLICE_NS),now);
        scheduler_set_state(p,PROC_READY);preempt_pending=1;
    }
    if(now-flow_switch_at>=FLOW_SLICE_NS)preempt_pending=1;
}

/* Preemptive reschedule, called from the timer IRQ ONLY when a user-mode task
 * was interrupted (CPL=3). This is the safe preemption point: the interrupted
 * task wasn't inside a kernel syscall, so no kernel state is mid-mutation, and
 * the cooperative kernel paths keep running to completion as before. Without
 * this, a process spinning in userspace (e.g. WebKit deadlocking on a thread)
 * never yields and freezes the whole machine, since plain scheduler_tick()
 * doesn't switch tasks. FlowSched selects an eligible group, then thread,
 * using indexed virtual deadlines without scanning the process inventory. */
void scheduler_preempt_user(struct interrupt_frame *frame) {
    scheduler_user_interrupt(frame);
    if (!preempt_pending || current_process == NULL || current_process == &kernel_process) {
        return;
    }
    if (current_process->state != PROC_RUNNING && !current_process->terminate_requested) {
        return;
    }
    schedule();
}

/* Set by the timer IRQ; consumed at the next syscall boundary. This is how we
 * time-slice a process that stays in kernel mode by looping fast syscalls
 * (e.g. WebKit after startup): the timer can't safely preempt arbitrary kernel
 * code, but the syscall return path is a clean point where no kernel state is
 * held, so we yield there instead. */

void scheduler_request_preempt(void) {
    preempt_pending = 1u;
}

void scheduler_preempt_if_pending(void) {
    if (preempt_pending == 0u) {
        return;
    }
    preempt_pending = 0u;
    if (current_process != NULL && current_process != &kernel_process &&
        (current_process->state == PROC_RUNNING || current_process->terminate_requested)) {
        schedule();
    }
}

void schedule(void) {
    smp_stop_safe_point();
    SCHED_LOCK;uint64_t sched_start=sched_clock_cycles();
    scheduler_stop_at_boundary();
    scheduler_account(sched_clock_ns());
    if (current_process == NULL) {
        return;
    }

    reap_zombies(current_process);

    FLOW_LOCK;
    process_t *next = select_next_ready();

    if (next == NULL && current_process != &kernel_process &&
        (current_process->state != PROC_RUNNING ||
         !flow_cpu_allowed(&current_process->scheduling,smp_current_index()))) {
        kernel_process.state = PROC_READY;
        next = &kernel_process;
    }

    if (next == NULL || next == current_process) {
        if (current_process->state == PROC_READY) {
            scheduler_set_state(current_process,PROC_RUNNING);
        }
        flow_local.cycles+=sched_clock_cycles()-sched_start;
        return;
    }

    process_t *previous = current_process;
    if(next->pid)++previous->dispatch_count; /* yield to work, not to idle */
    /* With FSGSBASE disabled, zero cannot change to nonzero without the
       native setter. Preserve user selector loads too when a base was set. */
    if(previous->user_gs_base)previous->user_gs_base=read_user_gs_slot();
    uint64_t gs_change=previous->user_gs_base==next->user_gs_base?UINT64_MAX:next->user_gs_base;
    if (previous->state == PROC_RUNNING) {
        scheduler_set_state(previous, previous->pid == 0 ? PROC_WAITING : PROC_READY);
    }
    if(previous->pid && previous->state==PROC_READY && !previous->scheduling.runnable_ns)
        previous->scheduling.runnable_ns=sched_clock_ns();

    if(next->pid) {
        if(next->scheduling.cpu!=&flow_local)++next->migrations;
        if(flow_claim(&flow_local,&next->scheduling))halt_forever();
    }
    scheduler_set_state(next,PROC_RUNNING);
    scheduler_local()->switch_from=previous;
    current_process = next;
    scheduler_local()->cycles_at=sched_clock_cycles();
    flow_switch_at=flow_account_at=sched_clock_ns();
    if(next->pid)flow_selected(&flow_local,&next->scheduling,flow_switch_at);
    preempt_pending=0;
    if (next->user_mode != 0)
        gdt_set_tss_rsp0(process_kernel_stack_top(next));
    flow_local.cycles+=sched_clock_cycles()-sched_start;
    scheduler_trace_fpu_switch("schedule", previous, next);
    scheduler_flow_unlock(&flow_guard); /* release before changing stacks */
    scheduler_context_switch(&previous->rsp, next->rsp, next->cr3,
                             previous->fpu_state, next->fpu_state,
                             scheduler_xsave_mask, next->fs_base, gs_change);
}

int scheduler_cpu_is_idle(void) {
    scheduler_cpu_t *cpu=scheduler_local();
    return cpu->current==&cpu->idle;
}
/* Groups with runnable threads queued on this CPU (diagnostic, read racily). */
uint32_t scheduler_cpu_queue_depth(void) {
    return scheduler_local()->flow.groups.count;
}
void scheduler_idle_wait(void) {
    SCHED_LOCK;
    {
        FLOW_LOCK;
        if(select_next_ready())return;
    }
    /* IF remains zero from the check to STI/HLT. An IPI published during this
       window remains pending and wakes HLT; there is no check/sleep gap. */
    __asm__ volatile("sti; hlt":::"memory");
}

int scheduler_isolate_prepared(uint32_t pid){
    FLOW_LOCK;process_t *p=find_process(pid);
    if(!p || p->state!=PROC_CONSTRUCTING || !construction_owned(p) || !p->group || p->abi!=VEX_ABI_NATIVE)return -EPERM;
    p->group->ultra_space=1;p->group->fs_sealed=1;
    p->capabilities=p->elevated_capabilities=0;return 0;
}
