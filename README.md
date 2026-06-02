# Vex-OS

Vex-OS is an experimental operating system built from scratch around a freestanding
x86_64 kernel, a native userspace ABI, and a growing Linux compatibility layer.

This repository is no longer a "first boot" milestone. The current tree already
contains:

- Limine-based boot and framebuffer bring-up
- Physical and virtual memory managers
- Scheduler, syscalls, process loading, and user mode entry
- Native userspace runtime (`libvex`)
- In-kernel GUI shell, terminal, file manager, editor, browser shell, and paint UI
- Storage, boot module import, and a simple filesystem layer
- Network device support (`e1000`, `virtio-net`) and a compact TCP/UDP stack
- Linux-ABI execution support for selected ELF workloads
- Browser/runtime experiments built around a Vex launcher plus Linux-side web process

## Project State

Vex-OS is best understood as a systems laboratory with a real kernel and userland,
not as a polished general-purpose OS.

The codebase currently mixes three layers of maturity:

1. Stable-enough core subsystems: boot, PMM, VMM, scheduler, syscalls, ELF loading
2. Working product-facing features: GUI shell, files, terminal, browser shell
3. Active compatibility and runtime experiments: Linux ABI support, browser bridge,
   GPU paths, imported third-party runtime payloads

## Build Requirements

- Git
- Clang/LLVM with `ld.lld`
- `xorriso`
- VirtualBox for the bundled local smoke-test flow

On Windows, the main project scripts are PowerShell-based. The first build fetches
pinned boot dependencies into `vendor/`.

## Build

```powershell
.\scripts\build.ps1
```

Build artifacts:

- Kernel ELF: `build\vex-os`
- Bootable ISO: `build\vex-os.iso`

Optional flags:

- `-NoIso`: build the kernel only
- `-SkipWebKit`: skip the browser/userspace packaging step

## Run

```powershell
.\scripts\run-virtualbox.ps1
```

## Smoke Tests

```powershell
.\scripts\run-smoke-tests.ps1
```

The current smoke path watches the serial log for autorun plus GPU/Vulkan checks.

## Repository Guide

High-value directories:

- `kernel/`: kernel sources, headers, architecture glue
- `userspace/`: native userspace runtime and Vex binaries
- `boot/`: Limine boot configuration
- `scripts/`: build, packaging, and run tooling
- `docs/`: architecture notes and browser/runtime status
- `vendor/`: pinned third-party source dependencies
- `build/`: generated artifacts

Core entry points:

- Kernel boot flow: `kernel/src/main.c`
- Scheduler: `kernel/src/scheduler.c`
- Virtual memory: `kernel/src/vmm.c`
- ELF loader: `kernel/src/elf.c`
- Syscall dispatch: `kernel/src/vex_syscall.c`
- Linux/PE compatibility: `kernel/src/compat_linux.c`, `kernel/src/compat_pe.c`
- GUI shell: `kernel/src/gui.c`

## Browser Stack

The product-facing browser surface is `Vex Browser`.

Today that means:

- the GUI shell collects commands and browser interactions
- a native Vex launcher process bridges display plus command transport
- a Linux-ABI child process performs web runtime work
- the supported runtime path is `scripts/build-webkit-userspace.ps1`

See:

- `docs/architecture.md`
- `docs/repository-map.md`
- `docs/webkit-vexos-bridge.md`
- `WEBKIT_PORT_STATUS.md`

## Current Constraints

The project is functional but still carries structural debt:

- documentation had drifted behind the codebase
- several large source files concentrate too many responsibilities
- the boot/runtime packaging path is still very manual for web payloads

This repository now documents those constraints explicitly instead of pretending
to be at an earlier milestone.
