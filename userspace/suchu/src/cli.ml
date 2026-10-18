open Parser
open Interpreter
open Runtime

(* Install the raylib GUI backend into Interpreter's hooks. Done at module
   initialisation rather than inside [main] so that every entry point is
   covered, including run_source/run_file called from generated OCaml stubs.

   Vex: raylib runs on the native window system with its software renderer,
   and audio on miniaudio's null device (no sound driver yet). The HTTP
   backend is not linked (TLS is not ported): its hook stays empty and a
   program that imports http is refused with an error. *)
let () = Gui_backend.register ()
let () = Audio_backend.register ()
let () = Browser_backend.register ()
let () = Platform_backend.register ()

let read_file path =
  let ic = open_in_bin path in
  Fun.protect ~finally:(fun () -> close_in ic) (fun () ->
      let len = in_channel_length ic in
      really_input_string ic len)

let write_file path content =
  let oc = open_out_bin path in
  Fun.protect ~finally:(fun () -> close_out oc) (fun () -> output_string oc content)

(* Copying and deleting used to be 'cp' and 'rm -rf' through the shell, which
   ties compiling a program to being on a machine that has them -- and on
   Windows 'cp' refuses to overwrite an executable that was produced a moment
   ago. Done here instead, it works wherever OCaml does. *)
let copy_file source destination =
  let from_channel = open_in_bin source in
  Fun.protect ~finally:(fun () -> close_in from_channel) (fun () ->
      (* open_out truncates, so an executable already sitting at the destination
         is overwritten rather than making the copy fail. *)
      let to_channel = open_out_bin destination in
      Fun.protect ~finally:(fun () -> close_out to_channel) (fun () ->
          let buffer = Bytes.create 65536 in
          let rec transfer () =
            let read = input from_channel buffer 0 (Bytes.length buffer) in
            if read > 0 then begin
              output to_channel buffer 0 read;
              transfer ()
            end
          in
          transfer ()))

let rec remove_directory path =
  if Sys.file_exists path then
    if Sys.is_directory path then begin
      Array.iter (fun entry -> remove_directory (Filename.concat path entry)) (Sys.readdir path);
      try Sys.rmdir path with Sys_error _ -> ()
    end
    else try Sys.remove path with Sys_error _ -> ()

(* Whether a command can be found, asked of the system rather than by walking the
   PATH here -- the rules for what counts as executable differ per platform, and
   the shell already knows them. *)
let tool_on_path name =
  let probe =
    if Sys.win32 then Printf.sprintf "where %s >NUL 2>NUL" name
    else Printf.sprintf "command -v %s >/dev/null 2>&1" name
  in
  Sys.command probe = 0

let rec find_file_upwards start filename =
  let candidate = Filename.concat start filename in
  if Sys.file_exists candidate then Some start
  else
    let parent = Filename.dirname start in
    if String.equal parent start then None else find_file_upwards parent filename

type update_args = {
  root : string option;
  install_args : string list;
  run_deps : bool;
}

let parse_update_args raw_args =
  let rec aux acc args =
    match args with
    | [] -> acc
    | "--skip-deps" :: rest -> aux { acc with run_deps = false } rest
    | "--deps" :: rest -> aux { acc with run_deps = true } rest
    | "--root" :: value :: rest -> aux { acc with root = Some value } rest
    | opt :: rest when String.length opt >= 7 && String.sub opt 0 7 = "--root=" ->
        let value = String.sub opt 7 (String.length opt - 7) in
        aux { acc with root = Some value } rest
    | arg :: rest ->
        aux { acc with install_args = arg :: acc.install_args } rest
  in
  let initial = { root = None; install_args = []; run_deps = true } in
  let result = aux initial raw_args in
  { result with install_args = List.rev result.install_args }

let resolve_update_root explicit_root =
  let validate_root path =
    let script = Filename.concat path "install.sh" in
    if Sys.file_exists script then Ok path
    else Error (Printf.sprintf "[suchu] install.sh not found in %s" path)
  in
  match explicit_root with
  | Some path -> validate_root path
  | None -> (
      match Sys.getenv_opt "SUCHU_ROOT" with
      | Some env_root -> validate_root env_root
      | None ->
          let cwd = Sys.getcwd () in
          match find_file_upwards cwd "install.sh" with
          | Some root -> Ok root
          | None ->
              Error
                "[suchu] Unable to locate project root. Set --root PATH or SUCHU_ROOT.")

