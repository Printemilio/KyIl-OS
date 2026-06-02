# Vex-OS Architecture

## Overview

Vex-OS currently has four main layers:

1. Boot and hardware bring-up
2. Kernel core services
3. Native Vex userspace
4. Compatibility/runtime bridges

The design is not microkernel-oriented. The current kernel is monolithic and owns:

- memory management
- scheduling
- filesystems and boot module import
- networking
- syscall dispatch
- GUI compositing and shell behavior
- compatibility shims used by Linux-ABI and PE-style workloads

## Boot Flow

Boot entry is orchestrated from `kernel/src/main.c`.

At a high level:

1. Limine provides framebuffer, memmap, HHDM, and boot module metadata.
2. Early kernel code enables CPU features and serial/TTY output.
3. PMM, VMM, disk, filesystem, and networking subsystems initialize.
4. Boot modules are imported into the in-memory filesystem namespace.
5. Kernel services and GUI-facing processes are launched.
6. Control shifts to the GUI shell and userspace-visible workflows.

## Memory Model

Physical memory:

- Managed by a bitmap PMM in `kernel/src/pmm.c`
- Limine usable ranges are marked free
- Reserved/kernel/bootloader regions remain unavailable by default

Virtual memory:

- Managed in `kernel/src/vmm.c`
- Separate user and kernel mappings
- Page tables are created lazily
- Empty user-side page tables are reclaimed on unmap

Process memory:

- `kernel/src/process_memory.c` layers region metadata and namespace tracking on top
  of raw mappings
- MRAC-backed metadata is used to describe active regions and snapshots
- JIT arenas and Linux-like mapping behavior are built on top of that substrate

## Execution Model

Scheduling and processes live in `kernel/src/scheduler.c`.

Key properties:

- kernel and user processes share one scheduler
- user processes carry their own `cr3`, user stack, ABI tag, and TLS state
- clone/fork-style behavior is supported
- futex wait/wake exists for thread synchronization
- zombie reaping and parent-child tracking are already present

Executable loading:

- `kernel/src/elf.c` loads both native and Linux-ABI-oriented ELF64 images
- `ET_EXEC` and `ET_DYN` are supported
- Linux dynamic interpreter loading is supported via a separate interpreter base
- user stack setup includes Linux-like auxv data where needed

## Syscall and ABI Surfaces

Native Vex ABI:

- Defined in `kernel/include/unos/vex_abi.h`
- Implemented primarily in `kernel/src/vex_syscall.c`
- Covers file IO, memory, timing, processes, threads, networking, display, browser
  messaging, MRAC, JIT arenas, and GPU operations

Compatibility layer:

- `kernel/src/compat_linux.c` translates selected Linux syscalls to Vex syscalls/kernel
  operations
- `kernel/src/compat_pe.c` hosts PE/Win32-flavored compatibility entry points

This is one of the strongest parts of the repository conceptually, but also one of
the most structurally dense implementation areas.

## Filesystem and Boot Modules

Filesystem behavior is concentrated in `kernel/src/fs.c`.

Current model:

- imported boot modules are registered as filesystem-visible content
- a compact disk-backed table exists for persistent entries
- user access is gated by path rules and current credentials
- Linux-ABI workloads are explicitly allowed to access runtime library paths

This is practical for bring-up, but still intentionally narrow in scale and policy.

## GUI and Product Layer

The GUI shell lives mainly in `kernel/src/gui.c`.

It currently bundles:

- compositor state
- window management
- terminal integration
- browser shell state
- paint tool state
- app switcher behavior
- parts of browser result rendering

This gives Vex-OS a strong "product feel", but it is also the main structural hotspot
for future refactoring.

## Browser Runtime Model

The browser surface exposed to the user is `Vex Browser`.

Current runtime arrangement:

1. GUI/browser shell collects user intent
2. Browser commands flow through kernel/browser messaging interfaces
3. A native launcher process manages display-facing shared memory and process spawn
4. A Linux-ABI child process performs web runtime work and writes frames back

Important naming note:

- browser/runtime compatibility code still lives in a shared substrate
- it should not be read as a separate product architecture from `Vex Browser`

## Third-Party Material

Third-party code and payloads are present in several forms:

- pinned boot dependencies under `vendor/`
- imported runtime payloads under `lib/`, `lib64/`, and `usr/`
- packaged Linux-side browser/runtime binaries included as boot modules

That means "what powers the browser" and "what source is present in-tree" are two
different questions. The status docs in this folder make that distinction explicit.
