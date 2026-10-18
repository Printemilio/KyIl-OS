# Chrome: actual PE execution checkpoint, 17 October 2026

**Historical checkpoint:** Ultra-Space now implements the decision below;
the actual Crashpad protected/inheritable pipe succeeds in complete 1- and
4-vCPU runs. See `windows-ultra-validation.md` for current evidence, image,
measurements and the next execution frontier.

Open work. The real Chrome EXE entry point is reached. **No Chrome window,
rendered Chrome page, or usable Chrome browser is claimed.** Root static import
resolution is distinct from the large remaining chrome.dll contract inventory,
delayed imports and dynamically discovered APIs.

## Executed package

The frozen `chrome-host-validation.exe` and matching chrome_elf.dll are
155.0.8059.39. The newer coherent package under `/usr/windows-apps/Chrome` is
155.0.8059.84, including that version's executable, adjacent chrome_elf.dll,
versioned chrome.dll, resources and locales. Do not mix these versions. Source
and staged hashes are recorded in `build/windows/chrome-current-package.json`.
Host System32 libraries and a host browser profile are not deployed.

The current EXE enters at `0x14023cd10`; the frozen EXE uses `0x14021da20`.
PE images execute directly using the Microsoft AMD64 ABI. The native launcher
being an ELF does not convert the PE payload to ELF.

## Completed contract batches since the eight-import checkpoint

| Area | Actual behavior and important limits |
|---|---|
| Process times | Real native creation/exit timestamps and user/kernel CPU time, retained after exit through the process capability. |
| Memory inspection | Native structured inventory, logical private commit charge, current/peak counters and actual PMM/kernel allocator facts. Logical charge is not a new Windows admission guarantee; foreign-process memory inspection remains unsupported. |
| File mutation | Confined atomic regular-file rename/replace/backup and readonly metadata, stable file identity and rollback. Open references are explicitly refused instead of silently retargeted. This is not power-loss journaling or full NT file sharing. |
| Affinity/shutdown | Real CPU eligibility/placement/migration; actual shutdown participant ordering. Windows affinity is limited to its exposed 64-CPU processor group, without reducing native CPU support. Unsupported shutdown flags fail. |
| Sparse Windows VM | Separate reserve/commit/decommit over native process_memory, zero on recommit, lazy materialization, subrange queries/protection, rollback and SMP invalidation. The 32-GiB reservation test does not commit 32 GiB. No RWX exception. |
| Loader | Extensionless DLL names, an explicit known fiber API-set mapping, real executable and DllMain entry traces. API-set names are not accepted through a catch-all prefix heuristic. |
| RNG/clock | ProcessPrng consumes the native RNG; timeGetTime reads the real monotonic clock with DWORD wrapping. No fake entropy or fictitious multimedia timer-resolution service. |
| Registry | Eight Win32 Reg* W entry points reuse the per-user transactional NT hive. Deletions update serialized length/checksum before publication; deleted identities do not resurrect on reopening. No host registry is used. |
| Loader lock | PEB.LoaderLock at AMD64 offset 0x110 references the actual recursive synchronization object. DllMain and thread notification ownership use that same object, not a separately maintained observable mirror. |
| Security data | Real SIDs, self-relative/absolute descriptors, DACL/SACL distinction, allow/deny/audit/object/mandatory-label ACE encoding, ACL merging and independent LocalAlloc ownership. CURRENT_USER resolves to the native user's S-1-22-1-UID SID, consistent with the registry. These operations serialize data; they do not enforce an ACL on a native resource or mint a token. |

## Observed causal execution frontier

1. The original eight root misses are resolved; genuine chrome_elf DllMain
   returns TRUE and the EXE entry is reached.
2. ProcessPrng and the large sparse PartitionAlloc reservation work, followed
   by genuine WINMM and ADVAPI32 loading.
3. The earlier null PEB.LoaderLock dereference is fixed by exposing the real
   loader critical section, validated by recursive and multi-threaded tests.
4. Crashpad's real SDDL is parsed; BuildSecurityDescriptorW and SetEntriesInAclW
   produce independent merged descriptors. Windows reference tests exposed that
   REVOKE_ACCESS removes grants/audits while retaining denials, and that success
   and failure audit modes are separate values, not an invented mode 7.
