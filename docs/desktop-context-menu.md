# Desktop context-menu composition correction

## Reproduced causes

The failure was reproduced in VirtualBox with Editor, Browser and GFM before
changing the composition path. Captures are in `build/context-menu/`.

1. `draw_context_menu()` used the **current cursor** as its origin on every
   redraw. `gui_compositor_restore_foreground()` invoked it on every app frame.
   Moving the mouse therefore moved the menu without restoring its previous
   position. Raylib's frequent presents produced the staircase; GFM's less
   frequent presents did not.
2. `vex_lvgl_overlays.refresh()` called `lv_refr_now()` on the shared shell
   display. Moving an object in the LVGL stack invalidates its parent (`lv_obj_move_to_index()` in `vendor/lvgl/src/core/lv_obj_tree.c`), here the shared screen. LVGL repainted sibling window roots, including their opaque bodies,
   after `app_surface_render_selected()` had composed application pixels.
   Those application pixels belong to independent Surface backings, not LVGL
   children. The resulting black/obsolete screen content was a **composition
   error**, not loss of the application's logical state or backing.
3. Whole-window repaint on every content presentation also blended rounded,
   antialiased chrome repeatedly onto the already-composited scene. A strict
   mono-vCPU pixel comparison detected 42 drifting corner pixels after the
   first two corrections. Restricting composition to actual client damage
   removes this accumulation.
4. Desktop icon hover repaint did not account for native app surfaces or the
   menu. Resize affordances were painted after overlays. These ordering paths
   now respect occlusion and the window-before-overlay order.

## Changes

- `gui_overlays.inc`: opening chooses and clamps a retained anchor; painting
  and hit testing use that same anchor until the menu is closed/reopened.
- `gui.c`, `vex_lvgl_ui.c`, `vex_lvgl_overlays.c`,
  `vex_lvgl_browser_ui.c`: LVGL rasterizes only the requested widget subtree
  into the existing 64-row scratch buffer. Vex controls layer ordering.
  No sibling window is refreshed as a side effect of painting an overlay.
  Rendering follows LVGL's offscreen layer/draw-task lifecycle; it does not
  introduce per-paint framebuffer allocations or an additional render engine.
- `app_surface.c`: content presents invalidate the client, not native chrome.
  Window creation explicitly composes the native frame once, before client
  updates; GFM no longer depends on its first present implicitly painting it.
  Occluding app windows and overlays are recomposed within the transaction's
  damage. Full scene rebuilding still paints overlays last, including on a
  window/hover state change.
- `compositor_surface.c`, `gpu.c`, `gpu_driver_software.c` and internal headers:
  pass a composition scissor through the **existing** driver path. It bounds
  writes without changing source sampling coordinates, backing, stride,
  capability checks or lifetime. Empty intersections produce empty damage.
  Native userspace syscall/Surface structures and public graphics ABI are
  unchanged; this is a kernel-internal composition parameter.
- Desktop icon damage checks include native app surfaces and the menu.
  Resize affordances are composed below overlays.

There is no polling, artificial delay, forced redraw on each mouse event,
application-specific bypass, or stored screenshot used as a replacement frame.

## Validation

- Kernel rebuilt with clang/ld.lld and ISO repacked with the rebuilt kernel.
- Real PS/2 input and real framebuffer captures in VirtualBox, 1 and 4 vCPU:
  Editor, GFM (non-Suchu/LVGL), Browser; menu opening, moving the mouse,
  dismissal, reopening above a window and continued WebKit animation.
- Strict pixel checks: **0 altered pixels** in Editor (excluding only its
  blinking caret) and GFM windows, including rounded corners; **0 residual
  pixels** in the desktop region after moving/closing the menu; menu pixels
  remain stable during subsequent frames.
  The regression also checks native close-control pixels in both captures,
  so an already-incomplete reference image cannot produce a false pass.
- Genuine WebKit fixture `file:///usr/linux-tests/webkit-render.html` continues
  updating while covered by the menu: 6,955 changed content pixels in the
  final mono-vCPU overlap comparison and 20,733 in the four-vCPU comparison, with
  **0 changed menu pixels**. These are real DOM/canvas animation frames.
- Four-vCPU manual drag, resize and Browser/Editor overlap captured in
  `browser-moved-resized.png`, `overlapping-apps-menu.png` and
  `overlapping-apps-dismissed.png`.
  Final ISO checks also cover a menu over a native resize corner:
  `final-hover-before.png` versus `final-hover-on-corner.png` and
  `final-hover-left.png` have **0 changed menu pixels**, excluding the actual
  cursor. Final drag/resize and Editor/GFM overlap captures are
  `final-editor-drag-resize.png` and `final-editor-gfm-overlap.png`.
- `tests/compositor_clip_test.c` executes the actual software compositor:
  **20 cases pass**, covering clipped copy/scaling, opacity, nontrivial pitch,
  negative/offscreen clips and preservation of pixels outside the clip.

Reusable VM regression (desktop with no open windows; default first window ID
assumes a fresh boot):

```powershell
.\scripts\test-desktop-context-menu.ps1 -OutputDirectory build/context-menu/replay
```

Independent checks of saved captures:

```powershell
python scripts/check-context-menu-pixels.py before.png after.png x y width height
```

Host driver test, from the repository root in Linux/WSL:

```sh
gcc -O2 -ffunction-sections -fdata-sections -Ikernel/include \
    -Ivendor/limine-protocol/include tests/compositor_clip_test.c \
    -Wl,--gc-sections -o build/context-menu/compositor_clip_test
build/context-menu/compositor_clip_test
```

The validation concerns the normal software/LVGL desktop. It is not a full
graphics conformance run, an OOM campaign, or validation of future hardware
drivers. Remote network behavior is not changed by this correction.

Final ISO regression captures and logs: 

- build/context-menu/release-1/ and release-4/
- build/context-menu/release-1-results.log and release-4-results.log

Both runs use the same rebuilt ISO. Intermediate failures remain archived separately.
