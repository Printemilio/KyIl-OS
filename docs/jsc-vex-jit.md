# JavaScriptCore: native Vex dual-alias JIT

## Contract

The port uses the existing tagged native `JIT_ARENA_CREATE/DESTROY` calls from
the Linux-hosted engine. It does not reinterpret Linux RWX mmap requests.
VSL still rejects WRITE|EXEC with EACCES.

An arena reserves a sparse backing with a RW/NX view and a RX/RO view. JSC's
executable handles, branch displacements, inline-cache locations, published
function pointers and disassembly addresses all name RX. Only stores are
translated by offset to RW. There is no interval with a writable executable
PTE. Both views have PROT_NONE guard pages.

`ExecutableAllocator` retains its upstream allocation/tiering/free algorithm.
`VexJITReservation` replaces only the platform page reservation. Logical commit
does not eagerly allocate RAM. Faults materialize the same physical page for
either alias. Quota counts physical backing pages, including pages retained
across fork, rather than the reserved VA or number of alias PTEs.

The common `performJITMemcpy` separate-heap hook performs writes, retaining
JSC's atomic slot-copy semantics. x86 link stores, jump replacement,
invalidation, nop filling and inline-cache rewrites must also use that hook.
The generic Memcpy repatching flag may describe a temporary assembler buffer
or a pool destination: the hook distinguishes those using JSC's actual pool
range, not syscall ABI heuristics. Temporary buffers still receive normal
copies. x86's existing cache-flush operation remains a no-op, consistent with
its coherent instruction/data caches; upstream publication/synchronization
remains in place.

Decommit uses the existing native MADV_DONTNEED operation, extended to remove
both local aliases' PTEs for the same backing offset. It releases physical
pages when all backing references are local inventory records. A fork or
namespace snapshot retaining the backing prevents physical release beneath a
foreign PTE. In that case local decommit is advisory. Arena destruction removes
both views and releases their references; process teardown retains its existing
ownership rules.

## Audited upstream gaps

The staged engine was WPE 2.53.3. Its source/build directory was absent. The
rebuild uses the official WPE 2.53.3 release archive with SHA256
`645babbc04b408b3ce3b026f1990e6307668aa41bf2fb860f387de72a18feb33`.

Setting only the separate-heap hook was insufficient. The first real `jsc`
boot faulted at ELF offset `0x17e1466`, in
`JSC::handleExceptionGenerator(JSC::VM&)`, writing an absolute call target to
the RX alias. x86's `setPointer/setInt32/setInt8`, several inline-cache rewrite
paths and Memcpy-marked machine-code copies previously bypassed that hook.
These stores now enter the same checked write path. RX addresses remain the
base of relative relocation calculations.

`scripts/webkit/apply-vex-jit.py` checks the exact upstream source patterns,
keeps original files beside the patched source, and is idempotent. A changed
upstream implementation fails the patch rather than guessing replacements.
The small ABI headers preserve the existing native wire numbers and layouts;
they avoid importing Vex libc declarations into hosted engine C++ headers.

## Rebuild and validation

Run `scripts/build-jsc-vex.sh` under the explicit usable WSL distribution.
The persistent default source/build location is `/root/vex-webkit`; `/tmp`
must not be used for the engine build because it is cleared between WSL runs.
Baseline, DFG and FTL are built. WebCodecs retains its real VideoFrame/video
dependency. Staging with `VEX_REQUIRE_JSC_JIT=1` requires the rebuilt engine,
helpers, injected bundle and JSC shell; it refuses the old engine fallback.

`linux_jit_alias_test` covers a lazy 1 GiB reservation with a two-page quota,
compiler-thread write/main-thread execution, patching, forbidden RX stores and
RW instruction fetches, generic Linux RWX rejection, paired decommit,
fork/parent destruction and stale handles over 16 cycles.

`linux_jsc_jit_test` runs the real JSC shell with normal tiering heuristics in
Baseline-only, DFG, FTL, and concurrent-FTL configurations. The workload verifies
each arithmetic result, exercises polymorphic inline caches and RegExp JIT,
requires tier compilation logs, and checks execution of JSC's final-tier
intrinsic. Compilation logs alone are not sufficient.

Navigation timing reads standard Navigation/Paint Timing once per completed
real document. It does not modify the document or substitute its resources.
Diagnostic tier logs are opt-in; timing runs must omit them to avoid serial
logging altering the workload.

## Baseline and current evidence

The previous real browser run is preserved in
`build/browser-no-jit-baseline.log`: TTFB 620 ms, main transfer 360 ms,
DOMInteractive 87030 ms, FCP 172360 ms, cold navigation 316260–408010 ms.
The reported "warm reload 10210 ms" was **not** a successful reload: the
same baseline log explicitly records a DNS temporary failure, no commit,
zero bytes and a failed main resource. LOAD_FINISHED is a terminal lifecycle
event even after failure. It cannot serve as a cache benchmark. These are
workload observations, not a universal network or CPU baseline.

The native alias regression and existing security/lazy/SHM/signal/process
regressions passed in VM. The first JSC tier test correctly failed on the RX
linker store described above. After the common write-path repair, all four
real-JSC configurations passed (`build/jsc-integrated-regressions-vm.log`), checksum
-1250079968 in each mode (600000 verified hot calls). DFG and FTL were actually entered, including concurrent
FTL: the `isFinalTier` intrinsic returned true from the tested hot function.
Ring-3 instruction-fetch faults materialized RX PTEs naming the same backing.
The generic `gpu_test: surface present failed` and `linux_file_mmap_smoke`
exit 117 remain visible; this run does not claim the entire harness green.
The latter passed shared/positional-I/O checks but failed in its subsequent
128 MiB file stage; that unrelated filesystem failure is not concealed.

