# Windows compatibility: shared foundations (12 October 2026)

The exact input frontier was 74 absent static imports in `chrome_elf.dll`.
This work groups contracts by implementation, rather than adding unrelated
exports and rebooting after each one. Source registrations are candidates;
only guest resolver logs establish the remaining static frontier. Export
presence is never a claim that all flags/variants are supported.

## Implemented foundations

* **Files and paths:** directory searches with generation-checked handles,
  bounded DOS wildcard matching, derived/pinned directory capabilities,
  metadata through the same anchor, real file IDs/types, truncation/extension,
  fixed compatibility volume identities, long/full path queries and standard
  handle assignment. `ftruncate` had bypassed its file's root authority; its
  backing write now uses the retained root/credentials, as flush already did.
  Missing creation timestamps and 8.3 aliases are not fabricated. Long path
  conversion preserves existing names because this volume has no short-name
  namespace. General Win32 case-insensitive path resolution remains incomplete.
* **NLS and calendar:** installed en-US/invariant data, actual UTF-8 code-page
  profile, BMP character classification and simple case mapping, locale
  information, date/time formatting, Gregorian conversion, UTC profile and
  supplied northern/southern DST rules. Reference tables contain data from
  public Windows NLS calls, with host/version/hash provenance; no Windows DLL
  or Windows executable code is staged into the guest. Supplementary casing,
  linguistic sort keys and other locale profiles fail explicitly. Some format
  flags (minute/second/AM-PM omission and era formatting) remain unsupported.
* **SEH:** reuse the PE `.pdata/.xdata` virtual unwinder for software exception
  delivery, compiler-generated C SEH, filters, cleanup/finally, target/exit
  unwind and context resume. Assembly captures the PE caller before entering
  the native runtime; no ELF frame is pretended to be PE. A software exception
  within a native fault handler retains the outer native event. Unwinding back
  to the application stack abandons that event through existing `EVENT_LEAVE`.
  Context execution/stack permissions are checked. Dynamic function tables,
  collided/nested handler dispositions, C++ EH personalities and LONGJUMP /
  UNWIND_CONSOLIDATE restore protocols are not implemented.
* **SLIST:** actual AMD64 header layout and atomic `CMPXCHG16B` push/pop/flush,
  depth and initialization. Pop retries a failed safe Next read only when the
  public header changed. Nodes are not read after successful publication by
  push. CPU support is checked; CPUs without CX16 receive explicit unsupported
  failure for these atomic operations. No unsafe emulation or unsupported
  instruction is introduced into the old-CPU kernel/boot path.
* **Machine metadata:** module metadata uses actual loaded images; computer
  names derive from native `uname`, including an empty unconfigured DNS suffix.

## Registry model

`registry.c` is entirely userspace, scoped under the existing per-user C: root.
The native layer sees files and resource-group advisory ranges, never NT keys.
The new tagged `FILE_RANGE/230` interface reuses the existing range/wait engine;
its owner is the Process resource group and closing any descriptor of the file
releases that group's ranges. It does not claim Windows mandatory/per-handle
file locking. Linux locking continues through the same internal engine, with
transactional input/output copies and no user pointer retained over a wait.

The hive has two alternating checksummed snapshots, monotonically assigned key
IDs, retained deletion tombstones, opaque local generation-checked handles and
explicit access masks. Value identity is separate from contents. Names are
matched without case and display spelling is stored. The current-user NT path
uses the POSIX-interoperable SID mapping `S-1-22-1-<native UID>` inside this
private prefix. Machine/User roots are actual empty hive state; Windows values
are not invented. Separate processes serialize durable mutations through a
native scheduler wait. Normal peer teardown resets an abandoned userspace gate
only after peer CPU quiescence, before DLL detach.

Supported NT operations: create/open/delete key, set/query value (basic/full /
partial / align64 layouts), current-user path and close through existing object
dispatch. Nonempty classes, custom security descriptors, volatile/link keys,
WOW32 views and inheritance are explicitly unsupported. ACL policy is the
private native user's prefix ownership plus per-handle rights; this does not
claim a full Windows security descriptor engine. Notifications, enumeration,
transactions and the high-level ADVAPI32 wrappers remain future work.

