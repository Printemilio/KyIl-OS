#!/bin/bash
# stage_webkit.sh — stage a WPE/WebKit Linux runtime into the Vex-OS tree.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJ=$(cd "$SCRIPT_DIR/.." && pwd)
WEBKIT_BUILD=${WEBKIT_BUILD:-/root/webkit/build}
WEBKIT_MAIN_BIN=${WEBKIT_MAIN_BIN:-/tmp/webkit_linux_main}
VEX_WPE_BACKEND_BIN=${VEX_WPE_BACKEND_BIN:-/tmp/libvex_wpe_backend.so}
LIB_DEST="$PROJ/lib"
LIB64_DEST="$PROJ/lib64"
LIBEXEC_DEST="$PROJ/usr/local/libexec/wpe-webkit-2.0"
INJECTED_BUNDLE_DEST="$PROJ/usr/local/lib/wpe-webkit-2.0/injected-bundle"

mkdir -p "$LIB_DEST" "$LIB64_DEST" "$LIBEXEC_DEST" "$INJECTED_BUNDLE_DEST"

# A JIT-port validation must never silently package the old interpreter-only
# engine when the actual engine build has not completed.
if [ "${VEX_REQUIRE_JSC_JIT:-0}" = 1 ]; then
    for artifact in lib/libWPEWebKit-2.0.so.1 bin/WPEWebProcess bin/WPENetworkProcess bin/jsc lib/libWPEInjectedBundle.so; do
        test -f "$WEBKIT_BUILD/$artifact" || { echo "Missing rebuilt engine: $artifact" >&2; exit 1; }
    done
    strings "$WEBKIT_BUILD/lib/libWPEWebKit-2.0.so.1" | grep -F 'VexJIT: dual-alias arena' >/dev/null || {
        echo 'Engine does not contain the real Vex JIT adapter' >&2; exit 1;
    }
fi

if [ -d "$WEBKIT_BUILD/lib" ]; then
    rm -f "$LIB_DEST"/libWPEWebKit-2.0.so*
    rm -f "$LIB_DEST"/libWPEInjectedBundle.so*
    rm -f "$LIB_DEST"/libwpe-1.0.so*
    rm -f "$INJECTED_BUNDLE_DEST"/libWPEInjectedBundle.so*
fi

