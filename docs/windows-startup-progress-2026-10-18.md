# Windows startup contracts after Ultra-Space

This checkpoint extends the actual Chrome 155.0.8059.84 startup. It does not
establish a working Chrome window or Windows conformance. PE images still execute
directly; all changes in this pass are userspace compatibility, build and tests.
The kernel image is unchanged (SHA-256
`DEAEF6EC86C16580D2BB020D6CF980BDE34F52D94A2CB7EF1743742D90FB75F8`).

The protected pipe, inherited events and process launch now reach the **second
actual `chrome.exe` entry**, inside the Crashpad child, in both CPU configurations.
The next independently observed failures are the absent
`USER32.dll!FindWindowExW` provider in the parent, and the child's attempt to
create `C:\Users\root\Temp\Google\Chrome\User Data\Crashpad` with a missing
parent directory (`ERROR_PATH_NOT_FOUND`, 3). The child's exit then explains
`TransactNamedPipe` reporting a closed pipe (109). Do not repair that secondary
error by pretending a peer exists, or make CreateDirectory recursively create
unrequested Chrome directories. The exact profile/directory-creation sequence
remains to be established. No USER32/GDI provider or Chrome GUI is claimed.

## Contracts implemented

- `synchronization.c`: `RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO` creates a
  real AMD64 debug record and links/unlinks it in the runtime diagnostic list.
  Actual Chrome requested `0x10000000`, rather than an unknown speculative flag.
- `process.c`: self `OpenProcess(0x1f0fff, TRUE, self_pid)` retains native process
  authority and a separate Windows access mask. Self-memory operations enforce
  the requested Windows rights and existing native protections. Duplication and
  inheritance preserve masks. Foreign-PID memory access remains unsupported.
- `shell_api.c`, `command_parse.h`, `namespace.c`: a real Shell32 provider for
  `CommandLineToArgvW` and the supported `SHGetFolderPathW` folders. Argument
  storage is a single LocalAlloc-compatible block. Paths resolve to genuine
  directories in the existing isolated prefix, derived from the native user;
  supported CREATE requests actually create directories through confined roots.
  Unsupported folder policies/tokens return errors.
- `handle_value.h` and the kernel-object providers: positive signed-31-bit,
  four-byte-aligned Windows handle values, separate from native capabilities.
  Type, 14-bit generation and 256-slot index are encoded explicitly. Exhausted
  generations retire a slot; stale handles cannot revive through wrapping.
  Heap/module pointers and pseudo/predefined handles keep their distinct ABI.
- `process.c`: preserve `STARTF_FORCEOFFFEEDBACK` in the real startup structure.
  KyIlOS does not display a Windows application-starting feedback cursor.
- `files.c`: the three standard streams explicitly supplied by the native
  launcher become inheritable Windows stream handles. Freshly opened files keep
  their requested policy; explicit child handle lists are never widened.
- `event_service.c`, `remote_event.inc`, `objects.c`: anonymous shareable events
  use an actual confined Ultra-Space process. Manual/auto reset, coalesced signal,
  waiter registration, finite/infinite wait, callback cancellation, duplicate,
  inheritance and lifetime are real cross-process operations. State is not copied
  into children. Per-object SEND/TRANSFER endpoints are authority; no global name
  or guessable object identifier grants access. Exported leases keep the service
  alive while an endpoint reference remains.
- `registered_wait.c`: the callback timeout argument is Windows `BOOLEAN`
  (one byte), not `BOOL` (four bytes). The Windows reference test caught an
  incorrect test prototype; both runtime and fixtures now use the real ABI.

## Evidence motivating the changes

`build/windows/chrome-contract-trace-4-cpu-20261017-225236.log` records the
critical-section flag and full self-process mask.

The real `chrome_elf.dll` helper at `0x180085a40` sign-extends a HANDLE through
LONG, compares it with the original, and breaks on mismatch at `0x180085a4c`.
The old enormous wrappers failed this check; disassembly is preserved in
`build/windows/chrome-elf-startup-disassembly.txt`. This is a general Windows
kernel-object ABI requirement, not a Chrome-specific handle exception.

