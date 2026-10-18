# Windows continuation: native execution control

This batch continues the 36-import Chrome frontier. Chrome entry remains the
end-to-end target; an export binding is not a complete Windows implementation.

## Shared mechanism

`VEX_SYS_THREAD_CONTROL` (231, tagged native domain) controls a TID only inside
the caller's resource-owning Process. TIDs do not grant foreign-process access.
It uses existing FlowSched metadata exclusion, execution claims, reschedule
IPIs, task leases, kernel stacks, wait/wake and FPU save/restore.

* PAUSE increments a count and requests a remote reschedule. The caller may
  supply a maximum count; the Windows limit 127 is userspace policy.
* The actual park happens at a ring-3 interrupt or final syscall return,
  after kernel transactions and event delivery. Kernel lock holders are not
  forcibly frozen. The context uses the existing native event register layout.
* SNAPSHOT waits through scheduler registration, not polling. The saved GPRs
  become readable with FXSAVE state only after the old CPU releases its claim.
  Initial XSAVE components are normalized to their architectural initial state.
* RESUME decrements one count. Only the last resume releases a parked context.
  Ordinary input/IPC/timer wakeups cannot accidentally release that park.
* Task exit, resume and park notify snapshot waiters. Stopped waiters lose both
  their registry lease and wait registration after CPU quiescence.
* CYCLES observes per-thread accumulated **TSC deltas while executing**, on each
  CPU independently. It never multiplies coarse scheduler ticks by a frequency.
  Remote observations include charges through that CPU's latest accounting
  boundary; this is not a hardware retired-instruction/performance counter.
* YIELD reports an actual switch to another non-idle context, rather than an
  unconditional true result.

Native THREAD_START gains a start-paused flag. Its return register frame is
now kernel-owned, like the existing fork frame. User-stack unmap/protect cannot
turn bootstrap register pops into kernel accesses through a user mapping.
The caller-owned stack and clear-on-exit word retain their existing lifetime.

## Windows projection

`SuspendThread`, counted `ResumeThread`, `GetThreadContext`,
`QueryThreadCycleTime` and `SwitchToThread` use this mechanism. CREATE_SUSPENDED
no longer relies on a one-shot userspace boolean/futex before the entry point.

Thread handles remain process-local, generation checked and refcounted.
Control/wait/query/close do not acquire the loader mutex: the suspended target
could own it. Handle readers use a nonblocking read-side reference protocol;
dead unpublished records are reclaimed only outside active readers. Collection
tries the loader gate without waiting, and normal creation/exit also collect.

Real limits, not success thunks:

* Control is same-Process only. Existing child-primary handles retain their
  initial launch-resume contract; other foreign context/control is unsupported.
* GetThreadContext supports AMD64 control/integer/legacy floating registers.
  Segment-register capture, debug registers, extended AVX/XSTATE, context
  mutation and self context snapshots are not implemented by this call.
* A context still blocked inside an unfinished syscall is explicitly refused
  (`ENOSYS` / ERROR_NOT_SUPPORTED), not fabricated from kernel registers. The
  pause request remains pending and can be cancelled by ResumeThread.
* Cycle queries for retained Windows handles whose native thread already
  exited are explicitly unsupported; no nominal-frequency estimate is returned.
* Priority, boost policy, APC/alertable waits and jobs are not made successful
  by setting metadata. Those contracts remain on the missing-import frontier.

## Completion race found by the SMP regression

`windows-thread-batch-4-cpu-20261016-045620.log` records a real failure:
`overlap: already complete cancel FAIL`. In `files.c::io_worker`, an IOCP packet
was visible before the job left the cancellation/reuse index. Another CPU could
dequeue the completion and then successfully cancel the completed request, or
have immediate reuse of its OVERLAPPED rejected as a duplicate.

