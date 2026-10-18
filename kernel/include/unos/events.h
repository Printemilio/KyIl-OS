#ifndef UNOS_EVENTS_H
#define UNOS_EVENTS_H

#include <stdint.h>
struct kernel_guard;
struct kernel_guard vex_event_state_begin(void);
void vex_event_state_end(struct kernel_guard *guard);
int vex_event_interrupt_pending(void);

/* Native VexOS event classes.  These identifiers are deliberately local to
 * VexOS; Linux signal numbers and sigaction layouts stay in compat_linux.c. */
#include <unos/vex_event_abi.h>
#define VEX_EVENT_CONTEXT_NONE   0u
#define VEX_EVENT_CONTEXT_ASYNC  1u
#define VEX_EVENT_CONTEXT_SYNC   2u
#define VEX_EVENT_CONTEXT_SYNC_COMPAT 3u
#define VEX_EVENT_CONTEXT_ASYNC_COMPAT 4u

struct interrupt_frame;
struct process;

typedef struct vex_event_registration {
    uint64_t handler;
    uint64_t restorer;
    uint64_t flags;
    uint64_t delivery_value;
} vex_event_registration_t;

/* Hardware facts captured at the point of a ring-3 CPU exception.  This is a
 * Vex event payload; Linux signal numbering and siginfo layouts live in VSL. */
typedef struct vex_sync_fault {
    uint64_t vector;
    uint64_t error_code;
    uint64_t address;
    uint64_t instruction;
    uint64_t access; /* bit 0 read, bit 1 write, bit 2 instruction fetch */
    uint8_t address_valid;
    uint8_t reserved[7];
} vex_sync_fault_t;

/* vex_event_fault_payload_t and vex_event_user_context_t are part of the
 * userspace ABI: see unos/vex_event_abi.h. */

int vex_event_validate_user_code(struct process *process, uint64_t address);

int vex_event_register(uint16_t event_id, const vex_event_registration_t *registration);
int vex_event_query(uint16_t event_id, vex_event_registration_t *registration);
int vex_event_set_mask(uint32_t how, uint64_t set, uint64_t *old_mask);
int vex_event_post(uint32_t process_id, uint32_t thread_id, uint16_t event_id);
/* VEX_SYS_EVENT_ALT_STACK: 0 or a negated errno. */
int vex_event_set_alt_stack(vex_event_alt_stack_req_t *req);
/* VEX_SYS_EVENT_LEAVE: 0 or a negated errno. */
int vex_event_leave(void);

/* Called only at the syscall return boundary, after the current frame is
 * fully recorded.  It may rewrite that frame to enter a validated userspace
 * handler. */
void vex_event_deliver_pending(void);

/* Deliver a synchronous CPU exception immediately from the ring-3 exception
 * path.  The common event engine owns context lifetime, stack validation,
 * handler entry and return.  The personality adapter supplies ABI-specific
 * siginfo/ucontext bytes and handler disposition. */
int vex_event_enter_sync(struct interrupt_frame *frame,
                         const vex_sync_fault_t *fault,
                         uint16_t event_id,
                         const vex_event_registration_t *registration,
                         const void *siginfo, uint32_t siginfo_size,
                         const void *user_context, uint32_t context_size,
                         uint32_t context_kind);
int vex_event_deliver_sync_native(struct interrupt_frame *frame,
                                  const vex_sync_fault_t *fault);

/* Restore the interrupted frame from the native event context. */
int vex_event_sigreturn(void);
int vex_event_sigreturn_context(const vex_event_user_context_t *context);
uint64_t vex_event_user_context_address(void);
uint32_t vex_event_context_kind(void);

/* Revoke pending/active state when a thread is reaped. */
struct process;
void vex_event_thread_exit(struct process *process);

#endif
