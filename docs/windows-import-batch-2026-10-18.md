# KSW: Chrome imports and registry write-guard correction

This continues the guarded-longjmp checkpoint. Chrome is still the unchanged
155.0.8059.84 PE package. Loader progress is not a Chrome GUI result.

## Dependency inventory

The prior compiled graph contains 242 missing unique static contracts:

| Provider | Missing static contracts before this batch |
|---|---:|
| Kernel32 | 130 |
| WinMM | 30 |
| WS2_32 | 52 (including legacy ordinals) |
| Crypt32 | 26 |
| NTDLL | 3 |
| DWrite | 1 |

`chrome.dll`'s actual ordered import descriptors are WinMM, Kernel32, WS2_32,
Crypt32, Version, NTDLL, chrome_elf and DWrite. Imports and delayed imports are
distinct; dynamic resolver calls are not predictable from this static graph.
`scripts/audit-pe-graph.py` remains the recursive inventory tool. The loader
now logs the exact dependency name and parent on search failure, rather than
leaving only an ambiguous DLL-absent error after earlier missing symbols.

The final compiled census resolves 856 candidate bindings (including modeled
ordinal aliases); this is not a count of fully implemented Windows APIs.
The recursive graph has 13 objects, 2,713 edges and no parse errors:

| Provider | Missing static contracts after this batch |
|---|---:|
| Kernel32 | 104 |
| WinMM | 0 |
| WS2_32 | 23 |
| Crypt32 | 26 |
| NTDLL | 3 |
| DWrite | 1 |
| Total | 157 |

This removes 85 of the prior 242 static misses. The graph still has 746
missing unique delayed contracts. The counts include the currently staged
provider DLLs and do not predict dynamically resolved names.

## Registry failure: cause and native correction

The retained prior evidence is `build/windows/longjump-security-audit.png`
and `build/windows/longjump-diagnostic-4-cpu-20261018-102137.log`.
Wait succeeded, but a producer exited 137: it was killed by native policy.
The audit shows `mrac-alert -> mrac-quarantine -> mrac-shadow-write -> mrac-kill`
on the two alternating hive snapshot files. The historical action names are
security policy names, unrelated to ORA canonicalization.

The old heuristic counted transitions between path hashes. Repeated A/B/A/B
writes accumulated the same score as changes between many unrelated victim
files. The RAM filesystem fallback on these VM boots made ordinary registry
transactions fast enough to trigger it.

The correction uses `fs_file_identity()`, an existing opaque native file
identity stable across content changes and rename, distinct after recreation.
The security window keeps a bounded exact set of nine identities. It retains
the 14-write threshold, the 80-tick observation window, quarantine duration,
capability stripping, and subsequent kill behavior. Nine distinct victims
correspond to eight changes from the first victim; repeated changes between
two journal slots no longer impersonate that population. No filename, KSW,
registry, process-name, or privileged-writer exception was added.

`security_write_window.h` owns this small decision algorithm. `security.c`
protects the shared write-window state and audit ring with separate short
gates. Scheduler/capability/kill calls run outside the write-window gate to
avoid inversion with process retirement. FS still enforces permissions and
root/resource protection before any write. Identity lookup failure refuses
the write; IDs are not truncated or replaced by content/path hashes.

The host test runs 10,065 checks: repeated A/B writes, distinct-object mass
rewrites, exact old write threshold, window reset and distinct 64-bit IDs with
equal low halves. This is a deterministic decision test, not a physical disk
or ransomware benchmark. The VM registry fixture separately verifies four
real producers and 96 values with the real security machinery active.
The first targeted 4-vCPU run passes that previously failing fixture.

This remains a mass-rewrite heuristic, not complete malware detection. Its
preexisting finite per-process tracker capacity and the FS's existing choice
of watched writes are not redesigned here. Repeated writes to two legitimate
objects remain subject to normal permissions, protection and capabilities.

## KSW contracts added

- ACP/UTF-8 environment access with byte-based sizing and deletion, narrow
  system/temp paths, narrow module lookup, computer name and processor facts;
  nonalertable waits, file-position and exclusive range-lock wrappers use
  existing implementations. Alertable waits remain explicitly unsupported.
- Named locale interfaces translate into the installed NLS/calendar data.
  Ordinal comparison is separate from linguistic collation. Unsupported
  locale/collation options and supplementary case folding fail explicitly.
