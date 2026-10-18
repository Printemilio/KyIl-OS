#!/bin/sh
set -eu
PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$PROJECT"
OUT=${VEX_GLSLANG_PREFIX:-"$PROJECT/build/glslang-install"}
if command -v cmake >/dev/null 2>&1; then
    CMAKE=cmake
elif [ -x "$PROJECT/build/cmake-tools/usr/bin/cmake" ]; then
    CMAKE="$PROJECT/build/cmake-tools/usr/bin/cmake"
    export LD_LIBRARY_PATH="$PROJECT/build/cmake-tools/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
else
    echo 'Install CMake >=3.27 and a C++ compiler, then rerun bootstrap-glslang.sh.' >&2
    exit 1
fi
"$CMAKE" -S vendor/glslang -B build/glslang-source \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DENABLE_OPT=OFF -DBUILD_EXTERNAL=OFF -DENABLE_GLSLANG_BINARIES=OFF \
    -DENABLE_HLSL=OFF -DGLSLANG_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$OUT"
"$CMAKE" --build build/glslang-source -j "${VEX_BUILD_JOBS:-4}"
"$CMAKE" --install build/glslang-source
echo "Real Khronos glslang compiler installed under $OUT"