let run_script path args =
  let command =
    let quoted_args =
      match args with
      | [] -> ""
      | _ ->
          " "
          ^ String.concat " "
              (List.map Filename.quote args)
    in
    "bash " ^ Filename.quote path ^ quoted_args
  in
  match Sys.command command with
  | 0 -> Ok ()
  | status ->
      Error
        (Printf.sprintf "[suchu] Command failed (exit code %d): %s" status command)

let run_update raw_args =
  let args = parse_update_args raw_args in
  match resolve_update_root args.root with
  | Error msg ->
      prerr_endline msg;
      1
  | Ok root ->
      let steps =
        let install_step =
          (Filename.concat root "install.sh", true, args.install_args)
        in
        if args.run_deps then
          [
            (Filename.concat root "install_deps.sh", false, []);
            install_step;
          ]
        else [ install_step ]
      in
      let rec execute = function
        | [] ->
            Printf.printf "[suchu] Update complete.\n%!";
            0
        | (path, required, extra_args) :: rest ->
            if Sys.file_exists path then (
              Printf.printf "[suchu] Running %s...\n%!" path;
              match run_script path extra_args with
              | Ok () -> execute rest
              | Error msg ->
                  prerr_endline msg;
                  1)
            else if required then (
              prerr_endline
                (Printf.sprintf "[suchu] Required script missing: %s" path);
              1)
            else (
              Printf.printf "[suchu] Skipping missing script: %s\n%!" path;
              execute rest)
      in
      execute steps

let run_source ?base_dir ?embedded_modules source =
  let program = parse source in
  let interpreter = create ?base_dir ?embedded_modules () in
  ignore (run_program interpreter program)

let run_file path =
  let source = read_file path in
  let absolute_path =
    if Filename.is_relative path then Filename.concat (Sys.getcwd ()) path else path
  in
  run_source ~base_dir:(Filename.dirname absolute_path) source

let find_substring text needle start =
  let text_length = String.length text in
  let needle_length = String.length needle in
  let rec loop index =
    if index + needle_length > text_length then None
    else if String.sub text index needle_length = needle then Some index
    else loop (index + 1)
  in
  loop start

let parse_number text start =
  let rec loop index =
    if index < String.length text && text.[index] >= '0' && text.[index] <= '9' then loop (index + 1)
    else index
  in
  let stop = loop start in
  if stop = start then None else Some (int_of_string (String.sub text start (stop - start)), stop)

let error_location message =
  match find_substring message "line " 0 with
  | None -> None
  | Some line_start -> (
      match parse_number message (line_start + 5) with
      | None -> None
      | Some (line, after_line) -> (
          match find_substring message "column " after_line with
          | None -> None
          | Some column_start ->
              Option.map (fun (column, _) -> (line, column)) (parse_number message (column_start + 7))))

let show_source_excerpt source message =
  match error_location message with
  | None -> ()
  | Some (line_number, column) ->
      (* The lexer skips a leading byte order mark, so the excerpt has to drop it
         too, or the first line prints a stray glyph and the caret sits three
         columns to the right of the character it is pointing at. *)
      let source =
        if Lexical.starts_with_bom source then
          String.sub source 3 (String.length source - 3)
        else source
      in
      let lines = String.split_on_char '\n' source in
      (match List.nth_opt lines (line_number - 1) with
      | None -> ()
      | Some line ->
          Printf.eprintf "  %d | %s\n" line_number line;
          Printf.eprintf "    | %s^\n" (String.make (max 0 (column - 1)) ' '))

let report_source_error kind source message =
  Printf.eprintf "%s: %s\n" kind message;
  show_source_excerpt source message

(* The CLI entry points hold a path, but show_source_excerpt needs the text to
   quote the offending line. Re-read it, and fall back to no excerpt if the file
   is unreadable -- a diagnostic must never raise over the error it reports. *)
