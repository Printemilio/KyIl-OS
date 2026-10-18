# Native capability rendezvous

This is an opaque, root-capability-scoped discovery service, not a Windows pipe
namespace or a Unix socket namespace. Native syscall 227 is in the tagged native
domain. Its versioned-by-size request is `vex_rendezvous_req_t` (120 bytes on
x86-64); labels are binary (1..512 bytes), traits binary (0..64 bytes).

## Authority and objects

The scope is a process-local **typed filesystem root FD**. Its object identity
defines the scope; an identity number, path, PID or label alone grants nothing.
Publishing requires WRITE|CREATE on that root; discovery requires READ|ENUM.
The service retains the root and backing independently of the creator process.
Only native message endpoints are currently publishable. The source must permit
TRANSFER. The delegated SEND/RECEIVE/TRANSFER rights are subsets, never enlarged.

PUBLISH returns a CLOEXEC registration FD. Its last close removes the offer,
including process exit, while an already acquired endpoint remains usable.
Registrations with the same label must agree on traits, rights and instance
limit (1..4096). EXCLUSIVE publication refuses an existing group.

CLAIM compares the expected traits again and returns a **new local endpoint FD**;
it does not transfer a descriptor number. It reserves one available offer.
Closing an uncommitted claim rolls the reservation back. Endpoint views retain
the reservation owner's object; closing its original FD is not premature abort.
COMMIT accepts the original claim object (including ordinary DUPs of it), not an
independent ATTENUATE view. Uncommitted claims/views cannot be delegated or
published. This prevents recursive reservation ownership cycles.

REARM on the registration makes its next offer available. This is caller policy:
the native service does not interpret connection protocols. Each reservation
has a generation: REARM invalidates earlier receipts, their COMMIT fails, and
their eventual close cannot undo a newer reservation. No public ABI change.
QUERY returns 1
(available) or 2 (all claimed), and copies actual traits/limit. Clear returned
fields that are not inputs when reusing a request for another operation.

WAIT registers with the existing scheduler and blocks; it does not poll.
Timeout zero is unbounded, finite waits use the scheduler timer granularity.
Availability check, registration and wakeup are serialized. After CPU quiescence,
stopped-thread cleanup unlinks wait nodes before their kernel stacks are freed.
Root/resource references spanning sleeps use tracked FD borrows.

CANCEL_ENDPOINT closes one endpoint view's operations and wakes its blocked
operations; independent views retain their backing. Closing an ordinary view
only closes its channel side when the last view is gone. LEASE_STATE returns
the actual reservation state. There is no remote root discovery or global name
authority.

## Message boundary and copy-out

Native message SEND snapshots userspace bytes and attachments before blocking.
RECV uses kernel scratch through its wait, then commits payload, attachment
handles and request metadata under one process-memory transaction. Copy-out
failure closes imported references. A message already consumed by RECV may be
lost when its destination was concurrently unmapped; it never writes freed user
memory or exposes a leaked handle. The dispatcher does not do a second RECV
copy-out after that commit.

## Windows translation

`userspace/windows/pipes.c` interprets `\\.\pipe\name`, instance negotiation,
message/byte read modes and Windows errors. Kernel labels/traits contain no
Windows structures or policy. Windows uses the per-user C: root already bound
by the runtime. Independent PE processes with that root can discover each other.

A SEND-only bootstrap claim transfers an attenuated data endpoint, then commits.
The real native channel provides ordered message boundaries, buffering,
backpressure, blocking and peer closure. Partial message reads retain remaining
bytes and return ERROR_MORE_DATA. Client/server direction permissions are
enforced again by native endpoint rights. Disconnect excludes active borrowers
and keeps the handle transaction intact through teardown/rearm. Concurrent active
disconnect is explicitly unsupported, rather than mutating state under a reader.

Current Windows coverage is synchronous **local** named pipes with ASCII folded
names, inbound/outbound/duplex and reconnect. OVERLAPPED named pipes, security
descriptors, remote names, FlushFileBuffers pipe drain semantics and writes above
65524 payload bytes are not implemented. They must not be inferred from the
independent regular-file OVERLAPPED implementation. No full Win32 named-pipe
conformance is claimed.
