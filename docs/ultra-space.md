# Ultra-Space: native ephemeral userspace services

Ultra-Space is a native process projection, not a VM or a kernel personality.
The implementation reuses the executable-resource launch transaction,
process-group/address-space ownership, FlowSched, ORA executable views,
capability FDs, message IPC and scheduler wait/completion paths.

## Authority and discovery

`VEX_PROCESS_ULTRA_OPEN` in the structured process-resource ABI takes an
executable capability and a root-scope capability. Discovery is keyed by the
root object's identity, exact executable bytes and exact explicit grants
(objects, child-local slots and flags). It is not keyed by a pathname or a
caller-supplied service ID. Attenuated views of the same root discover the
same service, without inheriting one another's rights. An explicitly delegated
scope can be used by another authenticated subject; the service checks that
subject on each request, rather than treating the first client's UID as policy.

The initial FD at child slot 8 receives service requests. Additional resources
must be explicitly granted into slots 16 and above. The scope is used for
discovery/admission, and is not implicitly installed in the service. There is
no inherited console, ambient path access, GUI, network-management authority,
legacy syscall entry or nested process launch. The process is sealed, with zero
ambient capability bits, before publication. The first implementation exposes a
bounded native service projection; it is not a claim that every native API is
available inside an arbitrary Ultra-Space.

The returned ingress capability has SEND|TRANSFER rights, without RECEIVE or
REVOKE. The kernel records the admitting root view's rights on that capability.
`VEX_SYS_MESSAGE_RECV_AUTH` obtains the exact sender PID, TID, native UID and
admission rights captured on the message at SEND time. These are not fields
trusted from the payload, a PID lookup after delivery, or the endpoint creator.

## Lifetime and concurrency

Discovery is a weak index. Live external leases retain the domain; the index
does not retain a service forever. Duplication, queued IPC attachments and
explicit launch grants preserve leases through the FD object/origin chain.
Message endpoints exported by the service receive their own domain lease:
holding a session without its discovery ingress still retains the service.
Internal service endpoints do not receive such leases, avoiding an ownership
cycle. Admission endpoints reject lifetime-bearing attachments, so a client
cannot strand the last lease inside a service's own queue. Ordinary IPC can
still delegate these capabilities.

The last lease removes the domain from discovery, requests process termination
and releases kernel references. Reuse and last-release are serialized by the
private Ultra-Space gate; this is not a global kernel execution lock.
`ULTRA_QUERY` reports an observational identity/PID/lease snapshot.
`ULTRA_OBSERVE` returns only QUERY|WAIT process authority, not a service lease,
so callers can observe actual teardown without themselves preventing it.
The `clients` field counts lease objects, not distinct users or processes.

The executable content view, delegated objects, root and process completion
backing have independent retained lifetimes. Failed construction uses the
existing launch rollback and closes partially installed FDs. No kernel memory
or physical address is exposed by these interfaces.

## Windows pipe service

`userspace/windows/pipe_service.c` is a separately linked native executable,
installed as `/usr/lib/kyil-windows/pipe-service`. Windows applications remain
direct PE programs. `pipes.c` translates their API calls to this service; the
application has no Ultra-Space-specific ABI requirement.

The service owns its private name/instance/security tables and the full-rights
ends of native message pairs. It publishes no rendezvous CLAIM name. Thus a
direct native rendezvous call cannot acquire a protected endpoint behind its
policy. Control sessions are capabilities, not global IDs. Returned data/control
views are attenuated and cannot REVOKE or enlarge rights. Explicit disconnect
revokes the connected native pair; normal close instead preserves queued data
until it is consumed and then delivers EOF. Both paths wake real waiters.

Descriptors and SECURITY_ATTRIBUTES are snapshotted by the Windows runtime.
The service validates self-relative SD/ACL/SID bounds and evaluates ordered
allow/deny ACEs against the authenticated native subject. Supported subjects
are native-user SIDs, Everyone and authenticated users; an ordinary native
subject is not silently made NT SYSTEM or an AppContainer. Empty DACL denies;
null/absent DACL permits; a missing explicit descriptor uses a same-native-user
default. The Crashpad untrusted mandatory-label ACE with zero policy mask is
represented as a genuinely nonrestrictive label. Other integrity policy,
impersonation/restricted tokens, conditional/object ACEs and audits are explicit
unsupported errors, not ignored authorization requirements.

An admitting scope must contain WRITE|CREATE to create pipe instances. READ-only
admission cannot borrow that authority from another client of a cached service.
Opening data requires the descriptor's requested rights, then a matching
attenuated native endpoint. Server handles can be inherited through native
explicit grants into a directly executed child PE. The child gets new local
FDs, not the parent's FD numbers. Independent close preserves other holders.

Service waits use native scheduler-backed poll and finite deadlines. There is
no periodic sleep or active poll loop. The current 128-entry poll array covers
the process's existing 128-FD limit; resource exhaustion is returned, not an
unmonitored 129th FD. OOM paths release partially created sessions and objects.

## Deliberate current limits

KSW's `resource-service` additionally orchestrates readonly protected opens
using explicitly delegated READ|TRANSFER filesystem roots. Native FS namespace
protection, not the service or a Windows lease table, enforces deletion/rename
denial. Returned file capabilities retain this protection independently of the
service's lifetime. See `windows-native-file-protection-2026-10-18.md`.

- ASCII local pipe names; no remote pipe transport.
- Synchronous byte/message I/O; named-pipe OVERLAPPED is refused explicitly.
- One write is bounded by the native message payload (65524 data bytes).
- Duplication/inheritance of a handle with active I/O, a pending connection or
  an already partially consumed message is refused, rather than copying a
  changing buffer or duplicating already-consumed bytes.
- This is not a complete NT token, impersonation or security-auditing system.
- No forced allocator-failure campaign or real cross-UID process fixture has
  yet validated every OOM/inter-user path; source checks are not those tests.

## Regression fixtures

`ultra_space` and `ultra_service` test eight complete domain lifecycles,
authenticated sender metadata against forged payload fields, confinement,
attenuation, reuse, four concurrent opening threads, queued IPC delegation,
stale handles, self-cycle refusal and exported-endpoint-only lifetime. Observer
WAIT checks actual process completion. Each run includes 520 echo RPCs plus
eight exported-endpoint probes. Per-cycle creation, 32-RPC and final teardown
durations are recorded in nanoseconds.

`windows-protected_pipe-smoke.exe` uses the actual Crashpad descriptor syntax
and CURRENT_USER merge, tests native CLAIM bypass attempts on held roots,
read-only admission, denied/empty/read-only DACLs, multiple instances, a genuine
inherited child PE with 32 bidirectional exchanges, independent close, final
message draining, explicit disconnect and rights denial. The pure host policy
fixture also exercises malformed/truncated descriptors and ordered ACEs.

See `windows-ultra-validation.md` for execution evidence and the Chrome frontier.
