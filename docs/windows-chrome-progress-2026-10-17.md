# Windows/PE checkpoint — 17 October 2026

**Historical checkpoint, superseded by [the execution checkpoint](windows-chrome-execution-2026-10-17.md).**
The eight root imports listed below have since been implemented and the actual
Chrome executable entry point has been observed in VM. This section preserves
the earlier evidence and limitations; it is not the current blocker list.

**Chrome's executable entry point has not been reached.** The original 94
missing imports in `chrome_elf.dll` remain resolved by the compiled resolver.
This pass reduces the next observed frontier in `chrome.exe` from 30 absent
exports to 8. This is not a claim that every contract of these exports, or every
transitive/delayed/dynamic Chrome dependency, is implemented.

## Implemented userspace contracts

| Family | Real behavior | Deliberate limits |
|---|---|---|
| Locale | Installed-locale enumeration/filter/callback, default locale name/LCID, per-thread locale at AMD64 TEB offset `0x108` | Installed en-US data; invariant thread locale. No fictional extra locales/sorts. |
| Heap | Private heap identities/generations; reserved arenas, page-granular growth, fixed capacity, 16-byte aligned blocks, reuse/split/coalescing, realloc and zeroing; recursive locks and operation pins; release all live blocks on destroy | 256 private heaps. No executable heap, LFH or generate-exception allocation mode. HeapWalk enumerates busy allocations. Concurrent destroy of a pinned heap fails rather than freeing an active object. |
| Heap corruption | Out-of-band allocation metadata and prefix/tail cookies; irreversible process-wide termination policy; actual fatal status `0xc0000374`, using existing WER when registered | Detection occurs on heap operations, not on every arbitrary user store. No claim of full Windows heap internals. |
| VM | `VirtualAllocEx/FreeEx/QueryEx` on the current-process pseudohandle reuse actual allocation/protection/inventory; `PrefetchVirtualMemory` reads one byte per covered page through native validated usercopy and materializes sparse pages | Remote VM authority is not inferred from a PID or query handle. Reserve/commit separation and decommit remain unsupported, as before. Prefetch does not change permissions. |
| Sections | `FlushViewOfFile` validates the actual mapped range and publishes writes to the native shared volatile backing | Current section implementation is pagefile/SHM-backed only. No disk flush or file-backed section is falsely advertised. |
| Mitigation query | Actual native NX enforcement and permanent DEP/disabled ATL-thunk policy | Other mitigation classes explicitly fail. |
| NT status | Thread-local LastStatusValue at `GS:0x1250`; NT-to-Win32 conversion records the NT cause; NoTeb variant and combined status/error setter | Status conversion table is bounded; unknown translations return `ERROR_MR_MID_NOT_FOUND`. NT query/close return values do not themselves overwrite the TEB status. |
| NT process inspection | Current-process basic information, actual PEB, PID/parent, online-CPU mask, PE32+ architecture, scoped image names | No foreign PEB inspection, debug attachment or invented resource accounting. Fixed-size classes enforce exact lengths; variable records publish the required length. |
| NT object inspection | Real validated types for process/thread/file/pipe/event/mutex/semaphore/section/job; actual handle flags for providers supported by GetHandleInformation | Basic reference/access counts and object names are not fabricated; unsupported classes/types fail explicitly. |
| File metadata | Basic/standard/attribute-tag records from native STAT_EX; real EOF truncation by retained writable file handle, without changing the cursor | Missing creation timestamps remain unavailable (zero). Other information/set classes fail. No implementation of reparse or delete-pending semantics is claimed. |
| Handle count | Counts live Windows object-handle slots across providers, excluding pseudohandles, closed-but-retained objects, heap handles and hidden native transport/root capabilities | Current process only; sampled count may naturally change with concurrent opens/closes. |
| Multiple wait | Atomic wait-all and ordered wait-any over existing event/mutex/semaphore objects, scheduler/futex blocking, timeout cleanup, exact token consumption and abandoned mutex ownership | 64 handles, no duplicates. A single process/thread/file target delegates to its existing single-wait contract. Mixed multi-object process/IO waitsets explicitly fail; no polling observer is substituted. |
| WOW64 context | Validates the thread, then rejects an i386 context for an actual AMD64 thread with error 87, matching the reference Windows observation | The loader only admits AMD64 PE32+; there is no WOW64 thread engine. No truncated register context is manufactured. |
| DLL search | Active/default application/System32/user-directory policy controls real capability-confined file opens; known native providers remain protected; selected file FD is the exact source parsed/mapped | User-directory list is empty (AddDllDirectory absent). Unsupported flags fail. Dynamic TLS and graph-safe dynamic unloading remain limited; they are not advertised as implemented. |
| CWD | Canonical C:/A: paths, per-drive relative resolution, PEB CurrentDirectory with its required trailing separator, safe string snapshots, retained native directory anchors | Only explicitly configured drives C:/A:. Other drives/UNC absent. Child receives the current canonical directory and anchor; inactive-drive history is not transferred as a complete DOS environment. |

## Lifecycle and loader corrections

- Native process/runtime/root capabilities are prepared **before** loading
  static DLL dependencies. Environment initialization reuses them instead of
  opening a second set after the graph has already been decoded.
- A CWD anchor is a genuine native root capability, not a path string or a
  guessed FD. CreateProcess passes it explicitly in private bootstrap slot 14;
  WER service launch follows the same rule. Failed launches close the retained
  resources. The child can keep its directory after the parent changes CWD.
- Relative image resolution and the actual confined image open share the CWD
  gate. The already-opened resource supplies the bytes to the PE loader.
- UTF-16 path reads use safe page-bounded batches, including page-crossing
  surrogate handling. Output copies do not raise an avoidable fault while the
  namespace gate is held. CWD changes also update PEB process parameters.