Two slots protect the previous generation while overwriting the inactive one;
this is not an assertion of a complete power-loss journal for VexFS. Media
failure, torn-write injection and exhaustive OOM/cancellation recovery still
require dedicated validation. The current implementation reloads/checks the
hive per operation and performs linear lookups; large hives need indexing.

## Evidence

* `windows-profile-reference`: 427,440 comparisons against real Windows for
  BMP classification/case, calendar, date/time patterns and DST conversions.
* The same compiler-generated SEH and SLIST PE fixtures execute successfully
  on Windows as reference tests, without staging host Windows libraries.
* `windows-coherent-seh-4-cpu-20261012-165029.log`: files/NLS/SEH and full existing
  native/Windows/Linux runner pass; compiled census confirms **74 → 45** absent
  imports. Real Chrome still refuses to load and has not reached its entry.
* An earlier file-batch failure exposed confined `ftruncate` bypass; corrected.
* `windows-coherent-registry-4-cpu-20261012-173738.log` retains the first registry
  failure: native ELF `wchar_t` literals were mistakenly cast to UTF-16. Runtime
  literals were changed to explicit UTF-16 `u"..."`.
* `windows-coherent-registry-4-cpu-20261012-174523.log`: registry with 4 real
  processes/96 values, SLIST with 8000 exact-once nodes and nested SEH fixtures
  pass, as do the runner's test paths. The harness times out on an interleaved
  final UART marker; this orchestration failure remains in the raw record.
* `windows-coherent-final-1-cpu-20261012-175227.log`: 97 preexisting registry
  values read **before writes after shutdown/reboot**, full runner and compiled
  census pass. Real Chrome confirms **74 → 36 absent imports** (38 addressed
  through shared foundations), still before entry. The exact 94-import matrix
  is refreshed from this guest census; 58 of the original 94 now resolve.
* `windows-coherent-final-4-cpu-20261012-175504.log`: the same reboot readback,
  full runner, compiled census and real Chrome frontier pass with the corrected
  SMP log recognizer. Both final VM runs complete through native shutdown.
* Recursive PE inventory rebuilt: 364 modules, 100,709 edges, 766 API sets,
  zero parse errors. These reference/transitive edge counts are separate from
  the exact 36 remaining `chrome_elf.dll` imports. PE graph unit tests: 5 pass.

Final image: `build/kyilos-windows-process.iso`, SHA-256
`15194A3E85C5CB903A535963FE02836F6295071F2EB53AA94155861A66C6B420`.
Kernel: `90995AB61BCE5C9BFC53974DC3B84B735A21C05427F53D07CA7653B3787D1448`.
Userspace runtime: `A1EC02CDD4B968C2C3CA434E09D61B4FE50B6A16EF6973CBBEDFF453ED160470`.

## Next batch leverage (work remains open)

**Continuation 2026-10-16:** native counted pause / user-boundary snapshots /
execution-cycle accounting now underpin real same-Process Windows thread
control. Final 1/4-vCPU regressions and unchanged Chrome confirm **36 -> 32**
absent imports. A completion/cancellation race exposed by the SMP suite was
corrected. See [the execution-control batch](windows-thread-control-batch.md)
for its exact contracts, limitations, failed run, final raw evidence and hashes.
The earlier image hashes above identify the historical checkpoint.

1. **Native execution-context control and observation** shared by suspend,
   thread context, accounting and process supervision/job contracts. Current
   legacy priority metadata must not be advertised as actual scheduling policy.
2. **Memory reserve/commit/discard and facts:** distinguish address reservation,
   materialization, commitment guarantees and physical accounting before
   exporting Windows memory-status numbers or remote-memory rights.
3. **File/pipe observation and locking:** share one resource lifetime/peek /
   transaction foundation. The new group-advisory ranges deliberately are not
   represented as Windows per-handle `LockFileEx`.
4. **Console + diagnostics/NLS completion:** Unicode I/O, actual console modes,
   formatted messages, additional locale/collation and version-policy contracts.
   Valid-but-unrepresented operations must continue to fail explicitly.

These are implementation/dependency batches, not proposals for another kernel
architecture. Chrome entry and dynamic execution remain the end-to-end goal;
this checkpoint does not declare that goal complete.

The intermittent preexisting native-execution failure is not declared fixed
merely because it passed these runs. The Dell's timer/boot paths and desktop
application sources are unchanged; no new physical Dell validation is claimed.