The actual WPEWebProcess diagnostic run also recorded Baseline compilation
and RX instruction fetches in its native arena, preserved in
`build/jsc-browser-diagnostic-vm.log`. This confirms browser JIT use; the
shell tests separately prove execution of the available optimized tiers.

## Measured kernel bottlenecks after enabling JIT

The first production (tier logs disabled) JIT-only navigation took 625800 ms.
It therefore did **not** improve the historical baseline. Standard timing
reported TTFB 350 ms, transfer 11940 ms, DOMInteractive 180460 ms,
DOMContentLoaded 246290 ms, DOMComplete 592240 ms and FCP 247560 ms.
Evidence: `build/jsc-browser-jit-only-vm.log`.

Read-only VirtualBox RIP samples were symbolized with the exact kernel used
at sampling time. These are statistical snapshots, not precise CPU-time
accounting: 120/200 were in MRAC `find_entry`, 28/200 in PMM's bitmap search.
Preserved raw samples and symbols: `build/jsc-navigation-rip-only.txt` and
`build/jsc-navigation-symbols.jsonl`. CS/CR3 from the first exploratory sample
are not treated as atomic with RIP; the final sample used RIP alone.

`process_memory`'s numeric region keys preserve the low bits of page-aligned
addresses. MRAC's old `hash % 1024` index consequently placed a namespace's
regions into a single linked list. Only `index_bucket` now avalanches the
key before indexing. Stored keys, namespace identity, equality, object
content hashing, ownership and versioning are unchanged. The host test uses
the actual index function: 8192 aligned keys occupy all 1024 buckets, with
largest chain 19. VM exercises 2048 aligned keys through real store, modify,
lookup, borrow/release and remove.

JIT + index correction navigation took 303490 ms, DOMInteractive 93990 ms,
FCP 133540 ms (`build/jsc-browser-mrac-vm.log`). The next active-phase sample
then found 72/100 RIPs in PMM's allocation scan
(`build/jsc-mrac-navigation-symbols.json`). Allocation previously rescanned
all low reserved/occupied RAM for every page. It now uses a circular search
cursor, preserving the bitmap as authority and allowing a contiguous free
run to straddle the cursor. The free-range bound also rejects integer
overflow. No allocator ABI or memory ownership model changed.

`userspace/linux_tests/pmm_allocator_host_test.c` includes the actual kernel
implementation and checks it against independent ownership accounting over
100000 transitions, wrap, fragmented/exhausted RAM and runs crossing the
cursor. Compile with host clang, `-idirafter kernel/include` and
`-Ivendor/limine-protocol/include` (not kernel's freestanding stdio).

Final integrated regression evidence is in
`build/jsc-final-integrated-regressions-vm.log`. Baseline, DFG, synchronous FTL
and concurrent FTL all passed, as did the native dual-alias lifecycle,
physical quota, W^X, fault/signal, thread/process, SHM and IPC regressions.
The two unrelated harness failures listed above remain visible.

## Final live-page timings

`build/jsc-final-browser-vm.log` records the production engine (no tier logs):

| Metric | Historical baseline | Final run |
| --- | ---: | ---: |
| TTFB, pomme | 620 ms | 351 ms |
| Main response transfer, pomme | 360 ms | 1769 ms |
| DOMInteractive, pomme | 87030 ms | 44009 ms |
| First contentful paint, pomme | 172360 ms | 56259 ms |
| Navigation, pomme | 408010 ms | 146860 ms |
| Navigation, poire fruit vitamines | 316260 ms | 126250 ms |
| Same-process reload, pomme | 10210 ms **DNS failure** | 113000 ms, success |

Poire's Navigation Timing reports TTFB 541 ms, transfer 169 ms,
DOMInteractive 30869 ms and FCP 37500 ms. Actual native-browser screenshots
confirm both remote documents rendered, not just LOAD_FINISHED events.
The site's legacy timing telemetry reports pomme DOMInteractive 40620 ms;
the new standard Navigation Timing probe reports 44009 ms. They are retained
as different time-origin measurements, not silently interchanged.

The probe now labels committed/failed/no-commit explicitly, reads timing only
for a successfully committed document, and discards replies from a superseded
tab generation. It cannot label the old DNS failure a successful warm reload.
The final helper was rebuilt and booted: the real file render document
committed in 3020 ms; a nonexistent file ended with `status=failed`, zero
commit and **no** navigation-timing result for that generation. Evidence:
`build/jsc-final-telemetry-vm.log`. Final validation VM: 1 vCPU, 4096 MiB.
The successful reload above did transfer a fresh main response (7994 bytes);
it is not asserted to be a fully cache-served navigation.

After MRAC/PMM corrections the next active-phase sample no longer has those
two scans at its head: 18/100 RIPs in syscall entry, 16/100 in FS-base restore,
19/100 userspace/unsymbolized (`build/jsc-final-navigation-symbols.json`).
This identifies the next investigation boundary, not a proven root cause of
all remaining latency. The existing renderer also explicitly rejects some
Skia fragment shaders (profile line 1117); those failures predate this port
and remain visible. SQLite/cache warnings remain, too.

These measurements use real live DuckDuckGo pages; they are not a controlled
replay of identical response bytes. Gains must not be attributed to JIT alone.