- WinMM exposes its genuinely empty Windows MIDI/wave adapter catalog.
  Invalid device IDs and handles fail; no audio/MIDI buffers are completed.
  Native HDA existence is not claimed to be a WinMM adapter. Timer-resolution
  changes return `TIMERR_NOCANDO`; the system clock is unchanged.
- A native-ABI userspace Winsock adapter owns generational local handles,
  delegates operations to existing native socket/resolver resources, validates
  Windows layouts and translates errors/options. Implemented paths include
  IPv4 sockets, connect/bind/listen/accept, synchronous send/receive/datagrams,
  select, nonblocking mode, supported socket options, numeric/system DNS address
  resolution, address formatting and event objects.
- In-flight socket operations retain native descriptors. Peer-stop quiescence
  and thread retirement release operation leases. Final WSACleanup with an
  active synchronous operation fails explicitly instead of pretending to
  cancel it. Legacy 1.x negotiation, IPv6, overlapped Winsock, provider discovery,
  dynamic DNS cancellation and half-close remain incomplete/not exported.
- New sockets do not grant `NET_CONNECT`. The unprivileged VM fixture tests
  the actual native denial. Successful UDP byte transport is tested against
  host Windows; it is **not** a positive KyIlOS network-transport proof.
- Until socket-provider state inheritance exists, unscoped inherit-all spawn
  with live sockets and explicit lists selecting sockets fail explicitly;
  other handle-list spawn remains unchanged. Socket handles count in the
  process's real KSW handle inventory. No native FD number is exposed as SOCKET.

## Loader ordinals and honest inventory

Legacy Winsock imports use ordinals such as 23 (`socket`) and 115 (`WSAStartup`).
`provider_ordinals.json` is the shared catalog for the runtime, generated PE
providers and compiled census. These mappings were checked against the real
host Windows WS2_32 export table. Public ordinal lookup and normal PE imports
use the same generated provider EAT. Unimplemented ordinal slots remain zero.
Every implemented WS2_32 name has a verified ordinal, including modern names
such as `inet_ntop`. Other native providers reject ordinals without a modeled
Windows mapping instead of selecting an unrelated internal export by position.
Ordinals in application/private DLLs still use those DLLs' actual PE tables.
The inventory parser now excludes zero EAT slots, avoiding a false support
claim for holes such as ordinal 22 (`shutdown`). Provider thunks import real
userspace functions; no guest PE conversion or synthetic success function.

The base-contract fixture compares actual Windows behavior for ACP byte
counts, locales, module identity, CPU groups, invalid waits and absent devices.
The Winsock fixture compares layout, stable ordinals, startup references,
numeric resolution, events, invalid handles and actual UDP/select on Windows.
Guest and host results deliberately distinguish capability denial from I/O.
The host transport fixture also consumes an oversized datagram into a short
buffer and checks `WSAEMSGSIZE` plus the delivered prefix. The KSW adapter asks
the native receive operation for the full datagram length rather than reporting
truncated UDP as successful complete delivery. Stream `sendto`/`recvfrom` use
the connected stream, independently of their ignored address arguments.

## Implementation failure retained

`build/windows/chrome-import-batch-4-cpu-20261018-105743.log` retains an initial fault in
the new ANSI fixture. Large native C automatic buffers skipped a PE stack
guard page at a call instruction. They were moved to bounded temporary heap
storage; select's similarly large workspace was moved as well. Neither stack
size nor guard handling was relaxed. The subsequent guest fixture passes.
Native runtime C functions now also receive the backend's inline stack probes
(`-Xclang -fstack-clash-protection`, native `x86_64-unknown-none-elf` target).
The plain driver option is silently inapplicable to this bare-metal target;
generated `CreateProcessA` code was inspected to verify an actual 4096-byte
step and stack touch. The base fixture spawns its real ANSI child and checks
exit 17, on Windows and in the guest. No Linux target or libc is substituted.

The final provider deliberately leaves `FreeLibraryAndExitThread`,
`GetProcessAffinityMask` and `lstrcmpW` unexported: safe last-reference DLL
unload, a truthful process affinity query and general linguistic collation
are not provided by the underlying implementations. Their names must not
inflate the inventory merely because a partial wrapper could be written.

