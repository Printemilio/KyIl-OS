# Suchu browser: cursor and shutdown correction

## Causes established in the guest

`gui.c:compositor_present()` restored the cursor's background **before** copying
the dirty scene to scanout, then composed the cursor afterwards. With Raylib's
continuous real frame presentations, the cursor was absent for each copy:
399 presentations / 388 ms cumulative absence, maximum 1.93 ms; later 672 /
669 ms, maximum 2.03 ms. No lost damage or stale WebKit backing was required.

Shutdown tracing isolated the main pause to final process storage retirement.
One WebProcess spent 320 ms in mappings; the NetworkProcess spent 160 ms.
A second run found 1,080–1,082 local flushes and 115 ms in VMM unmaps per runtime.
These range/page retirements ran under the process-memory gate, often from a
scheduler path with IRQs excluded, delaying GUI composition and input service.

The desktop launch also left Suchu parented to GUI without a wait-status
consumer. Its zombie retained storage. Once corrected, a further 334-ms cost
became observable: ORA's last releases freed raw allocator kernel chunks using
one global SMP shootdown **per page**.

## Changes

* `compositor_present` includes the cursor in synchronous scanout damage copy.
  A private 17x17 overlay transaction saves/restores the cursor-free scene;
  it never changes a producer/app Surface. Old/new cursor damage is retained.
  Cursor movement-only presentation remains small; no permanent repaint/timer.
  `gui_tick` avoids composing it again after a presentation already did so.
* `process_memory_reap_mappings` uses `vmm_retire_user_mappings` only at final
  group storage retirement (all threads and borrowed storage refs are gone).
  VMM closes the root, detaches its user half, acknowledges one CR3 shootdown,
  then releases detached tables/leaves. Inventory determines private vs
  backing-owned leaves, including unpublished namespaces and PROT_NONE.
  Shared/ORA/file/JIT backing refs are subsequently released by their owners.
  No allocation is required to detach tables. An existing competing walk uses
  the previous range-retirement fallback. Root metadata remains reserved until
  explicit address-space destruction and final active-CPU reference release.
  `exec`, ordinary `munmap`/`mprotect` and live address spaces keep their contracts.
* `kernel_allocator:release_chunk_backing` retires a complete kernel VA chunk via
  `vmm_unmap_kernel_range` before recycling any physical page. One acknowledged
  all-CPU invalidation replaces the per-page IPIs. Shared kernel table topology
  is retained. UP explicitly invalidates locally; huge-page ranges are rejected
  before modification. A rejected extent's physical backing is not recycled.
* Desktop browser launch uses existing `scheduler_detach_process`: the desktop
  needs no wait status. Browser-owned WebKit children retain their usual parent.
* Routine SIGKILL/SIGTERM stack-window dumps now require `VEX_COMPAT_TRACE`;
  fatal-signal diagnostics remain. No signal behavior changes.

No browser GUI/WebKit/Raylib rendering architecture, public ABI or W^X changes.
The private cursor overlay relies on the existing **synchronous** framebuffer
driver present contract; an asynchronous scanout driver must retain/composite
its cursor plane through its own completion contract.

## Validation evidence

Artifacts under `build/suchu-browser/`:

* `desktop-before.log`, `desktop-before-detailed.log`: baseline phase timings.
* `desktop-final-trace.log`: three browser sessions in the same 4-vCPU boot,
  HTTPS/example.com, file document, then New Page. Child shutdown waits measured
  75 / 65 / 39 ms; texture/view cleanup 12.3 / 12.7 / 0.02 ms. Suchu's final
  mappings retirement fell from 334 ms to about 2 ms. Runtime mappings are still
  real work (roughly 12–52 ms in this run), not fake completion.
* `cursor-samples/results.json`: 48 captures during animated real Web content,
  no missing/partial cursor and no capture retries. `cursor-counter`: 24 captures
  on Suchu Counter; `cursor-real-close`: 32 through a real browser close, no
  missing cursor and successive requested mouse displacements observed.
  Wrong setup coordinates/type/injection attempts are not counted as closes.
* `desktop-after-render.png`, `final-https-painted.png`,
  `reopened-file-drag.png`, `browser-drag-final.png`: actual WebKit pixels,
  HTTPS navigation, DOM click=1 / focus=1 / key=2 / value=ok, resize and movement.
  `counter-click-final.png`: independent Suchu/Raylib Counter reaches Count: 1.
* `desktop-final-top.png`: after HTTPS close, 6 system tasks, no browser/WPE,
  99.7% idle on 4 CPUs. No kernel fault/panic observed in these runs.
* `cursor_overlay_test.c` hosted PASS: all screen positions, edge clipping,
  fresh background and scene restoration, no cursor erasure during copy.
* `desktop_retirement_test.c` guest PASS: six fork/exits leaving dense 12-MiB
  pages, sparse 128-GiB reservation, PROT_NONE and shared backing; parent still
  accesses shared bytes after every child exit.
* `kmaptest` guest PASS: six threads, two CR3s, late root, shared kernel tree,
  NX/USER isolation, bulk unmap preserving the neighboring leaf, 32 root cycles.
* Final normal payload on **1 vCPU**: both guest retirement/kmap regressions
  PASS again; real animated Web document rendered; 24 additional cursor captures
  without retries, all complete. `normal-up-closed.png`: after browser close,
  six system tasks, 99.8% idle, no WPE/Suchu process left. Diagnostic messages
  are absent from `desktop-normal-up.log`.

The final payload is rebuilt with tracing off. `VEX_DESKTOP_TRACE` defaults to
zero; enable it for the three kernel instrumentation objects and use
`VEX_DESKTOP_TRACE=1 scripts/build-suchu.sh` plus the browser build for diagnosis.
This is a targeted desktop/lifetime regression run, not a full SMP/Vulkan CTS
or long-run memory-leak certification.
