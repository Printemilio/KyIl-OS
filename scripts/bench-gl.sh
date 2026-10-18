#!/bin/sh
# Build libvex_graphics for the host (no staging, no self-test) and run the software
# renderer benchmark. Usage: scripts/bench-gl.sh [frames] [size]
set -eu
PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$PROJECT"
OUT=${VEX_GL_OUT:-"$PROJECT/build/gl-bench"}
VEX_GL_OUT="$OUT" VEX_GL_HOST_TEST=0 /bin/sh scripts/build-gl-runtime.sh
gcc -O2 -Ivendor/khronos-gl/include userspace/linux_tests/gl_render_bench.c \
    "$OUT/libvex_graphics.so.1" -lpthread -lm -Wl,-rpath,"$OUT" -o "$OUT/gl_render_bench"
"$OUT/gl_render_bench" "$@"
