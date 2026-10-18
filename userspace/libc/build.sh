#!/usr/bin/env bash
# Build the Vex C library (musl portable code + native Vex OS layer) into a
# sysroot usable by a cross toolchain:
#
#   build/vex-sysroot/include       public headers
#   build/vex-sysroot/lib/crt1.o    program entry (carries .note.vexos)
#   build/vex-sysroot/lib/crti.o crtn.o
#   build/vex-sysroot/lib/libc.a    the library (libm, libpthread, ... live in it)
#   build/vex-sysroot/lib/libm.a libpthread.a librt.a libdl.a libutil.a libxnet.a
#                                   empty archives, so that -lm etc. link
#
# usage: bash userspace/libc/build.sh [-j N]
# Environment: CC (default gcc), AR (default ar), CFLAGS_EXTRA.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
VEX="$ROOT/userspace/libc"
MUSL="$ROOT/build/third_party/src/musl-1.2.5"
OUT="$ROOT/build/libc"
SYSROOT="$ROOT/build/vex-sysroot"
CC=${CC:-gcc}
AR=${AR:-ar}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
[ "${1:-}" = "-j" ] && JOBS=$2

[ -d "$MUSL/src" ] || sh "$ROOT/scripts/fetch-third-party.sh" musl

mkdir -p "$OUT/include/bits" "$OUT/src-internal" "$OUT/obj" "$SYSROOT/lib" "$SYSROOT/include"

# Generated headers. bits/syscall.h is deliberately absent: there are no
# Linux system call numbers on Vex.
sed -f "$MUSL/tools/mkalltypes.sed" "$MUSL/arch/x86_64/bits/alltypes.h.in" \
    "$MUSL/include/alltypes.h.in" > "$OUT/include/bits/alltypes.h.new"
cmp -s "$OUT/include/bits/alltypes.h.new" "$OUT/include/bits/alltypes.h" 2>/dev/null ||
    mv "$OUT/include/bits/alltypes.h.new" "$OUT/include/bits/alltypes.h"
rm -f "$OUT/include/bits/alltypes.h.new"
echo '#define VERSION "1.2.5-vex"' > "$OUT/src-internal/version.h"

# Compiler flags live in a response file: the checkout path may contain
# spaces, which a shell variable cannot carry through word splitting.
RSP="$OUT/cflags.rsp"
{
    for f in -std=c99 -nostdinc -ffreestanding -fexcess-precision=standard \
        -frounding-math -fno-strict-aliasing -Wa,--noexecstack \
        -D_XOPEN_SOURCE=700 -O2 -pipe -fomit-frame-pointer -fno-unwind-tables \
        -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
        -fno-stack-protector -fno-pie -fno-tree-loop-distribute-patterns \
        -Werror=implicit-function-declaration -Werror=implicit-int \
        -Werror=pointer-sign -Werror=pointer-arith -Werror=int-conversion \
        -Wno-unused-function ${CFLAGS_EXTRA:-}; do
        echo "$f"
    done
    for d in "$MUSL/arch/x86_64" "$MUSL/arch/generic" "$OUT/src-internal" \
        "$MUSL/src/include" "$MUSL/src/internal" "$VEX/include-internal" \
        "$OUT/include" "$VEX/include" "$MUSL/include"; do
        echo "\"-I$d\""
    done
    echo -idirafter
    echo "\"$ROOT/kernel/include\""
} > "$RSP.new"
cmp -s "$RSP.new" "$RSP" 2>/dev/null && rm -f "$RSP.new" || { mv "$RSP.new" "$RSP"; touch "$OUT/stamp"; }
export CC OUT RSP

python3 "$VEX/sources.py" "$MUSL" "$VEX" > "$OUT/sources.txt"
python3 "$VEX/sources.py" "$MUSL" "$VEX" --report > "$OUT/omitted.txt"

