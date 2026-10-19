# KSW: native file protection and the Chrome DLL probe

KSW = KyIl Subsystem for Windows. KSL is exclusively Linux. The user selected
native resource protection with Ultra-Space orchestration, **not full Windows
sharing leases** and not a second userspace authority over native file access.

## Actual authority and implementation

`fs_is_protected()` previously restricted `/sys`; there was no general dynamic
file protection registry. FS roots already pin directory anchors, not every
descendant file. Cooperative checks in KSW alone could not secure native access.

`VEX_FS_OPEN_PROTECTED` extends existing FS_ROOT OPEN. An authorized READ root
can open an existing regular file readonly with native namespace protection.
Write/create/truncate/directory requests are rejected before effects. Resolution,
permission checking, pin creation and descriptor publication share the existing
FS namespace transaction. Content is not hashed, copied or made immutable by
this pin. Both resident ORA files and lazy ISO stream descriptions are covered.

The native FD object owns the pin. DUP, fork, explicit inheritance, queued IPC
and mappings retain the object using existing refcounts. Failed construction,
failed delivery, normal close and process death release their references. The
final reference releases the pin, independently of creator PID or service life.
There is a bounded 4096-description registry; OOM returns an error, never an
unprotected successful open.

`fs_delete`, recursive prefix removal, move/replace and preserve-metadata replace
enforce protection, including source ancestors and destination replacement.
Native unlink/rename/rmdir and FS_ROOT operations return denial at the same
authority. Symlink creation cannot rebind a protected name/ancestor. A regular
file cannot overlay an existing directory to hide protected descendants.

FS_ADMIN (including explicitly delegated native root administrative authority)
or trusted kernel security code may intervene. Merely being UID 0 is insufficient.
Root operations still refuse unrelated writable live descriptions whose final
flush could recreate a removed name: administrative authority is not permission
to introduce that lifetime bug. Native read/write permissions remain required.

## Orchestration and Win32 projection

`namespace.c` uses its existing confined pathname translation. For protected
opens it launches/reuses `/usr/lib/kyil-windows/resource-service` in Ultra-Space.
Each request delegates exactly READ|TRANSFER on the applicable root plus a reply
endpoint. The service has zero ambient capabilities and no ambient pathname
authority. It validates the typed root, calls native protected OPEN, and sends
the resulting file capability back as a new local FD. Reply authority is
attenuated to SEND. Malformed requests and undeliverable replies release FDs.

The native FS remains authoritative even if a native/ELF/PE caller bypasses this
service. Native callers can also request the same protection directly. Chrome
receives no FS_ADMIN or other extra capability. No Windows access/share constants,
drive names or policy are added to the kernel.

KSW accepts `GENERIC_READ, share=3, OPEN_EXISTING`; fully shared opens retain
their prior behavior. Exclusive/deny-read/deny-write modes and unsupported
security/disposition combinations remain explicit ERROR_NOT_SUPPORTED.

Deliberate differences: this is an ephemeral canonical-name pin, not symmetric
Windows share admission, immutable bytes, delete-pending or durable policy.
Authorized native writes remain possible. Administrative rebinding is permitted;
the pin continues protecting its original canonical name, rather than following
an administrator's new pathname. Hard-link/NT alias semantics are not claimed.
KSW maps native EACCES on deletion/rebinding to ERROR_ACCESS_DENIED (5), rather
than pretending to report a Windows ERROR_SHARING_VIOLATION (32).

## Second cause uncovered by the real run

The first image passed native protection, KSW files and the full 1-vCPU runner,
but actual Chrome's protected open returned ENOENT. This was not staging:
`fs_get_metadata()` consulted disk/ISO only, while `fs_exists`, access checking
and reads already understood the ORA boot archive. Portable boot has no CD-ROM
backend. Metadata now reports real archive regular files, readonly, without
materializing their bytes and with writable disk overlays still taking priority.
The native regression opens its real boot-archive executable and verifies ELF
bytes through protected OPEN, in addition to mutable-file tests.

## Validation

The same final image passed in both 1 and 4 vCPU; execution evidence is below.
The native fixture checks ordinary zero-capability identity, direct native and
root bypass attempts, source/destination/ancestor rename, replacement, symlink
rebinding, concurrent opens/dups, mapping-only and queued-attachment lifetime,
fork plus kill and deletion after final release. The Win32 file fixture performs
the share=3 open through Ultra-Space, reads actual bytes and checks native denial.

