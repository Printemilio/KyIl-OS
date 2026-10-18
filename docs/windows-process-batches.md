# Windows compatibility: dependency graph and process milestone

This is an **ongoing** Windows/Chrome effort. Chrome's point of entry has not
been reached. This document separates implemented mechanisms from missing API
families; it is not a Windows conformance claim.

## Inventory and validation workflow

`scripts/audit-pe-graph.py` reads PE32/PE32+ without executing them. It follows
static imports, delay imports, export forwarders and the v6 API-set schema.
Ordinal/name imports, dependency roles, parse errors, missing-contract families
and dynamic resolver sites are preserved in JSON. Existing package PE exports
are distinguished from native adapter exports. Windows-host DLLs are reference
inputs only; they are neither deployed nor claimed to work on KyIlOS.

Actual compiled exports are queried using the native runtime's
`--export-census`. `--census <serial-log>` restricts the inventory to bindings
the VM actually resolved. A resolved export still has a partial contract;
presence does not prove every flag/operation. The earlier snapshot contained
364 modules / 100709 edges / 766 API sets and zero parse errors. Five independent
parser/graph tests pass (`tests/pe_graph_test.py`). Generated reports are private
artifacts under `build/windows/chrome-import-graph.{json,md}`.

`scripts/test-windows-vm.ps1` boots the dedicated VM, waits for the actual desktop,
starts Suchu Terminal, runs specified real programs, checks serial evidence and
shuts down through native shutdown. ISO changes are refused while running. It
does not treat compilation as execution or overwrite the human main VM.

## Generic native process resource

Tagged native syscall 228 (`vex_process_resource_req_t`) introduces executable-
resource launch and typed process-capability FDs. No NT/Win32 structures or
Windows pathname policy exist in this kernel interface.

An executable FD is obtained atomically under a root with READ|EXEC authority
using `VEX_FS_OPEN_EXECUTABLE`; existing file execute permissions remain checked.
Ordinary READ alone cannot launch it. ORA pins an immutable snapshot before the
existing ELF image preparation/mapping machinery consumes it. This first launch
backend supports tagged native ELF hosts without PT_INTERP/DT_NEEDED. It rejects
foreign image formats; the original PE is interpreted/executed directly by its
userspace runtime, never converted into ELF.

Caller argv/envp and resource grants are snapshotted. Grants use explicit child-
local FD slots, reject duplicates, validate TRANSFER, and retain the actual object.
The child is built non-runnable, receives its inventory and inherited seal, then
is published. OOM/validation failures never publish a stale PID. Suspension is
actual scheduler state, not a user sleep. Resume and termination use scheduler
reschedule machinery. Query/wait/terminate/resume/transfer rights can be attenuated.

Resource backing identity is separate from FD numbers and creator lifetime.
Scheduler observers retain execution metadata until quiescence, then cache both
primary-thread and process-group exit states and release their task pins. Keeping
an exit-status handle does not pin the dead process's CR3/pages. Native waiters
are removed before stopped kernel stacks; waits use scheduler wake/deadlines,
not polling. Legacy native child collection is preserved for non-resource launches.

## Userspace process contracts

Implemented paths include CreateProcessW, process/primary-thread handles,
GetProcessId/GetExitCodeProcess, WaitForSingleObject, TerminateProcess, suspended
creation/ResumeThread, GetStartupInfoW, handle flags and same-process
DUPLICATE_SAME_ACCESS, anonymous CreatePipe, repeated child creation and actual
stdin/stdout/stderr delegation. STARTUPINFOEX handle lists select real capabilities.
Windows handles copied to the child retain their Windows values while naming
new child-local native FDs. Default security descriptors and explicit resources
remain bounded by native authority; unsupported security descriptors/attributes
are refused. Local CreateRemoteThread delegates to actual CreateThread; foreign
process injection remains explicitly unsupported.

The native runtime image is captured before sealing. Children receive the existing
C:/A: authorities plus a readonly system-provider root, image FD and bounded
bootstrap endpoint. Windows command/environment/startup policy stays userspace.
The command line is preserved verbatim, Unicode environment is copied, directory
existence is checked and native ambient paths remain denied. Success indicates
creation, not completion of DLL initialization (matching the asynchronous nature
of CreateProcess); later loader failures terminate the child.

Limits still open: foreign DuplicateHandle/remote thread start, general Windows
tokens/security descriptors/job policies, named/event/section handle inheritance,
IOCP-associated file inheritance, most extended process attributes and creation
flags. The bootstrap currently bounds command/environment storage and inherited
files; full Unicode environment-size and Windows executable-search semantics are
not complete. Named-pipe OVERLAPPED and final DLL unload are also incomplete.
Unsupported combinations return errors, never fabricated PID/handles/success.