# One object per source; objects are named after their path so that equal
# basenames in different directories never collide.
compile() {
    src=$1
    case "$src" in
        "$MUSL"/*) rel=musl/${src#"$MUSL"/} ;;
        *) rel=vex/${src#"$VEX"/} ;;
    esac
    obj="$OUT/obj/$(echo "$rel" | tr '/' '_' | sed 's/\.[csS]$/.o/')"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$OUT/stamp" -nt "$obj" ]; then
        $CC @"$RSP" -c "$src" -o "$obj" || { echo "FAILED: $src" >&2; return 1; }
    fi
}
export -f compile
export MUSL VEX

# Headers or flags changed: rebuild everything.
for f in "$VEX/include-internal"/*.h "$MUSL/src/internal"/*.h "$ROOT"/kernel/include/unos/vex_*abi.h "$0"; do
    [ "$f" -nt "$OUT/stamp" ] 2>/dev/null && { touch "$OUT/stamp"; break; }
done
[ -f "$OUT/stamp" ] || touch "$OUT/stamp"

status=0
xargs -P "$JOBS" -I{} bash -c 'compile "$@"' _ {} < "$OUT/sources.txt" || status=1
if [ $status -ne 0 ]; then
    echo "libc: compilation failed" >&2
    exit 1
fi

rm -f "$OUT/libc.a.new"
# Objects from the current source list only (stale objects of removed files
# must not linger in the archive).
while read -r src; do
    case "$src" in
        "$MUSL"/*) rel=musl/${src#"$MUSL"/} ;;
        *) rel=vex/${src#"$VEX"/} ;;
    esac
    echo "\"$OUT/obj/$(echo "$rel" | tr '/' '_' | sed 's/\.[csS]$/.o/')\""
done < "$OUT/sources.txt" > "$OUT/objects.rsp"
$AR rcs "$OUT/libc.a.new" @"$OUT/objects.rsp"
# Each symbol must have exactly one strong definition: a Vex function and a
# musl function of the same name would otherwise depend on link order.
dups=$(nm -A --defined-only "$OUT/libc.a.new" 2>/dev/null |
    awk '$(NF-1) ~ /^[TDRB]$/ { print $NF }' | sort | uniq -d)
if [ -n "$dups" ]; then
    echo "libc: symbols defined more than once:" >&2
    echo "$dups" >&2
    exit 1
fi
mv "$OUT/libc.a.new" "$SYSROOT/lib/libc.a"

# Startup objects.
$CC @"$RSP" -c "$MUSL/crt/crt1.c" -o "$SYSROOT/lib/crt1.o"
$CC @"$RSP" -c "$MUSL/crt/x86_64/crti.s" -o "$SYSROOT/lib/crti.o"
$CC @"$RSP" -c "$MUSL/crt/x86_64/crtn.s" -o "$SYSROOT/lib/crtn.o"
for lib in m pthread rt dl util xnet crypt resolv; do
    rm -f "$SYSROOT/lib/lib$lib.a"
    $AR rc "$SYSROOT/lib/lib$lib.a"
done

# Public headers: musl's, plus the generated bits, minus the Linux system
# call interface.
rm -rf "$SYSROOT/include"
mkdir -p "$SYSROOT/include"
cp -R "$MUSL/include/." "$SYSROOT/include/"
cp -R "$MUSL/arch/generic/bits/." "$SYSROOT/include/bits/"
cp -R "$MUSL/arch/x86_64/bits/." "$SYSROOT/include/bits/"
cp "$OUT/include/bits/alltypes.h" "$SYSROOT/include/bits/alltypes.h"
rm -f "$SYSROOT/include/bits/alltypes.h.in" "$SYSROOT/include/bits/syscall.h.in" \
      "$SYSROOT/include/alltypes.h.in" "$SYSROOT/include/sys/syscall.h" \
      "$SYSROOT/include/syscall.h"
# The library's own native Vex API (<vex/...>).
cp -R "$VEX/include/." "$SYSROOT/include/"
# Native Vex interface headers, for programs that use Vex APIs directly.
mkdir -p "$SYSROOT/include/unos"
for h in syscall_wire.h vex_thread_abi.h vex_file_abi.h vex_event_abi.h vex_memory_abi.h; do
    cp "$ROOT/kernel/include/unos/$h" "$SYSROOT/include/unos/"
done

echo "libc: $(wc -l < "$OUT/sources.txt") sources, $(wc -l < "$OUT/omitted.txt") Linux-only musl sources omitted"
echo "libc: sysroot ready at $SYSROOT"
