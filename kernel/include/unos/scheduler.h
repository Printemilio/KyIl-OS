#ifndef UNOS_SCHEDULER_H
#define UNOS_SCHEDULER_H

#include <stdint.h>
#include <unos/vex_thread_control_abi.h>
#include <unos/vex_process_abi.h>
#include <stddef.h>
#include <unos/events.h>
#include <unos/flowsched.h>

#define SCHEDULER_SYSCALL_FRAME_SIZE 144u
#define SCHEDULER_INTERRUPT_FRAME_SIZE 176u
#define SCHEDULER_TASK_NAME_SIZE 24u

/* Process-wide ownership.  A process_t is a schedulable thread; all threads
 * in the same group point at one of these objects. */
typedef struct process_group {
    flow_group_t scheduling;
    uint32_t process_id;
    uint32_t refcount;
    /* Storage lifetime is independent of the number of execution contexts.
       One group reference plus explicit temporary address-space borrowers. */
    uint32_t storage_refs;
    uint64_t cr3;
    uint64_t user_alloc_base;
    uint64_t user_alloc_next;
    /* Process-wide Linux brk cursor, separate from the native mmap hint. */
    uint64_t linux_brk;
    uint8_t owns_address_space;
    uint8_t user_mode;
    uint8_t abi;
    char name[SCHEDULER_TASK_NAME_SIZE];
    /* Private teardown state, protected by the scheduler metadata gate. */
    uint8_t terminating, descriptors_closed;
    int resource_exit_status;
    uint8_t resource_status_valid;
    uint8_t ultra_space; /* No ambient authority; explicit native resources only. */
    uint8_t fs_sealed; /* Native filesystem authority is explicit once sealed. */
    uint64_t birth_realtime_ns,birth_monotonic_ns,exit_realtime_ns,user_cpu_ns,kernel_cpu_ns;
    uint8_t birth_time_valid;
    uint32_t exit_order;
    uint8_t shutdown_notified;
    /* Linux compatibility dispositions that are process-wide but do not
       belong in the native VexOS event table.  SIGPIPE is currently the
       only non-Vex event disposition needed by the real WPE workload. */
    uint64_t linux_sigpipe_handler;
    uint64_t linux_sigpipe_flags;
    uint64_t linux_sigpipe_restorer;
    uint64_t linux_sigpipe_mask[2];
    uint64_t linux_sigusr1_action_mask[2];
    /* Linux personality signal dispositions for the two NPTL-internal
       signals.  They are stored alongside the generic Vex event slots; no
       Linux signal identifiers are exposed by the native event ABI. */
    uint64_t linux_internal_action_mask[2][2];
    /* VSL dispositions for hardware faults.  The native event ABI does not
       expose Linux signal numbers; compat_linux owns their interpretation. */
    uint64_t linux_sync_fault_handler[2];
    uint64_t linux_sync_fault_flags[2];
    uint64_t linux_sync_fault_restorer[2];
    uint64_t linux_sync_fault_mask[2][2];
    uint32_t event_action_mask[VEX_EVENT_SLOT_COUNT];
    vex_event_registration_t events[VEX_EVENT_SLOT_COUNT];
} process_group_t;

#define SCHEDULER_FPU_STATE_SIZE 4096u

