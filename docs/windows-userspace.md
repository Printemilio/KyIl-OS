# KSW — KyIl Subsystem for Windows

Latest import-family and registry write-guard integration: [implementation and validation](windows-import-batch-2026-10-18.md).

Latest guarded-longjmp integration: [implementation, Windows reference and VM validation](windows-guard-longjump-2026-10-18.md).

Latest native file-protection integration: [implementation and validation](windows-native-file-protection-2026-10-18.md).
KSL denotes the Linux subsystem exclusively. Historical VSL references to
Windows in older checkpoint reports should be read as KSW.

Latest startup execution checkpoint: [message windows / tokens / loader](windows-message-windows-tokens-2026-10-18.md)
(previous [shared-events checkpoint](windows-startup-progress-2026-10-18.md))
and [machine-readable frontier](windows-chrome-main-frontier.json). The dated
description below remains the earlier implementation baseline.

This is an active implementation, **not Chrome compatibility**. Guest EXE/DLL
bytes remain PE32+ and execute directly. The native ELF host `kyil-windows`
contains the userspace adapters; it is not a converted guest executable.

`PE → userspace NT/Win32 adapters → tagged structured native KyIlOS ABI`.

No Linux ABI/libc, kernel Win32 implementation, RWX exception, or success thunk.
`kernel/src/pe.c` launches the native-tagged runtime with the existing PE wire
personality selected before publication. Untagged guest syscall numbers cannot
fall into Linux or legacy dispatch. `compat_pe.c` remains excluded/historical.

## Native interfaces and CPU state

- Existing process groups/address spaces, native THREAD_START, clear-on-exit
  word/futex, EXIT_UNMAP, process wait/exit-group, ORA/process_memory, lazy file
  mappings, events/alternate fault stacks, native heap, FD and SHM capabilities.
- 221 THREAD_CONTEXT: current thread GET or canonical FS/GS SET. Returns actual
  TID/CPU and the inventory region containing saved user RSP. User GS resides in
  the SWAPGS shadow MSR while kernel C runs; active kernel GS stays per-CPU.
  Scheduler switch/fork/clone save/inherit it; fresh native threads and exec
  reset it. RO output records/kernel bases are rejected before SET mutation.
- 222 THREAD_STOP: stop an exact TID in the caller's process group through the
  existing scheduler stop machinery. Clear-on-exit is the quiescence ACK.
- 223 THREAD_EXIT_WORD: register the initial thread's aligned USER/WRITE exit
  word, reusing native clear-child-tid/futex cleanup.
- 224 USER_IDENTITY: current native UID/capabilities/name/home, from the user
  database, not a caller-supplied environment or foreign UID.
- CR4.FSGSBASE is explicitly off: unchecked userspace WRGSBASE would violate
  the existing paranoid SWAPGS sign invariant. Baseline MSR context switching
  works on old x86-64 CPUs. No AVX/FSGSBASE requirement is added.

These are generic thread/identity primitives. TEB/PEB and Windows objects stay
in userspace. PIT/RTC/TSC/LAPIC, ORA, FlowSched policy, graphics, drivers and Suchu
are unchanged by this continuation.

## Implemented Windows contracts

- Bounded AMD64 PE parser, sections/directories/exports/forwarders/import graph,
  preferred-base/rebase DIR64, actual 64 KiB image bases. Images build privately
  RW/NX then become RO/RX/RW with PROT_NONE holes before callbacks execute. Input
  filesystem bytes are never patched. RWX/unknown relocation policies fail.
- Per-thread TEB at real user GS, shared PEB/process parameters/Ldr inventory;
  static TLS templates plus zero tails for EXE and multiple DLLs; mutable per-
  thread allocations, callback/attach/detach lifecycle, thread-local LastError
  and dynamic TlsAlloc/Free/Get/Set. Fresh threads receive their own TEB/TLS.
- CreateThread, CREATE_SUSPENDED/ResumeThread, native blocking join, exit code,
  generation-checked thread handles and safe reaping after native exit ACK.
  PE-owned reserve/commit stacks grow through native synchronous fault events;
  stack reservation is not fully physically committed.
- Loader serialization is recursive on the owning thread, allowing TLS/DLL
  callbacks to use implemented TLS APIs. Other threads wait via native futex.
  Concurrent ExitProcess has one cleanup owner; another caller waits to be
  stopped instead of killing the group underneath detach.
- Native heap-backed process heap and HeapAlloc/Free/Size/ReAlloc; SRW shared/
  exclusive/try locks with native futex blocking. No new scheduler or allocator.
