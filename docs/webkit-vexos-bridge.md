# Vex Browser Runtime Bridge

State updated for the current repository layout.

## What This Document Covers

This file describes the browser runtime path that is actually wired into the repo today.

It does not claim that a full WebKit source port lives in-tree.
It does explain how the product-facing browser experience currently bridges:

- the Vex GUI shell
- a native Vex launcher process
- a Linux-ABI child runtime
- shared-memory frame transport

## Product Names vs Implementation Names

Use this terminology:

- `Vex Browser`
  The product-facing browser surface in the GUI shell.

- `webkit`
  The current native launcher binary packaged into the Vex runtime.

- `webkit_linux_main`
  The Linux-ABI child binary spawned by the launcher.

## Runtime Architecture

Current flow:

1. The GUI/browser shell accepts an address or search-style input.
2. The kernel/browser command path makes that intent available to the browser runtime.
3. The native `webkit` launcher manages shared memory and process startup.
4. The Linux-ABI child runtime performs web work and writes rendered frames into SHM.
5. The launcher copies those frames into a display-presentable buffer for VexOS.

## Shared Memory Model

The current bridge uses two files under `/tmp/`:

- `/tmp/vex_webkit_fb`
  Shared framebuffer header plus pixel payload

- `/tmp/vex_webkit_cmd`
  Shared command buffer containing the latest requested URL or command string

The launcher:

- creates or opens the SHM files
- maps the framebuffer file
- spawns `webkit_linux_main`
- polls for incoming browser commands from the kernel side
- copies SHM pixels into a heap buffer before display present

The Linux child:

- maps or creates the same SHM files
- reads command-sequence changes
- loads or updates page content
- writes frame pixels back into the shared framebuffer

## Important Files

- Native launcher: `userspace/apps/webkit_launcher.c`
- Linux-side runtime process: `userspace/apps/webkit_linux_main.c`
- Browser shell and window logic: `kernel/src/gui.c`
- Browser/display syscalls and request structs: `kernel/include/unos/vex_abi.h`
- Compatibility facade: `kernel/include/unos/vex_web_compat.h`
- Legacy substrate still used underneath: `kernel/include/unos/vex_legacy_compat.h`

## What Is True Today

True:

- there is a real browser runtime bridge in this repository
- the product-facing browser path is now described consistently as `Vex Browser`
- Linux-ABI execution is part of the current browser strategy

Also true:

- the bridge is still a compatibility-heavy runtime arrangement
- packaging is still explicit and manually staged through the boot/runtime image

## What Still Needs Cleanup

Safe cleanup that can happen without changing behavior:

1. Prefer `vex_web_*` facade names in new public-facing code
2. Keep narrowing direct inclusion of `vex_legacy_compat.h`
3. Split browser-shell logic out of `kernel/src/gui.c`
4. Keep source-provenance docs separate from runtime-architecture docs