copy_real_file() {
    local src="$1"
    local dst="$2"
    if [ "$src" = "$dst" ]; then
        return 0
    fi
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
echo "=== Staging Vex WPE backend ==="
if [ -f "$VEX_WPE_BACKEND_BIN" ]; then
    copy_real_file "$VEX_WPE_BACKEND_BIN" "$LIB_DEST/libvex_wpe_backend.so"
    # libwpe's legacy loader looks for this exact implementation name unless
    # the application calls wpe_loader_init() explicitly.  Keep the alias in
    # the ISO as well so WPEWebProcess and future launchers select the same
    # VexOS backend instead of falling through to X11/Wayland defaults.
    copy_real_file "$VEX_WPE_BACKEND_BIN" "$LIB_DEST/libWPEBackend-default.so"
    chmod 755 "$LIB_DEST/libvex_wpe_backend.so"
    chmod 755 "$LIB_DEST/libWPEBackend-default.so"
    echo "  copied libvex_wpe_backend.so"
    echo "  copied libWPEBackend-default.so"
else
    echo "  WARNING: libvex_wpe_backend.so not found"
fi

echo ""
echo "=== Staging real shared EGL/GLES renderer ==="
VEX_GRAPHICS_DIR=${VEX_GRAPHICS_DIR:-"$PROJ/build/gl-runtime"}
for library in libvex_graphics.so.1 libEGL.so.1 libGLESv2.so.2 libGL.so.1; do
    test -f "$VEX_GRAPHICS_DIR/$library" || { echo "Missing real graphics runtime: $library" >&2; exit 1; }
    copy_real_file "$VEX_GRAPHICS_DIR/$library" "$LIB_DEST/$library"
done
ln -sf libEGL.so.1 "$LIB_DEST/libEGL.so"
ln -sf libGLESv2.so.2 "$LIB_DEST/libGLESv2.so"
ln -sf libGL.so.1 "$LIB_DEST/libGL.so"
# Each loader alias only depends on libvex_graphics.so.1. All GL/EGL state lives
# in that single DSO even when ISO staging flattens aliases into regular files.

if [ -n "${VEX_ABORT_TRACE_BIN:-}" ] && [ -f "$VEX_ABORT_TRACE_BIN" ]; then
    copy_real_file "$VEX_ABORT_TRACE_BIN" "$LIB_DEST/libvex_abort_trace.so"
    chmod 755 "$LIB_DEST/libvex_abort_trace.so"
    echo "  staged diagnostic abort unwinder"
else
    rm -f "$LIB_DEST/libvex_abort_trace.so"
    echo "  diagnostic abort unwinder not staged (opt-in only)"
fi

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
WPEWEBKIT_REAL=$(find_first_existing 'libWPEWebKit-2.0.so.*' "$WEBKIT_BUILD/lib" "$LIB_DEST" /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib)
if [ -n "${WPEWEBKIT_REAL:-}" ]; then
    stage_with_aliases "$WPEWEBKIT_REAL" \
        "$LIB_DEST/$(basename "$WPEWEBKIT_REAL")" \
        "$LIB_DEST/libWPEWebKit-2.0.so.1" \
        "$LIB_DEST/libWPEWebKit-2.0.so"
    echo "  staged libWPEWebKit from $(basename "$WPEWEBKIT_REAL")"
else
    echo "  WARNING: libWPEWebKit-2.0 runtime not found"
fi

INJECTED_BUNDLE=$(find_first_existing 'libWPEInjectedBundle.so*' "$WEBKIT_BUILD/lib" "$LIB_DEST" /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib)
if [ -n "${INJECTED_BUNDLE:-}" ]; then
    stage_with_aliases "$INJECTED_BUNDLE" \
        "$LIB_DEST/$(basename "$INJECTED_BUNDLE")" \
        "$LIB_DEST/libWPEInjectedBundle.so" \
        "$INJECTED_BUNDLE_DEST/$(basename "$INJECTED_BUNDLE")" \
        "$INJECTED_BUNDLE_DEST/libWPEInjectedBundle.so"
    echo "  staged libWPEInjectedBundle (lib + /usr/local/lib/wpe-webkit-2.0/injected-bundle)"
fi

echo ""
echo "=== Staging libwpe ==="
LIBWPE_REAL=$(find_first_existing 'libwpe-1.0.so.*' "${VEX_WPE_LIB_DIR:-/usr/local/lib}" /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib)
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
        LD_LIBRARY_PATH="$WEBKIT_BUILD/lib:${VEX_WPE_LIB_DIR:-/usr/local/lib}:/usr/local/lib" ldd "$B" 2>/dev/null
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
    if [ "${VEX_REQUIRE_JSC_JIT:-0}" = 1 ]; then
        # ld-linux and libc share GLIBC_PRIVATE internals. A real engine
        # rebuild must not mix the host loader with an older staged libc.
        for runtime in libc.so.6 libm.so.6 libresolv.so.2; do
            test -f "/lib/x86_64-linux-gnu/$runtime" || { echo "Missing matching glibc runtime: $runtime" >&2; exit 1; }
            copy_real_file "/lib/x86_64-linux-gnu/$runtime" "$LIB_DEST/$runtime"
        done
    fi
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

# Real system MIME database consumed by GIO/WebKit for file navigation.
if [ -r /usr/share/mime/mime.cache ]; then
    mkdir -p "$PROJ/usr/share/mime"
    cp /usr/share/mime/mime.cache "$PROJ/usr/share/mime/mime.cache"
fi

# GIO's actual TLS provider and trust store, never a browser-private transport.
# VSL canonicalizes Debian multiarch data paths to /usr/lib; stage the plugin
# at that canonical path as well as retaining genuine system dependencies.
TLS_MODULE=''
for candidate in /usr/lib/x86_64-linux-gnu/gio/modules/libgiognutls.so \
    "$PROJ/build/tls-debs/extracted/usr/lib/x86_64-linux-gnu/gio/modules/libgiognutls.so" \
    "$PROJ/usr/lib/gio/modules/libgiognutls.so" \
    "$PROJ/usr/lib/x86_64-linux-gnu/gio/modules/libgiognutls.so"; do
    if [ -r "$candidate" ]; then TLS_MODULE="$candidate"; break; fi
done
if [ -n "$TLS_MODULE" ]; then
    TLS_DIR="$PROJ/usr/lib/gio/modules"
    mkdir -p "$TLS_DIR" "$PROJ/etc/ssl/certs"
    copy_real_file "$TLS_MODULE" "$TLS_DIR/libgiognutls.so"
    TLS_DEPS=$(LD_LIBRARY_PATH="$PROJ/build/tls-debs/extracted/usr/lib/x86_64-linux-gnu:$LIB_DEST" ldd "$TLS_MODULE")
    if printf '%s\n' "$TLS_DEPS" | grep -q 'not found'; then
        printf '%s\n' "$TLS_DEPS" >&2; exit 1
    fi
    while IFS= read -r dependency; do
        [ -r "$dependency" ] || continue
        name=$(basename "$dependency")
        [ -f "$LIB_DEST/$name" ] || copy_real_file "$dependency" "$LIB_DEST/$name"
    done < <(printf '%s\n' "$TLS_DEPS" | sed -n 's/.*=> \(\/.*\) (0x.*/\1/p')
    gio-querymodules "$TLS_DIR"
    if [ -r /etc/ssl/certs/ca-certificates.crt ]; then
        cp /etc/ssl/certs/ca-certificates.crt "$PROJ/etc/ssl/certs/"
    elif [ ! -s "$PROJ/etc/ssl/certs/ca-certificates.crt" ]; then
        echo 'No real CA trust store available for TLS staging.' >&2; exit 1
    fi
    echo 'Staged real GIO GnuTLS module, dependencies and CA trust store.'
else
    echo 'No GIO TLS provider available; HTTPS runtime is unavailable.' >&2
fi

# Fontconfig's generic families are defined by these real configuration files,
# not by fonts.conf alone. Without conf.d, Skia rejects the unmatched generic
# family and WebCore measures an empty typeface while fallback glyphs render.
echo "=== Staging complete Fontconfig configuration ==="
FONTCONFIG_SOURCE=${VEX_FONTCONFIG_SOURCE:-/etc/fonts}
if [ ! -f "$FONTCONFIG_SOURCE/fonts.conf" ] || [ ! -d "$FONTCONFIG_SOURCE/conf.d" ]; then
    echo "Missing Fontconfig configuration/aliases: $FONTCONFIG_SOURCE" >&2
    exit 1
fi
copy_real_file "$FONTCONFIG_SOURCE/fonts.conf" "$PROJ/etc/fonts/fonts.conf"
mkdir -p "$PROJ/etc/fonts/conf.d"
for config in "$FONTCONFIG_SOURCE"/conf.d/*.conf; do
    test -f "$config" || { echo "Broken Fontconfig include: $config" >&2; exit 1; }
    # Resolve host symlinks into real files: ISO runtime paths must not depend
    # on the host's conf.avail layout or filesystem symlink representation.
    copy_real_file "$config" "$PROJ/etc/fonts/conf.d/$(basename "$config")"
done