let source_text_for_diagnostics path = try read_file path with _ -> ""

let report_file_error kind path message =
  report_source_error kind (source_text_for_diagnostics path) message

let repl () =
  let interpreter = ref (create ()) in
  print_endline "Suchu REPL 0.8 - enter :help for commands.";
  let rec loop () =
    print_string "suchu> ";
    flush stdout;
    match read_line () with
    | exception End_of_file -> print_endline "\nBye."
    | input ->
        let input = String.trim input in
        (match input with
        | "" -> ()
        | ":quit" | ":exit" -> raise Exit
        | ":help" ->
            print_endline "Enter one Suchu expression or statement per line.";
            print_endline "Commands: :help, :reset, :quit (or :exit)."
        | ":reset" ->
            interpreter := create ();
            print_endline "The REPL environment has been reset.";
        | source ->
            (* Statements need a ';' since 0.5, but typing one at every REPL
               prompt is tedious, so add it when the line clearly lacks one.
               A line ending in '}' closes a block and needs nothing. *)
            let source =
              let last = source.[String.length source - 1] in
              if last = ';' || last = '}' then source else source ^ ";"
            in
            (try
               let value = run_program !interpreter (parse source) in
               print_endline (value_to_string value)
             with
            | Lexical.Lexing_error message -> report_source_error "Lexing error" source message
            | Parser.Parse_error message -> report_source_error "Parse error" source message
            | Runtime_error message -> Printf.eprintf "Runtime error: %s\n" message));
        loop ()
  in
  try loop () with Exit -> print_endline "Bye."

let generate_stub source =
  let buffer = Buffer.create (String.length source) in
  String.iter
    (fun ch ->
      match ch with
      | '\\' ->
          Buffer.add_char buffer '\\';
          Buffer.add_char buffer '\\'
      | '"' ->
          Buffer.add_char buffer '\\';
          Buffer.add_char buffer '"'
      | '\n' ->
          Buffer.add_char buffer '\\';
          Buffer.add_char buffer 'n'
      | _ -> Buffer.add_char buffer ch)
    source;
  let escaped = Buffer.contents buffer in
  Printf.sprintf
    "(* Auto-generated Suchu 0.8 program *)\nlet suchu_source = \"%s\"\n\nlet () =\n  \
     Suchu.Cli.run_source suchu_source\n"
    escaped

let compile_to_stub source_path output_path =
  let source = read_file source_path in
  ignore (parse source); (* ensure the program is syntactically valid *)
  let stub = generate_stub source in
  write_file output_path stub

let rec imports_in_statements statements =
  List.concat_map
    (function
      | Ast.Import module_spec -> [ module_spec ]
      | Ast.FunctionDef declaration -> imports_in_statements declaration.body
      | Ast.IfStmt (_, then_block, else_block) ->
          imports_in_statements then_block
          @ Option.fold ~none:[] ~some:imports_in_statements else_block
      | Ast.MatchStmt (_, branches, default_block) ->
          List.concat_map (fun (_, block) -> imports_in_statements block) branches
          @ Option.fold ~none:[] ~some:imports_in_statements default_block
      | Ast.WhileStmt (_, block)
      | Ast.ForStmt (_, _, block) -> imports_in_statements block
      (* Both arms must be walked, or an import inside a try would be missing
         from a bundled binary. *)
      | Ast.TryStmt (attempted, _, handler) ->
          imports_in_statements attempted @ imports_in_statements handler
      | Ast.Gui _
      | Ast.ExprStmt _
      | Ast.Assign _
      | Ast.Break
      | Ast.Continue
      | Ast.Return _ -> [])
    statements

