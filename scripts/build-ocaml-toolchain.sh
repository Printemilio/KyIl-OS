#!/usr/bin/env bash
# OCaml for Vex.
#
#   scripts/build-ocaml-toolchain.sh host   OCaml 5.4.0 for the build machine
#                                           (a cross compiler must be built by
#                                           a non-cross compiler of the same
#                                           version)
#   scripts/build-ocaml-toolchain.sh cross  the x86_64-unknown-vexos cross
#                                           compiler and target libraries
#   scripts/build-ocaml-toolchain.sh all    both
#
# Results: $VEX_TOOLCHAIN/ocaml-host and $VEX_TOOLCHAIN/ocaml-vexos
# (default VEX_TOOLCHAIN=~/.local/vexos). OCaml's build and its installed
# configuration do not support an installation path containing spaces, which
# a Windows checkout path often does, so the toolchain lives outside it.
# Sources: build/third_party/src/ocaml-5.4.0 (pristine + third_party/patches/ocaml).
# Builds run out of the checkout, in $VEX_BUILD_DIR (default ~/.cache/vexos),
# because compiling thousands of files over a Windows mount is slow.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC="$ROOT/build/third_party/src/ocaml-5.4.0"
WORK=${VEX_BUILD_DIR:-$HOME/.cache/vexos}
TOOLCHAIN=${VEX_TOOLCHAIN:-$HOME/.local/vexos}
HOST_PREFIX="$TOOLCHAIN/ocaml-host"
CROSS_PREFIX="$TOOLCHAIN/ocaml-vexos"
case "$TOOLCHAIN" in *" "*) echo "VEX_TOOLCHAIN must not contain spaces" >&2; exit 1 ;; esac
TARGET=x86_64-unknown-vexos
JOBS=${JOBS:-$(nproc)}

sh "$ROOT/scripts/fetch-third-party.sh" ocaml

fresh_tree() {
    rm -rf "$1"
    mkdir -p "$1"
    # The pristine+patched tree, without the git metadata used to edit it.
    tar -C "$SRC" --exclude=.git -cf - . | tar -C "$1" -xf -
}

build_host() {
    # The cross compiler is linked with the host's marshalling library, so a
    # host OCaml with zstd would make it write compressed .cmi/.cmx files.
    # The Vex runtime (and so the native ocamlopt) is built without zstd and
    # could not read them: the host compiler must not compress either.
    if [ -x "$HOST_PREFIX/bin/ocamlopt.opt" ] &&
       [ "$("$HOST_PREFIX/bin/ocamlopt.opt" -version)" = 5.4.0 ] &&
       [ -z "$("$HOST_PREFIX/bin/ocamlopt.opt" -config-var compression_c_libraries)" ]; then
        echo "ocaml-host: already built"
        return
    fi
    local dir="$WORK/ocaml-host"
    fresh_tree "$dir"
    cd "$dir"
    ./configure --prefix="$HOST_PREFIX" --disable-ocamldoc --disable-debugger \
        --disable-ocamltest --without-zstd >/dev/null
    make -j"$JOBS" world.opt >/dev/null
    make install >/dev/null
    echo "ocaml-host: installed in $HOST_PREFIX"
}

build_cross() {
    local dir="$WORK/ocaml-vexos"
    [ -x "$HOST_PREFIX/bin/ocamlopt.opt" ] || { echo "build the host compiler first" >&2; exit 1; }
    export PATH="$HOST_PREFIX/bin:$ROOT/toolchain/bin:$PATH"
    fresh_tree "$dir"
    cd "$dir"
    # TARGET_LIBDIR is where the standard library lives on Vex itself.
    ./configure --prefix="$CROSS_PREFIX" --target="$TARGET" \
        TARGET_LIBDIR=/usr/lib/ocaml \
        --disable-shared --disable-ocamldoc --disable-debugger \
        --disable-ocamltest --without-zstd >/dev/null
    make -j"$JOBS" crossopt
    make installcross >/dev/null
    echo "ocaml-vexos: installed in $CROSS_PREFIX"
}

case "${1:-all}" in
    host) build_host ;;
    cross) build_cross ;;
    all) build_host; build_cross ;;
    *) echo "usage: $0 host|cross|all" >&2; exit 1 ;;
esac
