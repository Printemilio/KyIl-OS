# Windows userspace continuation — 2026-10-18

This continues the shared-events checkpoint. Chrome's executable entry and its
real Crashpad child were already executing. This is not a Chrome GUI milestone.

## Changes

- A native launch builds Windows environment defaults from the **existing real
  confined prefix/profile** (`USERPROFILE`, `LOCALAPPDATA`, `APPDATA`, temporary
  directory, Windows directory, home and username). Existing variables win,
  including empty values. Explicit child environment blocks are not expanded or
  silently supplemented. No Google/Chrome directory is created by this change.
- Corrected `CREATE_UNICODE_ENVIRONMENT`: **0x400**, not 0x40000 (which is
  `CREATE_PROTECTED_PROCESS`). The old implementation and its process fixture
  shared the same wrong constant. Protected-process creation is now refused.
- USER32 message-only windows have real per-process classes and owning-thread
  callbacks. A confined, prefix-scoped Ultra-Space `user-service` maintains the
  cross-process catalog. It authenticates PID/TID from native messages; a
  supplied HWND or payload identity cannot authorize mutation. Session endpoint
  HUP removes the dead process's classes/windows; thread retirement removes its
  windows. No Win32 window code or callback address enters the kernel.
- Window creation/destruction, NCCREATE/CREATE/DESTROY/NCDESTROY, class atoms,
  case-insensitive lookup/continuation, title/class queries, PID/TID inspection,
  extra bytes/userdata and same-thread text messages have actual state. Refcounts
  protect local records across reentrant callbacks. Procedures are checked for
  executable mappings. An aborted publication rolls back the catalog object.
- Read-only token handles project the authenticated native UID to the same
  `S-1-22-1-<uid>` namespace and Everyone/Authenticated groups already accepted by
  the named-pipe DACL broker. Query and duplicate rights are enforced; stale
  handles, protected close and concurrent references use the compact generation
  codec. They grant **no native capabilities** and no invented NT privileges.
- `LOAD_WITH_ALTERED_SEARCH_PATH` performs an actual confined absolute open and
  resolves transitive imports against the loaded DLL's directory, with scoped
  loader state and rollback. Known native providers retain precedence. Module
  filename queries and LDR full names preserve the actual loaded subdirectory.
- The transitive fixture exposed another real loader limitation: both DLLs have
  the same preferred address and no absolute fixups. An empty relocation table
  was incorrectly treated as IMAGE_FILE_RELOCS_STRIPPED. The parser now retains
  the actual COFF flag; stripped images must still use their preferred base,
  while position-independent images with an empty table can be rebased. The PE
  unit test has 25 checks, including both cases and invalid relocation records.
- Bounded file/directory/USER32/token traces expose remaining contract refusals.

## Explicit limits

USER32 is **not a desktop Win32 implementation**. Visible/top-level windows,
message loops, cross-thread dispatch, subclassing and graphics are not claimed.
`FindWindowW` is deliberately absent rather than incorrectly including
message-only windows. Creation currently accepts HWND_MESSAGE only. Catalog
resources are bounded (4,096 windows; finite atom and native endpoint tables),
with explicit allocation failure. Atom/window IDs are namespace identities, not
native capabilities. The service retains no creator-dependent backing object.

Token mutation, inheritance, impersonation, foreign-process token acquisition,
mandatory integrity, restricted-token policies and unsupported information
classes fail explicitly. In particular no fabricated medium-integrity SID is
returned. `OpenProcessToken` does not grant WRITE_OWNER/READ_CONTROL just because
a caller requests them. Native confinement remains in force independently.

Dynamic static-TLS DLL initialization and graph-safe DLL unloading remain prior
loader limitations. The altered-search fixture pins its module explicitly; it
does not turn unsupported unloading into success. Windows on the reference host
ignores incompatible load flags for an already-loaded module; KyIlOS rejects
that documented-forbidden combination. The negative case is guest-specific.

## Validation protocol

- `user_windows_smoke.c`: actual classes/callbacks, 24 windows in 12 cycles,
  title/class/atom/continuation, foreign lookup/denied destruction, normal child
  exit and killed child cleanup, invalid pointers and exact explicit environment.
- `token_query_smoke.c`: real SID bytes, sizing/access errors, 256 concurrent
  duplicate/query/close operations on two PE threads, retained identity after
  closing the original, stale handles and unsupported-policy refusals.
