#!/usr/bin/env bash
# Build Suchu for Vex with the OCaml cross compiler.
#
#   scripts/build-raylib.sh     # once: raylib, ctypes, integers in usr/lib/suchu
#   scripts/build-suchu.sh
#
# Sources: userspace/suchu, copied from the Suchu repository (upstream dune
# file kept as src/dune.upstream). Vex changes are limited to cli.ml (no HTTP
# backend, `suchu comp` calls ocamlopt directly) and suchu_cli.ml (Cli is a
# plain module).
#
# Results, staged on the ISO:
#   usr/bin/suchu                   the suchu command (run, build, comp, repl),
#                                   with the raylib GUI and audio backends
#   usr/lib/suchu/*.cmxa *.a *.cmi *.cmx   the libraries `suchu comp` links:
#                                   suchu_core (lexer, parser, runtime,
#                                   stdlib, transpiler), suchu_eval
#                                   (interpreter) and suchu (Gui_backend,
#                                   Audio_backend, the embedded typeface)
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOLCHAIN=${VEX_TOOLCHAIN:-$HOME/.local/vexos}
WORK=${VEX_TOOLCHAIN_WORK:-$HOME/.cache/vexos}/suchu
OCAMLOPT="$TOOLCHAIN/ocaml-vexos/bin/ocamlopt.opt"
OCAMLDEP="$TOOLCHAIN/ocaml-host/bin/ocamldep"
HOST_OCAMLOPT="$TOOLCHAIN/ocaml-host/bin/ocamlopt"
LIB="$ROOT/usr/lib/suchu"
export PATH="$ROOT/toolchain/bin:$PATH"

# The library composition of upstream src/dune (suchu.core, suchu.eval and,
# without http_backend, suchu).
CORE="lexical ast parser_utils gui_parser parser os_theme gui_tree runtime native_api suchu_stdlib http_wire audio_analysis transpiler util"
EVAL="interpreter"
DESKTOP="fonts_data audio_backend gui_backend browser_backend platform_backend"
STUBS="suchu_dwm_stubs suchu_path_stubs suchu_dialog_stubs browser_stubs platform_stubs"
FLAGS="-I +unix -I +str -w -a -O2"
# raylib and its bindings, in link order (scripts/build-raylib.sh); the same
# list is in cli.ml for `suchu comp`.
TOOLKIT="str.cmxa integers.cmxa ctypes.cmxa ctypes_stubs.cmxa raylib_c.cmxa raylib_core.cmxa
 raylib_c_audio.cmxa raylib_audio.cmxa raylib_c_models.cmxa raylib_models.cmxa
 raylib_c_shapes.cmxa raylib_shapes.cmxa raylib_c_text.cmxa raylib_text.cmxa
 raylib_c_textures.cmxa raylib_textures.cmxa rlgl_c.cmxa raylib_rlgl.cmxa raylib.cmxa"
[ -x "$OCAMLOPT" ] || { echo "missing $OCAMLOPT (scripts/build-ocaml-toolchain.sh)" >&2; exit 1; }
[ -f "$LIB/raylib.cmxa" ] && [ -f "$LIB/libraylib.a" ] ||
    { echo "missing raylib in $LIB (scripts/build-raylib.sh)" >&2; exit 1; }
"$OCAMLOPT" -version >/dev/null

rm -rf "$WORK" && mkdir -p "$WORK" && cd "$WORK"
for m in $CORE $EVAL audio_backend gui_backend browser_backend platform_backend cli suchu_cli; do cp "$ROOT/userspace/suchu/src/$m.ml" .; done
for s in $STUBS; do cp "$ROOT/userspace/suchu/src/$s.c" .; done

# The typeface travels inside the binary (upstream: a dune rule running
# tools/embed_fonts.exe on assets/fonts).
FONTS="$ROOT/userspace/suchu/assets/fonts"
"$HOST_OCAMLOPT" -o embed_fonts "$ROOT/userspace/suchu/tools/embed_fonts.ml"
# Inter is the KyIlOS system typeface (the desktop shell and the Figma frames
# use it); Liberation Mono stays for code and the terminal.
./embed_fonts "$FONTS/Inter-Regular.ttf" "$FONTS/Inter-SemiBold.ttf" \
    "$FONTS/LiberationMono-Regular.ttf" > fonts_data.ml