typedef struct process {
    uint32_t pid;
    uint64_t *stack;
    uint64_t kernel_stack_size;
    uint64_t rsp;
    uint64_t cr3;
    /* These fields are observed/updated by scheduler_tick from interrupt
       context while kernel code may be waiting or scanning ready processes. */
    volatile enum { PROC_RUNNING, PROC_READY, PROC_WAITING, PROC_SLEEPING, PROC_ZOMBIE, PROC_CONSTRUCTING } state;
    uint8_t priority;
    /* Legacy priority and Linux policy are compatibility metadata, not CPU
       entitlement. FlowSched independently accounts process/thread service. */
    uint32_t linux_sched_policy;
    int32_t linux_sched_priority;
    uint8_t linux_sched_reset_on_fork;
    uint32_t uid;
    uint8_t is_admin;
    uint32_t capabilities;
    uint32_t elevated_capabilities;
    uint8_t user_mode;
    uint8_t in_user_syscall;
    uint8_t abi;
    uint64_t user_rsp;
    uint64_t user_alloc_base;
    uint64_t user_alloc_next;
    volatile uint64_t sleep_until_tick;
    /* Precise wake-up time (sched_clock ns) of a timed native wait; when
       set it replaces the 10 ms tick granularity of sleep_until_tick. */
    uint64_t sleep_deadline_ns;
    /* Timer-tick accounting for the schedulable thread (TID). */
    volatile uint64_t cpu_ticks;
    char name[SCHEDULER_TASK_NAME_SIZE];
    uint32_t parent_pid;
    int exit_status;
    uint8_t wait_collected;
    uint8_t resource_suspended;
    uint8_t resource_owned;
    uint64_t syscall_frame_rsp;
    uint64_t resume_syscall_rsp;
    uint64_t pending_user_entry;
    uint64_t pending_old_cr3;
    uint8_t pending_old_cr3_owned;
    uint64_t fs_base;
    uint64_t user_gs_base;
    struct execution_gate *execution_gate;
    struct process *execution_wait_next;
    uint8_t execution_admitted,execution_counted,execution_pending,execution_woken;
    int kernel_errno;
    uint8_t shares_address_space;
    /* Temporary clone3/posix_spawn child borrowing its parent's address
       space until execve; it has a separate process-owned FD table. */
    uint8_t spawn_shared;
    uint32_t spawn_parent_pid;
    uint32_t spawn_wait_child_pid;
    uint8_t spawn_wait_done;
    uint64_t clear_child_tid;
    /* Native thread entry stacks are process-memory mappings whose lifetime
       follows this schedulable thread, not the process group's last thread. */
    uint64_t owned_user_stack_base;
    uint64_t owned_user_stack_size;
    uint32_t join_waiter_tid;
    uint8_t native_thread_joinable;
    uint8_t reserved_thread[3];
    uint64_t elevated_capabilities_expires_tick;
    uint32_t event_pending;
    uint32_t event_mask;
    uint32_t event_sender_pid[VEX_EVENT_SLOT_COUNT];
    /* Full Linux signal mask is compatibility state.  Native Vex event bits
       are synchronized only for events that have a Vex representation. */
    uint64_t linux_signal_mask;
    uint8_t event_active;
    uint8_t event_saved_event;
    uint16_t event_reserved;
    uint32_t event_saved_mask;
    /* Alternate stack for VEX_EVENT_ON_ALT_STACK handlers (size 0: none). */
    uint64_t event_alt_stack_base;
    uint64_t event_alt_stack_size;
    uint64_t event_user_context_addr;
    uint64_t event_return_rax;
    uint8_t event_context_kind;
    uint8_t event_return_rax_valid;
    uint8_t event_saved_frame[SCHEDULER_INTERRUPT_FRAME_SIZE];
    uint8_t event_saved_fpu_valid;
    uint8_t event_reserved1[7];
    uint8_t event_saved_fpu_state[SCHEDULER_FPU_STATE_SIZE] __attribute__((aligned(64)));
    process_group_t *group;
    flow_thread_t scheduling;
    flow_group_view_t *private_flow_views;
    uint8_t fpu_state[SCHEDULER_FPU_STATE_SIZE] __attribute__((aligned(64)));
    struct process *next;
    struct process *pid_next;
    struct process *zombie_next;
    uint8_t zombie_queued;
    /* Private registry leases. A zombie cannot retire its stack/group while
       another CPU holds a task reference. Outgoing stack leases are cancelled
       after this execution context has stopped, including killed waiters. */
    uint32_t borrowed_refs;
    struct scheduler_task_ref *outgoing_refs;
    /* Internal construction ownership, independent of parent/process identity.
       No context may run or kill this task before its creator publishes it. */
    uint32_t construction_tid, construction_cpu;
    int32_t wait_child_pid;
    uint8_t wait_child_active;
    uint8_t stopped_cleanup_done;
    uint8_t terminate_requested;
    uint8_t compat_stdio_ensuring;
    int terminate_status;
    struct vex_fd_borrow *fd_borrows;
    process_group_t *borrowed_memory_group;
    struct vex_scratch *scratch_buffers;
    void *native_request_snapshot;
    size_t native_request_snapshot_size;
    uint64_t native_request_user_address;
    uint64_t *execution_per_cpu;
    uint32_t migrations;
    uint8_t placement_explicit;
    uint64_t event_saved_rax;
    void *fs_authority; /* Trusted, stack-local FS transaction; never user input. */
    uint32_t pause_count,context_wait_tid;
    uint8_t pause_parked;
    vex_event_user_context_t paused_context;
    uint64_t cpu_cycles,dispatch_count;
    uint8_t accounting_user;
} process_t;

