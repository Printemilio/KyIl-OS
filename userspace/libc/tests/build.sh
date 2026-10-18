#!/usr/bin/env bash
# Build the native Vex libc conformance tests into usr/native-tests/, which
# build.ps1 stages on the ISO. Run after userspace/libc/build.sh.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CC="$ROOT/toolchain/bin/x86_64-unknown-vexos-gcc"
OUT="$ROOT/usr/native-tests"
mkdir -p "$OUT"
for src in "$ROOT"/userspace/libc/tests/*.c; do
    name=$(basename "$src" .c)
    "$CC" -O2 -g -Wall -Wextra -Wno-unused-parameter -o "$OUT/$name" "$src"
done
ls -la "$OUT"
