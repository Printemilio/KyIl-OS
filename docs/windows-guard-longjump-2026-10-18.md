# KSW: guarded long-jump targets (2026-10-18)

## Cause and contract

The previous loader refused the first nonzero load-config extension at offset
`0xb0`. In the unchanged Chrome 155.0.8059.84 `chrome.dll`, this is
`GuardLongJumpTargetTable`, with 92 targets and `GuardFlags=0x10500`.
It was an unimplemented mitigation, not evidence that all 92 entries were
malformed. Independent decoding confirms sorted executable targets, including
87 targets not aligned to 16 bytes. The table is at RVA `0x115a532c`;
load-config is at RVA `0xf506ac0`.

The table pointer is a relocated VA. Its entries are sorted RVAs, with the
stride specified by the GuardFlags high nibble (four RVA bytes plus reserved
metadata bytes). Landing sites can be unaligned. They are not ordinary CFG
function-entry targets. A declared empty table denies every long-jump target;
an image without the mitigation retains the legacy executable-image policy.

Primary references:

- [Microsoft PE mitigation metadata](https://learn.microsoft.com/en-us/windows/win32/secbp/pe-metadata)
- [GuardCheckLongJumpTarget](https://learn.microsoft.com/en-us/windows/win32/devnotes/guardchecklongjumptarget)
- [RtlRestoreContext](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-rtlrestorecontext)
- [LLVM COFF linker implementation](https://github.com/llvm/llvm-project/blob/main/lld/COFF/Writer.cpp): `.gljmp$y` / `.gfids$y`, object feature flag and linker-generated catalogs.

## Implementation

- `userspace/windows/load_config.c`: bounded count/span arithmetic, stride,
  sorted unique executable RVAs, zero reserved metadata; validate a private
  snapshot and seal it read-only. Binary-search this catalog separately from
  GFIDS. Free it on failed loads, dynamic-load rollback and process teardown.
- `userspace/windows/loader.c`, `windows.h`: module ownership and lifecycle;
  bounded catalog acceptance trace with count, stride, table RVA and image base.
- `userspace/windows/exception_abi.h`, `seh.c`: AMD64 jump-buffer layout;
  `STATUS_LONGJUMP` handling in `RtlRestoreContext` and the existing
  `RtlUnwind`/`RtlUnwindEx` engine. Validate the copied destination before
  invoking unwind handlers. Restore nonvolatile GPRs, stack/IP, FP control and
  XMM6–15 through the existing validated context-restoration path. Preserve
  return value and run the existing PE cleanup handlers for guarded unwind.
- `win32.c`: `GuardCheckLongJumpTarget` belongs to Kernel32/KernelBase, not an
  invented NTDLL export. Nonfatal queries return false; invalid fatal targets
  terminate with `0xc0000409`.
- Build/import-graph scripts and complete runner include the new real export
  and fixtures. No kernel changes, Chrome byte edits or W^X exceptions.

Unsupported nonimage/JIT long-jump policy remains explicitly denied. Newer
nonzero load-config mitigations still require their own implementation.
`STATUS_UNWIND_CONSOLIDATE`, unsupported unwind dispositions and arbitrary
native/fiber stacks are not silently accepted. The existing bounded PE unwind
engine remains the context engine; there is no parallel unsafe restore path.

## Tests

`tests/windows_longjump_table_test.c` compiles the actual parser: 52 checks,
including unaligned exact targets, adjacent/NX/outside targets, separate CFG
policy, reserved stride metadata, duplicate/order errors, overflowing counts,
spans and truncated fields, flagged empty versus legacy images, relocated
table VA with unchanged entry RVAs, source mutation after publication,
allocation/protection failure and balanced cleanup. Host allocation/protection
faults are injected; this host unit alone does not prove hardware protection.

The PE fixture uses real linker-produced `.gljmp$y`/`.gfids$y` catalogs and
AMD64 unwind metadata. It performs 32 direct restores plus 32 unwinds through
a compiler-generated `finally`, checks nonvolatile GPR/XMM restoration and
return value, and rejects an adjacent executable address which is not listed.
Two independent negative processes exercise both restoration APIs and must
exit `0xc0000409`. All three ran on actual host Windows successfully.

Host evidence: `build/windows/longjump-host.log` (complete reference suite,
exit 0), plus six import-graph unit tests.

### VM outcomes and an independent red regression

- `build/windows/longjump-final-1-cpu-20261018-101512.log`: full runner PASS,
  including longjmp positive/negative processes, existing CFG/SEH, native
  file protection, root confinement, Ultra-Space, threads, Linux TLS/FPU,
  W^X, faults and memory ownership. Compiled export census PASS.
- `build/windows/longjump-final-4-cpu-20261018-101744.log`: direct longjmp
  positive test PASS, then runner's positive and both fatal negative longjmp
  tests PASS. **Full runner FAILS** at the existing concurrent registry test.
  It did not reach its final native/Linux regression group.
- `build/windows/longjump-diagnostic-4-cpu-20261018-102137.log`: the registry
  failure also reproduces in isolation on a fresh boot. Added diagnostics in
  `userspace/windows/tests/registry_smoke.c` preserve the original success condition and show
  wait=0, queried child exit=137, wait error=0. This is a genuinely killed
  producer, not an invalid process wait or a longjmp refusal.
- `build/windows/longjump-security-audit.png`: guest `secaudit` confirms
  `mrac-alert -> mrac-quarantine -> mrac-shadow-write -> mrac-kill` for the
  actual four producers, on their alternating registry hive snapshot paths.
  Despite its historical naming, this is the existing native security policy
  in `kernel/src/security.c`, not ORA content canonicalization.

Both 4-vCPU boots report an unrecognized VexFS test volume and use the RAM
filesystem fallback; the successful 1-vCPU boot had a mounted VexFS volume.
`security_mrac_record_write()` quarantines an unprivileged writer after at
least 14 writes and 8 path changes within 80 scheduler ticks. The registry
fixture performs real transactional alternating-slot writes quickly enough
on the RAM backend to trigger this policy; the next write terminates it.
This accounts for the observed code 137 and prevents claiming a green complete
4-vCPU suite. There is no security exemption, artificial sleep, weakened
expectation or disabled test in this change. The kernel is byte-for-byte
unchanged. Repairing the test storage or reviewing this heuristic is separate
work; no disk was formatted or data removed here.

The diagnostic 4-vCPU boot separately completes the compiled export census
and executes actual Chrome after recording the registry failure. These are
independent validations, not a replacement PASS for the failed full runner.

## New real Chrome boundary

The actual versioned `LoadLibraryExW(..., 8)` now logs catalog acceptance:
count `0x5c`, stride 4, table RVA `0x115a532c`, and the actual randomized image
base. The staged DLL SHA-256 remains
`5B8A1C3F61996FA98289778DB40F29272C10793C9BEB3C8D829EDA9FEEE8F081`.
Host decoding with all 92 RVAs is in
`build/windows/chrome-longjump-table.json`.

The next boundary is static dependency/import binding, not guarded longjmp.
The first logged missing contract is `WINMM.dll!midiInAddBuffer`, followed by
other MIDI and Kernel32 APIs. The diagnostic caps missing-symbol output at
128 entries, so 128 is not the full inventory. Resolution then encounters an
absent dependency and returns error 126; Chrome exits 7. Inspection of the
ordered static descriptors and provider directory identifies `ws2_32.dll`
as the first non-provider dependency after WINMM/Kernel32. `crypt32.dll` and
`dwrite.dll` also remain absent. No substitutes were added in this task.

The compiled-census dependency graph is
`build/windows/chrome-longjump-compiled-graph.json`: 12 nodes, 2625 edges,
zero parse errors, 242 missing unique static contracts and 748 delayed ones.
These are graph/inventory counts, not a claim that all contracts are executed
or that implemented APIs have complete Windows semantics. The executable and
Crashpad child reach their real entries; `chrome.dll` initialization and a
Chrome window are **not** reached. Catalog acceptance does not prove Chrome
itself has executed a guarded longjmp; the independent PE fixture proves the
implementation's actual restore/unwind behavior.

## Delivery

- Full structured results: `build/windows/guard-longjump-validation.json`.
- Current machine-readable frontier: `docs/windows-chrome-main-frontier.json`.
- ISO: `build/kyilos-windows-process.iso`, 2,115,201,024 bytes.
- Final ISO SHA-256:
  `C5A2F2C2B55E258033D6AD3ED989E2D5A6DE0D3E0A9548B529F1FECC8299206A`.
- Unchanged kernel SHA-256:
  `696A888C967C0CF7DC1999424117829F8FFF2D6FFF999B3D96D0F9B19C962383`.
- Both guest configurations shut down through native `shutdown`; final VM
  state verified `poweroff`. No forced poweroff or storage formatting.

The final ISO adds only the registry-fixture failure diagnostics relative to
the first 1-vCPU full-run image. The KSW runtime and kernel are identical
between those images. The full 4-vCPU runner is deliberately recorded red;
the two successful censuses resolve all 710 candidate bindings, which is not
a claim that Chrome's broader import graph is satisfied.
