#!/usr/bin/env bash
# Cross-compile the OCaml runtime tests for native Vex into usr/native-tests/
# (run by the native test runner in the auto_native_test boot mode).
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
TOOLCHAIN=${VEX_TOOLCHAIN:-$HOME/.local/vexos}
export PATH="$TOOLCHAIN/ocaml-vexos/bin:$ROOT/toolchain/bin:$PATH"
OUT="$ROOT/usr/native-tests"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"
for src in "$ROOT"/userspace/ocaml/tests/*.ml; do
    name=$(basename "$src" .ml)
    cp "$src" "$WORK/"
    (cd "$WORK" && ocamlopt.opt -O2 2>/dev/null -I +unix -I +threads unix.cmxa threads.cmxa \
        "$name.ml" -o "$name" 2>&1 ||
     ocamlopt.opt -I +unix -I +threads unix.cmxa threads.cmxa "$name.ml" -o "$name")
    cp "$WORK/$name" "$OUT/$name"
done
ls -la "$OUT"