- Multi-wait links belong to the execution context. Exit cleanup retires them
  and mutex ownership even while an inspection thread HANDLE remains open.
- A failed DLL initialization is never subsequently returned as a valid
  module. Full-path reloads retain the existing loaded object.
- The Chrome harness now requires the specific static-import error **and**
  missing imports from the actual root executable. The earlier generic
  `load failed` pattern accepted an unrelated loader regression; that regression
  was diagnosed and corrected before this checkpoint.

The kernel was not modified for this pass. Renderer, network, Suchu, scheduler,
ORA and native ABI numbering were not redesigned.

## Executed validation

Host reference tests run against actual Windows APIs, not KyIlOS measurements:

- PE parser: 21 checks.
- 80,000 NLS comparisons and 80,000 UTF-8/UTF-16 comparisons.
- Existing IOCP, positional/overlapped file IO and 32 suspend/resume checks.
- New locale/VM/section, private heap (four workers), NT inspection/status,
  multiple-wait and CWD/file/child-environment fixtures.
- `build/windows/cwd-loader-host.log` contains the passing reference run.

Windows prefetch is advisory: the guest additionally verifies that its current
implementation actually materializes the sparse pages. Native CWD capabilities
also provide stronger **pre-start** directory pinning than Windows promises for
a suspended child. These are explicit guest-only checks, not relabeled Windows
guarantees. An initial host fixture incorrectly assumed the latter property;
it failed, its suspended test child was cleaned up, and the two contracts were
separated before the passing run.

VM evidence:

- `build/windows/chrome-main-4-cpu-20261016-130810.log` and
  `chrome-main-1-cpu-20261016-233943.log`: complete Windows/native/Linux runner
  after the first batch, including private heaps and deliberate fatal corruption.
- `build/windows/multi-contract-4-cpu-20261016-235325.log`: complete runner and
  compiled export census after NT/file/prefetch/multi-wait; the real Chrome
  root then reports 11 absent imports.
- `build/windows/cwd-loader-4-cpu-20261017-001015.log`: CWD, actual DLL execution,
  handle inspection, process/stdio, WER DLL callbacks and job service pass. Its
  final Chrome attempt exposed the static namespace-order regression above;
  that final attempt is **not** counted as a validated import frontier.
- `build/windows/cwd-loader-fixed-1-cpu-20261017-001606.log`: **complete runner**,
  compiled census and actual Chrome launch on the corrected final image. Chrome
  reports exactly 8 missing root imports, without reaching its entry point.
- `build/windows/cwd-loader-final-4-cpu-20261017-001844.log`: the **same final
  image** passes direct PE/DLL loading, CWD/child anchor, real searched-DLL code,
  NT/VM/file/handle inspection, multiple waits and compiled census in 4 vCPU;
  the actual Chrome launch reaches the same 8-import barrier.

No panic is present in those completed passing runs. `git diff --check` passes
(existing line-ending conversion notices remain).

Final test image: `build/kyilos-windows-process.iso`.

SHA-256: `2FC75CE666B4F1140E3D4B246A012AE36EF0222767232E6201BE8572BAA263EA`.

Kernel SHA-256 unchanged:
`5AD7F56703715130356F6591BB83CD952D4AC340597C1F6D313566BD917FED46`.

No new physical Dell validation or Chrome GUI/entry-point success is claimed.

## Changed source areas

- `userspace/windows/heap.c`: private arena allocator and corruption policy.
- `nls.c`, `profile_api.c`, `memory_api.c`, `mapping.c`, `win32.c`: locale,
  mitigation, VM/prefetch and shared-view contracts.
- `nt_core.c`, `windows.h`: NT inspection/status and preserved AMD64 layouts.
- `objects.c`, `environment.c`: multiple waits, object retirement, thread-local
  state, provider handle accounting and PEB directory updates.
- `process.c`, `process.h`, `namespace.c`, `loader.c`: canonical/confined image
  open, retained CWD capabilities, launch inheritance and genuine DLL search.
- `files.c`, `pipes.c`, `completion.c`, `registry.c`, `job.c`, `wer.c`: file
  metadata/EOF, provider inventory counts and WER directory inheritance.
- PE fixtures and import definitions under `userspace/windows/tests/`, plus
  `scripts/build-windows-runtime.ps1`, `test-windows-runtime-host.ps1` and
  `test-windows-vm.ps1`.

## Remaining observed root frontier

See `windows-chrome-main-frontier.json` for the machine-readable evidence.

1. `GetProcessTimes`: needs real creation/exit timestamps and user/kernel CPU
   accounting; total CPU time must not be mislabeled as an invented breakdown.
2. `K32GetPerformanceInfo`, `K32GetProcessMemoryInfo`: need genuine structured
   system/process memory/accounting facts, including the supported peak/commit
   semantics; arbitrary zeros are not acceptable substitutes.
3. `MoveFileW`, `ReplaceFileW`, `SetFileAttributesW`: need confined atomic native
   namespace/metadata operations, coherent file identity/open-reference handling
   and explicit rollback. String conversion followed by unscoped legacy rename
   or chmod would violate the chosen root-capability confinement.
4. `SetThreadAffinityMask`: needs an actual allowed-CPU set integrated with
   FlowSched placement/migration; storing a Windows mask alone is not support.
5. `SetProcessShutdownParameters`: native power currently closes windows then
   terminates remaining user groups without a personality shutdown-order
   participant. A remembered but unused Windows priority would be false support.

This checkpoint leaves the Windows/Chrome work open. Resolving these eight will
only establish the next execution frontier, not establish general Chrome or
Windows compatibility.