- Security cookie initialized from native RNG; validated load-config/GFIDs and
  actual guarded indirect-call checking/dispatch preserving x64 argument state.
  Unimplemented mitigations fail explicitly. Dormant XFG slots do not mean XFG
  support; dynamic RX/CFG call-target registration is not implemented.
- VEH over native synchronous faults: Windows exception record and AMD64 CONTEXT,
  examine/modify registers and resume; remove/stale handler checks. XSAVE when
  detected/enabled, otherwise FXSAVE; extended CPU state is preserved around the
  handler. This is **not full Windows SEH/unwind** or full XSTATE CONTEXT support.
- Validated delay-import metadata leaves real linker thunks intact. It does not
  prove delayed loading: LoadLibrary/GetProcAddress are still absent.
- Memory APIs currently implement reserve+commit together, whole allocation
  RW→RX and MEM_RELEASE. Separate reserve/commit/decommit/partial protect fail.
- Unnamed manual/automatic Windows events: explicit waiter registration, native
  futex sleep/wake, Set/Reset, actual timeout and generation-checked lifetime.
  Reset does not revoke a waiter already released by Set. Named objects,
  inheritance/security descriptors and multiple-object waits are unsupported.
- Pagefile-backed CreateFileMappingW/MapViewOfFile/UnmapViewOfFile: real native
  SHM capability, independent handles/views, 64 KiB view/offset alignment, bounded
  sizes/rights, coherent RW and RO views, views retained after handle close.
  File-backed/named/inherited/executable sections and FILE_MAP_COPY are refused.
  Native SHM creation currently commits its pages eagerly; it is not advertised
  as sparse pagefile backing. Memory-image ingestion is also still eager.
- CompareStringW: explicitly bounded invariant/en-US basic-Latin alphanumeric
  weights, case flag and empty/negative-length handling. Other weights/locales/
  flags fail; no ordinal comparison is passed off as general linguistic NLS.

## Approved namespace contract

Each native user owns `/home/<native-name>/.windows/C` in the native filesystem.
Windows/System32 and Users/<name>/{AppData/Local,AppData/Roaming,Desktop,Documents,
Downloads,Temp} are actual directories, created with native primitives.

Windows paths, drive names, registry policy and Win32 structures are userspace
concepts. C: is the isolated prefix. A: is the explicitly launched package
folder, mapped read-only. No implicit native root/home drive is exposed.
Lexical traversal outside either mapping, unknown drives and UNC are rejected.
The PEB image/cwd/command line use Windows paths. GetSystemDirectoryW,
GetWindowsDirectoryW, GetCurrentDirectoryW, GetTempPathW and CreateDirectoryW
use that contract; short buffers and native mkdir errors are handled.

**Limits:** no complete Windows sharing/disposition/overlapped contract,
case-insensitive resolution, persistent registry, known-folder API, physical
compatibility DLL installation or full Windows environment yet.
System32 is an actual directory, not a claim that its DLL inventory is complete.
The approved confinement is now implemented using native root capabilities
and irreversible process-group sealing. See `docs/native-fs-roots.md`. Windows
file/stdio handles are real process-local resources, and basic CreateFileW,
ReadFile/WriteFile/FlushFileBuffers/GetFileSizeEx/SetFilePointerEx use the native
FD API. Fully shared opens, CREATE_NEW/OPEN_EXISTING/TRUNCATE_EXISTING are
implemented; other sharing modes, CREATE_ALWAYS/OPEN_ALWAYS, security attributes
and overlapped operations are explicitly refused. Registry remains unimplemented.

## Tests and artifacts

Build: `scripts/build-windows-runtime.ps1` (normal ISO builds call it too), native
runtime plus direct COFF/PE fixtures. Kernel full rebuild after process layout
changes, then targeted relinks. Separate `build/kyilos-windows-tls.iso`; existing
Dell/Suchu ISO/USB images are preserved. Optional actual Chrome files are private
validation payloads in build/staging, never automatically copied into source.

Host parser: 21 checks PASS (`tests/pe_image_test.c`). Windows-only independent
NLS differential: 80,000 comparisons against this host's actual CompareStringW
PASS (`tests/windows_nls_reference_test.c`), limited to the advertised subset.

Runner `usr/native-tests/windows_runtime_test` verifies raw native exit status,
not POSIX shifts. It runs PE/DLL cycles and explicit refusals, EXE/two-DLL TLS
with 8 threads, loader callback re-entry, SRW/16,000 increments, GS migrations,
guard growth, VEH, CFG allow/deny, prefix/events/SHM-view lifetime, three eight-
thread process-exit races, and existing native/Linux regressions. Final VM logs
and outcomes are recorded in `docs/windows-threading-validation.txt`; build success
alone is not execution evidence.

