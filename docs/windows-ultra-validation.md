# Ultra-Space / protected named pipes — validation 2026-10-17

## Result

The actual Chrome **155.0.8059.84** package now passes its protected,
inheritable Crashpad `CreateNamedPipeW` operation. Both final VM traces contain:

```
windows-entry: chrome.exe rip=0x000000014023cd10
windows-pipe: CREATE backend=Ultra-Space protected=1 inherit=1 success
```

This is a real separately executed userspace service returning a native
capability, not a stub, a public rendezvous bypass, or a converted Chrome ELF.
The Windows policy interpreter remains userspace. W^X is unchanged.
The native contract and honest limits are described in `ultra-space.md`.

Chrome does **not** yet display a GUI. The next observed failures, independently
of the now-successful pipe creation, are:

1. `critical_section_with_debug_info.cc:35`: `InitializeCriticalSectionEx`
   reports ERROR_NOT_SUPPORTED (50).
2. `crashpad_client_win.cc:430`: `OpenProcess` reports the same error, then
   Crashpad reports `crash server failed to launch, self-terminating` at line144.
3. The main trace also reaches a delayed load of `SHELL32.dll`, with library
   load error126 and unresolved `CommandLineToArgvW`. This is a separate missing
   DLL/API contract; the trace does not establish the precise requested
   `OpenProcess` rights or critical-section flags. They must be captured before
   extending those contracts. None of these failures was changed into success.

Other optional dynamic probes still return honest missing-export errors. The
compiled export census validates resolver registration, not complete semantics
or Windows conformance. The broader `chrome.dll` inventory is still unfinished;
zero missing root static imports is not a claim that all Chrome APIs exist.

## Final-image VM evidence

Same ISO, VirtualBox BIOS guest, complete runner followed by compiled export
census and actual Chrome execution:

| Configuration | Raw serial log | Outcome |
|---|---|---|
| 1 vCPU | `build/windows/ultra-final-1-cpu-20261017-211120.log` | runner PASS, census PASS, Chrome pipe succeeds; subsequent failures above |
| 4 vCPU | `build/windows/ultra-final-4-cpu-20261017-211314.log` | runner PASS, census PASS, same Chrome frontier |

Harness logs: `build/windows/ultra-final-vm1.log` and `ultra-final-vm4.log`.
The 4-vCPU thread-control fixture was slow enough to warrant a register/screen
capture (`ultra-full4-state.png`); it subsequently completed without a code
change. It is not counted as a deadlock or a corrected bug.

Each complete runner includes native executable/root/process/thread authority,
PE loading and refusal paths, threads/contexts, files, IOCP, registry/security,
process creation/inheritance, concurrent exit, Linux TLS/FPU, synchronous fault
return, W^X and memory ownership. Both also execute the new Ultra-Space and
protected-pipe fixtures, with the exact final markers:

```
ultra-space: ... exported endpoint lifetime / 520 authenticated calls PASS
windows-protected-pipe: ... inherited producer close / 32 duplex messages / disconnect revoke / lifetime PASS
windows-runner: native GS / libc pthread / console-rights / Linux TLS-FPU / W_X / faults / memory ownership PASS
windows-export-census: compiled resolver PASS
```

The first test runs eight domain creation/reuse/concurrency/delegation/teardown
cycles, including last-lease-only exported endpoints, no ambient console/path/
legacy/GUI access, no rights expansion and no self-lifetime attachment cycle.
The second tests actual descriptors and inherited pipe traffic after parent
server close, not just successful API return values. No kernel panic is present
in either final serial log. Both VMs shut down via the guest power path.

## Host/reference checks

- `build/windows/ultra-host-suite.log`: complete host script PASS, including
  malformed/ordered pipe ACLs (10000 deterministic malformed records), PE image
  parsing, 80000 NLS and 80000 UTF conversions, native Windows reference
  IOCP/overlapped/thread/heap/registry/security/process contracts and the WSL
  guard assembly ABI check.
- `build/windows/ultra-protected-host.log`: the protected-pipe fixture PASS on
  actual Windows (native KyIlOS bypass/admission probes are guest-only).
- `python tests/pe_graph_test.py`: six PASS.
- Runtime and kernel rebuilt; `git diff --check` passes (existing newline
  conversion warnings are not test failures).

No new physical Dell test, full Suchu application campaign, cross-native-UID
process fixture or exhaustive allocation fault injection is claimed.

## Measured overhead

Guest monotonic elapsed time, eight lifecycle samples per configuration, within
the complete runner. Creation measures executable launch/domain publication;
RPC is a serial 32-call batch including IPC transport and reply handling.
Teardown measures final lease close through actual process-observer completion,
not an independently timed global allocator reclamation sweep.

| Metric | 1 vCPU median (min–max) | 4 vCPU median (min–max) |
|---|---|---|
| Domain creation | 15.217 ms (7.112–19.595) | 8.924 ms (7.384–9.973) |
| Per RPC, batch/32 | 19.794 µs (16.241–429.189) | 21.442 µs (20.081–63.697) |
| Last lease → process completion | 41.321 µs (34.291–68.652) | 711.378 µs (367.601–910.814) |

Raw samples and summaries: `build/windows/ultra-final-bench.json`.
Reproduce with:

```
python scripts/analyse-ultra-space.py build/windows/ultra-final-1-cpu-20261017-211120.log build/windows/ultra-final-4-cpu-20261017-211314.log
```

These are elapsed VM measurements with scheduling/host variability; they do
not establish linear scaling or CPU-only service cost. In particular, 4-vCPU
completion observation is substantially slower here than 1-vCPU observation.

## Integration changes

- Native ABI/core: `vex_process_abi.h`, `vex_abi.h`, `ultra_space.inc`,
  `process_resource.c`, `scheduler.h/.c`, `syscall.c`, `vex_syscall.c`.
- FD/IPC/security metadata: `message_ipc.h/.c`, `vex_socket_compat.c`,
  `vex_posix_compat.c`, root-provider borrow-rights support, generic endpoint
  REVOKE operation in the rendezvous control interface.
- Userspace Windows: new `pipe_service.c`, `pipe_policy.h`, `pipe_protocol.h`;
  `pipes.c`, descriptor snapshot support in `security_data.c`, inheritance/
  duplication records in `process.h/.c` and `files.c`.
- Build/tests: standalone native service staging, native Ultra-Space fixtures,
  protected PE fixture, runner integration, host policy tests, VM failure
  detection and measurement summarizer.

## Image receipt

`build/kyilos-windows-process.iso`

SHA-256:
`F7C8396ACB935D9F9019D72F1BC6C2EE3E5A4748CF77E7AC2B547F712B2F0B15`

Kernel SHA-256:
`DEAEF6EC86C16580D2BB020D6CF980BDE34F52D94A2CB7EF1743742D90FB75F8`

Standalone service SHA-256 (same in `usr/lib` and ISO staging):
`9FCC0288D485C916943DB7550082E0CA267F82C62799E605E2A07DF2D9E316B0`.

The ISO was rebuilt only while the dedicated VM was powered off. Build receipt:
`build/windows/ultra-final-iso.log`. The service ELF was staged explicitly along
with provider DLLs, rather than being omitted by a DLL-only copy pattern.
