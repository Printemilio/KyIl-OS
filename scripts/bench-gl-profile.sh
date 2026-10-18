#!/bin/sh
# Sample the software renderer benchmark and print its hottest source lines.
# Usage: scripts/bench-gl-profile.sh [frames] [size]      (TOP=40 for more lines)
set -eu
PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$PROJECT"
OUT=${VEX_GL_OUT:-"$PROJECT/build/gl-bench"}
VEX_BENCH_PROFILE=1 /bin/sh scripts/bench-gl.sh "$@" > "$OUT/profile.txt"
grep '^bench' "$OUT/profile.txt"
grep '^PROFSAMPLE' "$OUT/profile.txt" |
    awk '{c=$2; o=$NF; m=$0; sub(/^PROFSAMPLE [0-9]+ /,"",m); sub(/ 0x[0-9a-f]+$/,"",m); print c "|" m "|" o}' |
    while IFS='|' read -r count module offset; do
        case "$module" in
        *libvex_graphics*)
            where=$(addr2line -f -e "$OUT/libvex_graphics.so.1" "$offset" | tr '\n' ' ')
            echo "$count $where" ;;
        esac
    done |
    awk '{n=$1; $1=""; a[$0]+=n; t+=n} END {for (k in a) printf "%6d %5.1f%% %s\n", a[k], 100*a[k]/t, k}' |
    sort -rn | head -"${TOP:-30}"
