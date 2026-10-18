#!/usr/bin/env bash
# Build GNU as and ld that run natively on Vex (host = target =
# x86_64-unknown-vexos), cross-compiled with the Vex C library.
#
#   scripts/build-binutils.sh
#
# Sources: build/third_party/src/binutils-2.42 (scripts/fetch-third-party.sh,
# patched by third_party/patches/binutils). The build runs in
# $VEX_TOOLCHAIN_WORK (default ~/.cache/vexos/binutils) because the repository
# path contains spaces. Result: usr/bin/as and usr/bin/ld in the repository,
# staged on the ISO as /usr/bin/as and /usr/bin/ld.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC="$ROOT/build/third_party/src/binutils-2.42"
WORK=${VEX_TOOLCHAIN_WORK:-$HOME/.cache/vexos}/binutils
JOBS=${JOBS:-$(nproc)}
export PATH="$ROOT/toolchain/bin:$PATH"

[ -f "$SRC/.vex-patched" ] || sh "$ROOT/scripts/fetch-third-party.sh" binutils

# Configure from a copy without spaces in its path.
rm -rf "$WORK"
mkdir -p "$WORK/build" "$WORK/stage" "$WORK/src"
tar -C "$SRC" --exclude=.git -cf - . | tar -C "$WORK/src" -xf -
cd "$WORK/build"
../src/configure \
    --build=x86_64-pc-linux-gnu --host=x86_64-unknown-vexos --target=x86_64-unknown-vexos \
    --prefix=/usr --program-prefix= \
    CC=x86_64-unknown-vexos-gcc AR=x86_64-unknown-vexos-ar RANLIB=x86_64-unknown-vexos-ranlib \
    CFLAGS="-O2" LDFLAGS="-static" \
    --disable-nls --disable-werror --disable-shared --enable-static \
    --disable-gdb --disable-gdbserver --disable-gprof --disable-gprofng --disable-gold \
    --disable-plugins --disable-lto --disable-libctf --disable-sim --disable-readline \
    --without-zstd --without-debuginfod --without-msgpack --with-system-zlib=no \
    > "$WORK/configure.log"
make -j"$JOBS" all-gas all-ld > "$WORK/make.log" 2>&1 || { tail -40 "$WORK/make.log"; exit 1; }
make DESTDIR="$WORK/stage" install-gas install-ld > "$WORK/install.log" 2>&1

mkdir -p "$ROOT/usr/bin"
for tool in as ld; do
    install -m 755 "$WORK/stage/usr/bin/$tool" "$ROOT/usr/bin/$tool"
    x86_64-unknown-vexos-strip "$ROOT/usr/bin/$tool"
done
ls -la "$ROOT/usr/bin/as" "$ROOT/usr/bin/ld"
