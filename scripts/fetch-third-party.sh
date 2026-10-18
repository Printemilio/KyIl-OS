#!/bin/sh
# Fetch, verify, extract and patch the pinned third-party sources.
#
#   scripts/fetch-third-party.sh [name...]
#
# Sources land in build/third_party/src/<name>-<version>. An existing tree is
# left untouched unless it is older than one of its patches; delete it to
# force a clean re-extraction.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOCK="$ROOT/third_party/sources.lock"
DL="$ROOT/build/third_party/dl"
SRC="$ROOT/build/third_party/src"
PATCHES="$ROOT/third_party/patches"
mkdir -p "$DL" "$SRC"

wanted() {
    [ $# -eq 0 ] && return 0
    for w in "$@"; do [ "$w" = "$NAME" ] && return 0; done
    return 1
}

grep -v '^#' "$LOCK" | while read -r NAME VERSION SHA URL; do
    [ -n "${NAME:-}" ] || continue
    wanted "$@" || continue
    FILE="$DL/$(basename "$URL")"
    if [ ! -f "$FILE" ]; then
        echo "fetch: $URL"
        curl -fsSL --retry 3 -o "$FILE.part" "$URL"
        mv "$FILE.part" "$FILE"
    fi
    echo "$SHA  $FILE" | sha256sum -c --quiet - || {
        echo "fetch: checksum mismatch for $FILE" >&2; exit 1; }
    TREE="$SRC/$NAME-$VERSION"
    STAMP="$TREE/.vex-patched"
    if [ -f "$STAMP" ] && [ -z "$(find "$PATCHES/$NAME" -newer "$STAMP" -name '*.patch' 2>/dev/null)" ]; then
        continue
    fi
    rm -rf "$TREE"
    mkdir -p "$TREE"
    tar -xf "$FILE" -C "$TREE" --strip-components=1
    if [ -d "$PATCHES/$NAME" ]; then
        for p in "$PATCHES/$NAME"/*.patch; do # glob order is lexical
            [ -f "$p" ] || continue
            echo "patch: $NAME $(basename "$p")"
            patch -d "$TREE" -p1 --no-backup-if-mismatch -s < "$p"
        done
    fi
    touch "$STAMP"
    echo "ready: $TREE"
done
