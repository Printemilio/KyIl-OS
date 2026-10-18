#!/bin/sh
set -eu
PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$PROJECT"
TOOLS=${VEX_KHRONOS_TOOLS:-"$PROJECT/build/glslang-install"}
if [ -f "$TOOLS/lib/libglslang.a" ]; then
    COMPILER_LIB="$TOOLS/lib"
    COMPILER_OPT_LIBS=""
else
    COMPILER_LIB="$TOOLS/lib/x86_64-linux-gnu"
    COMPILER_OPT_LIBS="-lSPIRV-Tools-opt -lSPIRV-Tools"
fi
OUT=${VEX_GL_OUT:-"$PROJECT/build/gl-runtime"}
EXTRA=${VEX_GL_SANITIZERS:+-fsanitize=address,undefined -fno-omit-frame-pointer}
mkdir -p "$OUT"
test -f "$COMPILER_LIB/libglslang.a" || {
    echo "Missing Khronos glslang compiler under $TOOLS; run scripts/bootstrap-glslang.sh" >&2; exit 1;
}
for unit in gles egl vex_render_core; do
    gcc $EXTRA -DVEX_HOSTED_BUILD -fPIC -O2 -g -std=c11 \
        -Ivendor/khronos-gl/include -Ivendor/vulkan-headers/include \
        -Iuserspace/libvex/include -idirafter kernel/include \
        -c "userspace/libvex/src/$unit.c" -o "$OUT/$unit.o"
done
g++ $EXTRA -fno-rtti -fPIC -O2 -g -std=c++17 -Ivendor/glslang -I"$TOOLS/include" \
    -c userspace/libvex/src/vex_glsl.cpp -o "$OUT/compiler.o"
g++ $EXTRA -shared "$OUT/gles.o" "$OUT/egl.o" "$OUT/vex_render_core.o" "$OUT/compiler.o" \
    -L"$COMPILER_LIB" -Wl,--start-group \
    -lglslang -lSPIRV -lglslang-default-resource-limits $COMPILER_OPT_LIBS \
    -Wl,--end-group -lpthread -ldl -lm -Wl,-z,defs \
    -Wl,-soname,libvex_graphics.so.1 -o "$OUT/libvex_graphics.so.1"
gcc $EXTRA -DVEX_HOSTED_BUILD -O2 -g -Ivendor/khronos-gl/include -Iuserspace/libvex/include -idirafter kernel/include userspace/apps/egl_gles_test.c \
    "$OUT/libvex_graphics.so.1" -lpthread -Wl,-rpath,'$ORIGIN' -o "$OUT/egl_gles_test"
if [ "${VEX_GL_HOST_TEST:-1}" = 1 ]; then "$OUT/egl_gles_test"; fi

# Loader aliases contain no graphics implementation/state. This also works
# when ISO staging dereferences symlinks: EGL/GLES always share one core DSO.
for alias in libEGL.so.1 libGLESv2.so.2 libGL.so.1; do
    gcc -shared -x c /dev/null -x none -Wl,--no-as-needed "$OUT/libvex_graphics.so.1" \
        -Wl,-soname,"$alias" -Wl,-rpath,/lib -o "$OUT/$alias"
done
if [ "${VEX_GL_STAGE:-0}" = 1 ]; then
    cp "$OUT/libvex_graphics.so.1" "$OUT/libEGL.so.1" "$OUT/libGLESv2.so.2" "$OUT/libGL.so.1" "$PROJECT/lib/"
    cp "$OUT/egl_gles_test" "$PROJECT/usr/linux-tests/"
fi
