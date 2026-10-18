#!/bin/bash
# build_and_stage_webkit.sh — compile webkit_linux_main and stage the WPE runtime.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJ=$(cd "$SCRIPT_DIR/.." && pwd)
WEBKIT_BUILD=${WEBKIT_BUILD:-/root/webkit/build}
SRC="$PROJ/userspace/apps/webkit_linux_main.c"
DOWNLOADS_SRC="$PROJ/userspace/apps/webkit_downloads.c"
OUT="/tmp/webkit_linux_main"
BACKEND_SRC="$PROJ/userspace/libvex/src/vex_wpe_backend.c"
BACKEND_OUT="/tmp/libvex_wpe_backend.so"
CC=${CC:-$(command -v clang || command -v gcc)}

if [ -z "${CC:-}" ]; then
    echo "No C compiler (clang or gcc) found in WSL." >&2
    exit 1
fi
TRACE_CC=${TRACE_CC:-$(command -v gcc || printf '%s' "$CC")}

echo "=== Compiling webkit_linux_main ==="
if [ -d "$WEBKIT_BUILD" ]; then
    export PKG_CONFIG_PATH="$WEBKIT_BUILD${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    read -r -a CFLAGS <<< "$(pkg-config --cflags wpe-webkit-2.0-uninstalled glib-2.0 gio-2.0)"
    read -r -a LIBS <<< "$(pkg-config --libs wpe-webkit-2.0-uninstalled glib-2.0 gio-2.0)"
    EXTRA_LIB_DIR="$WEBKIT_BUILD/lib"
else
    echo "Missing WEBKIT_BUILD directory: $WEBKIT_BUILD; using staged WPE runtime fallback."
    read -r -a CFLAGS <<< "$(pkg-config --cflags glib-2.0 gio-2.0 gobject-2.0)"
    CFLAGS=("-I$PROJ/userspace/wpe_stub" "${CFLAGS[@]}")
    read -r -a LIBS <<< "$(pkg-config --libs glib-2.0 gio-2.0 gobject-2.0)"
    # The staged WPE runtime is built against libsoup, but the host WSL
    # installation does not necessarily ship a pkg-config file for it.  Keep
    # the dependency explicit so ld can resolve the DT_NEEDED edge while
    # linking webkit_linux_main against the staged shared objects.
    # libsoup is shipped as a versioned runtime only, so pass its concrete
    # path instead of relying on an absent lib*.so linker symlink.
    LIBS=("-lWPEWebKit-2.0" "-lwpe-1.0" "$PROJ/lib/libsoup-3.0.so.0" "${LIBS[@]}")
    EXTRA_LIB_DIR="$PROJ/lib"
fi

WEBKIT_TEST_FLAGS=()
if [ "${WEBKIT_RENDER_SMOKE:-0}" = 1 ]; then
    WEBKIT_TEST_FLAGS=(-DVEX_WEBKIT_RENDER_SMOKE)
    mkdir -p "$PROJ/usr/linux-tests"
    cp "$PROJ/userspace/linux_tests/webkit-render.html" "$PROJ/usr/linux-tests/"
fi
if [ "${WEBKIT_RUNTIME_TRACE:-0}" = 1 ]; then
    WEBKIT_TEST_FLAGS+=(-DVEX_WEBKIT_RUNTIME_TRACE -Wl,--export-dynamic)
fi
"$CC" -O2 -std=c11 "${WEBKIT_TEST_FLAGS[@]}" "$SRC" "$DOWNLOADS_SRC" \
    -I"$PROJ/userspace/libvex/include" \
    -idirafter "$PROJ/kernel/include" \
    "${CFLAGS[@]}" \
    -L"$EXTRA_LIB_DIR" \
    -Wl,-rpath-link,"$EXTRA_LIB_DIR" \
    -Wl,-rpath,/lib \
    "${LIBS[@]}" \
    -lpthread -lm -ldl \
    -o "$OUT"

echo "COMPILE OK → $OUT"
echo ""

echo "=== Compiling libvex_wpe_backend.so ==="
read -r -a BACKEND_CFLAGS <<< "$(pkg-config --cflags glib-2.0 gio-unix-2.0)"
read -r -a BACKEND_LIBS <<< "$(pkg-config --libs glib-2.0 gio-unix-2.0)"
if pkg-config --exists wpe-1.0 2>/dev/null && [ -f "$(pkg-config --variable=includedir wpe-1.0)/wpe-1.0/wpe/wpe-egl.h" ]; then
    read -r -a WPE_CFLAGS <<< "$(pkg-config --cflags wpe-1.0)"
    read -r -a WPE_LIBS <<< "$(pkg-config --libs wpe-1.0)"
else
    test -f "$PROJ/userspace/wpe_stub/wpe/wpe-egl.h" || { echo "Missing WPE ABI declarations" >&2; exit 1; }
    WPE_CFLAGS=("-I$PROJ/userspace/wpe_stub")
    WPE_LIBS=("-L$PROJ/lib" "-lwpe-1.0")
fi
"$CC" -shared -fPIC -O2 -g -std=c11 -DVEX_HOSTED_BUILD "$BACKEND_SRC" \
    -I"$PROJ/userspace/libvex/include" -idirafter "$PROJ/kernel/include" \
    "${BACKEND_CFLAGS[@]}" "${WPE_CFLAGS[@]}" \
    "${BACKEND_LIBS[@]}" "${WPE_LIBS[@]}" -Wl,-z,defs -o "$BACKEND_OUT"
echo "COMPILE OK -> $BACKEND_OUT"
echo ""

echo '=== Building real shared Vex EGL/GLES/core ==='
VEX_GL_STAGE=1 /bin/sh "$SCRIPT_DIR/build-gl-runtime.sh"

if [ "${VEX_GL_TRACE:-0}" = 1 ]; then
    echo "=== Compiling opt-in abort/CFI diagnostic preload ==="
    ABORT_TRACE_SRC="$PROJ/userspace/libvex/src/abort_trace.c"
    ABORT_TRACE_OUT="/tmp/libvex_abort_trace.so"
    "$TRACE_CC" -shared -fPIC -O1 -g -fno-omit-frame-pointer -fno-stack-protector \
        -std=c11 "$ABORT_TRACE_SRC" -ldl -o "$ABORT_TRACE_OUT"
    echo "COMPILE OK -> $ABORT_TRACE_OUT"
    echo ""
    WEBKIT_MAIN_BIN="$OUT" VEX_WPE_BACKEND_BIN="$BACKEND_OUT" \
        VEX_GRAPHICS_DIR="$PROJ/build/gl-runtime" \
        VEX_ABORT_TRACE_BIN="$ABORT_TRACE_OUT" WEBKIT_BUILD="$WEBKIT_BUILD" \
        bash "$SCRIPT_DIR/stage_webkit.sh"
else
    WEBKIT_MAIN_BIN="$OUT" VEX_WPE_BACKEND_BIN="$BACKEND_OUT" \
        VEX_GRAPHICS_DIR="$PROJ/build/gl-runtime" \
        WEBKIT_BUILD="$WEBKIT_BUILD" bash "$SCRIPT_DIR/stage_webkit.sh"
fi
