#!/usr/bin/env bash
# Test the actual vendored renderer; reference disables only our fast paths.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OUT=build/raylib-optimization
mkdir -p "$OUT/patched"
cp build/third_party/src/raylib-ocaml-2.2.1/src/c/vendor/raylib/src/external/rlsw.h "$OUT/patched/rlsw.h"
python3 scripts/patch-raylib-software.py "$OUT/patched/rlsw.h"
gcc -O2 -I. -I"$OUT/patched" tests/rlsw_optimization_test.c -lm -o "$OUT/fast-test"
gcc -O2 -DRLSW_VEX_DISABLE_OPTIMIZATIONS -I. -I"$OUT/patched" tests/rlsw_optimization_test.c -lm -o "$OUT/reference-test"
"$OUT/fast-test" "$OUT/fast.bin"
"$OUT/reference-test" "$OUT/reference.bin"
cmp "$OUT/fast.bin" "$OUT/reference.bin"
gcc -O1 -g -fsanitize=address,undefined -I. -I"$OUT/patched" tests/rlsw_optimization_test.c -lm -o "$OUT/asan-test"
"$OUT/asan-test" "$OUT/asan.bin"
echo 'RLSW: 136 scenes pixel-identical; semantic alpha/blend/clamp checks and ASan/UBSan passed'
