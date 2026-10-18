open Ast
open Runtime


exception Return_signal of value

(* Loop control uses exceptions like Return_signal does, so it unwinds nested
   blocks without every construct having to thread a status value around. Both
   are caught by the innermost enclosing loop. *)
exception Break_signal
exception Continue_signal


type interpreter = {
  globals : environment;
  mutable env : environment;
  mutable current_dir : string;
  module_cache : (string, value) Hashtbl.t;
  loading_modules : (string, unit) Hashtbl.t;
  embedded_modules : (string, string) Hashtbl.t;
}

(* --- Optional backend hooks -------------------------------------------------

   The evaluator carries no GUI dependency so it can be compiled to JavaScript
   with js_of_ocaml. Gui_backend installs the real implementations at startup;
   in a build without it, a [window] statement raises instead of opening one. *)

let gui_hook : (interpreter -> gui_block -> value) ref =
  ref (fun _ _ ->
      raise (Runtime_error "GUI support is not available in this build"))

(* Builtins contributed by optional backends, applied by [create]. *)
let extra_builtins : (interpreter -> unit) list ref = ref []


let with_scope interp env f =
  let previous = interp.env in
  interp.env <- env;
  Fun.protect ~finally:(fun () -> interp.env <- previous) f


let rec run_program interp (program : program) =
  let result = ref V_null in
  List.iter (fun stmt -> result := execute interp stmt) program;
  !result

and execute interp = function
  | Import module_spec ->
      let alias = module_alias module_spec in
      if String.equal alias "" then raise (Runtime_error "Import module name cannot be empty");
      let module_value =
        match find_native_module module_spec with
        | Some bindings ->
            let module_env = child_env interp.globals in
            List.iter (fun (name, value) -> env_define module_env name value) bindings;
            V_module module_env
        | None ->
            let path = resolve_module_path interp.current_dir module_spec in
            (match Hashtbl.find_opt interp.module_cache path with
            | Some value -> value
            | None ->
                if Hashtbl.mem interp.loading_modules path then
                  raise
                    (Runtime_error
                       (Printf.sprintf "Circular import detected while loading '%s'" module_spec));
                let embedded_source = Hashtbl.find_opt interp.embedded_modules path in
                if embedded_source = None && not (Sys.file_exists path) then
                  raise
                    (Runtime_error
                       (Printf.sprintf "Cannot import '%s': file not found at %s" module_spec path));
                Hashtbl.add interp.loading_modules path ();
                Fun.protect
                  ~finally:(fun () -> Hashtbl.remove interp.loading_modules path)
                  (fun () ->
                    let source =
                      match embedded_source with
                      | Some source -> source
                      | None ->
                          let channel =
                            try open_in_bin path with
                            | Sys_error message ->
                                raise
                                  (Runtime_error
                                     (Printf.sprintf "Cannot import '%s': %s" module_spec message))
                          in
                          Fun.protect
                            ~finally:(fun () -> close_in channel)
                            (fun () -> really_input_string channel (in_channel_length channel))
                    in
                    let program =
                      try Parser.parse source with
                      | Lexical.Lexing_error message ->
                          raise
                            (Runtime_error
                               (Printf.sprintf "In imported module '%s': %s" module_spec message))
                      | Parser.Parse_error message ->
                          raise
                            (Runtime_error
                               (Printf.sprintf "In imported module '%s': %s" module_spec message))
                    in
                    let module_env = child_env interp.globals in
                    let previous_dir = interp.current_dir in
                    interp.current_dir <- Filename.dirname path;
                    let value =
                      Fun.protect
                        ~finally:(fun () -> interp.current_dir <- previous_dir)
                        (fun () ->
                          ignore (with_scope interp module_env (fun () -> run_program interp program));
                          V_module module_env)
                    in
                    Hashtbl.replace interp.module_cache path value;
                    value))
      in
      env_define interp.env alias module_value;
      module_value
  | FunctionDef decl ->
      let fn = V_function { params = decl.params; body = decl.body; env = interp.env } in
      env_define interp.env decl.name fn;
      V_null
  | ExprStmt expr -> evaluate interp expr
  | Assign (target, value_expr) ->
      let value = evaluate interp value_expr in
      assign interp target value;
      value
  | Return maybe_expr ->
      let value = Option.map (evaluate interp) maybe_expr |> Option.value ~default:V_null in
      raise (Return_signal value)
  | IfStmt (condition, then_block, else_block) ->
      if truthy (evaluate interp condition) then execute_block interp then_block
      else (
        match else_block with
        | Some block -> execute_block interp block
        | None -> V_null)
  | MatchStmt (target_expr, branches, default_block) ->
      let target_value = evaluate interp target_expr in
      let rec dispatch = function
        | [] -> (
            match default_block with
            | Some block -> execute_block interp block
            | None -> V_null)
        | (case_expr, case_block) :: rest ->
            let case_value = evaluate interp case_expr in
            if value_equals target_value case_value then execute_block interp case_block else dispatch rest
      in
      dispatch branches
  | Break -> raise Break_signal
  | Continue -> raise Continue_signal
  | TryStmt (attempted, binding, handler) -> (
      try execute_block interp attempted with
      | Runtime_error message ->
          (* A record, not a bare string, so more fields can be added later
             without breaking handlers that already read .message *)
          let error = V_record (ref [ ("message", V_string message) ]) in
          let handler_env = child_env interp.env in
          Option.iter (fun name -> env_define handler_env name error) binding;
          with_scope interp handler_env (fun () -> execute_block interp handler))
  | WhileStmt (condition, body) ->
      (try
         while truthy (evaluate interp condition) do
           try ignore (execute_block interp body) with Continue_signal -> ()
         done
       with Break_signal -> ());
      V_null
  | ForStmt (name, iterable_expr, body) ->
      let iterable = evaluate interp iterable_expr in
      let values = collect_iterable iterable in
      let loop_env = child_env interp.env in
      with_scope interp loop_env (fun () ->
          try
            List.iter
              (fun value ->
                if env_has_local loop_env name then env_assign loop_env name value
                else env_define loop_env name value;
                try ignore (execute_block interp body) with Continue_signal -> ())
              values
          with Break_signal -> ());
      V_null
  | Gui block -> !gui_hook interp block

