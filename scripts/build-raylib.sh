#!/usr/bin/env bash
# Build raylib and its OCaml bindings for Vex.
#
#   scripts/build-raylib.sh [--c-only]
#
# --c-only rebuilds and restages libraylib.a alone (after a change to
# rcore_vexos.c); the OCaml libraries already in usr/lib/suchu are kept.
#
# 1. libraylib.a: raylib 6.0 (vendored by raylib-ocaml 2.2.1) for
#    PLATFORM_VEXOS (userspace/raylib/rcore_vexos.c: one native app window,
#    raw input) with the software renderer rlsw (GRAPHICS_API_OPENGL_SOFTWARE)
#    and miniaudio's null backend (Vex has no sound driver yet).
# 2. ctypes 0.24.0, integers 0.8.0 and raylib-ocaml 2.2.1 for the Vex OCaml
#    cross compiler. dune cannot cross-compile ctypes stubs (its generators
#    must run), so the libraries are built for the host first -- generating
#    the stub sources, which only depend on the x86-64 System V ABI shared by
#    both -- and the library compilations are then replayed with the cross
#    compiler (scripts/ocaml-cross-replay.py).
#
# Needs: the opam switch 5.2.0 (dune, ctypes, integers, dune-configurator) for
# the host build, the Vex OCaml cross compiler (scripts/build-ocaml-toolchain.sh)
# and the Vex sysroot (userspace/libc/build.sh).
#
# Result, flat in usr/lib/suchu (ISO /usr/lib/suchu, next to the Suchu
# libraries that `suchu comp` links): *.cmi *.cmx *.cmxa *.a of integers,
# ctypes, ctypes.stubs and every raylib sub-library, plus libraylib.a.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VO=${VEX_TOOLCHAIN:-$HOME/.local/vexos}/ocaml-vexos
WORK=${VEX_TOOLCHAIN_WORK:-$HOME/.cache/vexos}/raylib
SWITCH=${VEX_OPAM_SWITCH:-5.2.0}
SRC="$ROOT/build/third_party/src"
export PATH="$ROOT/toolchain/bin:$PATH"
[ -x "$VO/bin/ocamlopt.opt" ] || { echo "missing $VO (scripts/build-ocaml-toolchain.sh)" >&2; exit 1; }
[ -f "$ROOT/build/vex-sysroot/lib/libc.a" ] || { echo "missing Vex sysroot (userspace/libc/build.sh)" >&2; exit 1; }
sh "$ROOT/scripts/fetch-third-party.sh" raylib-ocaml ctypes integers
HOST_OPAM_LIB=$(opam var --switch="$SWITCH" lib 2>/dev/null)

C_ONLY=0
[ "${1:-}" = "--c-only" ] && C_ONLY=1
LIB="$ROOT/usr/lib/suchu"
rm -rf "$WORK" && mkdir -p "$WORK"

# --- 1. libraylib.a for Vex -------------------------------------------------
mkdir -p "$WORK/c"
(cd "$SRC/raylib-ocaml-2.2.1/src/c" && tar -cf - vendor/raylib/src enable_formats.patch hotfixes.patch) |
    tar -xf - -C "$WORK/c"
# The two patches raylib-ocaml's own build applies (image/sound formats, FLAC fix).
(cd "$WORK/c" && for p in enable_formats.patch hotfixes.patch; do
    patch -p1 --no-backup-if-mismatch -s -i "$p"; done)
RAYLIB_FLAGS=(-O2 -I. -I"$ROOT/userspace/raylib" -I"$ROOT/userspace/libvex/include"
    -idirafter "$ROOT/kernel/include" -DPLATFORM_VEXOS -DGRAPHICS_API_OPENGL_SOFTWARE
    -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL -DMA_NO_RUNTIME_LINKING
    -D_GNU_SOURCE -w)
python3 "$ROOT/scripts/patch-raylib-software.py" "$WORK/c/vendor/raylib/src/external/rlsw.h"
mkdir -p "$WORK/c/obj"
(cd "$WORK/c/vendor/raylib/src" &&
    for m in rcore rshapes rtextures rtext rmodels raudio; do
        x86_64-unknown-vexos-gcc "${RAYLIB_FLAGS[@]}" -c "$m.c" -o "$WORK/c/obj/$m.o" &
    done; wait)
for m in rcore rshapes rtextures rtext rmodels raudio; do
    [ -f "$WORK/c/obj/$m.o" ] || { echo "raylib: $m.c failed to compile" >&2; exit 1; }
