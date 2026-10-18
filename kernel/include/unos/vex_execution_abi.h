#ifndef VEX_EXECUTION_ABI_H
#define VEX_EXECUTION_ABI_H
#include <stdint.h>
/* Process-local admission to a userspace work stream. No payloads, callbacks,
 * I/O policy or personality structures are interpreted by the kernel.
 * A member consumes a slot while runnable, not while scheduler-blocked.
 * Resuming an admitted member may temporarily exceed capacity; new admission
 * waits until occupancy falls below capacity. Preemption/yield keep the slot.
 * Membership is thread-local, never inherited by fork/exec or new threads.
 * Tokens are opaque and checked against process ownership on every call. */
#define VEX_SYS_EXECUTION_GATE 226ull
#define VEX_EXECUTION_CREATE 1u
#define VEX_EXECUTION_ENTER 2u
#define VEX_EXECUTION_LEAVE 3u
#define VEX_EXECUTION_CLOSE 4u
#define VEX_EXECUTION_QUERY 5u
#define VEX_EXECUTION_WAITERS 6u
#define VEX_EXECUTION_CAPACITY 7u
#define VEX_EXECUTION_RESUME 8u
typedef struct {
    uint32_t size,operation,capacity,reserved;
    uint64_t capability,timeout_ns;
} vex_execution_req_t;
/* CREATE: capacity 0 = detected CPU count; returns token. ENTER: returns 0
 * or -errno; timeout 0 = infinite. LEAVE: capability 0 detaches current member.
 * CLOSE: revoke owned token, wake pending ENTER with -ECANCELED.
 * QUERY: returns runnable_members; CAPACITY returns configured capacity.
 * Results remain nonnegative for every uint32 capacity. Other unused fields 0. */
/* WAITERS: current number of pending ENTER registrations, for diagnostics. */
/* RESUME: restore consumer activity after a work wait returned without new
 * work (e.g. timeout). Like scheduler reactivation it may exceed capacity.
 * The gate is admission accounting, not a security quota or CPU entitlement. */
#endif