and execute_block interp statements =
  let block_env = child_env interp.env in
  with_scope interp block_env (fun () ->
      let result = ref V_null in
      try
        List.iter (fun stmt -> result := execute interp stmt) statements;
        !result
      with Return_signal value -> raise (Return_signal value))


and assign interp target value =
  match target with
  | Identifier name -> (
      try
        ignore (env_lookup interp.env name);
        env_assign interp.env name value
      with
      | Runtime_error _ -> env_define interp.env name value)
  | Attribute (Identifier "Length", name) ->
      raise (Runtime_error (Printf.sprintf "Cannot assign to Length.%s" name))
  (* items[0] = x and user["name"] = x *)
  | Index (target, index) ->
      let container = evaluate interp target in
      let key = evaluate interp index in
      index_set container key value
  (* user.age = 22, and user.city = "Paris" to add a field. Records are
     references, so mutating one is visible through every name bound to it. *)
  | Attribute (target, name) -> (
      match evaluate interp target with
      | V_record fields -> record_set fields name value
      | other ->
          raise
            (Runtime_error
               (Printf.sprintf "Cannot assign field '%s' on a %s" name (type_of_value other))))
  | _ -> raise (Runtime_error "Invalid assignment target")

and evaluate interp = function
  | Int n -> V_int n
  | Float f -> V_float f
  | Bool b -> V_bool b
  | String s -> V_string s
  | Identifier name -> env_lookup interp.env name
  | List entries ->
      let values = entries |> List.map (evaluate interp) in
      V_list (list_of_values values)
  | Index (target, index) ->
      let container = evaluate interp target in
      index_get container (evaluate interp index)
  | Record entries ->
      (* Later fields win, so { a: 1; a: 2; } holds 2 without duplicating 'a'. *)
      let fields = ref [] in
      List.iter (fun (name, expr) -> record_set fields name (evaluate interp expr)) entries;
      V_record fields
  | Set entries ->
      let values = entries |> List.map (evaluate interp) in
      make_set values
  | Null -> V_null
  | FunctionLiteral literal ->
      V_function { params = literal.params; body = literal.body; env = interp.env }
  | Unary (op, expr) ->
      let operand = evaluate interp expr in
      evaluate_unary op operand
  | Postfix (op, expr) ->
      evaluate_postfix interp op expr
  (* && and || have to decide before the right-hand side is touched, which is
     the whole point of them: 'len(xs) > 0 && xs[0] == 1' must not index an
     empty list. Every other operator wants both sides first. *)
  | Binary (left, B_and, right) ->
      let lv = evaluate interp left in
      if truthy lv then evaluate interp right else lv
  | Binary (left, B_or, right) ->
      let lv = evaluate interp left in
      if truthy lv then lv else evaluate interp right
  | Binary (left, op, right) ->
      let lv = evaluate interp left in
      let rv = evaluate interp right in
      evaluate_binary interp op lv rv
  | Call (callee_expr, arguments) ->
      let callee = evaluate interp callee_expr in
      let args = List.map (evaluate interp) arguments in
      call interp callee args
  | Attribute (expr, name) ->
      let obj = evaluate interp expr in
      evaluate_attribute interp obj name
  | IfExpr (cond, then_expr, else_expr) ->
      if truthy (evaluate interp cond) then evaluate interp then_expr else evaluate interp else_expr
  | Range (start_expr, end_expr) ->
      let start = evaluate interp start_expr in
      let finish = evaluate interp end_expr in
      range_value start finish
  | Annotated (declared, name, inner) -> check_annotation declared name (evaluate interp inner)