let bundle_modules source_path source =
  let source_absolute =
    if Filename.is_relative source_path then Filename.concat (Sys.getcwd ()) source_path
    else source_path
  in
  let visited = Hashtbl.create 16 in
  let rec collect real_dir virtual_dir program =
    imports_in_statements program
    |> List.concat_map (fun module_spec ->
           match Runtime.find_native_module module_spec with
           | Some _ -> []
           | None ->
               let real_path = Runtime.resolve_module_path real_dir module_spec in
               let virtual_path = Runtime.resolve_module_path virtual_dir module_spec in
               if Hashtbl.mem visited real_path then []
               else if not (Sys.file_exists real_path) then
                 raise
                   (Runtime_error
                      (Printf.sprintf "Cannot bundle import '%s': file not found at %s"
                         module_spec real_path))
               else
                 let module_source = read_file real_path in
                 let module_program =
                   try parse module_source with
                   | Lexical.Lexing_error message ->
                       raise
                         (Runtime_error
                            (Printf.sprintf "In imported module '%s': %s" module_spec message))
                   | Parser.Parse_error message ->
                       raise
                         (Runtime_error
                            (Printf.sprintf "In imported module '%s': %s" module_spec message))
                 in
                 Hashtbl.add visited real_path ();
                 (virtual_path, module_source)
                 :: collect (Filename.dirname real_path) (Filename.dirname virtual_path)
                      module_program)
  in
  collect (Filename.dirname source_absolute) "/suchu-bundle" (parse source)

let sanitize_filename name =
  String.map
    (fun ch ->
      match ch with
      | 'a' .. 'z'
      | 'A' .. 'Z'
      | '0' .. '9'
      | '_'
      | '-' -> ch
      | _ -> '_')
    name

let ensure_directory path =
  if Sys.file_exists path then (
    if not (Sys.is_directory path) then
      raise (Runtime_error (Printf.sprintf "%s exists and is not a directory" path)))
  else Sys.mkdir path 0o755

let generate_native_entry source bundled_modules =
  let modules =
    bundled_modules
    |> List.map (fun (path, module_source) -> Printf.sprintf "    (%S, %S);" path module_source)
    |> String.concat "\n"
  in
  Printf.sprintf
    "let source = %S\n\nlet embedded_modules = [\n%s\n  ]\n\nlet () =\n  \
     Suchu.Cli.run_source ~base_dir:%S ~embedded_modules source\n"
    source modules "/suchu-bundle"

let build_native source_path output_path =
  Suchu_stdlib.register ();
  let source = read_file source_path in
  ignore (parse source);
  let bundled_modules = bundle_modules source_path source in
  let project_root =
    match find_file_upwards (Sys.getcwd ()) "dune-project" with
    | Some root -> root
    | None ->
        raise
          (Runtime_error
             "suchu build currently needs to run inside the Suchu project checkout")
  in
  let source_name =
    Filename.basename source_path
    |> Filename.remove_extension
    |> sanitize_filename
  in
  let build_root = Filename.concat project_root "build" in
  ensure_directory build_root;
  let app_dir =
    Filename.concat build_root
      (source_name ^ "-" ^ String.sub (Digest.to_hex (Digest.string source_path)) 0 8)
  in
  ensure_directory app_dir;
  write_file (Filename.concat app_dir "main.ml")
    (generate_native_entry source bundled_modules);
  write_file (Filename.concat app_dir "dune")
    "(executable\n (name main)\n (libraries suchu)\n (flags (:standard -w -a)))\n";
  let relative_app_dir =
    Filename.concat "build" (Filename.basename app_dir)
  in
  let target = Filename.concat relative_app_dir "main.exe" in
  let build_command =
    Printf.sprintf "dune build --root %s %s"
      (Filename.quote project_root) (Filename.quote target)
  in
  if Sys.command build_command <> 0 then
    raise (Runtime_error "Native compilation failed");
  let built_binary =
    Filename.concat
      (Filename.concat (Filename.concat project_root "_build") "default")
      target
  in
  (try copy_file built_binary output_path
   with Sys_error message ->
     raise
       (Runtime_error
          (Printf.sprintf "Could not copy native executable to %s: %s" output_path message)));
  Printf.printf "[suchu] Native executable: %s\n" output_path;
  Printf.printf "[suchu] Bundled modules: %d\n" (List.length bundled_modules);
  Printf.printf "[suchu] Target: %s\n%!" Sys.os_type

(* --- translating to OCaml -------------------------------------------------

   'build' writes the OCaml a reader can open; 'comp' does the same
   translation without keeping the file, and hands the result to ocamlopt.
   Both go through Transpiler, so what you read is what gets compiled. *)

