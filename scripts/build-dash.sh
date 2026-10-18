#!/usr/bin/env bash
# Build dash, the native /bin/sh of Vex, cross-compiled with the Vex C
# library. Its build-time generators (mknodes, mksyntax, ...) run on the build
# machine with the host compiler.
#
#   scripts/build-dash.sh
#
# Result: usr/bin/sh in the repository, staged on the ISO as /usr/bin/sh
# (the kernel resolves /bin/sh to it).
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC="$ROOT/build/third_party/src/dash-0.5.12"
WORK=${VEX_TOOLCHAIN_WORK:-$HOME/.cache/vexos}/dash
JOBS=${JOBS:-$(nproc)}
export PATH="$ROOT/toolchain/bin:$PATH"

[ -f "$SRC/.vex-patched" ] || sh "$ROOT/scripts/fetch-third-party.sh" dash

rm -rf "$WORK"
mkdir -p "$WORK/src"
tar -C "$SRC" --exclude=.git -cf - . | tar -C "$WORK/src" -xf -
cd "$WORK/src"
./configure --build=x86_64-pc-linux-gnu --host=x86_64-unknown-vexos --prefix=/usr \
    CC=x86_64-unknown-vexos-gcc CC_FOR_BUILD=gcc CFLAGS="-O2" LDFLAGS="-static" \
    --disable-fnmatch --disable-glob --without-libedit \
    > "$WORK/configure.log"
make -j"$JOBS" > "$WORK/make.log" 2>&1 || { tail -40 "$WORK/make.log"; exit 1; }

mkdir -p "$ROOT/usr/bin"
install -m 755 src/dash "$ROOT/usr/bin/sh"
x86_64-unknown-vexos-strip "$ROOT/usr/bin/sh"
ls -la "$ROOT/usr/bin/sh"