and evaluate_unary op operand =
  match op with
  | U_neg -> negate_operator operand
  | U_pos -> (
      match operand with
      | V_int _
      | V_float _ -> operand
      | _ -> raise (Runtime_error "Unary '+' expects a number"))
  | U_not -> V_bool (not (truthy operand))
  | U_pull -> pull_operator operand

and evaluate_postfix interp op expr =
  match expr with
  | Identifier name ->
      let current = env_lookup interp.env name in
      let direction = match op with Post_inc -> 1 | Post_dec -> -1 in
      env_assign interp.env name (step_value direction current);
      current
  | _ -> raise (Runtime_error "Postfix operators require an identifier")

and evaluate_binary interp op left right =
  match op with
  | B_plus -> plus_operator left right
  | B_minus -> minus_operator left right
  | B_mul -> multiply_operator left right
  | B_div -> divide_operator left right
  | B_mod -> modulo_operator left right
  | B_pow -> pow_operator left right
  | B_union -> set_union left right
  | B_intersection -> set_intersection left right
  | B_eq -> V_bool (value_equals left right)
  | B_neq -> V_bool (not (value_equals left right))
  | B_lt -> compare_operator `Lt left right
  | B_le -> compare_operator `Le left right
  | B_gt -> compare_operator `Gt left right
  | B_ge -> compare_operator `Ge left right
  (* Unreachable through [evaluate], which decides these before evaluating the
     right-hand side. Kept so the table stays total, and so a caller that
     already holds both values still gets the right answer. *)
  | B_and -> if truthy left then right else left
  | B_or -> if truthy left then left else right
  | B_at -> at_operator left right

and evaluate_attribute interp obj name =
  match obj with
  (* Length reads a variable by name, so it needs the scope. Everything else is
     the same lookup the compiler emits. *)
  | V_length -> length_of interp.env name
  | other -> attribute_of other name

and call interp callee args =
  match callee with
  | V_native fn -> fn args
  | V_function fn ->
      if List.length args <> List.length fn.params then
        raise
          (Runtime_error
             (Printf.sprintf "Function expected %d arguments but received %d" (List.length fn.params)
                (List.length args)));
      let local_env = child_env fn.env in
      List.iter2 (env_define local_env) fn.params args;
      with_scope interp local_env (fun () ->
          (* A function yields what it returns, and nothing else. Statements do
             carry a value -- the REPL prints it, and an assignment gives back
             what it assigned -- but that value stops at the end of the body: a
             function that falls off the end yields none. Accumulating it here
             instead made 'fun f(n) { x = n * 2; }' quietly answer with x. *)
          try
            List.iter (fun stmt -> ignore (execute interp stmt)) fn.body;
            V_null
          with Return_signal value -> value)
  | _ -> raise (Runtime_error "Attempted to call a non-callable value")