let translate source_path =
  (* The compiler has to know which modules are native -- 'import json' needs
     translating differently from 'import "tools.suchu"' -- and it learns that
     from the same registry the evaluator uses. *)
  Suchu_stdlib.register ();
  let source = read_file source_path in
  let program = parse source in
  (* Relative imports resolve from the file being translated, as they do when it
     is interpreted. *)
  try Transpiler.program ~directory:(Filename.dirname source_path) program with
  | Transpiler.Unsupported message ->
      raise
        (Runtime_error
           (String.concat "\n"
              [ message ^ ".";
                "  Every construct of the language compiles. What is left here is a";
                "  program the interpreter would not accept either, refused now rather";
                "  than when it runs." ]))

let build_ocaml source_path output_path =
  write_file output_path (translate source_path)

(* --- compiling to a native binary ------------------------------------------

   The translated OCaml is handed to ocamlfind, which knows where the Suchu
   library lives and what it needs linking against. Nothing about this needs the
   Suchu sources: 'suchu comp' works in any directory once the library is
   installed in the switch, which is what makes it a tool for shipping a program
   rather than one for working on the language.

   It used to write a dune file and shell out to dune, which meant dune had to be
   installed and the command had to run inside the checkout. *)

(* --- the libraries a Windows program carries -------------------------------

   A program built with mingw is not quite self-contained: it asks Windows to load
   a few DLLs that Windows does not have -- mingw's threading, and for a program
   that imports http, the arithmetic behind TLS. Windows loads them before the
   first line of the program runs, so a missing one is a program that refuses to
   start, with a message about a file rather than about anything the person did.

   So 'suchu comp' puts them next to the program. Which ones is read from the
   program itself, from the table of libraries its header asks for, rather than
   from a list kept here that would be wrong the day a dependency changed.
   Anything Windows ships is left where it is. *)

let le16 text offset = Char.code text.[offset] lor (Char.code text.[offset + 1] lsl 8)
let le32 text offset = le16 text offset lor (le16 text (offset + 2) lsl 16)

(* The names in an executable's import table. Best effort: a file that is not
   what was expected gives no names, and so nothing to copy. *)
let imported_libraries path =
  try
    let data = read_file path in
    let header = le32 data 0x3c in
    let section_count = le16 data (header + 6) in
    let optional = header + 24 in
    let sections = optional + le16 data (header + 20) in
    let directories = optional + if le16 data optional = 0x20b then 112 else 96 in
    let offset rva =
      let rec find index =
        if index >= section_count then None
        else
          let at = sections + (index * 40) in
          let size = max (le32 data (at + 8)) (le32 data (at + 16)) in
          let address = le32 data (at + 12) in
          if rva >= address && rva < address + size then Some (rva - address + le32 data (at + 20))
          else find (index + 1)
      in
      find 0
    in
    let rec names entry found =
      match offset entry with
      | None -> List.rev found
      | Some at -> (
          let name_rva = le32 data (at + 12) in
          if name_rva = 0 then List.rev found
          else
            match offset name_rva with
            | None -> List.rev found
            | Some start ->
                let stop = String.index_from data start '\000' in
                names (entry + 20) (String.sub data start (stop - start) :: found))
    in
    let table = le32 data (directories + 8) in
    if table = 0 then [] else names table []
  with _ -> []

let directory_of_tool name =
  try
    let channel = Unix.open_process_in (Printf.sprintf "where %s 2>NUL" name) in
    let line = try Some (input_line channel) with End_of_file -> None in
    ignore (Unix.close_process_in channel);
    Option.map (fun path -> Filename.dirname (String.trim path)) line
  with _ -> None

