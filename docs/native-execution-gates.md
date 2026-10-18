# Native execution gates (development ABI 226)

An execution gate controls admission to a process-local userspace work stream.
It contains no Windows packets, file operations, callbacks or interpreter.
The creator owns an opaque random token; every operation checks the calling
process_group identity. It is shared by that group's threads, not by foreign
processes. Capacity is immutable; zero selects the detected logical CPU count.

`vex_execution_req_t` is 32 bytes. CREATE returns a positive token. ENTER binds
the calling thread (one membership at a time) and waits for admission. LEAVE
detaches it. CLOSE revokes the token and wakes pending admissions with
ECANCELED. QUERY, WAITERS and CAPACITY report separate nonnegative counts.
Timeouts are relative nanoseconds; zero means infinite. Invalid/stale/foreign
tokens fail with EBADF. Invalid fields fail with EINVAL. Allocation failure
is ENOMEM. The tagged native dispatch copies/validates the request first.

An admitted READY/RUNNING thread occupies one slot. Preemption, migration and
yield do not release it. WAITING/SLEEPING releases it, including waits initiated
by unrelated native services. Returning to READY restores an existing member;
that can temporarily exceed capacity. Further admissions wait until occupancy
falls below capacity. This is admission, not CPU entitlement: FlowSched group
fairness, placement, runqueues and CPU execution exclusivity are unchanged.
RESUME accounts a consumer returning from its work wait without new work
(such as a timeout); like a scheduler wake it may temporarily exceed capacity.
Before requesting another work item the runtime drops its previous admission
and enters again. This prevents an oversubscribed consumer from bypassing the
admission limit simply because it has a persistent stream association.

Pending admissions are LIFO. A selected slot is reserved before waking its
thread, preventing two CPUs from admitting against the same available slot.
The existing futex block helper performs the predicate/event/deadline and
WAITING/SLEEPING transition atomically under scheduler metadata protection.
There is no runtime polling, sleep loop or a second scheduling engine.

The object has one process-owned reference and one per member. ZOMBIE drops
membership and pending registrations synchronously with scheduler state;
exec detaches the calling thread. Closing does not free an object referenced
by another member. Last release retires it; allocation and destruction occur
outside FLOW_LOCK. Process storage teardown revokes remaining owned tokens.
Pending registrations are in kernel thread metadata, not pointers to a stack
that a killed thread might already have retired.

Code: `kernel/src/execution_gate.inc`, included by `scheduler.c`; request
validation in `vex_syscall.c`; public development record in
`kernel/include/unos/vex_execution_abi.h`. Ordinary tasks have a NULL membership
and take only that check on scheduler state transitions.

The primitive is process-local. Cross-process gate transfer, capacity changes
and arbitrary remote membership control are not advertised.