## Source changes

- `userspace/windows/base_contracts.c`, `nls.c`, `calendar.c`, `multimedia.c`,
  `winsock.c`: the contract implementations above.
- `userspace/windows/environment.c`, `process.c`, `windows.h`, `win32.c`:
  lifetime/peer-stop integration, explicit inheritance refusal, registrations.
- `userspace/windows/loader.c`, `provider_ordinals.json`,
  `scripts/generate-windows-export-census.py`,
  `scripts/build-windows-provider-dlls.py`, `scripts/audit-pe-graph.py`:
  real provider resolution, ordinals and dependency diagnostics/inventory.
- `kernel/include/unos/security_write_window.h`, `security.h`,
  `kernel/src/security.c`, `fs.c`: native object-identity security correction.
- `userspace/windows/tests/base_contracts_smoke.c`, `winsock_smoke.c`,
  `tests/security_write_window_test.c`, `tests/pe_graph_test.py`:
  positive/negative fixtures and independent policy/format checks.
- `scripts/build-windows-runtime.ps1`, `test-windows-runtime-host.ps1`,
  `test-windows-vm.ps1`: build, real stack probes, reference and VM integration.

## Remaining dependency work

The next observed absent provider is `CRYPT32.dll`, required by `chrome.dll`.
This is not its only unresolved import family. Kernel32 process/IO/runtime
contracts, asynchronous Winsock/provider discovery, certificate/crypto,
remaining NT contracts and DWrite still require real implementations. Delayed
imports and dynamic resolutions form separate frontiers. Adding a nominal
Crypt32 provider with failure-only placeholders would not establish that Chrome
can initialize, let alone show a window; no such provider is staged here.

Actual successful KSW socket I/O still needs a separately authorized native
network test. Windows reference UDP is not substituted for that missing proof.
No Chrome DLL entry, browser GUI, physical D630 regression run, or persistence
across reboot is claimed by this batch.

## Validation and current frontier

Final exact VM logs, full-runner/census outcomes, unchanged package hashes,
ISO provenance and the next actual Chrome failure are recorded in
`build/windows/chrome-import-batch-validation.json` and the current
`docs/windows-chrome-main-frontier.json`. Any red intermediate run remains in
the evidence; final results must not be inferred from compilation alone.

| Validation on the final build | 1 vCPU | 4 vCPU |
|---|---|---|
| Complete Windows/native regression runner | PASS | PASS |
| Concurrent registry, four producers / 96 values | PASS | PASS |
| Base contracts, real ANSI child exit 17 | PASS | PASS |
| Winsock layouts, ordinals, events, native network denial | PASS | PASS |
| Compiled census | 856 resolved, 0 absent | 856 resolved, 0 absent |
| Chrome's 92 LongJump entries | accepted | accepted |
| Real Chrome frontier | Crypt32 absent, error 126, exit 7 | same |
| Chrome DLL initialization / GUI | not reached | not reached |
| Native shutdown | completed | completed |

Final serial traces:

- `build/windows/chrome-import-final-1-cpu-20261018-114231.log`
- `build/windows/chrome-import-final-4-cpu-20261018-114434.log`

Independent checks: seven import/ordinal graph tests and 10,065 native
write-policy assertions pass. Actual Windows comparison results are in
`build/windows/chrome-import-batch-host.log` and
`build/windows/chrome-import-final-host-fixtures.log`. Restricted host attempts
returned `0xC0000008` before output; the authorized Windows runs pass, including
UDP transport/truncation and ANSI child execution. The restricted invocation
failure was not diagnosed further and is not a guest KSW transport result.

The final ISO is `build/kyilos-windows-process.iso`, 2,115,528,704 bytes, SHA-256
`27617A9BBB862A37AB379EEBDD9ED3D850D59465906EA9EEC40C2EA7F0BBB8E2`.
All 140 runtime/provider/test payload files match portable staging; Chrome's
three original package hashes are unchanged. This is the dedicated Windows
validation image, not a new physical USB-HDD validation. The VM is powered off.

Primary contract references:

- [Microsoft: MIDI device IDs and errors](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinopen)
- [Microsoft: timer-resolution requests](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod)
- [Microsoft: WSADATA layout](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/ns-winsock2-wsadata)
- [Microsoft: Winsock initialization](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsastartup)
