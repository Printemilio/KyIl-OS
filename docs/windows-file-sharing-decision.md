# File-use authority required by the real Chrome DLL probe

Status: **superseded by the user's native resource protection decision**.
KSW (KyIl Subsystem for Windows) now projects readonly `share=3` opens onto
native namespace protection, orchestrated by Ultra-Space. Neither full Windows
leases nor an exclusive second file authority is selected. The alternatives
below are historical, not pending decisions. See the actual contract and
validation in [native file protection](windows-native-file-protection-2026-10-18.md).

Evidence: `build/windows/message-windows-final-4-cpu-20261018-020140.log`.

```
A:\155.0.8059.84\chrome.dll
CreateFileW(access=0x80000000, share=3, disposition=3, attributes=0)
caller=0x14003de22
```

The current userspace adapter rejects `share != 7` with ERROR_NOT_SUPPORTED
before opening the native file. Chromium's `ModuleCanBeRead` therefore returns
false and selects the development fallback `A:\chrome.dll`, which genuinely
does not exist. `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)` now accepts
the flag and returns ERROR_MOD_NOT_FOUND for that wrong fallback path. The real
302,364,312-byte versioned DLL is present in the portable staging image.

`share=3` allows concurrent read/write opens but forbids delete/rename while the
open remains alive. Accepting it as `share=7` would be a false contract. Native
root capabilities confine paths and pin directory anchors; those pins are not
file-use sharing leases. The byte-range lock API does not provide this contract
either. Root/capability permission checks remain necessary independently.

## A — generic native file-use leases (recommended)

- Add explicit, delegable/attenuable authority to acquire resource-use claims.
  An ordinary read capability must not silently become unrestricted authority
  to deny other principals' operations.
- Claims describe generic uses (read, write, namespace rebinding), independent
  of Windows constants. KSW translates Windows desired access and sharing rules.
- Admission must be atomic with confined resolution/open and any create/truncate
  side effects; reject conflicts before changing data. Use real object/name
  identities, not client pathname strings as proof of identity.
- Enforce claims at native/ELF/legacy open and namespace mutation boundaries.
  An application must not bypass them by invoking another native API directly.
- Separate backing lifetime from lease/open-view lifetime. Duplication,
  explicit transfer, inheritance, async I/O, mappings, close and process death
  must retain/release the appropriate distinct references exactly once.
- Test cross-process/native bypass, read/write/delete/rename conflicts,
  duplicate/inherit/creator death, rename/open races and OOM rollback.

This extends the public native resource/capability contract. No Win32 or
FILE_SHARE_* values belong in the kernel.

## B — exclusive userspace file mediator

Ultra-Space owns the relevant roots; Windows clients receive only mediated
object views. Open and mutation operations for the protected namespace must all
go through that service. Leaving clients their current broad direct root
authority would permit a bypass and is not a valid implementation.

This keeps sharing policy in the service but changes current root delegation
and the runtime file path substantially (including registry/profile services,
inheritance, mappings and async I/O). Merely adding a cooperative lock catalog
while retaining unrestricted direct access is not this option.

## Independent outstanding frontiers

Crashpad also requests a non-message-only session-notification window and its
database's parent profile directories do not yet exist. Neither is repaired by
lying about file sharing. Chrome's broad `chrome.dll` import inventory still has
many unimplemented contracts; its entry and GUI have not been reached.
