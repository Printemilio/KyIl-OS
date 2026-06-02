#!/bin/bash
# build_and_stage_webkit.sh — compile webkit_linux_main and stage the WPE runtime.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJ=$(cd "$SCRIPT_DIR/.." && pwd)
WEBKIT_BUILD=${WEBKIT_BUILD:-/root/webkit/build}
SRC="$PROJ/userspace/apps/webkit_linux_main.c"
OUT="/tmp/webkit_linux_main"

if [ ! -d "$WEBKIT_BUILD" ]; then
    echo "Missing WEBKIT_BUILD directory: $WEBKIT_BUILD" >&2
    exit 1
fi

echo "=== Compiling webkit_linux_main ==="
export PKG_CONFIG_PATH="$WEBKIT_BUILD"

CFLAGS=$(pkg-config --cflags wpe-webkit-2.0-uninstalled glib-2.0 gio-2.0)
LIBS=$(pkg-config --libs wpe-webkit-2.0-uninstalled glib-2.0 gio-2.0)

clang -O2 -std=c11 "$SRC" \
  $CFLAGS \
  -L"$WEBKIT_BUILD/lib" \
  -Wl,-rpath,/lib \
  $LIBS \
  -lpthread -lm -ldl \
  -o "$OUT"

echo "COMPILE OK → $OUT"
echo ""

WEBKIT_MAIN_BIN="$OUT" WEBKIT_BUILD="$WEBKIT_BUILD" bash "$SCRIPT_DIR/stage_webkit.sh"