typedef struct scheduler_task_ref {
    process_t *target,*owner;
    struct scheduler_task_ref *previous,*next;
} scheduler_task_ref_t;
/* Initialize to zero; release before reusing. Never copy an active lease. */
process_t *scheduler_task_acquire(scheduler_task_ref_t *ref,uint32_t id,int process_identity);
void scheduler_task_release(scheduler_task_ref_t *ref);
#define SCHEDULER_TASK_REF(name) scheduler_task_ref_t name __attribute__((cleanup(scheduler_task_release)))={0}

typedef struct scheduler_process_info {
    uint32_t pid;
    uint32_t process_id;
    uint32_t parent_pid;
    uint8_t state;
    uint8_t priority;
    uint8_t user_mode;
    uint8_t is_admin;
    uint32_t capabilities;
    uint32_t base_capabilities;
    uint32_t elevated_capabilities;
    uint32_t uid;
    uint64_t sleep_until_tick;
    uint64_t elevated_capabilities_expires_tick;
    uint64_t cpu_ticks;
    char name[SCHEDULER_TASK_NAME_SIZE];
} scheduler_process_info_t;

void scheduler_init(void);
uint32_t scheduler_current_pid(void);
/* The compatibility PID is the process identity shared by its threads. */
uint32_t scheduler_current_process_id(void);
uint32_t scheduler_current_memory_owner_pid(void);
void process_spawn_child_ready(uint32_t child_pid);
void process_wait_spawn(uint32_t child_pid);
/* A thread identity suitable for clone/TLS/clear-child-tid operations. */
uint32_t scheduler_current_tid(void);
uint32_t scheduler_current_ppid(void);
uint8_t scheduler_process_accepts_input(uint32_t pid);
uint32_t process_create(void (*entry)(void), uint8_t priority);
uint32_t process_create_with_stack(void (*entry)(void), uint8_t priority, uint64_t stack_size);
uint32_t process_create_in_address_space(void (*entry)(void), uint8_t priority, uint64_t cr3);
/* Kernel-only two-phase construction. Prepared tasks are registered but not
   runnable. Publish/abort is restricted to the constructing execution context. */
uint32_t process_prepare_in_address_space(void (*entry)(void), uint8_t priority, uint64_t cr3);
uint32_t process_prepare_user_abi_caps(void (*entry)(void), uint8_t priority, uint64_t cr3, uint64_t user_rsp, uint8_t abi, uint32_t caps);
int scheduler_publish_process(uint32_t pid);
/* Internal construction placement; does not introduce a userspace ABI. */
int scheduler_bind_prepared_task(uint32_t pid,uint32_t cpu_index);
/* Internal placement at a blocked, unclaimed-context rendezvous. */
int scheduler_bind_blocked_task(uint32_t tid,uint32_t cpu_index);
uint64_t scheduler_pending_retirements(void);
void scheduler_enable_smp_dispatch(void);
int scheduler_allow_migration(uint32_t tid);
void scheduler_dump_cpu_distribution(uint32_t process_id);
int process_wait_event_interruptible(const volatile uint8_t *flag);
int scheduler_relocate_current(uint32_t cpu_index);
int scheduler_abort_process(uint32_t pid);
uint32_t process_create_user(void (*entry)(void), uint8_t priority, uint64_t cr3, uint64_t user_rsp);
uint32_t process_create_user_abi_caps(void (*entry)(void), uint8_t priority, uint64_t cr3, uint64_t user_rsp, uint8_t abi, uint32_t caps);
uint32_t process_create_user_abi(void (*entry)(void), uint8_t priority, uint64_t cr3, uint64_t user_rsp, uint8_t abi);
void scheduler_record_syscall_frame(uint64_t rsp);
int scheduler_thread_control(vex_thread_control_req_t *request);
void scheduler_user_return(uint64_t frame,uint64_t result);
struct interrupt_frame;
void scheduler_user_interrupt(struct interrupt_frame *frame);
uint32_t process_fork_current(void);
uint32_t process_fork_current_tid(uint64_t child_tid, uint8_t set_tid, uint8_t clear_tid);
/* Process-style clone with an explicit userspace stack (clone3/posix_spawn).
   The child gets an independent address space but resumes the interrupted
   syscall on the supplied stack before invoking glibc's child trampoline. */
