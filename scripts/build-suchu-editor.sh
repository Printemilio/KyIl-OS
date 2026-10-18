#!/usr/bin/env bash
# Compile the real Suchu editor and its Suchu model regression, in this copy.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TOOLCHAIN=${VEX_TOOLCHAIN:-$HOME/.local/vexos}
LIB="$ROOT/usr/lib/suchu"
WORK="$ROOT/build/suchu-editor"
mkdir -p "$WORK" "$ROOT/usr/bin" "$ROOT/usr/native-tests"
export PATH="$ROOT/toolchain/bin:$PATH"
cd "$WORK"
"$TOOLCHAIN/ocaml-host/bin/ocamlopt" -I "$LIB" -I +unix -I +str \
  unix.cmxa str.cmxa suchu_core.cmxa suchu_eval.cmxa "$ROOT/userspace/suchu/tools/export_browser.ml" -o export_editor
TOOLKIT="str.cmxa integers.cmxa ctypes.cmxa ctypes_stubs.cmxa raylib_c.cmxa raylib_core.cmxa
 raylib_c_audio.cmxa raylib_audio.cmxa raylib_c_models.cmxa raylib_models.cmxa
 raylib_c_shapes.cmxa raylib_shapes.cmxa raylib_c_text.cmxa raylib_text.cmxa
 raylib_c_textures.cmxa raylib_textures.cmxa rlgl_c.cmxa raylib_rlgl.cmxa raylib.cmxa"
for app in editor editor_test; do
  ./export_editor "$ROOT/userspace/suchu/examples/$app.suchu" "$app.ml"
  "$TOOLCHAIN/ocaml-vexos/bin/ocamlopt.opt" -O2 -w -a -I "$LIB" -I +unix -I +str \
    unix.cmxa suchu_core.cmxa suchu_eval.cmxa $TOOLKIT suchu.cmxa "$app.ml" \
    -cclib -lraylib -o "$app"
done
cp editor "$ROOT/usr/bin/kyil-editor"
cp editor_test "$ROOT/usr/native-tests/suchu_editor_test"
echo 'Suchu editor and model regression built'