- `altered_search_smoke.c`: real nested DLL calling a real transitive DLL,
  different-value executable-directory shadow, system-only default, actual
  module pathname and failed-load search-state rollback.
- Positive/reference contracts execute against actual Windows DLLs. The host
  raises STATUS_INVALID_HANDLE on the stale-token query (isolated probe saved in
  `build/windows/token_query_reference_probe.c`); that negative remains enabled
  in the KyIlOS guest. No exception is silently counted as a passing reference.
- Targeted VM 4-vCPU USER32/token/process tests pass in
  `build/windows/user-token-target-4-cpu-20261018-015012.log`.
- Host suite, including the new USER32/token references:
  `build/windows/user-token-host-suite.log` (PASS). Altered-search host reference
  also passes and is integrated into the host script. Six PE inventory tests pass.

Final **same-image** validation passes:

- Full Windows/native/ELF/Linux runner and compiled export census, 1 vCPU:
  `build/windows/message-windows-final-1-cpu-20261018-020808.log`.
- Same runner/census, 4 vCPU:
  `build/windows/message-windows-final-4-cpu-20261018-020140.log`.
- Final host suite: `build/windows/message-windows-final-host.log` (PASS).
- Both VM runs execute two actual Chrome EXE entries; no kernel panic observed.
  The 4-vCPU suspend/resume fixture again takes several minutes but passes.
- Kernel unchanged; VM shut down cleanly and verified powered off.
- ISO: `build/kyilos-windows-process.iso`, 2,114,891,776 bytes, SHA-256
  `F24D48B0814127865DDCF97C94B63832B3AD30DE3A9D6D5397D399DCADCD7CF2`.
- Runtime/staged-runtime hash match:
  `FF36958A4238152932B7AC667E9DFA9CD0D4B774674F83B6838D4DF47A29F50B`.
  User-service/staged-service hash match:
  `5245A9F1976C8B0093AE1B8A1783316BD5AD2019D0DE4F305B291EAD18455173`.

## Final real Chrome frontier

The real `A:\155.0.8059.84\chrome.dll` is present. Its readability probe is
`CreateFileW(GENERIC_READ, share=3, OPEN_EXISTING, attributes=0)`, caller
`0x14003de22`. VSL rejects non-fully-shared opens before touching the native
file. Chrome therefore considers the versioned DLL unreadable, chooses
`A:\chrome.dll`, and correctly receives error 126 for this absent fallback.
It exits with code 7. This is **not** a missing versioned DLL or another unknown
`LOAD_WITH_ALTERED_SEARCH_PATH` failure.

A real file-use sharing contract is needed; accepting share=3 as share=7 would
silently drop denial of delete/rename. See the concrete
[file-use authority decision](windows-file-sharing-decision.md). No dependent
native ABI/capability change was chosen while that decision is pending.

Crashpad independently still lacks its application-specific profile parent tree
and a non-message-only session-notification window. A broader OpenProcessToken
request (`0xa0008`) remains honestly refused; Chrome proceeds after it. Its
query-only token projection should not be mistaken for a full NT token model.

The compiled-resolver inventory is
`build/windows/chrome-message-windows-compiled-graph.json`: 12 nodes, 2,624 edges,
zero parse errors, 242 missing unique static contracts and 748 delayed contracts
across the package including `chrome.dll`. Root EXE/chrome_elf static imports bind.
**chrome.dll's entry and Chrome's GUI are still not reached.**

Intermediate failures are retained, not counted as passing validation:
`user-windows-frontier-*` revealed the wrong Unicode flag;
`user-windows-unicode-*` revealed the foreign-DestroyWindow error mismatch;
`message-windows-full-*` revealed the empty-relocation-table restriction. The
final image includes the corresponding fixes. The positive altered-search
fixture also executes against actual Windows with the same DLL bytes.

## Primary contracts

- [Process creation flags](https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags)
- [FindWindowExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-findwindowexw)
- [CreateWindowExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createwindowexw)
- [OpenProcessToken](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocesstoken)
- [GetTokenInformation](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-gettokeninformation)
- [LoadLibraryExW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)
- [Chromium user-data directory selection](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/chrome/install_static/user_data_dir.cc)
- [Chromium main DLL path selection](https://raw.githubusercontent.com/chromium/chromium/main/chrome/app/main_dll_loader_win.cc)

Upstream sources explain intent. The actual Chrome 155.0.8059.84 binary trace is
the evidence for the branch this package executes.