5. Crashpad requests a local message-mode named pipe with SECURITY_ATTRIBUTES.
   This remains explicitly rejected by the current pipe provider. Capturing
   the descriptor as bytes is insufficient to enforce it on native discovery
   and connections. Inheritance of named-pipe provider handles is also not
   yet implemented; existing anonymous pipe/file inheritance is not evidence
   of named-pipe inheritance.

The relevant upstream contracts are [TRUSTEE_W](https://learn.microsoft.com/en-us/windows/win32/api/accctrl/ns-accctrl-trustee_w),
[ACCESS_MODE](https://learn.microsoft.com/en-us/windows/win32/api/accctrl/ne-accctrl-access_mode)
and [Crashpad's registration protocol](https://chromium.googlesource.com/crashpad/crashpad/+/refs/heads/main/util/win/registration_protocol_win.cc).
The VM's own error text and PE unwind establish the executed frontier; upstream
source aids interpretation rather than substituting for execution.

## Security boundary that must not be bypassed

`kernel/src/rendezvous.c:vex_rendezvous_call` currently enforces root scope,
root discovery/publish rights and attenuated endpoint rights. It does not
evaluate per-client identity policies. `vex_fs_root_provider.inc` permits
explicit transfer of roots carrying FS_ROOT_TRANSFER; grant_uid represents
delegated filesystem authority, not a per-connection DACL check.

Checking a Windows descriptor only in pipes.c would leave native CLAIM calls
able to bypass it. Accepting SECURITY_ATTRIBUTES while ignoring its descriptor
or inheritance would be false support. No such workaround was added.

A native generic identity-conditioned resource admission policy, or an isolated
userspace broker that alone holds the rendezvous authority, would require a
security/authority contract decision. Windows SID/ACL interpretation must remain
userspace in either design. A current-user gate alone would not implement
restricted tokens, application packages, integrity levels or impersonation.

## Validation scope

Raw logs live under `build/windows/`. `security-merge-4-cpu-20261017-155934.log`
validates the ACL merge fixture then observes Chrome's next genuine failure.
`security-merge-host-unrestricted.log` passes the host reference suite, including
SID/SD/ACL tests, file mutation, sparse VM, registry, loader lock, IOCP, suspend
contexts, NLS and encoding. An earlier sandboxed reference run failed with
ACCESS_DENIED on host MoveFile instead of the expected already-exists error;
the same suite passed with ordinary host filesystem permissions. That failed
run is not counted as passing evidence.

The CURRENT_USER fixture was additionally compared with actual Windows after
the full host suite: `build/windows/current-user-host-reference.log`.

The same final ISO passes the **complete native/Windows/Linux runner** and
compiled export census in both configurations:

- `current-user-final-1-cpu-20261017-160418.log`;
- `current-user-final-4-cpu-20261017-160632.log`.

Both observe actual Chrome entry and the same first-instance pipe rejection:
access `0x80003`, message/read mode `0x6`, two instances, 512-byte input/output
buffers, default timeout, a 24-byte SECURITY_ATTRIBUTES with non-null descriptor
and inherit=TRUE. There is no remaining BuildSecurityDescriptor error in either
trace. Crashpad retries five times then emits the fatal CreateNamedPipe error
50 and reaches chrome_elf.dll+0x1ff162. The trace is a diagnosed failure, not a
Chrome functional PASS. No kernel panic is present in these runs. Guest shutdown
syncs files and parks the other CPUs.

The final runner includes native GS, TLS/FPU, Linux fault return, W^X and memory
ownership, in addition to PE/thread/process/file/registry/descriptor fixtures.
Six `pe_graph_test.py` cases and `git diff --check` pass. The recursive compiled
inventory has 11 modules, 2900 import edges and no parse errors; the root EXE and
chrome_elf static imports have zero missing edges, while versioned chrome.dll
still has 242 static and 754 delayed missing edges. Dynamic calls and the
partially implemented contracts of registered exports remain separate work.

Image: `build/kyilos-windows-process.iso`.
SHA-256: `6F0F419B2A9FA5575388DE6DB5FA26A5168016702BC7EA4391CD94A37C5E6239`.
Kernel SHA-256 unchanged through these userspace security batches:
`DC238A1C097F06583C02490F6EDEE8A83C907B48271336428798E4C0CB794A8C`.
No physical Dell validation is claimed here.
