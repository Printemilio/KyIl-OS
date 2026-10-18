# Windows rendezvous / regular-file async continuation

## Implemented, not facade exports

- Generic native capability rendezvous, syscall 227, scoped by an actual root FD.
  Independent object/root identities, attenuated local endpoint views, registration
  refs, reservation commit/abort generations, scheduler waits and stopped-waiter
  cleanup. No Windows names/types are interpreted by the kernel.
- Userspace synchronous local Windows named pipes: independent PE server/client,
  ASCII name folding, direction checks, real capability handoff, ordered native
  messages, retained partial reads (ERROR_MORE_DATA), duplex reply, reconnect,
  exclusive first instance, close wake and stale handle refusal.
- Regular-file OVERLAPPED I/O: one lazy native worker, positional PREAD/PWRITE,
  retained file/event/IOCP references, FIFO jobs, actual completions/errors,
  hEvent low-bit notification suppression, GetOverlappedResult and CancelIoEx.
  Worker termination/ACK precedes retirement of any PE thread's buffer/stack.
- Native message SEND/RECV snapshots through blocking and transactional copy-out
  with imported-reference rollback when the output was concurrently unmapped.

Two races were addressed without changing public architecture: DisconnectNamedPipe
keeps its borrower exclusion through teardown/rearm; stale native claim receipts
cannot commit or cancel a newer reservation after REARM.

## Tests and raw evidence

Host: `build/windows/rendezvous-host-validation.log`: parser 21 checks; NLS
80,000 comparisons against actual Windows (only basic-Latin supported subset);
posted IOCP reference; regular-file OVERLAPPED reference, 65536 nonconstant bytes,
16 writes + 16 reads, wide keys, event signaling and EOF errors. All pass. The
SysV/Microsoft ABI assembly test also passes when rerun in authorized WSL; its
initial sandbox WSL E_ACCESSDENIED is not a guest software failure.

The independent Windows run refused a kernel buffer immediately with error 998,
without a completion packet. KyIlOS's worker refuses it with an error completion.
The test now distinguishes immediate rejection from accepted pending work and
verifies the corresponding packet/no-packet contract. It does not require the
same scheduling timing. Earlier failed host test output is retained.

The VM runner includes the native/Linux GS, TLS/FPU, W^X, synchronous faults,
memory ownership, libc pthread, console, CFG/TLS/header refusals, DLL callbacks,
16,000 SRW and 32,000 mutex/critical increments, IOCP concurrent consumers and
teardown, process-exit races, actual regular-file async I/O and independent PE
named-pipe server/client. The targeted native rendezvous fixture covers scopes,
rights, commit/abort, stale receipts across REARM, creator process death, stopped
waiters, backing lifetime and blocked RECV vs unmap with attachment reclamation.

Final ISO: `build/kyilos-windows-rendezvous.iso`; same image, BIOS VM, 4096 MiB,
1 and 4 vCPU, disposable VexFS data disk. Final runner PASS in both configurations:
`build/windows/rendezvous-current-1cpu.log` and
`build/windows/rendezvous-final-4cpu.log`. No unexpected panic observed. The
separate native root test also passes: identity, attenuated rights, symlink and
dotdot refusal, pinned anchor, irreversible seal, SMP readers, fork, producer
death during IPC and file lifetime. Suchu Terminal runs the commands in both.

TLS fixture reports CPU mask 0x1 / 0 migrations in the final 1-vCPU run, and
mask 0xf / 37 migrations in the final 4-vCPU run. Both libc pthread regressions
report 187 checks / 0 failures. Each rendezvous fixture retires eight waiting
threads and their stacks, then rearms; its creator-death and receive/unmap tests
verify actual channel bytes and final-reference EOF rather than only callbacks.
The generation test verifies that a stale claim's COMMIT fails with ECANCELED
and its close cannot reopen the currently claimed offer.

Clean final Windows host run: `build/windows/rendezvous-host-final.log`.
The separate authorized WSL guard ABI test reports PASS.

SHA-256:

```
ISO      D23E4CDD2CDAC70A5F31106A1CDDFEB3DEBD7A4DDA41AE534D853E24F0A57AF9
kernel   4157DE47329579F74060403C82AD856452B3B97C3B505D8703CB2497C75B6F9B
runtime  5EDF9FB450741DB2F97F59414AF89D376D8913864ADB68866172FAAF4B3131C9
```

The dedicated test VM was shut down through the native shutdown command and
left configured for four CPU. Human main VM and existing Dell USB/Suchu images
were not replaced.

An earlier added scope test failed because it reused QUERY's output `limit` in
the next request; the kernel's EINVAL was correct. The input reset was fixed,
with the refusal preserved. `rendezvous-final-1cpu.log` records that failure;
`rendezvous-final2-1cpu.log` records the corrected pre-generation run.

## Exact frontier, not Chrome success

The actual unmodified Chrome EXE and chrome_elf.dll now reach
`windows: missing import KERNEL32.dll!CreateProcessW`. No Chrome EXE entry,
window, child, network operation or navigation is claimed. Chrome import binding
alone does not establish the call arguments of its later named-pipe operations.

CreateProcessW remains absent. The current native launch API consumes an ambient
path, while Windows has an irreversible sealed prefix. Completing child launch
needs executable-resource authority, explicit child capability/resource grants,
argv/environment and a userspace bootstrap success/failure boundary. That must
not become a wait for all DLL initialization: actual CreateProcess returns before
that initialization finishes, and a later missing/refusing DLL can terminate the
child. See [Microsoft CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw).
The existing
prepared-process/publication lifecycle should be reused; no seal reset, global
path exception, guessed PID or fake successful CreateProcessW is permissible.

## Limits

OVERLAPPED named pipes, pipe IOCP association, Windows security descriptors/remote
pipes, pipe flush/drain, arbitrary Unicode pipe names and payloads above 65524
are not implemented. Active simultaneous disconnect is explicitly refused.
Synchronous regular file handles with OVERLAPPED are also refused. Actual pending
CancelIoEx races, forced allocation failure and full Win32/NT I/O conformance are
not exhaustively tested. One regular-file worker is not a general asynchronous
device/file API. Userspace bad-pointer exception fidelity is still partial.

The native renderer, JIT, Suchu sources, ORA model and D630 timer/CPU fallbacks
were not redesigned. VM execution is not a fresh physical Dell validation.
