#ifndef VEX_THREAD_ABI_H
#define VEX_THREAD_ABI_H

#include <stdint.h>

/* Native threading, synchronisation and error primitives used by the Vex C
 * library.  These numbers live after the frozen legacy interval and are only
 * reachable through the tagged native syscall domain.
 *
 * Unlike the historical native calls, which return (uint64_t)-1 and leave the
 * reason in the thread's error slot, every call below returns either a
 * non-negative result or a negated errno value. */
#define VEX_SYS_THREAD_START       200ull
#define VEX_SYS_THREAD_EXIT_UNMAP  201ull
#define VEX_SYS_FUTEX              202ull
#define VEX_SYS_THREAD_SET_TLS     203ull
#define VEX_SYS_THREAD_SELF        204ull
#define VEX_SYS_LAST_ERROR         205ull
#define VEX_SYS_PROCESS_EXIT_GROUP 206ull
#define VEX_SYS_CPU_TIME           211ull
#define VEX_SYS_THREAD_NAME        216ull
#define VEX_SYS_THREAD_CONTEXT     221ull
#define VEX_SYS_THREAD_STOP        222ull
#define VEX_SYS_THREAD_EXIT_WORD   223ull
/* Set/unset (arg1 == 0) the calling thread's existing clear-on-exit word.
 * This supports an initial thread as well as THREAD_START children. The
 * aligned writable word is cleared only after CPU quiescence and futex-woken. */
/* Request termination of a thread in the caller's own process group only.
 * Completion is the existing clear_on_exit/futex notification, not this ACK.
 * Cancellation is deferred to safe scheduler boundaries; no foreign-process
 * authority, callback or Windows lifecycle policy is implied. */
typedef struct vex_thread_stop_req {
    uint32_t tid;
    int32_t status;
} vex_thread_stop_req_t;
/* Architecture-local thread pointers, independent of personality. Existing
 * SET_TLS/203 remains unchanged. Kernel GS/per-CPU state is never exposed.
 * GET returns the current user pointers, TID/CPU and the inventory region
 * containing the saved user stack pointer (0/0 if no such region exists).
 * SET changes only pointers selected by mask; both must be canonical userspace.
 * No scheduling/blocking occurs while committing a SET and its copy-out. */
#define VEX_THREAD_CONTEXT_GET 0u
#define VEX_THREAD_CONTEXT_SET 1u
#define VEX_THREAD_CONTEXT_FS 1u
#define VEX_THREAD_CONTEXT_GS 2u
typedef struct vex_thread_context_req {
    uint32_t size, operation, mask, reserved;
    uint64_t fs_base, gs_base;
    uint64_t stack_low, stack_high;
    uint32_t tid, cpu;
} vex_thread_context_req_t;

/* Start a thread in the calling Process on a caller-owned stack.
 *
 * The new thread enters `entry(arg)` with RSP == stack_top - 8 (stack_top
 * must be 16-byte aligned), FS base == tls_base and the caller's event mask.
 * Its id is stored in *tid_out (if non-NULL) before the call returns and
 * before the thread can run.  When the thread terminates, the kernel stores
 * 0 to *clear_on_exit (if non-NULL) and wakes one futex waiter there.
 *
 * Threads started this way are reaped by the kernel on exit: they are not
 * collected with VEX_SYS_THREAD_JOIN.  Joining is a library protocol built on
 * clear_on_exit. */
typedef struct vex_thread_start_req {
    uint32_t size;          /* sizeof(vex_thread_start_req_t) */
    uint32_t flags;         /* 0 or VEX_THREAD_START_PAUSED (thread_control ABI) */
    uint64_t entry;
    uint64_t arg;
    uint64_t stack_top;
    uint64_t tls_base;
    uint64_t tid_out;       /* int32_t * */
    uint64_t clear_on_exit; /* int32_t * */
} vex_thread_start_req_t;

/* Unmap [base, base+length) from the calling Process and terminate the
 * calling thread with `status`.  Used by a thread whose own stack lives in
 * that range; the call never returns on success. */
typedef struct vex_thread_exit_unmap_req {
    uint64_t base;
    uint64_t length;
    int32_t status;
    uint32_t reserved;
} vex_thread_exit_unmap_req_t;

#define VEX_FUTEX_WAIT 0u
#define VEX_FUTEX_WAKE 1u

/* WAIT: block while *addr == value, until woken, until the relative timeout
 * expires (-ETIMEDOUT) or until a deliverable native event arrives (-EINTR).
 * A timeout of 0 waits without bound. Returns 0 after a wake and -EAGAIN
 * when *addr did not hold `value`. `flags` must be 0.
 * WAKE: wake up to `value` waiters; returns the number woken. */
typedef struct vex_futex_req {
    uint64_t addr;          /* 4-byte aligned user address */
    uint32_t op;
    uint32_t value;
    uint32_t flags;
    uint32_t reserved;
    uint64_t timeout_ns;    /* relative; 0 = no bound */
} vex_futex_req_t;

/* VEX_SYS_THREAD_SET_TLS: arg1 is the new FS base of the calling thread.
 * VEX_SYS_THREAD_SELF: returns the calling thread's id.
 * VEX_SYS_LAST_ERROR: returns the errno recorded by the most recent failing
 * native call of this thread (0 when that call did not record one).
 * VEX_SYS_PROCESS_EXIT_GROUP: terminate every thread of the calling Process
 * with status arg1 (VEX_SYS_PROCESS_EXIT ends only the calling thread).
 * VEX_SYS_CPU_TIME: CPU time in nanoseconds consumed by the calling thread
 * (arg1 == VEX_CPU_TIME_THREAD) or by the live threads of its Process
 * (VEX_CPU_TIME_PROCESS). Accounting granularity is the 10 ms scheduler
 * tick. */
#define VEX_CPU_TIME_THREAD  0u
#define VEX_CPU_TIME_PROCESS 1u

/* VEX_SYS_THREAD_NAME: set or read the name of a thread of the calling
 * Process (tid 0: the caller), as shown by the system's task views. Names
 * are truncated to VEX_THREAD_NAME_MAX - 1 bytes. Returns 0 or -errno. */
#define VEX_THREAD_NAME_MAX 24u
#define VEX_THREAD_NAME_SET 0u
#define VEX_THREAD_NAME_GET 1u

typedef struct vex_thread_name_req {
    uint32_t tid;
    uint32_t op;
    char *name;
    uint64_t size;
} vex_thread_name_req_t;

#endif
