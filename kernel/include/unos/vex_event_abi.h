#ifndef VEX_EVENT_ABI_H
#define VEX_EVENT_ABI_H

/* Native VexOS event classes, delivered with VEX_SYS_EVENT_REGISTER /
 * MASK / POST / RETURN. They are Vex concepts with their own numbering;
 * POSIX signals are a library mapping onto them (see userspace/libc), and
 * the Linux personality keeps its own mapping in compat_linux.c.
 *
 * Slots 2 and 3 are reserved for the Linux personality. A registration with
 * handler 0 terminates the process with status 128 + delivery_value (or
 * 128 + the event class without one); handler 1 ignores the event. */
#define VEX_EVENT_USER_INTERRUPT     1u  /* interactive interrupt request */
#define VEX_EVENT_SYNCHRONOUS_FAULT  4u  /* CPU exception in user code */
#define VEX_EVENT_TERMINATE          5u  /* polite termination request */
#define VEX_EVENT_HANGUP             6u  /* controlling session ended */
#define VEX_EVENT_QUIT               7u  /* interactive quit request */
#define VEX_EVENT_USER_1             8u  /* application-defined */
#define VEX_EVENT_USER_2             9u  /* application-defined */
#define VEX_EVENT_ALARM             10u  /* timer expiry */
#define VEX_EVENT_BROKEN_PIPE       11u  /* write to a closed pipe or socket */
#define VEX_EVENT_CHILD             12u  /* a child changed state */
#define VEX_EVENT_WINDOW_CHANGE     13u  /* terminal size changed */
#define VEX_EVENT_CONTINUE          14u  /* resume request */
#define VEX_EVENT_SLOT_COUNT        16u

#define VEX_EVENT_IGNORE_HANDLER     1u
#define VEX_EVENT_SIGINFO            4u  /* registration flag: 3-argument handler */
#define VEX_EVENT_ON_ALT_STACK       8u  /* registration flag: run on the thread's alternate stack */

/* VEX_SYS_EVENT_ALT_STACK: the calling thread's alternate event stack, used
 * by handlers registered with VEX_EVENT_ON_ALT_STACK (for instance to handle
 * a stack overflow). `size` 0 disables it. The previous setting is returned
 * in old_base/old_size; old_active tells whether the thread is running on
 * it now, in which case it cannot be changed (-EPERM). */
#define VEX_SYS_EVENT_ALT_STACK    212ull

/* VEX_SYS_EVENT_LEAVE: the running handler will not return (it left with a
 * non-local jump). The thread stops being "in a handler", so later events
 * are delivered again; its event mask is left as the caller sets it. */
#define VEX_SYS_EVENT_LEAVE        213ull
#define VEX_EVENT_ALT_STACK_MIN   2048u

typedef struct vex_event_alt_stack_req {
    uint64_t base;
    uint64_t size;
    uint32_t set;           /* 0: only query */
    uint32_t old_active;
    uint64_t old_base;
    uint64_t old_size;
} vex_event_alt_stack_req_t;

#include <stdint.h>

/* A VEX_EVENT_SYNCHRONOUS_FAULT handler is entered as
 *   handler(delivery_value, const vex_event_fault_payload_t *,
 *           vex_event_user_context_t *)
 * and may edit the context: VEX_SYS_EVENT_RETURN resumes from it. */
typedef struct vex_event_fault_payload {
    uint32_t version;
    uint32_t size;
    uint64_t vector;        /* x86 exception vector */
    uint64_t error_code;
    uint64_t address;
    uint64_t instruction;
    uint64_t access;        /* bit 0 read, bit 1 write, bit 2 instruction fetch */
    uint64_t address_valid;
} vex_event_fault_payload_t;

typedef struct vex_event_user_context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
    uint64_t vector, error_code, fault_address;
} vex_event_user_context_t;

#endif
