#!/bin/bash
# stage_webkit.sh — stage a WPE/WebKit Linux runtime into the Vex-OS tree.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJ=$(cd "$SCRIPT_DIR/.." && pwd)
WEBKIT_BUILD=${WEBKIT_BUILD:-/root/webkit/build}
WEBKIT_MAIN_BIN=${WEBKIT_MAIN_BIN:-/tmp/webkit_linux_main}
LIB_DEST="$PROJ/lib"
LIB64_DEST="$PROJ/lib64"
LIBEXEC_DEST="$PROJ/usr/local/libexec/wpe-webkit-2.0"

mkdir -p "$LIB_DEST" "$LIB64_DEST" "$LIBEXEC_DEST"

rm -f "$LIB_DEST"/libWPEWebKit-2.0.so*
rm -f "$LIB_DEST"/libWPEInjectedBundle.so*
rm -f "$LIB_DEST"/libwpe-1.0.so*

copy_real_file() {
    local src="$1"
    local dst="$2"
    mkdir -p "$(dirname "$dst")"
    cp -Lf "$src" "$dst"
}

copy_if_present() {
    local src="$1"
    local dst="$2"
    if [ -f "$src" ] || [ -L "$src" ]; then
        copy_real_file "$src" "$dst"
        echo "  copied $(basename "$dst")"
        return 0
    fi
    return 1
}

stage_with_aliases() {
    local src="$1"
    shift
    local dst
    for dst in "$@"; do
        copy_real_file "$src" "$dst"
    done
}

find_first_existing() {
    local pattern="$1"
    shift
    local dir
    for dir in "$@"; do
        if [ -d "$dir" ]; then
            find "$dir" -maxdepth 1 \( -type f -o -type l \) -name "$pattern" | sort | head -1
        fi
    done | head -1
}

echo "=== Staging webkit_linux_main binary ==="
if [ ! -f "$WEBKIT_MAIN_BIN" ]; then
    echo "Missing Linux-side runtime binary: $WEBKIT_MAIN_BIN" >&2
    exit 1
fi
copy_real_file "$WEBKIT_MAIN_BIN" "$PROJ/webkit_linux_main"
chmod 755 "$PROJ/webkit_linux_main"
echo "  copied webkit_linux_main"

echo ""
echo "=== Staging WPE helper processes ==="
for BIN in WPEWebProcess WPENetworkProcess; do
    SRC="$WEBKIT_BUILD/bin/$BIN"
    if copy_if_present "$SRC" "$LIBEXEC_DEST/$BIN"; then
        chmod 755 "$LIBEXEC_DEST/$BIN"
    else
        echo "  SKIP (not found): $BIN"
    fi
done
for BIN in WPEWebDriver jsc; do
    SRC="$WEBKIT_BUILD/bin/$BIN"
    if copy_if_present "$SRC" "$PROJ/$BIN"; then
        chmod 755 "$PROJ/$BIN"
    fi
done

echo ""
echo "=== Staging WebKit libs ==="
WPEWEBKIT_REAL=$(find_first_existing 'libWPEWebKit-2.0.so.*' "$WEBKIT_BUILD/lib" /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib)
if [ -n "${WPEWEBKIT_REAL:-}" ]; then
    stage_with_aliases "$WPEWEBKIT_REAL" \
        "$LIB_DEST/$(basename "$WPEWEBKIT_REAL")" \
        "$LIB_DEST/libWPEWebKit-2.0.so.1" \
        "$LIB_DEST/libWPEWebKit-2.0.so"
    echo "  staged libWPEWebKit from $(basename "$WPEWEBKIT_REAL")"
else
    echo "  WARNING: libWPEWebKit-2.0 runtime not found"
fi

INJECTED_BUNDLE=$(find_first_existing 'libWPEInjectedBundle.so*' "$WEBKIT_BUILD/lib" /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib)
if [ -n "${INJECTED_BUNDLE:-}" ]; then
    stage_with_aliases "$INJECTED_BUNDLE" \
        "$LIB_DEST/$(basename "$INJECTED_BUNDLE")" \
        "$LIB_DEST/libWPEInjectedBundle.so"
    echo "  staged libWPEInjectedBundle"
fi

echo ""
echo "=== Staging libwpe ==="
LIBWPE_REAL=$(find_first_existing 'libwpe-1.0.so.*' /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib)
if [ -n "${LIBWPE_REAL:-}" ]; then
    stage_with_aliases "$LIBWPE_REAL" \
        "$LIB_DEST/$(basename "$LIBWPE_REAL")" \
        "$LIB_DEST/libwpe-1.0.so.1" \
        "$LIB_DEST/libwpe-1.0.so"
    echo "  staged libwpe from $(basename "$LIBWPE_REAL")"
fi

echo ""
echo "=== Staging system .so deps (ldd closure) ==="
BINARIES=("$PROJ/webkit_linux_main" "$LIBEXEC_DEST/WPEWebProcess" "$LIBEXEC_DEST/WPENetworkProcess")
ALL_LIBS=$(
    for B in "${BINARIES[@]}"; do
        [ -f "$B" ] || continue
        LD_LIBRARY_PATH="$WEBKIT_BUILD/lib:/usr/local/lib" ldd "$B" 2>/dev/null
    done | awk '{print $3}' | grep '^/' | grep -v '/lib64/ld' | sort -u
)

COPIED=0
for SOPATH in $ALL_LIBS; do
    SONAME=$(basename "$SOPATH")
    [ -f "$LIB_DEST/$SONAME" ] && continue
    [ -f "$SOPATH" ] || continue
    copy_real_file "$SOPATH" "$LIB_DEST/$SONAME"
    echo "  dep: $SONAME"
    COPIED=$((COPIED + 1))
done
echo "  $COPIED system libs staged."

echo ""
echo "=== Staging ld-linux ==="
LD_SRC=$(find /lib64 /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu -name "ld-linux-x86-64.so.2" 2>/dev/null | head -1)
if [ -n "${LD_SRC:-}" ]; then
    copy_real_file "$LD_SRC" "$LIB64_DEST/ld-linux-x86-64.so.2"
    copy_real_file "$LD_SRC" "$PROJ/ld-linux-x86-64.so.2"
    echo "  copied ld-linux from $LD_SRC"
else
    echo "  WARNING: ld-linux-x86-64.so.2 not found"
fi

echo ""
echo "=== Summary ==="
echo "Binaries:"
ls -lh "$PROJ/webkit_linux_main" "$LIBEXEC_DEST/WPEWebProcess" "$LIBEXEC_DEST/WPENetworkProcess" 2>/dev/null | awk '{print "  "$NF" ("$5")"}'
echo "Libs total: $(ls "$LIB_DEST" | wc -l)"
echo "DONE"