The prior malformed-header panic was causal: inst.c's uint32 e_lfanew+24 wrapped
before a read. Arithmetic is widened and subtraction-bounded; malformed spawn
now fails with PID cleared. PE failures stay in the guest process.

## Chrome frontier and remaining work

Real installed Windows Chrome 155.0.8059.39 and chrome_elf.dll have valid static
TLS/load-config/CFG/delay tables. Before section APIs the actual VM loader
stopped on `KERNEL32.dll!CreateFileMappingW`, after the earlier CompareStringW
frontier. The rendezvous continuation now stops on `KERNEL32.dll!CreateProcessW`
after synchronous local named pipes and regular-file OVERLAPPED I/O
(see `docs/windows-rendezvous-validation.md`). **No Chrome point
of entry or window is claimed merely because metadata and earlier imports pass.**

Large remaining contracts: full NT/Win32 DLL/API-set resolution and module
handles/dynamic DLL TLS/unload; Windows SEH .pdata/.xdata unwind; files/registry/
NLS; synchronization/IO completions/IPC; process/job/token/sandbox semantics;
USER32/native window integration; networking and Chrome subprocess lifetime.
All unsupported operations fail explicitly; no complete Windows ABI is exposed.

References: [Microsoft PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format),
[TEB](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-teb),
[CFG](https://learn.microsoft.com/en-us/windows/win32/secbp/control-flow-guard),
[CreateEvent](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventw),
[CompareStringW](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringw).
## Native root / file continuation

Generic native syscall 225, FS_ROOT: typed process-local FD capability, independent
anchor identity/refcount, rights attenuation/transfer, atomic scoped opens,
metadata/mkdir, irreversible group seal and fork/child/exec policy inheritance.
Namespace anchors reserve own/ancestor names until last reference. Opened file,
directory and stream resources retain their root; IPC grants a new local FD to
the same object, never a transferred number. Actor principal is captured per
capability, separate from object identity. No Windows paths/types in kernel.

C: and A: are now bound and verified before PE entry, then pathname access is
sealed. Generic unscoped open/stat cannot bypass those roots. Existing file
resources continue to work after root close. Native file/STD handles replace
old pseudo STD constants in the Windows runtime. A rejected native buffer now
records EFAULT; lseek addition/subtraction is checked before arithmetic.

Final evidence and exact partial Win32 file contracts:
`docs/windows-roots-validation.txt` and `docs/native-fs-roots.md`.

## Completion and synchronization continuation

Posted IOCP packets, FIFO/LIFO wakeup, timeouts, close-wake and thread association
are implemented in userspace. Generic native execution gates track admission
against actual scheduler runnable state; unrelated blocking releases capacity.
Regular-file OVERLAPPED/IOCP association now uses an actual positional-I/O worker,
retained files/events/ports and real completion errors; named-pipe OVERLAPPED
remains explicitly unsupported. Unnamed mutexes
include recursive ownership and abandonment; x64 critical sections use native
futexes. Stopped-thread registrations are retired before their stacks, and
ExitProcess quiesces those metadata gates before peer termination.
See `docs/native-execution-gates.md` and `docs/windows-completion-validation.md`
for exact scope, limitations and execution evidence.

## Root-scoped rendezvous continuation

Native syscall 227 publishes/claims rights-attenuated message endpoint capabilities
under a typed root authority. Registration, backing, local FD and creator PID have
separate lifetimes. Last registration close removes discovery; retained clients
continue using the channel. Scheduler waits are retired before stopped stacks.
Windows names/protocols stay in `userspace/windows/pipes.c`.

See `docs/native-rendezvous.md` for the contract and
`docs/windows-rendezvous-validation.md` for exact execution evidence. The next
Chrome import is CreateProcessW. The existing pathname-only spawn is insufficient
inside a sealed prefix: launching must consume executable authority and explicit
resource grants, without temporarily removing the seal or reopening ambient
paths. No fake CreateProcessW export has been added.

## Process and dependency-graph batches

The CreateProcessW frontier above is historical. Native syscall 228 now launches
authorized executable snapshots with explicit child-local grants and process
capabilities; userspace CreateProcessW exercises sealed children, actual pipes,
argv/environment, suspension, completion and termination. Real PE platform DLL
adapters, dynamic module/export resolution and UTF8/UTF16 converters are also
tested. Exact coverage, limitations and the recursive compiled-export inventory
are recorded in `docs/windows-process-batches.md`. That import frontier is now
historical: Chrome's real EXE entry has been reached. The current execution and
security boundary is documented in `docs/windows-chrome-execution-2026-10-17.md`;
the Windows effort remains open and no Chrome GUI success is claimed.