done
x86_64-unknown-vexos-ar rcs "$WORK/libraylib.a" "$WORK/c/obj"/*.o
if [ $C_ONLY = 1 ]; then
    mkdir -p "$LIB" && cp "$WORK/libraylib.a" "$LIB/"
    echo "raylib: libraylib.a restaged (relink programs: scripts/build-suchu.sh)"
    exit 0
fi

# --- 2. host build (generates the ctypes stubs) ------------------------------
GEN="$WORK/gen"
mkdir -p "$GEN/ct" "$GEN/rl"
(cd "$SRC" && tar -cf - --exclude=.vex-patched ctypes-0.24.0 integers-0.8.0) | tar -xf - -C "$GEN/ct"
(cd "$SRC" && tar -cf - --exclude=.vex-patched raylib-ocaml-2.2.1) | tar -xf - -C "$GEN/rl"
echo '(lang dune 3.20)' > "$GEN/ct/dune-workspace"
echo '(lang dune 3.20)' > "$GEN/rl/dune-workspace"
# The host build needs a raylib to link the generators against, not a usable
# one: the dependency-free memory platform with the software renderer.
cat > "$GEN/rl/raylib-ocaml-2.2.1/src/c/dune" <<'EOF'
(data_only_dirs vendor)
(rule (targets library_flags.sexp) (action (with-stdout-to library_flags.sexp (echo "()"))))
(rule (targets libraylib.a dllraylib.so)
 (deps (source_tree vendor/raylib))
 (action (system "mkdir -p hostobj && cd vendor/raylib/src && for f in rcore rshapes rtextures rtext rmodels raudio; do gcc -I. -c -O1 -fPIC -w -DPLATFORM_MEMORY -DGRAPHICS_API_OPENGL_SOFTWARE $f.c -o ../../../hostobj/$f.o || exit 1; done && cd ../../.. && ar rc libraylib.a hostobj/*.o && gcc -shared -o dllraylib.so hostobj/*.o -lm -lpthread -ldl")))
EOF
host_dune() { # dir log packages
    (cd "$1" && opam exec --switch="$SWITCH" -- dune build --root . --cache=disabled \
        --display verbose --only-packages "$3" --profile release @install 2> "$2") ||
        { grep -i -B2 -A12 "error" "$2" | tail -40 >&2; exit 1; }
}
host_dune "$GEN/ct" "$WORK/ct.log" ctypes,integers
host_dune "$GEN/rl" "$WORK/rl.log" raylib

# --- 3. replay for Vex ------------------------------------------------------
TGT="$WORK/target"
mkdir -p "$TGT/shims"
cp -a "$GEN/ct" "$TGT/ct"
cp -a "$GEN/rl" "$TGT/rl"
python3 "$ROOT/scripts/ocaml-cross-replay.py" "$WORK/ct.log" "$TGT/ct" \
    "$HOST_OPAM_LIB/ocaml=$VO/lib/ocaml" 2> "$WORK/ct-replay.log" ||
    { tail -20 "$WORK/ct-replay.log" >&2; exit 1; }
CTL="$TGT/ct/_build/install/default/lib"
python3 "$ROOT/scripts/ocaml-cross-replay.py" "$WORK/rl.log" "$TGT/rl" \
    "$HOST_OPAM_LIB/ctypes=$CTL/ctypes" "$HOST_OPAM_LIB/integers=$CTL/integers" \
    "$HOST_OPAM_LIB/stdlib-shims=$TGT/shims" "$HOST_OPAM_LIB/ocaml=$VO/lib/ocaml" 2> "$WORK/rl-replay.log" ||
    { tail -20 "$WORK/rl-replay.log" >&2; exit 1; }

# --- 4. stage -----------------------------------------------------------------
mkdir -p "$LIB"
# dune's outputs are read-only; install gives the copies a normal mode.
for d in "$CTL/integers" "$CTL/ctypes" "$TGT/rl/_build/install/default/lib/raylib"; do
    find -L "$d" \( -path '*/top' -prune \) -o -type f \
        \( -name '*.cmi' -o -name '*.cmx' -o -name '*.cmxa' -o -name '*.a' \) -print |
        while read -r f; do install -m 644 "$f" "$LIB/"; done
done
cp "$WORK/libraylib.a" "$LIB/"
# Every archive must be a 5.4 Vex one, never a stale host file.
for f in "$LIB"/*.cmxa; do
    "$VO/bin/ocamlobjinfo" "$f" >/dev/null 2>&1 || { echo "not a Vex archive: $f" >&2; exit 1; }
done
echo "raylib: staged $(ls "$LIB" | wc -l) files in usr/lib/suchu ($(du -sh "$LIB" | cut -f1))"
