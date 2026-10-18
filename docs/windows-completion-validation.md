# Windows userspace completion and synchronization checkpoint

This continues the root-capability checkpoint; it does not claim Chrome can
start. Guest PE/DLL instructions still execute directly, with the runtime using
only the tagged native ABI. No guest conversion or kernel NT/Win32 engine.

## Real contracts

- CreateIoCompletionPort creates an unassociated, process-local port with
  INVALID_HANDLE_VALUE and no ExistingCompletionPort. Zero concurrency uses
  the real native CPU count. Local handles have type and generation checks.
- PostQueuedCompletionStatus stores real FIFO packets (DWORD bytes,
  pointer-sized key and opaque OVERLAPPED value). The pointer is not dereferenced.
- GetQueuedCompletionStatus consumes a packet or blocks on native futexes.
  Packet waiters are notified newest-first; admission uses the native execution
  gate. Timeout returns FALSE/258 and NULL OVERLAPPED; closing a pending port
  returns FALSE/735 and NULL OVERLAPPED. Bytes/key are left untouched when no
  packet was dequeued. Each thread has at most one persistent port association.
- Runnable-consumer capacity is real scheduler state, including unrelated
  waits, not a userspace boolean. A yield remains runnable. Reactivating an
  existing consumer can temporarily exceed capacity, without admitting another.
  Timeout resumes associated activity. Each subsequent dequeue relinquishes
  that previous activity before admission, including oversubscribed consumers.
- Port handle, persistent association and outstanding call references are
  distinct. Closing invalidates the handle and wakes calls; retained objects
  and queued packets are freed after their final reference.
- Unnamed CreateMutexW/ReleaseMutex/WaitForSingleObject support ownership,
  recursion, timeouts, wait/wake and WAIT_ABANDONED after owner thread exit.
- CRITICAL_SECTION has the 40-byte x64 layout, recursive ownership, try-enter,
  native blocking and release/acquire visibility. Its internal LockCount uses
  this runtime's encoding; it is not a claim to reproduce Windows' opaque
  implementation fields. SpinCount is stored as an optimization hint, not a
  promise of active spinning. Invalid release/deletion terminates the process.

IOCP association with real files is NOT implemented: current CreateFileW
does not support FILE_FLAG_OVERLAPPED. A valid file association returns
ERROR_NOT_SUPPORTED, invalid files/ports return ERROR_INVALID_HANDLE.
Named/security/inherited mutexes remain explicitly unsupported. No named-pipe,
socket overlapped-I/O, cancellation, GQCSEx, DuplicateHandle or full NT object
namespace is claimed.

Win32 contracts consulted: [I/O completion ports](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports),
[CreateIoCompletionPort](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-createiocompletionport)
and [GetQueuedCompletionStatus](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus).
These describe expected semantics, not proof of KyIlOS implementation coverage.

## Lifetime corrections

Object wait registrations carry the calling runtime thread. A stopped thread's
registrations are unlinked before its stack is unmapped. Abandoned mutex
ownership is then transferred or made available. Completion calls similarly
register their borrowed reference and packet waiter for cancellation by reaping.

ExitProcess quiesces the event/mutex and completion metadata gates before
requesting native peer stops, holds them through clear-on-exit acknowledgements,
then reaps records. This prevents killing a peer that owns those runtime gates.
It is not a general promise that arbitrary application locks are safe across
forced termination; Windows TerminateThread is not implemented.

The native futex block helper also now checks an already elapsed deadline
before subtracting, and uses overflow-safe ceiling division. A deadline crossing
the final pre-block check cannot become a huge unsigned duration.

## Validation protocol and source

Implementation files: `kernel/include/unos/vex_execution_abi.h`, `vex_abi.h`,
`vex_legacy_compat.h`, `scheduler.h`; `kernel/src/execution_gate.inc`,
`scheduler.c`, `vex_syscall.c`; `userspace/windows/completion.c`, `objects.c`,
`synchronization.c`, `environment.c`, `files.c`, `windows.h`, `win32.c`.
Fixture and runner sources are under `userspace/windows/tests`; build and host
reference scripts are `build-windows-runtime.ps1` and
`test-windows-runtime-host.ps1`. No GPU, Suchu source, ORA or hardware timer
calibration implementation was replaced.

`scripts/build-windows-runtime.ps1` builds genuine COFF/PE fixtures and the native
host. `scripts/build.ps1 -NoIso -SkipWebKit` rebuilt the whole kernel after
process_t changed; subsequent scheduler-only edits were compiled and relinked.
`scripts/make-portable-iso.ps1 -IsoName kyilos-windows-completion.iso -IsoOnly`
creates a separate BIOS/UEFI test ISO. Dell/Suchu images are preserved.

Tests in the native runner:

- windows-iocp-smoke: 512 ordered packets, zero timeout, association change,
  64-bit key/opaque-pointer retention, timeout-associated consumer activity and
  readmission blocked by another runnable consumer at concurrency one,
  eight consumers blocking on another event with concurrency one, close wake,
  stale handles, 128 cycles destroying a port with queued packets, invalid
  creation combinations.
