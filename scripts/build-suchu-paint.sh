#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TOOLCHAIN=${VEX_TOOLCHAIN:-$HOME/.local/vexos}
LIB="$ROOT/usr/lib/suchu";WORK="$ROOT/build/paint-comparison/suchu"
mkdir -p "$WORK"; export PATH="$ROOT/toolchain/bin:$PATH"; cd "$WORK"
"$TOOLCHAIN/ocaml-host/bin/ocamlopt" -I "$LIB" -I +unix -I +str unix.cmxa str.cmxa suchu_core.cmxa suchu_eval.cmxa "$ROOT/userspace/suchu/tools/export_browser.ml" -o export_paint
for app in paint paint_test; do
./export_paint "$ROOT/userspace/suchu/examples/$app.suchu" "$app.ml"
"$TOOLCHAIN/ocaml-vexos/bin/ocamlopt.opt" -O2 -w -a -I "$LIB" -I +unix -I +str \
 unix.cmxa suchu_core.cmxa suchu_eval.cmxa str.cmxa integers.cmxa ctypes.cmxa ctypes_stubs.cmxa \
 raylib_c.cmxa raylib_core.cmxa raylib_c_audio.cmxa raylib_audio.cmxa raylib_c_models.cmxa raylib_models.cmxa \
 raylib_c_shapes.cmxa raylib_shapes.cmxa raylib_c_text.cmxa raylib_text.cmxa raylib_c_textures.cmxa raylib_textures.cmxa \
 rlgl_c.cmxa raylib_rlgl.cmxa raylib.cmxa suchu.cmxa "$app.ml" -cclib -lraylib -o "$app"
done
cp paint "$ROOT/usr/bin/kyil-paint"
cp paint_test "$ROOT/usr/native-tests/suchu_paint_test"
cp paint_test "$ROOT/build/iso_root/usr/native-tests/suchu_paint_test"
cp "$ROOT/usr/bin/kyil-paint" "$ROOT/build/iso_root/usr/bin/kyil-paint"
cp "$ROOT/userspace/suchu/examples/paint"*.suchu "$ROOT/usr/share/suchu/examples/"
cp "$ROOT/userspace/suchu/examples/paint"*.suchu "$ROOT/build/iso_root/usr/share/suchu/examples/"
