# Vex-OS Repository Map

## Top Level

- `kernel/`
  Kernel source, headers, arch glue, compat layers, GUI shell, networking, and loaders.

- `userspace/`
  Native Vex userspace runtime (`libvex`) plus user binaries and browser-facing launchers.

- `boot/`
  Limine configuration and boot-time module declarations.

- `scripts/`
  Build, packaging, smoke-test, and VM launch scripts.

- `docs/`
  Architecture notes, browser stack explanation, and project status documents.

- `vendor/`
  Pinned third-party source trees used by boot or browser/runtime experiments.

- `build/`
  Generated output. Kernel binary, ISO, logs, objects, staged userspace payloads.

- `lib/`, `lib64/`, `usr/`, `etc/`
  Runtime payloads and packaged assets staged into the boot/runtime environment.

## Kernel Subsystems

- `kernel/src/main.c`
  Boot orchestration and service startup sequence.

- `kernel/src/pmm.c`
  Physical page allocator.

- `kernel/src/vmm.c`
  Page-table management and address-space mapping helpers.

- `kernel/src/scheduler.c`
  Process/thread scheduling, fork/clone behavior, futex wait/wake, exit/reap logic.

- `kernel/src/process_memory.c`
  Region metadata, namespace snapshots, and mapping bookkeeping.

- `kernel/src/elf.c`
  Native and Linux-oriented ELF loading.

- `kernel/src/vex_syscall.c`
  Native syscall dispatch and higher-level service entry points.

- `kernel/src/compat_linux.c`
  Linux syscall translation and Linux-ABI-facing syscall shaping.

- `kernel/src/compat_pe.c`
  PE/Win32-flavored compatibility entry points and thunk behavior.

- `kernel/src/fs.c`
  Filesystem view, boot module registration, path policy, and persistence table.

- `kernel/src/netdev.c`
  NIC detection and backend implementation for `e1000` and `virtio-net`.

- `kernel/src/tcp.c`, `kernel/src/udp.c`, `kernel/src/dns.c`, `kernel/src/dhcp.c`
  Compact network stack components.

- `kernel/src/gui.c`
  Compositor plus product shell logic. Major structural hotspot.

- `kernel/src/shell.c`, `kernel/src/shell_commands.c`
  Terminal shell input loop, history/editing, and builtin command handling.

- `kernel/src/vex_browser_display.c`, `kernel/src/vex_socket_compat.c`, `kernel/src/vex_posix_compat.c`
  Legacy-derived compatibility/runtime substrate now moved under Vex-oriented filenames.

## Userspace Layout

- `userspace/libvex/src/`
  Native runtime, libc-like helpers, syscalls, graphics/network helpers, browser/runtime
  support glue.

- `userspace/libvex/include/`
  Public native runtime headers for Vex userspace code.

- `userspace/apps/`
  User binaries and browser/runtime entry points, including launcher, Linux-side web
  process, tests, and experiments.

## Browser-Relevant Files

- `userspace/apps/webkit_launcher.c`
  Native Vex launcher process. This is the supported `webkit` userspace entry point.

- `userspace/apps/webkit_linux_main.c`
  Linux-ABI child process for browser runtime work through WPE/WebKit.

- `kernel/src/gui.c`
  Browser shell UI, address entry, result rendering, and browser window behavior.

- `kernel/include/unos/vex_abi.h`
  Browser/display syscalls and request structs used across the boundary.

- `kernel/include/unos/vex_web_compat.h`
  Public browser/runtime compatibility surface used by launcher-side code.

## Naming Guidance

Use these meanings when reading the codebase:

- `Vex-OS`
  The operating system project.

- `Vex Browser`
  The product-facing browser experience inside the GUI shell.

- `webkit_*`
  Current browser launcher/runtime binaries and bridge processes on the supported path.

This distinction matters because the repository still contains historical names that no
longer match the intended public architecture.