`build/windows/chrome-shared-events-4-cpu-20261017-233635.log` proves six actual
child lifecycles using the new service. Chrome then advances to explicit handle
list item 5 (`0x57000400`, stdin), rejected because the runtime had incorrectly
initialized its supplied standard streams with inheritance disabled. The
ordinary-file exclusion test remains unchanged.

## Tests and honest limits

- Host parser differential: 60,000 comparisons with actual Windows Shell32.
- Handle codec host test; guest creates/uses/closes 16,385 events, verifies LONG
  round-trip and stale-handle rejection beyond the first slot's last generation.
- Startup fixture: parser/LocalFree, actual profile directories, debug-record
  lifetime, self-memory permissions, restricted duplicate and invalid pointers.
- Shared-event fixture: six real child processes, creator closing first,
  manual/auto reset, coalescing, duplicate, protected close, registered callback
  and cancellation. Also executed against real Windows.
- The host suite initially encountered sandbox `ERROR_ACCESS_DENIED` on its
  temporary rename reference; rerunning that exact fixture outside the sandbox
  passes. The failed evidence is retained in
  `build/windows/chrome-startup-events-host-suite.log`.

Shared/mixed `WaitForMultipleObjects`, remote events used as OVERLAPPED completion
targets, named events, non-null event security descriptors, and upgrading a
private local event to inheritable remain explicitly unsupported. They are not
implemented by copying private event state. Broker capacity is bounded by native
FD limits; allocation/resource failures propagate. This is not a complete NT
object manager. No USER32/GDI window implementation is claimed here.

Final execution evidence on the **same image**:

- `build/windows/chrome-inherited-streams-1-cpu-20261018-002846.log`
- `build/windows/chrome-inherited-streams-4-cpu-20261018-002041.log`

Both contain the complete runner, export census, protected pipes, Ultra-Space,
shared events, thread-control, native/ELF/Linux TLS-FPU, W^X, faults and ownership
PASS markers. Both reach two Chrome entries and retain the real next errors.
No kernel panic was observed. The 4-vCPU thread-control smoke was unusually
slow but eventually passed. A read-only VBox core inspection briefly interrupted
subsequent keyboard injection; the partial shell command is retained in the log,
and Chrome was retried from a clean prompt. The harness now rejects keyboard
delivery failures immediately. The temporary 4-GiB diagnostic core was removed.

The host suite passes outside the filesystem sandbox:
`build/windows/chrome-startup-events-host-unsandboxed.log`. Six PE-graph tests
also pass. The refreshed recursive inventory
`build/windows/chrome-startup-compiled-graph.json` has 12 modules, 2,902 edges,
zero parser errors, 242 missing unique static contracts and 766 missing unique
delayed contracts across the broader package including `chrome.dll`. These are
not execution results or a claim that `chrome.dll` has been initialized.

Final image: `build/kyilos-windows-process.iso`, SHA-256
`40CA8EE3387DDD3F27F36B786D190C0FA8BCDCB7C9FD02954AD2A524733AC893`.
Build receipt: `build/windows/chrome-inherited-streams-iso.log`.
The real native event service matches its ISO staging copy, SHA-256
`DA2AF6DBC252E277595F810BACE146ABE588C3EB1EFC47D4593C14D800CB6359`.
No physical Dell run, exhaustive OOM injection or full graphics regression run
was performed in this userspace pass.

## Primary contracts used

- [Microsoft: InitializeCriticalSectionEx](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializecriticalsectionex)
- [Microsoft: CommandLineToArgvW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw)
- [Microsoft: handle interoperability](https://learn.microsoft.com/en-us/windows/win32/winprog64/interprocess-communication)
- [Microsoft: inheritance](https://learn.microsoft.com/en-us/windows/win32/procthread/inheritance)
- [Microsoft: wait callback BOOLEAN ABI](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/ms687066(v=vs.85))
- [Crashpad startup source](https://chromium.googlesource.com/chromium/src/+/master/third_party/crashpad/crashpad/client/crashpad_client_win.cc)

Current upstream source explains intent; actual package trace/disassembly above
determines which branches this Chrome build really executed.