No forced allocator-failure campaign, real cross-UID fixture or live elevated
administrator test is claimed. The privileged override has been source-audited.
Older D630 hardware has not been retested in this pass.

## Actual Chrome boundary after the correction

On the final 4-vCPU image the unchanged Chrome 155.0.8059.84 performs:

```
CreateFileW(A:\155.0.8059.84\chrome.dll, READ, share=3, OPEN_EXISTING)
KSW protected open -> native FD 14
LoadLibraryExW(A:\155.0.8059.84\chrome.dll, LOAD_WITH_ALTERED_SEARCH_PATH)
load reason=unsupported load-config extension
```

The absent `A:\chrome.dll` development fallback is no longer selected. The
subsequent READ|EXECUTE/share=1 probe remains explicitly unsupported; it does
not prevent this actual versioned DLL load attempt. No claim of full Win32
sharing is made.

The new loader frontier is precise: `wr_load_config()` rejects the first
nonzero newer field at offset `0xb0`, **GuardLongJumpTargetTable**. The real
DLL's load-config RVA is `0xf506ac0`, size `0x140`, GuardFlags `0x10500`, and its
GuardLongJumpTargetCount is **92** (`0x5c`). Field names are verified against
the local Microsoft Windows SDK `winnt.h` (10.0.26100.0). Existing code accepts
the flag but only an empty table, then refuses a nonzero table in the newer
extension loop. It is not a missing file or a file authority failure. This
security contract has not been ignored, disabled or implemented in this pass.

`build/windows/chrome-native-protection-load-config.json` contains raw decode
evidence; `native-file-protection-package-proof.json` checks all three actual
application binaries against their preexisting package hashes. The broader
compiled census still has 242 static/748 delayed unique missing contracts,
12 nodes, 2624 edges, zero parse errors. Neither chrome.dll entry nor Chrome
GUI is reached. Crashpad's independent profile/window/token limits remain.

## Final image, tests and raw receipts

- ISO: `build/kyilos-windows-process.iso`, 2,115,141,632 bytes.
- SHA-256: `D827E2B00589B220442A96CFD2DF517A1080E60D5C53DECF9F475214569C185B`.
- Kernel (also verified in portable staging):
  `696A888C967C0CF7DC1999424117829F8FFF2D6FFF999B3D96D0F9B19C962383`.
- Build: `build/windows/native-file-protection-build.log`,
  `native-file-protection-kernel-build.log`, `native-file-protection-final-iso.log`.
  Final FS/provider objects were recompiled and relinked after the archive fix.
- Raw 1-vCPU serial:
  `build/windows/native-file-protection-archive-1-cpu-1-cpu-20261018-094434.log`.
- Raw 4-vCPU serial:
  `build/windows/native-file-protection-archive-4-cpu-4-cpu-20261018-093602.log`.
- Both boots pass `native_file_protection`, `native_fs_roots_test`,
  `windows-file-smoke.exe`, the **complete** `windows_runtime_test` and compiled
  export census. Both execute unmodified Chrome and reach the same new loader
  refusal. No kernel panic is observed. VM ultimately powered off via native
  shutdown on both boots, without forced poweroff.
- Host references: `build/windows/native-file-protection-host-rerun.log`, PASS;
  PE graph tests: 6 PASS. One initial **host Windows** IOCP reference attempt
  failed (`native-file-protection-host.log`). The unchanged diagnostic with
  debug output redirected to stdout and the full unchanged suite rerun pass.
  The intermittent host condition is not diagnosed or hidden by skipping it.
- Harness caveat: its broad Chrome exit marker can match the child before the
  main process finishes. Automated shutdown was sent too early on these boots;
  the logs retain that warning. Actual main-process DLL refusal/exit were
  observed separately, then native shutdown was issued manually. This is not
  evidence of a successful Chrome UI.
- Machine receipt: `build/windows/native-file-protection-validation.json`;
  current contract frontier: `docs/windows-chrome-main-frontier.json`.

Changed implementation files: `kernel/include/unos/{fs.h,vex_fs_root_abi.h,
vex_legacy_compat.h}`, `kernel/src/{fs.c,vex_posix_compat.c,
vex_fs_root_provider.inc}`, `userspace/windows/{files.c,namespace.c,
resource_protocol.h,resource_service.c}`, tests `{native_file_protection.c,
file_smoke.c,runner.c}`, runtime build script and VM failure-detection pattern.
No native process/Windows token privileges, W^X policy, ORA ownership or
scheduler algorithms were changed.
