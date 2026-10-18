#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OUT=build/paint-retained
mkdir -p "$OUT/patched"
cp build/third_party/src/raylib-ocaml-2.2.1/src/c/vendor/raylib/src/external/rlsw.h "$OUT/patched/rlsw.h"
python3 scripts/patch-raylib-software.py "$OUT/patched/rlsw.h"
gcc -O1 -g -fsanitize=address,undefined -I. -Iuserspace/raylib -I"$OUT/patched" \
  tests/rlsw_canvas_cache_test.c -lm -o "$OUT/cache-test"
"$OUT/cache-test"
