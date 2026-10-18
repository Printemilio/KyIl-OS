# raylib on Vex

raylib 6.0 runs natively on Vex as `PLATFORM_VEXOS`. It is what Suchu's GUI
(`Window(...) { ... }`, canvases, scenes) draws with, and any OCaml program
compiled on or for Vex can use it through raylib-ocaml.

```
raylib-ocaml 2.2.1 (OCaml bindings, ctypes stubs)
        |
raylib 6.0  rcore + rshapes + rtextures + rtext + rmodels + raudio
        |                          |
rlsw software renderer        miniaudio, null backend
        |
userspace/raylib/rcore_vexos.c  (PLATFORM_VEXOS)
        |
VEX_SYS_APP_WINDOW_*  (open, present, poll/wait, resize, close)
        |
kernel app_surface: compositor window, RAW_INPUT events
```

## Platform (`userspace/raylib/rcore_vexos.c`)

* One native app window, opened with `VEX_APP_WINDOW_FLAG_RAW_INPUT`
  (and `RESIZABLE` when the program sets `FLAG_WINDOW_RESIZABLE`).
* Rendering is rlsw (`GRAPHICS_API_OPENGL_SOFTWARE`): every `EndDrawing`
  reads the software framebuffer back as BGRA and presents it. Shaders are
  accepted and ignored (rlsw has no programmable pipeline); everything else
  -- shapes, text, textures, render textures, blending, scissor, 3D with
  depth -- is drawn.
* Input comes from the RAW_INPUT events: key press/release with repeat,
  text (Latin-1 characters from the kernel's AZERTY map), pointer position,
  left/right/middle buttons, wheel, focus. A release that arrives in the same
  poll as its press is applied one frame later, so `IsKeyPressed` and
  `IsMouseButtonPressed` see quick taps. Letters follow the layout (Ctrl+A is
  the key that types `a`); other keys keep their physical position.
* `EnableEventWaiting()` blocks in `VEX_APP_WINDOW_POLL_WAIT` until the next
  event, as GLFW's wait does.
* Resizing follows the compositor's CONFIGURE events.
* Clipboard: private to the process (no clipboard syscall yet). Cursor
  shapes, window icon, monitor queries, `SetWindowSize/Position`: no-ops.
* Audio: miniaudio's null device. Sounds and music load, play and advance in
  time, silently: Vex has no sound driver yet.

## Kernel: RAW_INPUT windows

Legacy app windows receive `MOUSE_CLICK` (left button, drags) and `KEY`
(one character, no release). A window opened with
`VEX_APP_WINDOW_FLAG_RAW_INPUT` receives instead (`kernel/include/unos/vex_abi.h`):

| event      | fields |
|------------|--------|
| `POINTER`  | x, y content position (signed); flags: buttons held (`VEX_APP_POINTER_*`); w: wheel steps |
| `KEY_RAW`  | x: character or 0x110000+scancode; y: set-1 scancode (+128 for E0); flags: bit 0 pressed, bits 8-15 modifiers |
| `FOCUS`    | flags: 1 gained, 0 lost |

The GUI loop (`gui.c`) sends physical key edges from the keyboard's
structured queue to the focused RAW_INPUT window, and pointer motion,
buttons and wheel while it is focused. Motion with unchanged buttons
replaces the previous queued sample. Keys without a character (F1-F12, Caps
Lock) now have edges too (`0x110000 + scancode`).

## Building

```
scripts/build-raylib.sh     # libraylib.a + ctypes, integers, raylib-ocaml for Vex
scripts/build-suchu.sh      # Suchu with the GUI and audio backends
```

Sources are pinned in `third_party/sources.lock` (raylib-ocaml 2.2.1, which
vendors raylib 6.0; ctypes 0.24.0; integers 0.8.0).
`third_party/patches/raylib-ocaml/0001-vexos-platform.patch` touches only
`rcore.c`: it includes `rcore_vexos.c` for `PLATFORM_VEXOS`, gives
`__vexos__` the POSIX timer/sleep paths, and fixes `BeginScissorMode` for
rlsw, whose scissor rectangle has a top-left origin (raylib passed the GL
bottom-left one, so clipped text vanished).

ctypes stubs cannot be cross-compiled by dune: the stanza runs generator
programs. `build-raylib.sh` therefore builds ctypes, integers and the
bindings for the host with dune (the generated stub sources depend only on
the x86-64 System V ABI, which host and Vex share), then
`scripts/ocaml-cross-replay.py` replays every library compilation from
dune's verbose log with the Vex cross compiler, skipping host-only work
(generators, executables, plugins). Everything lands flat in
`usr/lib/suchu`, so `ocamlopt -I /usr/lib/suchu` on Vex finds it.

Link order for a program (also in `cli.ml` and `build-suchu.sh`):

```
str.cmxa integers.cmxa ctypes.cmxa ctypes_stubs.cmxa raylib_c.cmxa raylib_core.cmxa
raylib_c_audio.cmxa raylib_audio.cmxa raylib_c_models.cmxa raylib_models.cmxa
raylib_c_shapes.cmxa raylib_shapes.cmxa raylib_c_text.cmxa raylib_text.cmxa
raylib_c_textures.cmxa raylib_textures.cmxa rlgl_c.cmxa raylib_rlgl.cmxa raylib.cmxa
... -cclib -lraylib
```

`-lraylib` is repeated last because the stub archives named after
`raylib_c.cmxa` need it and the link is static.

## Not done yet

* GPU rendering: Vex has EGL/GLES2 (`libvex_graphics`), but it needs C++
  (glslang) and is built for the hosted ABI; a raylib on it would also give
  shaders.
* A shared clipboard, cursor shapes, program-driven window size/position.
* Sound output (needs a sound driver).