- native_execution_test: yield retains admission, finite admission timeout,
  unrelated futex blocking releases capacity, reactivation produces occupancy
  two at capacity one, registered pending member killed remotely, pending
  admission closed/revoked, foreign-process and stale token refusal.
- windows-mutex-critical: recursive ownership and public struct layout,
  16,000 increments under recursive critical sections plus 16,000 under mutexes,
  abandoned owner, stale and unsupported named mutex.
- windows-iocp-exit: three independent processes terminate with status 23
  while eight completion callers are waiting; no dangling waiter/panic.
- Earlier PE/DLL/TLS/CFG/VEH, guarded stacks, root/file isolation, concurrent
  process exit, GS context, native console, libc threads (187 checks), Linux
  TLS/FPU, W^X, synchronous faults and memory ownership.

Host parser (21 checks), NLS differential against actual host Windows
(80,000 comparisons) and SysV-to-Microsoft guard register preservation passed
in `build/windows/completion-host-tests.log`. The WSL part first hit sandbox
E_ACCESSDENIED and passed on the authorized retry.

An additional execution of the PE IOCP fixture on host Windows ended with
STATUS_INVALID_HANDLE (0xc0000008) during its invalid-handle cases. It is NOT
counted as a passed Windows differential test, and its precise host exception
site was not established. The guest VM results below are separate evidence.
The positive-only variant (IOCP_HOST_REFERENCE), retaining the same FIFO,
wide-packet, timeout-activity, resumed-quota, blocking-consumer and close-wake
assertions, subsequently passed against actual host Windows. Deliberately
invalid handle operations are excluded only from that host variant, and remain
in the guest fixture. This independent comparison is now scripted in
`scripts/test-windows-runtime-host.ps1`.

## VM results and Chrome frontier

Initial and expanded 4-vCPU guest runs passed all tests above; raw logs are
`build/windows/completion-first-4cpu.log` and
`build/windows/completion-final-4cpu.log`. Root regressions also passed.
The actual unchanged installed Chrome 155.0.8059.39, with its chrome_elf.dll,
now stops at:

```
windows: userspace direct PE loader / native ABI
windows: missing import KERNEL32.dll!CreateNamedPipeW
windows: load failed: Windows API not implemented
```

This is an import-binding frontier, not an executed named-pipe call. No Chrome
entry point, window, process thread pool, networking or navigation has run.
Actual named-pipe arguments are therefore not yet observed. The preceding
frontiers were CreateIoCompletionPort and then CreateMutexW.

## Final same-image validation

ISO `build/kyilos-windows-completion.iso`, 4096 MiB, BIOS, dedicated
KyIlOS-Legacy-Test VM, disposable VexFS disk. Both 1 and 4 vCPU passed the
complete runner described above, including the added timeout-activity and
readmission checks, plus native_fs_roots_test.

Raw final logs: `build/windows/completion-current-1cpu.log` and
`build/windows/completion-current-4cpu.log`. Earlier logs refer to earlier
builds and are retained as separate evidence. No unexpected kernel panic was
observed. Deliberate RWX/TLS/header/CFG refusals remain expected test failures
inside their respective processes, verified by the runner.

The final TLS test reports mask 0x1 / zero migrations on 1 vCPU, and mask 0xf /
126 migrations on 4 vCPU. Each configuration passes libc-thread 187 checks with
zero failures, 32,000 mutex/critical protected increments, the IOCP FIFO and
128 queued teardown cycles, three status-23 completion-waiter process exits,
three status-17 existing concurrent process exits, and native root IPC transfer
with producer death. No claim of exhaustive OOM injection or IOCP conformance.

Existing Suchu Terminal ran commands in both configurations. On the final
4-vCPU image, Suchu Editor opened, displayed actual keyboard input
"Windows completion checkpoint", showed its dirty document state, and closed
using Discard. Captures: `build/windows/completion-suchu-editor-input-4cpu.png`
and `completion-suchu-editor-closed-4cpu.png`. This is a targeted desktop smoke,
not a browser/graphics benchmark. No physical Dell test was performed.

Final actual Chrome frontier on this image remains CreateNamedPipeW, before
any Chrome EXE entry point. Named pipes and OVERLAPPED I/O are remaining work;
import binding cannot supply evidence of their eventual call arguments.

SHA-256:

```
ISO      2D903F26F67A21FF71F2C44DA9761D4570B241F593F7D224059C15B90CC10B81
kernel   CD778A6C01803D58CB2E042306665C8273B3BCB122FEE63335C980B39DC17C2D
runtime  797A83892FEDBC07D21F5EAE3FC1E38C4F904E3E57F5F0636C3E5B76E7D3832A
```

The existing Dell USB image, Suchu desktop ISO and human main VM were not
replaced. The dedicated test VM is shut down cleanly and left at four vCPU.
