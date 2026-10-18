#!/usr/bin/env bash
# No compilation in the original Suchu repository. Uses KyIlOS's copied compiler.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOLCHAIN=${VEX_TOOLCHAIN:-$HOME/.local/vexos}
WORK="$ROOT/build/suchu-browser"
LIB="$ROOT/usr/lib/suchu"
mkdir -p "$WORK"
export PATH="$ROOT/toolchain/bin:$PATH"
cd "$WORK"
cp "$ROOT/userspace/suchu/tools/export_browser.ml" .
"$TOOLCHAIN/ocaml-host/bin/ocamlopt" -I "$LIB" -I +unix -I +str \
 unix.cmxa str.cmxa suchu_core.cmxa suchu_eval.cmxa export_browser.ml -o export_browser
./export_browser "$ROOT/userspace/suchu/examples/browser.suchu" browser.ml
TOOLKIT="str.cmxa integers.cmxa ctypes.cmxa ctypes_stubs.cmxa raylib_c.cmxa raylib_core.cmxa
 raylib_c_audio.cmxa raylib_audio.cmxa raylib_c_models.cmxa raylib_models.cmxa
 raylib_c_shapes.cmxa raylib_shapes.cmxa raylib_c_text.cmxa raylib_text.cmxa
 raylib_c_textures.cmxa raylib_textures.cmxa rlgl_c.cmxa raylib_rlgl.cmxa raylib.cmxa"
"$TOOLCHAIN/ocaml-vexos/bin/ocamlopt.opt" -O2 -w -a -I "$LIB" -I +unix -I +str \
 unix.cmxa suchu_core.cmxa suchu_eval.cmxa $TOOLKIT suchu.cmxa browser.ml \
 -cclib -lraylib -o kyil-browser
mkdir -p "$ROOT/usr/bin"
cp kyil-browser "$ROOT/usr/bin/kyil-browser"
echo 'Suchu browser: translated and linked native executable'
