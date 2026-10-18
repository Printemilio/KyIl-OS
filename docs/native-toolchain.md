# Native toolchain on Vex

Vex runs its own development tools: a POSIX shell, the GNU assembler and
linker, a small compiler driver and the OCaml native compiler. All of them
are static native Vex programs (ELF x86-64, System V ABI, `.note.vexos`),
built on a Linux build machine with the Vex C library and staged on the ISO.

```
/bin/sh, /usr/bin/sh            dash 0.5.12
/usr/bin/as, /usr/bin/ld        GNU binutils 2.42 (gas, ld; host = target = x86_64-unknown-vexos)
/usr/bin/cc                     vexcc driver (userspace/toolchain/vexcc.c)
/usr/bin/x86_64-unknown-vexos-gcc   same driver, under the name OCaml's config uses
/usr/bin/x86_64-unknown-vexos-ld    ld, under the name `ocamlopt -pack` uses
/usr/bin/ocamlopt               launcher (sets OCAMLLIB, runs the compiler)
/usr/libexec/ocaml/ocamlopt.opt OCaml 5.4 native compiler, running on Vex
/usr/lib/crt1.o crti.o crtn.o crtbeginT.o crtend.o libc.a libgcc.a ...
/usr/lib/ocaml                  standard library, unix, threads, str, ...
```

## Building

```
scripts/build-ocaml-toolchain.sh all          # host OCaml + Vex cross compiler (once)
bash userspace/libc/build.sh                  # Vex libc sysroot
scripts/build-native-toolchain.sh --with-deps # binutils, dash, driver, ocamlopt, stdlib
scripts/run-native-tests.ps1 -Disk Keep       # boots the ISO, runs usr/native-tests
```

`build-binutils.sh` and `build-dash.sh` can be run alone. Sources are pinned
in `third_party/sources.lock`; Vex patches live in `third_party/patches/`.

## How the pieces fit

* **binutils** knows `x86_64-*-vexos*` (config.sub, bfd, gas, ld target
  tables: plain `elf_x86_64`). Only gas and ld are built.
* **vexcc** turns compiler-driver command lines into `as` and `ld` runs.
  `cc -c x.s` assembles; a link adds, in GCC's order, `crt1.o crti.o
  crtbeginT.o`, the inputs, `-lc libgcc.a`, `crtend.o crtn.o`, always static
  and non-PIE. There is no C compiler on Vex yet: `.c`/`.S` inputs are
  rejected with a message (never skipped).
* **ocamlopt.opt for Vex** is linked by the cross compiler from the
  compiler-libs that `make crossopt` rebuilds for the target, plus
  `driver/optmain.ml`. Its built-in configuration is the cross compiler's,
  which is why it calls `x86_64-unknown-vexos-gcc -c` to assemble and links
  through the same name; Vex provides those names natively. The configured
  standard library path is the build machine's, so the `/usr/bin/ocamlopt`
  launcher sets `OCAMLLIB=/usr/lib/ocaml`.
* ocamlopt runs its tools through `Sys.command`, i.e. `/bin/sh -c`: dash is
  required, not optional.
* The host OCaml is configured `--without-zstd`. The cross compiler is
  linked with the host's marshalling library; with zstd it writes compressed
  `.cmi`/`.cmx` files that the Vex runtime (built without zstd) cannot read
  ("Corrupted compiled interface").

## Kernel and libc support this relies on

* Main-thread stack: 8 MiB, of which the top 512 KiB are populated at load
  and the rest is a lazy reservation (`ELF_USER_STACK_RESERVE`, elf.c).
  gas and ld overflowed the former fixed 512 KiB.
* `getrlimit(RLIMIT_NOFILE)` asks the kernel directly; it used to call
  `sysconf(_SC_OPEN_MAX)`, which musl answers with `getrlimit` -- an
  infinite recursion the first time a program (binutils) asked.
* ISO9660 Rock Ridge names and directory listing (`/usr/lib/ocaml` is read
  straight from the ISO).

## Tests

`userspace/libc/tests/toolchain_test.c` (suite `toolchain`), run in the VM:
shell exit status, expansions, redirections and loops; an assembly program
assembled, linked and run through `cc`; a C input rejected; `ocamlopt`
compiling and running a program; separate compilation with an interface and
the unix library; a type error reported with status 2.

## Performance (VirtualBox, 4 vCPU, 2026-10-07)

Measured by `toolchain_test` (`  time ...` lines in the serial log):

| step | time |
|---|---|
| `sh -c true` through `system()` | 120 ms |
| `as` on a small file | 310 ms |
| `cc` static link (ld, libc.a, libgcc.a) | 750 ms |
| `ld --version` | 295 ms |
| `ocamlopt -version` | 1.0-1.3 s |
| `ocamlopt -c hello.ml` | 2.5 s |
| `ocamlopt hello.cmx -o hello` | ~10 s |

What is known:

* Loading the 10 MiB `ocamlopt.opt` image costs ~90 ms (instrumented
  `elf_exec_current_args`: inst/ABI analysis ~40 ms because
  `analyze_binary` hashes the whole file 3-4 times per exec, image spec
  ~48 ms, region registration ~19 ms, materialisation ~3 ms). Not the
  bottleneck, but the repeated analysis is worth caching per file.
* `system()`/`posix_spawn` use a real `fork` (copy of the parent's address
  space) before `exec`; from a large parent (ocamlopt) a fork measured up to
  800 ms. A vfork-style spawn (child borrows the parent's memory until exec)
  is the fix.
* The remaining ~0.9 s of `ocamlopt -version` is outside exec: the OCaml 5
  runtime start/exit (minor-heap reservations for 128 domains, their
  munmap page by page under `memory_gate`) is the prime suspect; to measure
  next.
* Kernel samples during builds are dominated by sections under
  `memory_gate` (page-table walks, kernel allocator).

## Not yet

* No C compiler on Vex: OCaml programs with C stubs (Raylib bindings) must
  have their C parts built on the build machine for now.
* `ocamlc`/bytecode, `ocamlfind`, dune, opam are not ported.
* No coreutils: scripts can use dash builtins only.

## Speed-ups (2026-10-08)

* `posix_spawn` (hence `system()`, `popen()`, every tool ocamlopt runs) no
  longer forks the whole parent: `userspace/libc/src/process/posix_spawn.c`
  starts the child with `VEX_SYS_PROCESS_SPAWN_SHARED` (219), which runs it on
  its own 64 KiB stack inside the parent's memory until `execve`, like
  `clone(CLONE_VM)`. The child skips the library's signal table (it is the
  parent's); exec resets caught signals. A kernel without the call falls back
  to fork.
* The kernel no longer hashes the whole binary (FNV-1a, twice per analysis,
  3-4 analyses) on every exec (`inst.c`); the hash is computed only for the
  info/translation commands that show it.

Measured in the native tests: `ocamlopt` link of hello 12.2 s -> 6.6 s,
`-c` 2.6 s -> 1.4 s, `suchu comp` 18-20 s -> 11 s, `suchu comp` of a window
program 22 s -> 14 s, whole suchu suite 178 s -> 76 s.
