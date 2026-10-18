#!/bin/bash
# Rebuild the actual engine, not just webkit_linux_main. No staged fallback.
set -euo pipefail
PROJ=$(cd "$(dirname "$0")/.." && pwd)
WORK=${VEX_WEBKIT_WORKDIR:-/root/vex-webkit}
VERSION=2.53.3
SOURCE="$WORK/wpewebkit-$VERSION"
BUILD="$WORK/build"
ARCHIVE="$PROJ/build/webkit-source/wpewebkit-$VERSION.tar.xz"
mkdir -p "$WORK" "$(dirname "$ARCHIVE")"
if [ ! -f "$ARCHIVE" ]; then
    curl -fL --retry 2 "https://wpewebkit.org/releases/wpewebkit-$VERSION.tar.xz" -o "$ARCHIVE"
fi
printf '%s  %s\n' 645babbc04b408b3ce3b026f1990e6307668aa41bf2fb860f387de72a18feb33 "$ARCHIVE" | sha256sum -c -
if [ ! -d "$SOURCE" ]; then tar -xJf "$ARCHIVE" -C "$WORK"; fi
python3 "$PROJ/scripts/webkit/apply-vex-jit.py" "$SOURCE"
export PKG_CONFIG_PATH="$WORK/deps/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cmake -S "$SOURCE" -B "$BUILD" -G Ninja \
    -DPORT=WPE -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_C_FLAGS="-DENABLE_VEX_JIT=1 -DENABLE_SEPARATED_WX_HEAP=1 -I$SOURCE/vex-include" \
    -DCMAKE_CXX_FLAGS="-DENABLE_VEX_JIT=1 -DENABLE_SEPARATED_WX_HEAP=1 -I$SOURCE/vex-include" \
    -DUSE_SYSTEM_MALLOC=ON -DENABLE_JIT=ON -DENABLE_DFG_JIT=ON -DENABLE_FTL_JIT=ON \
    -DENABLE_WPE_PLATFORM=OFF -DENABLE_WPE_QT_API=OFF -DENABLE_GPU_PROCESS=OFF \
    -DUSE_GBM=OFF -DUSE_LIBDRM=OFF -DUSE_ATK=OFF -DUSE_LIBHYPHEN=OFF \
    -DUSE_LIBBACKTRACE=OFF -DENABLE_SPEECH_SYNTHESIS=OFF -DENABLE_SPELLCHECK=OFF \
    -DENABLE_VIDEO=ON -DENABLE_WEB_AUDIO=OFF -DENABLE_MEDIA_STREAM=OFF \
    -DENABLE_INTROSPECTION=OFF -DENABLE_DOCUMENTATION=OFF \
    -DENABLE_BUBBLEWRAP_SANDBOX=OFF -DUSE_SYSPROF_CAPTURE=OFF \
    -DENABLE_JOURNALD_LOG=OFF -DENABLE_WEBDRIVER=OFF -DENABLE_MINIBROWSER=OFF \
    -DENABLE_GAMEPAD=OFF -DENABLE_WEB_RTC=OFF -DENABLE_PDFJS=OFF \
    -DENABLE_API_TESTS=OFF -DENABLE_LAYOUT_TESTS=OFF
cmake --build "$BUILD" --parallel "${VEX_WEBKIT_JOBS:-4}" \
    --target WebKit WebProcess NetworkProcess WPEInjectedBundle jsc
mkdir -p "$PROJ/build/webkit-source"
cp "$BUILD/CMakeCache.txt" "$PROJ/build/webkit-source/CMakeCache.txt"
sha256sum "$BUILD"/lib/libWPEWebKit-2.0.so.* "$BUILD/bin/jsc" \
    > "$PROJ/build/webkit-source/engine.sha256"
echo "Engine built at $BUILD. Stage explicitly using WEBKIT_BUILD=$BUILD."