## Loader and shared contracts

Real AMD64 PE provider DLLs are built by `build-windows-provider-dlls.py`: executable
RIP-relative JMP-through-IAT exports import exact userspace native implementations
from the private `kyilwin-provider.dll` binding contract. These are real API
adapters, not zero-return stubs. Source candidates are checked by the compiled
VM resolver. Original guest PEs are untouched. Templates are backed by actual
readonly files/ORA under `/usr/lib/kyil-windows`, projected as Windows system
modules in userspace through a separate readonly root capability.

Module handles, names, PE export/name/ordinal/forwarder lookup, GetProcAddress,
LoadLibrary variants, reference counting of pinned platform providers and
GetModuleHandleEx(FROM_ADDRESS) are real. Failed graphs before constructor entry
roll back. Dynamic static-TLS insertion and final graph-safe unload are explicitly
refused; constructor-side rollback and full dependency reference management need
further work. API-set reference mappings in the graph are not yet runtime support.

Additional implemented families: process-local environment snapshot/get/set/
expansion; UTF-8/UTF-16 conversions with scalar/surrogate validation and actual
Windows replacement rules; counted semaphores; ANSI event/mutex adapters; real
monotonic/RTC/FILETIME conversion, process pointer secret, current debugger/guest
architecture queries. Locale/NLS, code pages and time-zone coverage remain partial.

## Execution evidence

Host:
- parser: 21 checks;
- NLS: 80000 actual Windows comparisons (advertised basic-Latin subset);
- encoding: 80000 actual Windows UTF8/UTF16 comparisons, valid/invalid inputs;
- existing posted IOCP and regular-file OVERLAPPED reference tests: PASS.

Windows CreateProcess reference execution is **not passed**: this host returns
577 (ERROR_INVALID_IMAGE_HASH) for the unsigned child fixture. It is not bypassed.
It also classifies the corrupt MZ/PE-header fixture as unsupported DOS/machine
(216), whereas current KyIlOS prevalidation returns bad executable format (193).
That error-classification difference remains documented rather than hidden.

VM 4 CPU: native process resource security fixture PASS; direct PE parent/child
fixture PASS (four real pipe/argv/env/suspend cycles, terminate); actual provider
PE export execution PASS; the extended runner, platform and encoding smokes PASS.
VM 1 CPU: same extended runner, native root confinement and compiled export census
PASS. Raw evidence:
`build/windows/windows-batch-4-cpu-20261011-221452.log`,
`build/windows/windows-batch-1-cpu-20261011-223641.log`,
`build/windows/windows-loader-4-cpu-20261011-214726.log`.
Both runner configurations preserve TLS/FPU, CFG/VEH, ORA/memory ownership,
JIT W^X, Linux faults, native root/IPC, futex and libc pthread regressions (187
checks / zero failures). No unexpected kernel panic observed. Suchu Terminal
runs commands in these boots; no fresh physical Dell validation is claimed.

The real Chrome executable and chrome_elf.dll remain unmodified. The last run
stopped on CreateRemoteThread; its local adapter is now implemented, but needs
the next real Chrome run. Static import families are still missing; neither
point-of-entry execution nor Chrome navigation/window is claimed.

Initial milestone image (superseded): `build/kyilos-windows-process.iso`.

```
ISO      D0AEDD989B093250D14BE2E7CD53D6AAEDACD7A54F11837486DF06AC784A9B66
kernel   BD58FDD7B86F6313BCB751FC85A03228C508F07EB8D21B0784358D37A90FF245
runtime  0F59F68799713AB03A0165D770473F335C3B4CC1B579B2CD2507AFEBB512C77C
```

The latest compiled-export census has 292 bindings (139 each in kernel32/kernelbase,
14 in ntdll), all resolved. The graph still records substantial missing package
contracts. Windows/Dell original images and the human main VM remain preserved.

## Dependency order for continued batches

1. Close remaining process contracts (cross-process handle exchange, process
   metadata/limits and job/supervision semantics) with capability-backed tests.
2. Generalize loader TLS/unload/API sets/forwarders and Windows exception unwind.
3. Complete shared synchronization and memory/file contracts identified by the
   package-static inventory, including inherited object state and async pipes.
4. Add truthful system/locale/time/registry/module-resource views from actual
   native state. Then resolve all remaining static module families.
5. Enter the unmodified Chrome EXE, trace dynamic resolution and implement its
   demonstrated contracts in coherent batches; no guessed success adapters.

References: [CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw),
[WideCharToMultiByte](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte).


Latest implementation and execution evidence: [coherent batches checkpoint](windows-coherent-batches.md).

