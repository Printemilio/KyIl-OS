#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
tools=${VEX_SPIRV_TOOLS:-"$root/build/khronos-tools/usr/bin"}
mkdir -p build/compute-shaders
for source in userspace/apps/shaders/*.comp userspace/apps/shaders/*.frag; do
    [ -f "$source" ] || continue
    filename=$(basename "$source")
    name=${filename%.*}
    "$tools/glslangValidator" -V --target-env vulkan1.0 "$source" -o "build/compute-shaders/$name.spv"
    "$tools/spirv-val" --target-env vulkan1.0 "build/compute-shaders/$name.spv"
    "$tools/spirv-dis" "build/compute-shaders/$name.spv" -o "build/compute-shaders/$name.spvasm"
done