(* Where a DLL could be: the PATH, and the place the compiler keeps its own. That
   place is not on the PATH on a normal machine, which is the whole reason a
   program built here does not run anywhere else without being given them. It is
   found from the tools the build already needs -- the C compiler and ocamlfind --
   because opam's Windows install keeps them in a private directory of its own. *)
let library_directories () =
  let path = try String.split_on_char ';' (Sys.getenv "PATH") with Not_found -> [] in
  let beside_compiler =
    match directory_of_tool "x86_64-w64-mingw32-gcc" with
    | Some bin -> [ Filename.concat bin "../x86_64-w64-mingw32/sys-root/mingw/bin" ]
    | None -> []
  in
  let beside_opam =
    match directory_of_tool "ocamlfind" with
    | Some bin ->
        let root = Filename.dirname (Filename.dirname bin) in
        [ Filename.concat root ".cygwin/root/usr/x86_64-w64-mingw32/sys-root/mingw/bin" ]
    | None -> []
  in
  List.filter (fun directory -> directory <> "") path @ beside_compiler @ beside_opam

let ships_with_windows name =
  let lower = String.lowercase_ascii name in
  (String.length lower >= 7 && String.equal (String.sub lower 0 7) "api-ms-")
  ||
  let system = Filename.concat (try Sys.getenv "SystemRoot" with Not_found -> "C:\\Windows") "System32" in
  Sys.file_exists (Filename.concat system name)

let ship_libraries output_path =
  if Sys.win32 then begin
    let program = if Sys.file_exists output_path then output_path else output_path ^ ".exe" in
    let target = Filename.dirname program in
    let directories = lazy (library_directories ()) in
    List.iter
      (fun name ->
        if (not (ships_with_windows name)) && not (Sys.file_exists (Filename.concat target name)) then
          match
            List.find_opt
              (fun directory -> Sys.file_exists (Filename.concat directory name))
              (Lazy.force directories)
          with
          | Some directory ->
              copy_file (Filename.concat directory name) (Filename.concat target name);
              Printf.printf "[suchu] %s needs %s, copied beside it\n%!" (Filename.basename program) name
          | None ->
              Printf.eprintf
                "[suchu] warning: %s needs %s and it was not found. The program will not start \
                 on a machine that does not have it; copy it next to the program.\n%!"
                (Filename.basename program) name)
      (imported_libraries program)
  end

let compile_native ?(console = false) source_path output_path =
  let ocaml = translate source_path in
  (* Windows opens a terminal for any executable linked as a console program,
     whether or not it ever prints. A program that opens a window is an
     application, not a command, so it is linked for the window subsystem and
     starts without one.

     The price is that such a program has no standard output to write to, so its
     'print' goes nowhere and so does an uncaught error. '--console' keeps the
     terminal for the times that is the thing wanted, which is most of the time
     while a program is being written. *)
  ignore console;
  if !Transpiler.uses_http then
    raise (Runtime_error "This program uses http, which Suchu on Vex cannot compile yet");
  (* Vex has no ocamlfind: ocamlopt is called directly, with the Suchu
     libraries installed next to the standard library. *)
  let library =
    match Sys.getenv_opt "SUCHU_LIB" with Some dir when dir <> "" -> dir | _ -> "/usr/lib/suchu"
  in
  if not (Sys.file_exists (Filename.concat library "suchu_eval.cmxa")) then
    raise
      (Runtime_error
         (Printf.sprintf "suchu comp needs the Suchu library in %s (set SUCHU_LIB to change it)"
            library));
  (* A directory of its own, so the .cmi and .cmx ocamlopt leaves behind land
     somewhere that is swept up rather than beside the user's program. *)
  let work =
    Filename.concat (Filename.get_temp_dir_name ())
      ("suchu-comp-" ^ String.sub (Digest.to_hex (Digest.string source_path)) 0 8)
  in
  ensure_directory work;
  let main = Filename.concat work "main.ml" in
  write_file main ocaml;
  (* A window or sound brings raylib and its bindings, in link order (see
     scripts/build-raylib.sh); libraylib.a is named again last because the
     stub archives listed after raylib_c need it. *)
  let toolkit =
    if !Transpiler.uses_gui || !Transpiler.uses_audio then
      "-I +str str.cmxa integers.cmxa ctypes.cmxa ctypes_stubs.cmxa raylib_c.cmxa raylib_core.cmxa        raylib_c_audio.cmxa raylib_audio.cmxa raylib_c_models.cmxa raylib_models.cmxa        raylib_c_shapes.cmxa raylib_shapes.cmxa raylib_c_text.cmxa raylib_text.cmxa        raylib_c_textures.cmxa raylib_textures.cmxa rlgl_c.cmxa raylib_rlgl.cmxa raylib.cmxa        suchu.cmxa -cclib -lraylib"
    else ""
  in
  let command =
    Printf.sprintf "cd %s && ocamlopt -w -a -I +unix -I %s unix.cmxa suchu_core.cmxa suchu_eval.cmxa %s -o %s main.ml"
      (Filename.quote work) (Filename.quote library) toolkit
      (Filename.quote
         (if Filename.is_relative output_path then Filename.concat (Sys.getcwd ()) output_path
          else output_path))
  in
  let status = Sys.command command in
  remove_directory work;
  if status <> 0 then raise (Runtime_error "Native compilation failed")

let usage () =
  print_endline "Usage:";
  print_endline "  suchu run <file.suchu>";
  print_endline "  suchu build <file.suchu> [-o output.ml]   translate to OCaml";
  print_endline "  suchu comp [--console] <file.suchu> [-o output]  compile to a native binary";
  print_endline "  suchu comp --bundle <file.suchu> [-o output]  ship the interpreter instead";
  print_endline "  suchu repl";
  print_endline "  suchu update [--root PATH] [--skip-deps] [install.sh options]"

let main argv =
  match Array.to_list argv with
  | _ :: "run" :: source :: rest -> (
      try
        (* Anything after the script belongs to the script, not to us. *)
        Runtime.program_arguments := rest;
        run_file source;
        0
      with
      | Runtime_error msg ->
          prerr_endline ("Runtime error: " ^ msg);
          3
      | Lexical.Lexing_error msg ->
          report_file_error "Lexing error" source msg;
          2
      | Parser.Parse_error msg ->
          report_file_error "Parse error" source msg;
          2)
  | _ :: "comp" :: rest -> (
      let rec flags bundled console = function
        | "--bundle" :: tail -> flags true console tail
        | "--console" :: tail -> flags bundled true tail
        | tail -> (bundled, console, tail)
      in
      let bundled, console, arguments = flags false false rest in
      match arguments with
      | [] ->
          prerr_endline "Usage: suchu comp [--bundle] [--console] <file.suchu> [-o output]";
          1
      | source :: tail -> (
          let output =
            match tail with
            | [] -> Filename.remove_extension source
            | [ "-o"; path ] | [ "--output"; path ] -> path
            | _ -> ""
          in
          if String.equal output "" then (
            prerr_endline "Usage: suchu comp [--bundle] <file.suchu> [-o output]";
            1)
          else
            try
              if bundled then begin
                build_native source output;
                Printf.printf "[suchu] Interpreter bundled with the program: %s\n" output
              end
              else begin
                compile_native ~console source output;
                Printf.printf "[suchu] Compiled to native code: %s\n" output
              end;
              0
            with
            | Runtime_error msg ->
                prerr_endline ("Compile error: " ^ msg);
                3
            | Lexical.Lexing_error msg ->
                report_file_error "Lexing error" source msg;
                2
            | Parser.Parse_error msg ->
                report_file_error "Parse error" source msg;
                2))
  | _ :: "build" :: source :: rest -> (
      let output =
        match rest with
        | [] -> Filename.remove_extension source ^ ".ml"
        | [ "-o"; path ]
        | [ "--output"; path ] -> path
        | _ ->
            prerr_endline "Usage: suchu build <file.suchu> [-o output.ml]";
            ""
      in
      if String.equal output "" then 1
      else
        try
          build_ocaml source output;
          Printf.printf "[suchu] OCaml written to %s\n" output;
          0
        with
        | Runtime_error msg ->
            prerr_endline ("Build error: " ^ msg);
            3
        | Lexical.Lexing_error msg ->
            report_file_error "Lexing error" source msg;
            2
        | Parser.Parse_error msg ->
            report_file_error "Parse error" source msg;
            2)
  | _ :: "repl" :: [] ->
      repl ();
      0
  | _ :: "update" :: rest -> run_update rest
  | _ ->
      usage ();
      1
