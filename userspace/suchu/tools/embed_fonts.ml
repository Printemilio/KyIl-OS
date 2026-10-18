(* Turns the .ttf files under assets/fonts into an OCaml module, so that a
   built application carries its typeface instead of hoping the target machine
   has one installed. That is what makes "a single native binary, nothing to
   install" true rather than aspirational.

   Run by a dune rule, never by hand; the output goes to stdout. *)

let escape_into buffer data =
  String.iteri
    (fun index character ->
      (* Every byte as a three-digit decimal escape: uniform, and safe
         whatever the byte happens to be. *)
      Buffer.add_string buffer (Printf.sprintf "\\%03d" (Char.code character));
      (* Break the literal up so the generated file stays openable. A
         backslash before the newline makes OCaml skip both it and the
         indentation that follows. *)
      if (index + 1) mod 40 = 0 then Buffer.add_string buffer "\\\n     ")
    data

let read_file path =
  let channel = open_in_bin path in
  Fun.protect
    ~finally:(fun () -> close_in channel)
    (fun () -> really_input_string channel (in_channel_length channel))

let emit name path =
  let data = read_file path in
  let buffer = Buffer.create (String.length data * 4) in
  Printf.printf "(* %s, %d bytes *)\nlet %s = \"\\\n     " (Filename.basename path)
    (String.length data) name;
  escape_into buffer data;
  print_string (Buffer.contents buffer);
  print_string "\"\n\n"

let () =
  match Sys.argv with
  | [| _; regular; bold; mono |] ->
      print_string
        "(* Generated from assets/fonts by tools/embed_fonts.ml -- do not edit.\n\
        \   Liberation Sans and Liberation Mono, (c) 2012 Red Hat, Inc.,\n\
        \   SIL Open Font License 1.1. The licence travels with the sources in\n\
        \   assets/fonts. *)\n\n";
      emit "regular" regular;
      emit "bold" bold;
      emit "mono" mono
  | _ ->
      prerr_endline "Usage: embed_fonts <regular.ttf> <bold.ttf> <mono.ttf>";
      exit 1