uint32_t process_fork_current_with_stack(uint64_t child_stack);
uint32_t process_clone_thread(uint64_t child_stack, uint64_t flags, uint64_t parent_tid, uint64_t child_tid, uint64_t tls);
/* The stack belongs to the caller (Vex C library threads): the kernel
   neither unmaps it nor keeps the thread for VEX_SYS_THREAD_JOIN. */
#define PROCESS_NATIVE_THREAD_CALLER_STACK (1u << 0)
#define PROCESS_NATIVE_THREAD_START_PAUSED (1u << 1)
uint32_t process_create_native_thread(uint64_t stack_base, uint64_t stack_size,
                                      uint64_t user_entry, uint64_t user_arg,
                                      uint64_t tls_base, uint64_t clear_tid,
                                      uint64_t tid_out, uint32_t flags);
int process_wait_thread(uint32_t tid, int *status_out);
void process_set_clear_child_tid(uint64_t addr);
void process_exit_group(int status);
int futex_wait(uint64_t uaddr, uint32_t expected);
int futex_wake(uint64_t uaddr, uint32_t count);
/* Native futex: negated errno results (-EAGAIN, -ETIMEDOUT, -EINTR, ...). */
int futex_wait_native(uint64_t uaddr, uint32_t expected, uint64_t timeout_ns);
int futex_wake_native(uint64_t uaddr, uint32_t count);
/* CPU time consumed by the calling thread, or by its whole Process. */
uint64_t scheduler_cpu_time_ns(int whole_process);
/* Logs every user thread (state, futex, return address) for diagnostics. */
void scheduler_dump_user_threads(void);
/* A thread's name and the id of its Process; -1 for an unknown thread. */
int scheduler_thread_name(uint32_t tid, char *out, size_t capacity, uint32_t *process_id);
int process_exec_image(uint64_t new_cr3, uint64_t user_entry, uint64_t user_rsp, uint8_t abi, uint64_t user_alloc_next);
int process_prepare_exec_identity(void);
void process_yield(void);
void process_yield_to(uint32_t pid);
void process_sleep_ticks(uint64_t ticks);
void process_wait(void);
/* Atomically arm a blocking wait on a kernel-owned wake flag.  The caller
 * must have linked its waiter before calling; a wake racing the sleep cannot
 * be lost. */
void process_wait_event(const volatile uint8_t *woken);
void process_wait_event_timeout(const volatile uint8_t *woken, uint64_t ticks);
void process_wake(uint32_t pid);
int64_t scheduler_execution_gate(unsigned operation,unsigned capacity,uint64_t capability,uint64_t timeout_ns);
int process_kill(uint32_t pid);
int scheduler_stop_own_thread(uint32_t tid, int status);
/* Terminate a resource-owning Process, including every execution context.
 * Internal kernel API; process_kill retains its legacy task-targeted contract. */
int process_kill_group(uint32_t process_id, int status);
void process_exit(void);
void process_exit_status(int status);
void process_request_exit(int status);
int process_wait_pid(int32_t pid, int *status, uint32_t flags, uint32_t *reaped_pid);
/* Kernel-owned lifecycle managers may reap a known child on behalf of its
 * actual parent.  This never waits and validates the recorded parent PID. */
int process_reap_child_pid(uint32_t parent_pid, uint32_t child_pid,
                           int *status, uint32_t *reaped_pid);
