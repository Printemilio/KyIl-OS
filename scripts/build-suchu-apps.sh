#!/usr/bin/env bash
# Compile integrated Suchu sources; no application model implemented in a bridge.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TOOLCHAIN=${VEX_TOOLCHAIN:-$HOME/.local/vexos}
LIB="$ROOT/usr/lib/suchu"; WORK="$ROOT/build/suchu-apps"
mkdir -p "$WORK" "$ROOT/usr/bin" "$ROOT/usr/native-tests"
export PATH="$ROOT/toolchain/bin:$PATH"; cd "$WORK"
"$TOOLCHAIN/ocaml-host/bin/ocamlopt" -I "$LIB" -I +unix -I +str unix.cmxa str.cmxa suchu_core.cmxa suchu_eval.cmxa \
 "$ROOT/userspace/suchu/tools/export_browser.ml" -o export_app
TOOLKIT="str.cmxa integers.cmxa ctypes.cmxa ctypes_stubs.cmxa raylib_c.cmxa raylib_core.cmxa raylib_c_audio.cmxa raylib_audio.cmxa raylib_c_models.cmxa raylib_models.cmxa raylib_c_shapes.cmxa raylib_shapes.cmxa raylib_c_text.cmxa raylib_text.cmxa raylib_c_textures.cmxa raylib_textures.cmxa rlgl_c.cmxa raylib_rlgl.cmxa raylib.cmxa"
if [ "$#" = 0 ]; then set -- files files_test launcher terminal terminal_test; fi
for app do
 case "$app" in files|files_test|launcher|terminal|terminal_test) ;; *) echo "Unknown app: $app" >&2; exit 1;; esac
 ./export_app "$ROOT/userspace/suchu/examples/$app.suchu" "$app.ml"
 "$TOOLCHAIN/ocaml-vexos/bin/ocamlopt.opt" -O2 -w -a -I "$LIB" -I +unix -I +str \
  unix.cmxa suchu_core.cmxa suchu_eval.cmxa $TOOLKIT suchu.cmxa "$app.ml" -cclib -lraylib -o "$app"
done
cp files "$ROOT/usr/bin/kyil-files"
cp launcher "$ROOT/usr/bin/kyil-launcher"
cp terminal "$ROOT/usr/bin/kyil-terminal"
cp terminal_test "$ROOT/usr/native-tests/suchu_terminal_test"
cp files_test "$ROOT/usr/native-tests/suchu_files_test"
mkdir -p "$ROOT/build/iso_root/usr/bin" "$ROOT/build/iso_root/usr/native-tests" "$ROOT/build/iso_root/usr/share/suchu/examples"
cp "$ROOT/usr/bin/kyil-files" "$ROOT/usr/bin/kyil-launcher" "$ROOT/usr/bin/kyil-terminal" "$ROOT/build/iso_root/usr/bin/"
cp terminal_test "$ROOT/build/iso_root/usr/native-tests/suchu_terminal_test"
cp files_test "$ROOT/build/iso_root/usr/native-tests/suchu_files_test"
cp "$ROOT/userspace/suchu/examples/"{files,files_model,files_test,launcher,terminal,terminal_model,terminal_test}.suchu "$ROOT/build/iso_root/usr/share/suchu/examples/"
echo 'Suchu Files, Launcher and real filesystem regression built'