let register_interpreter_builtins interp =
  let define name fn = env_define interp.globals name (V_native fn) in
  define "map"
    (function
      | [ fn; list_value ] ->
          let list_ref = expect_list "map" list_value in
          let mapped = list_values list_ref |> List.map (fun item -> call interp fn [ item ]) in
          V_list (list_of_values mapped)
      | _ -> raise (Runtime_error "map expects (function, list)"));
  define "filter"
    (function
      | [ fn; list_value ] ->
          let list_ref = expect_list "filter" list_value in
          let filtered =
            list_values list_ref
            |> List.filter (fun item -> truthy (call interp fn [ item ]))
          in
          V_list (list_of_values filtered)
      | _ -> raise (Runtime_error "filter expects (function, list)"));
  define "reduce"
    (function
      | [ fn; list_value ] ->
          let list_ref = expect_list "reduce" list_value in
          (match list_values list_ref with
          | [] -> raise (Runtime_error "reduce() of empty list")
          | head :: tail ->
              List.fold_left (fun acc item -> call interp fn [ acc; item ]) head tail)
      | [ fn; list_value; initial ] ->
          let list_ref = expect_list "reduce" list_value in
          List.fold_left (fun acc item -> call interp fn [ acc; item ]) initial (list_values list_ref)
      | _ -> raise (Runtime_error "reduce expects (function, list[, initial])"));
  (* eval() runs a snippet in a room of its own.

     It used to run in the caller's scope, which made it the one place where a
     compiled program and an interpreted one could not agree: the caller's
     variables are entries in an environment when interpreted and OCaml refs when
     compiled, and a snippet can only reach the first. 'eval("n = 99;")' changed n
     in one and not the other, silently.

     Now it reaches nothing by default. It sees the built-ins that only compute,
     and whatever the caller chose to hand it. Both sides therefore behave the
     same for the reason that matters -- there is no ambient anything to differ
     about -- and eval loses the reputation it earned by having some.

     To let a snippet affect the program, give it a record:

       knobs = { speed: 1.0; };
       eval("speed = speed * 2;", knobs);
       knobs.speed                        // 2.0

     A record is one shared mutable value whether the program is interpreted or
     compiled, so the way back out is the same in both. *)
  let rec forbids_import statements =
    List.iter
      (fun statement ->
        match statement with
        | Import _ ->
            raise
              (Runtime_error
                 "eval cannot import: hand the snippet a record of what it may use")
        | IfStmt (_, a, b) ->
            forbids_import a;
            Option.iter forbids_import b
        | WhileStmt (_, body) | ForStmt (_, _, body) -> forbids_import body
        | TryStmt (a, _, b) ->
            forbids_import a;
            forbids_import b
        | MatchStmt (_, branches, default_block) ->
            List.iter (fun (_, block) -> forbids_import block) branches;
            Option.iter forbids_import default_block
        | FunctionDef declaration -> forbids_import declaration.body
        | _ -> ())
      statements
  in
  let evaluate_snippet source exposed =
    (* A snippet that does not parse is a Suchu error, not a compiler one: the
       text usually came from whoever is using the program, and a mistyped
       formula must be catchable rather than fatal. It used to escape 'try'
       entirely and take the program with it. *)
    let program =
      try Parser.parse source with
      | Parser.Parse_error message | Lexical.Lexing_error message ->
          raise (Runtime_error ("eval could not read the source: " ^ message))
    in
    forbids_import program;
    let sandbox = empty_env () in
    List.iter
      (fun name ->
        match Hashtbl.find_opt interp.globals.table name with
        | Some value -> env_define sandbox name value
        | None -> ())
      sandbox_builtins;
    (match exposed with
    | Some fields -> List.iter (fun (name, value) -> env_define sandbox name value) !fields
    | None -> ());
    let result = with_scope interp sandbox (fun () -> run_program interp program) in
    (* Whatever the snippet did to a name the caller exposed goes back into the
       record, which is how the caller hears about it. Names the snippet made up
       are its own and stay behind. *)
    (match exposed with
    | Some fields ->
        List.iter
          (fun (name, _) ->
            match Hashtbl.find_opt sandbox.table name with
            | Some value -> record_set fields name value
            | None -> ())
          !fields
    | None -> ());
    result
  in
  define "eval"
    (function
      | [ V_string source ] -> evaluate_snippet source None
      | [ V_string source; V_record fields ] -> evaluate_snippet source (Some fields)
      | [ V_string _; other ] ->
          raise
            (Runtime_error
               ("eval's second argument is a record of what the snippet may use, got "
              ^ type_of_value other))
      | _ -> raise (Runtime_error "eval expects a source string, and optionally a record"))

let create ?(base_dir = Sys.getcwd ()) ?(embedded_modules = []) () =
  Suchu_stdlib.register ();
  let globals = create_global_environment () in
  let embedded_table = Hashtbl.create (max 8 (List.length embedded_modules)) in
  List.iter (fun (path, source) -> Hashtbl.replace embedded_table path source) embedded_modules;
  let interp =
    {
      globals;
      env = globals;
      current_dir = base_dir;
      module_cache = Hashtbl.create 16;
      loading_modules = Hashtbl.create 8;
      embedded_modules = embedded_table;
    }
  in
  register_interpreter_builtins interp;
  List.iter (fun register -> register interp) !extra_builtins;
  interp