# Compile in dependency order.
sorted() { "$OCAMLDEP" -sort $(for m in "$@"; do echo "$m.ml"; done); }
CORE_ML=$(sorted $CORE)
for f in $CORE_ML; do "$OCAMLOPT" $FLAGS -c "$f"; done
"$OCAMLOPT" -a -o suchu_core.cmxa $(echo "$CORE_ML" | sed 's/\.ml/.cmx/g')
"$OCAMLOPT" $FLAGS -c interpreter.ml
"$OCAMLOPT" -a -o suchu_eval.cmxa interpreter.cmx

# suchu.cmxa: the desktop backends, plus the Suchu module through which
# compiled programs reach them (Suchu.Gui_backend, as with dune's wrapped
# library). The C stubs ride along as libsuchu_stubs.a.
for s in $STUBS; do
    x86_64-unknown-vexos-gcc -O2 -DVEX_HOSTED_BUILD=1 -DVEX_DESKTOP_TRACE="${VEX_DESKTOP_TRACE:-0}" -I "$TOOLCHAIN/ocaml-vexos/lib/ocaml" \
      -I "$ROOT/userspace/libvex/include" -I "$ROOT/userspace/apps" -I "$ROOT/userspace/raylib" \
      -I "$ROOT/build/third_party/src/raylib-ocaml-2.2.1/src/c/vendor/raylib/src" \
      -idirafter "$ROOT/kernel/include" -c "$s.c" -o "$s.o"
done
x86_64-unknown-vexos-ar rcs libsuchu_stubs.a $(for s in $STUBS; do echo "$s.o"; done)
x86_64-unknown-vexos-gcc -O2 -DVEX_HOSTED_BUILD=1 -I "$ROOT/userspace/libvex/include" -idirafter "$ROOT/kernel/include" -c "$ROOT/userspace/libvex/src/vex_bench.c" -o vex_bench.o
x86_64-unknown-vexos-ar rcs libsuchu_stubs.a vex_bench.o
# The cmxa auto-link also searches LIB. Do not combine a stale staged stub
# archive with the freshly compiled explicit archive when new symbols appear.
cp libsuchu_stubs.a "$LIB/"
DESKTOP_ML=$(sorted $DESKTOP)
for f in $DESKTOP_ML; do "$OCAMLOPT" $FLAGS -I "$LIB" -c "$f"; done
printf 'module Gui_backend = Gui_backend\nmodule Audio_backend = Audio_backend\nmodule Browser_backend = Browser_backend\nmodule Platform_backend = Platform_backend\n' > suchu.ml
"$OCAMLOPT" $FLAGS -c suchu.ml
"$OCAMLOPT" -a -o suchu.cmxa $(echo "$DESKTOP_ML" | sed 's/\.ml/.cmx/g') suchu.cmx -cclib -lsuchu_stubs

"$OCAMLOPT" $FLAGS -I "$LIB" -c cli.ml suchu_cli.ml
"$OCAMLOPT" $FLAGS -I "$LIB" -I . unix.cmxa suchu_core.cmxa suchu_eval.cmxa $TOOLKIT suchu.cmxa \
    cli.cmx suchu_cli.cmx -cclib -lraylib -o suchu
x86_64-unknown-vexos-strip suchu

# Only Suchu's own files are replaced: usr/lib/suchu also holds raylib.
mkdir -p "$LIB" "$ROOT/usr/bin"
for a in suchu_core suchu_eval suchu; do cp "$a.cmxa" "$a.a" "$LIB/"; done
cp libsuchu_stubs.a "$LIB/"
for m in $CORE $EVAL $DESKTOP suchu; do cp "$m.cmi" "$m.cmx" "$LIB/"; done
install -m 755 suchu "$ROOT/usr/bin/suchu"

# Example programs and the differential suite (run vs comp must agree).
SHARE="$ROOT/usr/share/suchu"
rm -rf "$SHARE" && mkdir -p "$SHARE"
cp -r "$ROOT/userspace/suchu/examples" "$SHARE/examples"
cp -r "$ROOT/userspace/suchu/tests/differential" "$SHARE/differential"
du -sh "$LIB" "$ROOT/usr/bin/suchu" "$SHARE"