Jobs now distinguish **completed** from **retired**. Under the existing file
gate, completion becomes final before status/event/IOCP publication. Cancel and
duplicate detection ignore completed jobs; their resource leases stay alive
until worker cleanup, including shutdown cancellation. The file's signaled
state is computed before publication from remaining unfinished jobs and is not
overwritten by an older job after a new submission. The fixture performs 128
immediate reuse/cancel cycles of the same OVERLAPPED without sleeps.

## Validation protocol

* `windows-thread-control.exe`: 32 creation/exit cycles, initial suspension,
  nested counts, exact R12/R13/XMM6 values from a CPU-spinning thread, no
  progress while stopped, resume, cycle monotonicity and invalid/stale handles.
* `native_thread_control_test`: malformed/readonly/kernel requests, foreign
  TID refusal, caller-selected count limits, 32 kills of parked contexts, and
  resume after removing the caller-owned user stack (stackless test entry).
* The same positive thread fixture passes on real Windows. Invalid/stale-handle
  calls are excluded from that host reference because the host raises invalid
  handle exceptions; those negative tests remain enabled in the KyIlOS fixture.
* Host runtime suite: 21 PE checks, 80,000 NLS and 80,000 UTF8/UTF16 comparisons,
  IOCP, overlapped IO including the 128-cycle regression, and thread control.
  PE graph unit tests: 5 pass.

## Final executed evidence (2026-10-16)

* `build/windows/windows-thread-final-4-cpu-20261016-050358.log`: complete
  Windows/native/Linux runner PASS, including the corrected overlapped test,
  both new thread fixtures, native GS, libc threads, Linux TLS/FPU, W^X, fault
  events and memory ownership. Compiled export census PASS. Native shutdown.
* `build/windows/windows-thread-final-1-cpu-20261016-050635.log`: the same final
  image, complete runner and compiled census PASS, followed by native shutdown.
* Both runs execute the unchanged real Chrome PE and confirm **36 -> 32 absent
  static imports in chrome_elf.dll** (`count=0x20`). Chrome is still refused
  before its entry point. The exact original 94-import matrix is updated from
  the final guest census; the four new bindings have the partial limits above.
* `build/windows/thread-control-host-tests.log`: host reference suite PASS.
  The earlier host invocation without the invalid-handle exclusion exited
  `0xc0000008`; this is not represented as a passing negative host test.
* `git diff --check` and new-source whitespace checks pass. Existing unrelated
  line-ending warnings remain. No fresh physical Dell run is claimed.

Final ISO: `build/kyilos-windows-process.iso`
SHA-256: `220AF7C00A782CF0AA1F96270EB7DEE846AC2C9BFA076E6B3AE939A12226058F`.
Kernel: `2FA927747BCA2E2E8C237F8B3ABA228A75C3C63EF4A1390EDB19178F16FC388B`.
Runtime: `AE87879F71C23DBFCAABC0D41700D7725659EE449855381F57BDB55B8B1D44C1`.
Runtime and kernel staging hashes were checked against the rebuilt artifacts.

## Changed source areas

* Native ABI: `vex_thread_control_abi.h`, `vex_thread_abi.h`, `vex_abi.h`.
* Scheduler/entry: `scheduler.h`, `scheduler.c`, `thread_control.inc`,
  `irq.c`, `syscall.c`, `vex_syscall.c`.
* Userspace: `windows/environment.c`, `native.c`, `runtime.h`, `files.c`.
* Fixtures: `thread_control.c/.S`, `native_thread_control.c`,
  `overlapped_smoke.c`, `runner.c`, `kernel32.def`.
* Build/validation: `build-windows-runtime.ps1`,
  `test-windows-runtime-host.ps1`, `test-windows-vm.ps1`, generated provider
  DLLs/export census and the exact frontier documents.

Next high-leverage dependencies remain memory commitment/residency facts,
per-file-description locking and pipe observation/transactions, console and
diagnostics/NLS, followed by priority/APC/job policies with real native effects.
No always-success or always-unsupported export was added to erase that frontier.
