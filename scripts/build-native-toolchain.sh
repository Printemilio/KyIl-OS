#!/usr/bin/env bash
# Assemble the native development toolchain of Vex into the repository's
# ISO payload directories:
#
#   /bin/sh, /usr/bin/sh                 dash               (build-dash.sh)
#   /usr/bin/as, ld                      GNU binutils       (build-binutils.sh)
#   /usr/bin/x86_64-unknown-vexos-ld     ld, under the name ocamlopt packs with
#   /usr/bin/cc, x86_64-unknown-vexos-gcc  vexcc driver (as + ld + startup files)
#   /usr/bin/ocamlopt                    launcher setting OCAMLLIB
#   /usr/libexec/ocaml/ocamlopt.opt      the OCaml native compiler, for Vex
#   /usr/lib/{crt1,crti,crtn,crtbeginT,crtend}.o, libc.a, libgcc.a, ...
#   /usr/lib/ocaml                       standard library and otherlibs
#
#   scripts/build-native-toolchain.sh [--with-deps]
#
# --with-deps first (re)builds binutils and dash. Requires the Vex libc
# (userspace/libc/build.sh) and the OCaml cross toolchain
# (scripts/build-ocaml-toolchain.sh).
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOLCHAIN=${VEX_TOOLCHAIN:-$HOME/.local/vexos}
OCAML_BUILD=${VEX_TOOLCHAIN_WORK:-$HOME/.cache/vexos}/ocaml-vexos
WORK=${VEX_TOOLCHAIN_WORK:-$HOME/.cache/vexos}/native-toolchain
SYSROOT="$ROOT/build/vex-sysroot"
CROSS_LIB="$TOOLCHAIN/ocaml-vexos/lib/ocaml"
OCAMLOPT="$TOOLCHAIN/ocaml-vexos/bin/ocamlopt.opt"
export PATH="$ROOT/toolchain/bin:$PATH"
CC=x86_64-unknown-vexos-gcc

if [ "${1:-}" = "--with-deps" ]; then
    bash "$ROOT/scripts/build-binutils.sh"
    bash "$ROOT/scripts/build-dash.sh"
fi
for f in "$ROOT/usr/bin/as" "$ROOT/usr/bin/ld" "$ROOT/usr/bin/sh" "$OCAMLOPT" "$SYSROOT/lib/libc.a"; do
    [ -e "$f" ] || { echo "missing $f (see the header of this script)" >&2; exit 1; }
done

BIN="$ROOT/usr/bin" LIB="$ROOT/usr/lib" LIBEXEC="$ROOT/usr/libexec/ocaml"
mkdir -p "$BIN" "$LIB" "$LIBEXEC" "$ROOT/bin" "$WORK"

# Shell at the POSIX path system() and posix_spawn use.
install -m 755 "$BIN/sh" "$ROOT/bin/sh"

# Driver, linker alias, launcher.
"$CC" -O2 -Wall -o "$BIN/cc" "$ROOT/userspace/toolchain/vexcc.c"
install -m 755 "$BIN/cc" "$BIN/x86_64-unknown-vexos-gcc"
install -m 755 "$BIN/ld" "$BIN/x86_64-unknown-vexos-ld"
"$CC" -O2 -Wall -o "$BIN/ocamlopt" "$ROOT/userspace/toolchain/ocaml_launcher.c"
x86_64-unknown-vexos-strip "$BIN/cc" "$BIN/x86_64-unknown-vexos-gcc" "$BIN/ocamlopt"

# Startup objects and libraries every native link uses.
cp "$SYSROOT"/lib/*.o "$SYSROOT"/lib/*.a "$LIB/"
cp "$(gcc -print-file-name=crtbeginT.o)" "$(gcc -print-file-name=crtend.o)" "$LIB/"
cp "$(gcc -print-libgcc-file-name)" "$LIB/libgcc.a"

# The native compiler: compiler-libs built for Vex by the cross toolchain,
# plus the driver's main module compiled for Vex here.
rm -rf "$WORK/ocamlopt" && mkdir -p "$WORK/ocamlopt" && cd "$WORK/ocamlopt"
cp "$OCAML_BUILD/driver/optmain.mli" "$OCAML_BUILD/driver/optmain.ml" .
"$OCAMLOPT" -I +compiler-libs -c optmain.mli optmain.ml
"$OCAMLOPT" -I +compiler-libs -o ocamlopt.opt ocamlcommon.cmxa ocamloptcomp.cmxa optmain.cmx
x86_64-unknown-vexos-strip ocamlopt.opt
install -m 755 ocamlopt.opt "$LIBEXEC/ocamlopt.opt"

# The standard library and otherlibs, compiled artefacts only (interfaces
# for typing, .cmx for cross-module optimisation, archives and objects for
# linking). Sources, typed trees and compiler-libs are not needed to build
# programs.
rm -rf "$LIB/ocaml"
(cd "$CROSS_LIB" && find . -path ./compiler-libs -prune -o -type f \
    \( -name '*.cmi' -o -name '*.cmx' -o -name '*.cmxa' -o -name '*.a' -o -name '*.o' \
       -o -name '*.mli' -o -name 'ld.conf' -o -name 'META' \) -print) |
    while read -r f; do
        mkdir -p "$LIB/ocaml/$(dirname "$f")"
        cp "$CROSS_LIB/$f" "$LIB/ocaml/$f"
    done
du -sh "$LIB/ocaml" "$LIBEXEC/ocamlopt.opt"