void schedule(void);
/* Atomic no-runnable-work check followed by STI/HLT; remote wake uses IPI. */
void scheduler_idle_wait(void);
int scheduler_cpu_is_idle(void);
uint32_t scheduler_cpu_queue_depth(void);
void scheduler_preempt_user(struct interrupt_frame *frame);
void scheduler_request_preempt(void);
void scheduler_preempt_if_pending(void);
void scheduler_tick(void);
uint64_t scheduler_time_ns(void);
flow_context_t scheduler_flow_current(void);
void scheduler_flow_adopt(flow_context_t flow);
void scheduler_wake_flow(uint32_t tid,flow_context_t flow);
void scheduler_wake_origin(uint32_t tid,uint32_t origin,uint64_t budget_ns);
int scheduler_wait_dependency(uint32_t owner_tid);
int scheduler_dependency_set(uint32_t waiter_tid,uint32_t owner_tid);
void scheduler_wait_dependency_clear(void);
int scheduler_waiter_precedes(uint32_t first_tid,uint32_t second_tid);
void scheduler_flow_report(void);
void scheduler_nonpreempt_enter(void);
void scheduler_nonpreempt_leave(void);
void scheduler_safe_point(void);
uint64_t scheduler_ticks_now(void);
uint64_t scheduler_idle_ticks_now(void);
uint64_t scheduler_idle_ns_now(void);
uint64_t scheduler_thread_cpu_ns(uint32_t tid);
uint32_t scheduler_current_uid(void);
uint32_t scheduler_current_capabilities(void);
uint8_t scheduler_current_is_admin(void);
uint8_t scheduler_current_has_capability(uint32_t capabilities);
uint8_t scheduler_current_user_mode(void);
uint8_t scheduler_current_abi(void);
int scheduler_process_alive(uint32_t pid);
int scheduler_can_control_process(uint32_t pid);
void scheduler_detach_process(uint32_t pid);
int scheduler_set_process_admin(uint32_t pid, uint8_t is_admin);
int scheduler_grant_process_capabilities(uint32_t pid, uint32_t capabilities, uint64_t duration_ticks);
int scheduler_set_process_capabilities(uint32_t pid, uint32_t capabilities);
void scheduler_set_process_abi(uint32_t pid, uint8_t abi);
void scheduler_set_process_user_alloc_next(uint32_t pid, uint64_t user_alloc_next);
void scheduler_set_process_fs_base(uint32_t pid, uint64_t fs_base);
void scheduler_set_current_credentials(uint32_t uid, uint32_t capabilities);
void scheduler_set_current_fs_base(uint64_t fs_base);
void scheduler_restore_current_fs_base(void);
uint64_t scheduler_current_fs_base(void);
void scheduler_set_current_user_gs_base(uint64_t);
uint64_t scheduler_current_user_gs_base(void);
uint64_t scheduler_current_user_rsp(void);
void scheduler_capture_fpu_state(uint8_t *state);
void scheduler_restore_fpu_state(const uint8_t *state);
int scheduler_fpstate_valid(const void *legacy_state);
uint64_t scheduler_current_user_alloc_next(void);
process_t *scheduler_current_process(void);
int scheduler_init_current_cpu(void);
void scheduler_map_kernel_page_all(uint64_t virt, uint64_t phys, uint64_t flags);
void scheduler_unmap_kernel_page_all(uint64_t virt);
const char *scheduler_state_name(uint32_t pid);
const char *scheduler_state_name_value(uint8_t state);
void scheduler_set_process_priority(uint32_t pid, uint8_t priority);
uint64_t scheduler_process_count(void);
int scheduler_process_info_at(uint64_t index, scheduler_process_info_t *out);
int scheduler_set_process_name(uint32_t pid, const char *name);
int scheduler_construction_owned(uint32_t);
int scheduler_isolate_prepared(uint32_t);
typedef struct scheduler_process_observer {struct scheduler_process_observer *next;uint32_t registered;vex_process_resource_info_t info;vex_process_usage_t usage;} scheduler_process_observer_t;
int scheduler_resource_observe(scheduler_process_observer_t *,uint32_t,int);
void scheduler_resource_release(scheduler_process_observer_t *);
int scheduler_resource_info(scheduler_process_observer_t *,void *);
int scheduler_resource_usage(scheduler_process_observer_t *,vex_process_usage_t *);
void scheduler_execution_mode(int user);
int64_t scheduler_process_exit_order(uint32_t,uint64_t,int);
void scheduler_shutdown_order_begin(int);
uint32_t scheduler_shutdown_next(uint32_t *);
int scheduler_process_active(uint32_t);
void scheduler_inventory_counts(uint32_t *,uint32_t *);
int scheduler_resource_wait(scheduler_process_observer_t *,uint64_t,int);
int scheduler_resource_resume(uint32_t);
int scheduler_resource_terminate(uint32_t,int);

#endif
