(* Suchu to OCaml.

   The point is not escaping the interpreter for its own sake. It is that a
   Suchu variable currently costs a lookup by name through a chain of hash
   tables, and every expression costs a walk over AST nodes. Translated, a
   variable becomes an OCaml [ref] and an expression becomes OCaml code that
   ocamlopt turns into machine instructions.

   What does not change: Suchu is dynamically typed, so every value stays a
   [Runtime.value] and every operator stays a match on its constructors. That
   is the ceiling of this approach. It is also why the output calls the very
   same operator functions the evaluator calls -- [plus_operator] here is
   [plus_operator] there -- so the arithmetic cannot drift between the two.

   The output is meant to be read. Someone should be able to open the generated
   file, recognise their program, and follow it. *)

open Ast

exception Unsupported of string

let unsupported what =
  raise (Unsupported (Printf.sprintf "the compiler does not handle %s yet" what))

(* --- names ----------------------------------------------------------------

   Suchu and OCaml agree on the shape of an identifier. They disagree on
   OCaml's reserved words, and on Suchu allowing a leading capital. Prefixing
   with 'v_' settles both at once, and 'v_total' still reads as 'total' to
   whoever opens the file -- which a hash or a counter would not. *)

let ocaml_name suchu_name = "v_" ^ suchu_name

(* --- scopes ---------------------------------------------------------------

   One frame per Suchu block. A name in some frame is bound; a name in none is a
   global -- a built-in like [print].

   Bound names come in two kinds. Almost all are [Ref]: an OCaml ref, which is
   the whole point of compiling. The exception is the top level of an imported
   module, which is a [Slot] in a real environment, because 'math_tools.pi' has
   to read the same binding the module's own functions read, and an environment
   holds values rather than refs so there is nothing to share. It costs a
   hashtable lookup for a module's own top-level names -- exactly what the
   evaluator costs -- and the function bodies around them are still compiled. *)

type binding =
  | Ref of string
  | Slot of string * string  (* the environment's OCaml name, and the Suchu name *)
  (* A native 'int ref'. Every value in Suchu is boxed, because the language is
     dynamically typed and any variable may hold anything. But a variable that is
     only ever given integers does not need the box, and the box is most of the
     cost: 'total = total + i' otherwise allocates a V_int for the result and
     goes through a match on constructors to get there.

     A variable qualifies when every assignment to it, anywhere in the block that
     owns it, is an expression provably of integer type. Reading it where a value
     is wanted costs one box; reading it inside integer arithmetic costs
     nothing. *)
  | IntRef of string
  (* The same for floats. Worth its own kind rather than folding into the above,
     because the two do not mix: '%' refuses floats, and '/' gives a float even
     when both sides are integers. *)
  | FloatRef of string
  (* A plain value, not a cell: a parameter the function never writes to. There
     is nothing to write through, so there is nothing to allocate. *)
  | Value of string

type scope = (string, binding) Hashtbl.t list

let new_scope () : scope = [ Hashtbl.create 16 ]
let push (scope : scope) : scope = Hashtbl.create 16 :: scope

let declare (scope : scope) suchu_name =
  let name = ocaml_name suchu_name in
  (match scope with frame :: _ -> Hashtbl.replace frame suchu_name (Ref name) | [] -> ());
  name

let declare_slot (scope : scope) env_name suchu_name =
  match scope with
  | frame :: _ -> Hashtbl.replace frame suchu_name (Slot (env_name, suchu_name))
  | [] -> ()

let rec lookup (scope : scope) suchu_name =
  match scope with
  | [] -> None
  | frame :: rest -> (
      match Hashtbl.find_opt frame suchu_name with
      | Some binding -> Some binding
      | None -> lookup rest suchu_name)

let declare_value (scope : scope) suchu_name =
  let name = ocaml_name suchu_name in
  (match scope with frame :: _ -> Hashtbl.replace frame suchu_name (Value name) | [] -> ());
  name

let declare_numeric (scope : scope) kind suchu_name =
  let name = ocaml_name suchu_name in
  let binding = match kind with `Int -> IntRef name | `Float -> FloatRef name in
  (match scope with frame :: _ -> Hashtbl.replace frame suchu_name binding | [] -> ());
  name

(* Reading and writing a bound name as a Suchu value, whichever kind it is. An
   integer variable is boxed here and only here -- at the edge, where something
   that takes values is being handed one. *)
let read_binding = function
  | Ref name -> "!" ^ name
  | Value name -> name
  | IntRef name -> Printf.sprintf "(V_int !%s)" name
  | FloatRef name -> Printf.sprintf "(V_float !%s)" name
  | Slot (env_name, suchu_name) ->
      Printf.sprintf "(env_lookup %s %s)" env_name (Printf.sprintf "%S" suchu_name)

let write_binding binding value =
  match binding with
  | Ref name -> Printf.sprintf "%s := %s" name value
  (* Only names the analysis saw being written are cells, so this cannot arise. *)
  | Value name -> Printf.sprintf "ignore %s; ignore (%s)" name value
  | IntRef name -> Printf.sprintf "%s := expect_int (%s)" name value
  | FloatRef name -> Printf.sprintf "%s := fst (expect_number (%s))" name value
  | Slot (env_name, suchu_name) ->
      Printf.sprintf "env_set %s %s (%s)" env_name (Printf.sprintf "%S" suchu_name) value

let globals_used : (string, unit) Hashtbl.t = Hashtbl.create 16

(* Built-ins the program calls by name and never shadows. These are not looked up
   through [Lazy.force g_name] at the call; they are bound once, at the top of the
   file, to the OCaml function inside -- so a call reads as a call. *)
let direct_used : (string, unit) Hashtbl.t = Hashtbl.create 16

(* The built-ins that are plain functions, which is all a direct call can be made
   to. A value like 'pi' is not one, and a name the interpreter defines only when
   it starts up -- the window helpers, say -- might not exist, which has to be
   reported where the program mentions it and not before it has printed. *)
let core_builtins =
  lazy
    (let names = Hashtbl.create 32 in
     let env = Runtime.create_global_environment () in
     Hashtbl.iter
       (fun name value ->
         match value with Runtime.V_native _ -> Hashtbl.replace names name () | _ -> ())
       env.Runtime.table;
     names)

(* The names the block currently being compiled has decided hold integers.
   Saved and restored around every block, since a nested one decides its own. *)
let numeric_names : (string * [ `Int | `Float ]) list ref = ref []

(* Whether the program opens a window. Only then does the generated file mention
   Gui_backend, and only then does the linker pull in raylib and the embedded
   typeface -- eight megabytes that a program printing to a terminal has no use
   for. *)
let uses_gui = ref false
let uses_browser = ref false
let uses_platform = ref false

(* 'import http' pulls in sockets and TLS, which a program that never makes a
   request has no business linking. *)
let uses_http = ref false

(* 'import audio' pulls in the sound card, which a program that never plays
   anything has no business linking. *)
let uses_audio = ref false

(* --- imported modules -----------------------------------------------------

   Each imported Suchu file is translated once and emitted before the program
   that wants it, as a lazy value. Lazy for two reasons: a module's top level
   runs when it is first imported and not before, and a module imported from two
   places must run once -- which is the evaluator's cache, expressed in OCaml.

   [directory] is where relative imports are resolved from: the directory of the
   file being translated, which changes as the compiler walks into a module. *)

type module_state = {
  mutable emitted : (string * string) list;  (* path, the OCaml name of its lazy *)
  mutable in_progress : string list;
  mutable definitions : string list;  (* finished module definitions, in order *)
  mutable directory : string;
  mutable counter : int;
  (* Whether what is being translated right now is a module rather than the
     program itself. A window only opens from the program's own top level. *)
  mutable inside_module : bool;
}

let modules =
  {
    emitted = [];
    in_progress = [];
    definitions = [];
    directory = ".";
    counter = 0;
    inside_module = false;
  }

let reset_modules directory =
  modules.emitted <- [];
  modules.in_progress <- [];
  modules.definitions <- [];
  modules.directory <- directory;
  modules.counter <- 0;
  modules.inside_module <- false

(* Forced at the first use rather than bound at startup, for two reasons. A name
   that does not exist has to fail where the program mentions it, not before the
   program has printed anything -- which is what the evaluator does. And 'eval'
   can define a name while the program runs, so the lookup cannot already have
   happened. Lazy keeps the lookup to once per name all the same. *)
let global_name suchu_name =
  Hashtbl.replace globals_used suchu_name ();
  Printf.sprintf "(Lazy.force g_%s)" suchu_name

(* --- expressions --------------------------------------------------------- *)

let quoted text = Printf.sprintf "%S" text

(* The shortest decimal that reads back as the very same float. '%h' would also
   be exact, but it writes 1.5 as 0x1.8p+0, and someone opening the generated
   file should recognise the number they wrote. Seventeen significant digits
   always round-trip a double, so the search ends. *)
let float_literal value =
  if Float.is_nan value then "Float.nan"
  else if value = Float.infinity then "Float.infinity"
  else if value = Float.neg_infinity then "Float.neg_infinity"
  else
    let rec shortest precision =
      if precision > 17 then Printf.sprintf "%h" value
      else
        let text = Printf.sprintf "%.*g" precision value in
        if float_of_string text = value then text else shortest (precision + 1)
    in
    let text = shortest 1 in
    (* OCaml reads 2 as an integer, so a float that came out whole needs saying
       so, and a negative one needs wrapping where it sits beside an operator. *)
    let text = if String.contains text '.' || String.contains text 'e' then text else text ^ "." in
    if String.length text > 0 && text.[0] = '-' then "(" ^ text ^ ")" else text

let operator_of = function
  | B_plus -> "plus_operator"
  | B_minus -> "minus_operator"
  | B_mul -> "multiply_operator"
  | B_div -> "divide_operator"
  | B_mod -> "modulo_operator"
  | B_pow -> "pow_operator"
  | B_union -> "set_union"
  | B_intersection -> "set_intersection"
  | B_lt -> "(compare_operator `Lt)"
  | B_le -> "(compare_operator `Le)"
  | B_gt -> "(compare_operator `Gt)"
  | B_ge -> "(compare_operator `Ge)"
  | B_at -> "at_operator"
  | B_eq | B_neq | B_and | B_or -> assert false

(* OCaml does not promise the order in which it evaluates the arguments of a
   function or the elements of a list -- in practice it goes right to left.
   Suchu promises left to right, and a call in an operand is enough to make the
   difference visible.

   Saying so explicitly means binding each operand to a name in turn, which is
   correct but unpleasant to read, and the generated file is meant to be read.
   So it is only done where the order can be observed, and that needs a word for
   what an operand is. A literal commutes with everything. The reading of a
   variable commutes with other readings but not with an operand that might
   write it: in 'x + bump()' the left x has to be read before bump() changes it,
   and OCaml reads it after. Everything else -- a call, an operator, anything
   that might print, raise or assign -- is an effect, and two effects cannot be
   swapped either, since two operators that both raise would report whichever ran
   first. *)
(* --- which variables hold integers ----------------------------------------

   Deciding this wrongly does not make a program slow, it makes it wrong, so the
   rule is narrow and everything not understood is refused.

   A name introduced in a block is an integer when every assignment to it in that
   block, however deeply nested, is an expression of integer type -- and when
   nothing else in the block binds the same name, since a parameter, a 'for'
   variable or a 'try' error of that name would be a different variable the
   analysis is not tracking.

   Integer type means: an integer literal; a variable already known to hold
   integers; '+', '-', '*' or '%' of two of those; a negation of one. Not '/',
   which always gives a float, and not '**', which leaves the integers as soon as
   it overflows. *)

(* Every name the statements bind by any means other than plain assignment. *)
let rec shadowing_binders statements =
  List.concat_map
    (fun statement ->
      match statement with
      | ForStmt (name, _, body) -> name :: shadowing_binders body
      | TryStmt (attempted, binding, handler) ->
          Option.to_list binding @ shadowing_binders attempted @ shadowing_binders handler
      | FunctionDef declaration ->
          (declaration.name :: declaration.params) @ shadowing_binders declaration.body
      | Import specification -> [ Runtime.module_alias specification ]
      | IfStmt (_, then_block, else_block) ->
          shadowing_binders then_block
          @ (match else_block with Some block -> shadowing_binders block | None -> [])
      | WhileStmt (_, body) -> shadowing_binders body
      | MatchStmt (_, branches, default_block) ->
          List.concat_map (fun (_, block) -> shadowing_binders block) branches
          @ (match default_block with Some block -> shadowing_binders block | None -> [])
      | _ -> [])
    statements

(* Names bound inside function literals, which are their own scopes but can also
   assign to a name from around them. Collected from expressions. *)
let rec expression_binders expr =
  match expr with
  | FunctionLiteral literal -> literal.params @ shadowing_binders literal.body
  | Binary (left, _, right) -> expression_binders left @ expression_binders right
  | Unary (_, operand) | Postfix (_, operand) -> expression_binders operand
  | Call (callee, arguments) ->
      expression_binders callee @ List.concat_map expression_binders arguments
  | Index (target, index) -> expression_binders target @ expression_binders index
  | Attribute (target, _) -> expression_binders target
  | IfExpr (condition, then_expr, else_expr) ->
      expression_binders condition @ expression_binders then_expr @ expression_binders else_expr
  | Range (from_expr, to_expr) -> expression_binders from_expr @ expression_binders to_expr
  | Annotated (_, _, inner) -> expression_binders inner
  | List entries | Set entries -> List.concat_map expression_binders entries
  | Record entries -> List.concat_map (fun (_, value) -> expression_binders value) entries
  | _ -> []

(* Every plain assignment in the statements, as (name, right-hand side), and
   every name stepped by '++' or '--'. A stepped name keeps whatever type it had,
   so it is reported separately rather than as an assignment. *)
let rec assignments_and_steps statements =
  let from_expression expr =
    let rec walk expr =
      match expr with
      | Postfix (_, Identifier name) -> ([], [ name ])
      | Postfix (_, operand) -> walk operand
      | Binary (left, _, right) -> join (walk left) (walk right)
      | Unary (_, operand) -> walk operand
      | Call (callee, arguments) ->
          List.fold_left (fun acc argument -> join acc (walk argument)) (walk callee) arguments
      | Index (target, index) -> join (walk target) (walk index)
      | Attribute (target, _) -> walk target
      | IfExpr (condition, then_expr, else_expr) ->
          join (walk condition) (join (walk then_expr) (walk else_expr))
      | Range (from_expr, to_expr) -> join (walk from_expr) (walk to_expr)
      | Annotated (_, _, inner) -> walk inner
      | List entries | Set entries ->
          List.fold_left (fun acc entry -> join acc (walk entry)) ([], []) entries
      | Record entries ->
          List.fold_left (fun acc (_, value) -> join acc (walk value)) ([], []) entries
      | FunctionLiteral literal -> assignments_and_steps literal.body
      | _ -> ([], [])
    and join (a, b) (c, d) = (a @ c, b @ d) in
    walk expr
  in
  let join (a, b) (c, d) = (a @ c, b @ d) in
  List.fold_left
    (fun acc statement ->
      let here =
        match statement with
        | Assign (Identifier name, value_expr) ->
            join ([ (name, value_expr) ], []) (from_expression value_expr)
        | Assign (target, value_expr) ->
            join (from_expression target) (from_expression value_expr)
        | ExprStmt expr | Return (Some expr) -> from_expression expr
        | IfStmt (condition, then_block, else_block) ->
            join (from_expression condition)
              (join (assignments_and_steps then_block)
                 (match else_block with
                 | Some block -> assignments_and_steps block
                 | None -> ([], [])))
        | WhileStmt (condition, body) ->
            join (from_expression condition) (assignments_and_steps body)
        | ForStmt (_, iterable, body) ->
            join (from_expression iterable) (assignments_and_steps body)
        | TryStmt (attempted, _, handler) ->
            join (assignments_and_steps attempted) (assignments_and_steps handler)
        | MatchStmt (target, branches, default_block) ->
            let branch_part =
              List.fold_left
                (fun acc (case_expr, block) ->
                  join acc (join (from_expression case_expr) (assignments_and_steps block)))
                ([], []) branches
            in
            join (from_expression target)
              (join branch_part
                 (match default_block with
                 | Some block -> assignments_and_steps block
                 | None -> ([], [])))
        | FunctionDef declaration -> assignments_and_steps declaration.body
        | Gui _ -> ([], [])
        | _ -> ([], [])
      in
      join acc here)
    ([], []) statements

(* How many 'return's a body has, not counting those inside a function written
   within it -- those belong to that one. A body whose only return is its last
   statement, or which has none at all, does not need an exception to carry the
   value out: the value is simply what the body ends with. That matters because
   the exception was being raised and caught on every single call. *)
let rec count_returns statements =
  let rec in_expression expr =
    match expr with
    | FunctionLiteral _ -> 0
    | Binary (left, _, right) -> in_expression left + in_expression right
    | Unary (_, operand) | Postfix (_, operand) -> in_expression operand
    | Call (callee, arguments) ->
        in_expression callee + List.fold_left (fun n a -> n + in_expression a) 0 arguments
    | Index (target, index) -> in_expression target + in_expression index
    | Attribute (target, _) -> in_expression target
    | IfExpr (condition, then_expr, else_expr) ->
        in_expression condition + in_expression then_expr + in_expression else_expr
    | Range (from_expr, to_expr) -> in_expression from_expr + in_expression to_expr
    | Annotated (_, _, inner) -> in_expression inner
    | List entries | Set entries -> List.fold_left (fun n e -> n + in_expression e) 0 entries
    | Record entries -> List.fold_left (fun n (_, e) -> n + in_expression e) 0 entries
    | _ -> 0
  in
  List.fold_left
    (fun total statement ->
      total
      +
      match statement with
      | Return None -> 1
      | Return (Some expr) -> 1 + in_expression expr
      | Assign (target, value_expr) -> in_expression target + in_expression value_expr
      | ExprStmt expr -> in_expression expr
      | IfStmt (condition, then_block, else_block) ->
          in_expression condition + count_returns then_block
          + (match else_block with Some block -> count_returns block | None -> 0)
      | WhileStmt (condition, body) -> in_expression condition + count_returns body
      | ForStmt (_, iterable, body) -> in_expression iterable + count_returns body
      | TryStmt (attempted, _, handler) -> count_returns attempted + count_returns handler
      | MatchStmt (target, branches, default_block) ->
          in_expression target
          + List.fold_left (fun n (_, block) -> n + count_returns block) 0 branches
          + (match default_block with Some block -> count_returns block | None -> 0)
      | FunctionDef _ -> 0
      | _ -> 0)
    0 statements

(* True when the body can hand its value back without an exception. *)
let returns_only_at_the_end body =
  match count_returns body with
  | 0 -> true
  | 1 -> ( match List.rev body with Return _ :: _ -> true | _ -> false)
  | _ -> false

type ordering =
  | Constant  (* a literal: commutes with everything *)
  | Read  (* the reading of a variable: commutes with reads, not with writes *)
  | Effect  (* might print, raise or assign: commutes with nothing but a literal *)

let rec ordering_of = function
  | Int _ | Float _ | Bool _ | String _ | Null -> Constant
  | Identifier _ -> Read
  | Unary (U_pos, operand) -> ordering_of operand
  | _ -> Effect

(* The operands of a call are evaluated callee first. A callee that is a plain
   name is treated as a constant here: what it would take is an argument that
   reassigns the very function being called, and binding the callee to a name
   for that would put 'let part0 = ...' in front of every call in every program. *)
let callee_ordering = function Identifier _ -> Constant | other -> ordering_of other

let sequenced parts assemble =
  let count kind = List.length (List.filter (fun (_, ordering) -> ordering = kind) parts) in
  let effects = count Effect and reads = count Read in
  (* Nothing to reorder unless an effect has company: another effect, or a read
     that the effect might change underneath it. *)
  if effects = 0 || (effects = 1 && reads = 0) then
    Printf.sprintf "(%s)" (assemble (List.map fst parts))
  else
    let bindings =
      List.mapi (fun index (part, _) -> Printf.sprintf "let part%d = %s in " index part) parts
      |> String.concat ""
    in
    let names = List.mapi (fun index _ -> Printf.sprintf "part%d" index) parts in
    Printf.sprintf "(%s%s)" bindings (assemble names)

(* Is this expression an integer, given a set of names assumed to be? Used both
   during the fixed point, where [assumed] is the candidate set, and afterwards
   when emitting, where the scope has the answer. *)
let rec numeric_kind scope assumed expr =
  let recurse = numeric_kind scope assumed in
  (* '+', '-' and '*' stay in the integers only if both sides are there; one
     float makes the result a float, which is what the boxed operators do by
     falling through to expect_number. *)
  let widen left right =
    match (recurse left, recurse right) with
    | Some `Int, Some `Int -> Some `Int
    | Some _, Some _ -> Some `Float
    | _ -> None
  in
  match expr with
  | Int _ -> Some `Int
  | Float _ -> Some `Float
  | Identifier name -> (
      match List.assoc_opt name assumed with
      | Some kind -> Some kind
      | None -> (
          match lookup scope name with
          | Some (IntRef _) -> Some `Int
          | Some (FloatRef _) -> Some `Float
          | _ -> None))
  | Binary (left, (B_plus | B_minus | B_mul), right) -> widen left right
  (* Division always leaves the integers: 6 / 3 is 2.0, not 2. *)
  | Binary (left, B_div, right) -> (
      match (recurse left, recurse right) with Some _, Some _ -> Some `Float | _ -> None)
  (* Modulo refuses floats outright, so it is only ever integer. *)
  | Binary (left, B_mod, right) -> (
      match (recurse left, recurse right) with
      | Some `Int, Some `Int -> Some `Int
      | _ -> None)
  | Unary (U_neg, operand) | Unary (U_pos, operand) -> recurse operand
  (* An annotation the analysis can already prove is no check at all, and the
     variable stays a native integer or float: 'int n = 5;' costs nothing. What it
     cannot prove it leaves to the runtime. *)
  | Annotated ("int", _, inner) -> (match recurse inner with Some `Int -> Some `Int | _ -> None)
  | Annotated ("float", _, inner) -> (match recurse inner with Some `Float -> Some `Float | _ -> None)
  | _ -> None

(* The names introduced in this block that hold integers throughout it. Start by
   assuming every candidate does -- which is what lets 'total = total + i' hold
   itself up -- then drop any whose assignments do not agree, and repeat until
   nothing more drops. *)
let numeric_locals scope statements =
  let assignments, stepped = assignments_and_steps statements in
  let shadowed = shadowing_binders statements in
  let literal_binders =
    List.concat_map (fun (_, value_expr) -> expression_binders value_expr) assignments
  in
  let candidates =
    assignments
    |> List.filter_map (fun (name, _) ->
           if
             lookup scope name = None
             && (not (List.mem name shadowed))
             && not (List.mem name literal_binders)
           then Some name
           else None)
    |> List.sort_uniq String.compare
  in
  (* A stepped name is fine -- '++' keeps whatever type it had -- so stepping
     says nothing that has to be checked here. *)
  ignore stepped;
  (* Every candidate starts at the narrowest kind and only ever widens: integer,
     then float, then dropped. Starting narrow is what lets 'total = total + i'
     hold itself up, and only widening means this settles. *)
  let rec settle assumed =
    let next =
      List.filter_map
        (fun (name, kind) ->
          let observed =
            assignments
            |> List.filter (fun (assigned, _) -> assigned = name)
            |> List.map (fun (_, value_expr) -> numeric_kind scope assumed value_expr)
          in
          if List.exists (fun kind -> kind = None) observed then None
          else if List.exists (fun observed -> observed = Some `Float) observed then
            Some (name, `Float)
          else Some (name, kind))
        assumed
    in
    if next = assumed then assumed else settle next
  in
  settle (List.map (fun name -> (name, `Int)) candidates)

let rec operands scope left right =
  [ (expression scope left, ordering_of left); (expression scope right, ordering_of right) ]

and parts scope entries =
  List.map (fun entry -> (expression scope entry, ordering_of entry)) entries

(* The same expression as native integer arithmetic, when it is one. Every case
   here has to compute exactly what the boxed operator computes -- including
   'Modulo by zero', which is the one place the integers can still raise. *)
and int_expression scope expr =
  match numeric_kind scope [] expr with
  | Some `Int -> Some (numeric_code scope `Int expr)
  | _ -> None

and float_expression scope expr =
  match numeric_kind scope [] expr with
  | Some `Float -> Some (numeric_code scope `Float expr)
  | _ -> None

(* The expression as native arithmetic of the given kind. An integer wanted as a
   float is converted here, which is where the boxed operators convert it too. *)
and numeric_code scope wanted expr =
  let own = numeric_kind scope [] expr in
  let promote code = if wanted = `Float && own = Some `Int then Printf.sprintf "(float_of_int %s)" code else code in
  let both operator left right =
    let inner = if wanted = `Float then `Float else `Int in
    Printf.sprintf "(%s %s %s)" (numeric_code scope inner left) operator
      (numeric_code scope inner right)
  in
  match expr with
  | Annotated (_, _, inner) -> numeric_code scope wanted inner
  | Int value -> promote (string_of_int value)
  | Float value -> float_literal value
  | Identifier name -> (
      match lookup scope name with
      | Some (IntRef name) -> promote ("!" ^ name)
      | Some (FloatRef name) -> "!" ^ name
      | _ -> assert false)
  | Binary (left, B_plus, right) -> both (if wanted = `Float then "+." else "+") left right
  | Binary (left, B_minus, right) -> both (if wanted = `Float then "-." else "-") left right
  | Binary (left, B_mul, right) -> both (if wanted = `Float then "*." else "*") left right
  (* Division is always a float, whatever it was handed. *)
  | Binary (left, B_div, right) ->
      Printf.sprintf "(%s /. %s)" (numeric_code scope `Float left) (numeric_code scope `Float right)
  | Binary (left, B_mod, right) ->
      promote
        (Printf.sprintf
           "(let divisor = %s in if divisor = 0 then raise (Runtime_error \"Modulo by zero\") else \
            %s mod divisor)"
           (numeric_code scope `Int right) (numeric_code scope `Int left))
  | Unary (U_neg, operand) ->
      if wanted = `Float then Printf.sprintf "(-. %s)" (numeric_code scope `Float operand)
      else Printf.sprintf "(- %s)" (numeric_code scope `Int operand)
  | Unary (U_pos, operand) -> numeric_code scope wanted operand
  | _ -> assert false

(* A comparison of two integers as a native bool, for the condition of a 'while'
   or an 'if'. Saves boxing a V_bool per turn of the loop as well. *)
and int_condition scope expr =
  match expr with
  | Binary (left, ((B_lt | B_le | B_gt | B_ge | B_eq | B_neq) as op), right) -> (
      match (numeric_kind scope [] left, numeric_kind scope [] right) with
      (* Only between two integers. Between floats the boxed comparison has its
         own answers for NaN and for negative zero, and matching them exactly
         matters more than saving a box. *)
      | Some `Int, Some `Int ->
          let symbol =
            match op with
            | B_lt -> "<"
            | B_le -> "<="
            | B_gt -> ">"
            | B_ge -> ">="
            | B_eq -> "="
            | _ -> "<>"
          in
          Some
            (Printf.sprintf "(%s %s %s)" (numeric_code scope `Int left) symbol
               (numeric_code scope `Int right))
      | _ -> None)
  | _ -> None

(* How a condition is tested: natively when both sides are integers, through
   [truthy] otherwise. *)
and condition_code scope expr =
  match int_condition scope expr with
  | Some native -> native
  | None -> Printf.sprintf "truthy %s" (expression scope expr)

(* The operands of a chain of '@', leftmost first, when it opens with a string
   literal. Only the left spine is followed: a right operand that is itself an
   '@' is an ordinary value to convert, and may well be a list being appended. *)
and string_chain expr =
  match expr with
  | Binary (left, B_at, right) -> (
      match string_chain left with Some chain -> Some (chain @ [ right ]) | None -> None)
  | String _ -> Some [ expr ]
  | _ -> None

and is_string_chain expr = Option.is_some (string_chain expr)

(* One operand of such a chain, as OCaml text. An integer the analysis already
   holds natively is written out directly; that is value_to_string's own answer
   for an integer, without the box. *)
and text_of scope operand =
  match operand with
  | String text -> quoted text
  | Int value -> quoted (string_of_int value)
  | _ when ordering_of operand = Read && numeric_kind scope [] operand = Some `Int ->
      Printf.sprintf "(string_of_int %s)" (numeric_code scope `Int operand)
  | _ -> Printf.sprintf "(value_to_string %s)" (expression scope operand)

and direct_builtin scope name =
  Option.is_none (lookup scope name) && Hashtbl.mem (Lazy.force core_builtins) name

and expression scope expr =
  match expr with
  | Int value -> Printf.sprintf "(V_int %d)" value
  | Float value -> Printf.sprintf "(V_float %s)" (float_literal value)
  | Bool value -> Printf.sprintf "(V_bool %b)" value
  | String text -> Printf.sprintf "(V_string %s)" (quoted text)
  | Null -> "V_null"
  | Identifier name -> (
      match lookup scope name with
      | Some binding -> read_binding binding
      | None -> global_name name)
  (* Lazy, exactly as in the evaluator: the right side must not run once the
     left has decided. *)
  | Binary (left, B_and, right) ->
      Printf.sprintf "(let left = %s in if truthy left then %s else left)"
        (expression scope left) (expression scope right)
  | Binary (left, B_or, right) ->
      Printf.sprintf "(let left = %s in if truthy left then left else %s)"
        (expression scope left) (expression scope right)
  | Binary (left, B_eq, right) ->
      sequenced (operands scope left right) (fun names ->
          Printf.sprintf "V_bool (value_equals %s)" (String.concat " " names))
  | Binary (left, B_neq, right) ->
      sequenced (operands scope left right) (fun names ->
          Printf.sprintf "V_bool (not (value_equals %s))" (String.concat " " names))
  (* A chain of '@' that starts with a string literal is a string all the way
     along: at_operator on a string is the text followed by the right operand as
     text, and the result is a string again. So the chain is one concatenation,
     instead of a call per link that boxes each number only to unbox it.

     Each operand is turned into text where it is evaluated, not afterwards: in
     '"a" @ list @ grow()' the list has to be written out before grow() adds to
     it, which is what the chain of calls did. *)
  | Binary (_, B_at, _) when is_string_chain expr -> (
      match string_chain expr with
      | Some chain ->
          sequenced
            (List.map (fun operand -> (text_of scope operand, ordering_of operand)) chain)
            (fun names -> Printf.sprintf "V_string (%s)" (String.concat " ^ " names))
      | None -> assert false)
  | Binary (left, op, right) ->
      sequenced (operands scope left right) (fun names ->
          Printf.sprintf "%s %s" (operator_of op) (String.concat " " names))
  | Unary (U_neg, operand) ->
      Printf.sprintf "(negate_operator %s)" (expression scope operand)
  | Unary (U_pos, operand) -> expression scope operand
  | Unary (U_not, operand) ->
      Printf.sprintf "(V_bool (not (truthy %s)))" (expression scope operand)
  | Unary (U_pull, operand) -> Printf.sprintf "(pull_operator %s)" (expression scope operand)
  | IfExpr (condition, then_expr, else_expr) ->
      Printf.sprintf "(if %s then %s else %s)" (condition_code scope condition)
        (expression scope then_expr) (expression scope else_expr)
  (* The callee is evaluated before the arguments, and the arguments in the
     order they are written. *)
  | Call (Identifier name, arguments) when direct_builtin scope name ->
      Hashtbl.replace direct_used name ();
      sequenced (parts scope arguments) (fun names ->
          Printf.sprintf "builtin_%s [%s]" name (String.concat "; " names))
  | Call (callee, arguments) ->
      sequenced
        ((expression scope callee, callee_ordering callee)
        :: List.map (fun part -> (expression scope part, ordering_of part)) arguments)
        (fun names ->
          match names with
          | callee_name :: argument_names ->
              Printf.sprintf "call_value %s [%s]" callee_name
                (String.concat "; " argument_names)
          | [] -> assert false)
  | List entries ->
      sequenced (parts scope entries) (fun names ->
          Printf.sprintf "V_list (list_of_values [%s])" (String.concat "; " names))
  | Set entries ->
      sequenced (parts scope entries) (fun names ->
          Printf.sprintf "make_set [%s]" (String.concat "; " names))
  (* Written as a sequence of record_set rather than an association list because
     a later field has to overwrite an earlier one of the same name instead of
     shadowing it -- '{ a: 1; a: 2; }' holds one field. Doing it in order is
     also what evaluates the fields left to right. *)
  | Record entries ->
      let buffer = Buffer.create 64 in
      Buffer.add_string buffer "(let fields = ref [] in ";
      List.iter
        (fun (name, value_expr) ->
          Buffer.add_string buffer
            (Printf.sprintf "record_set fields %s %s; " (quoted name) (expression scope value_expr)))
        entries;
      Buffer.add_string buffer "V_record fields)";
      Buffer.contents buffer
  | Index (target, index) ->
      sequenced (operands scope target index) (fun names ->
          Printf.sprintf "index_get %s" (String.concat " " names))
  (* 'Length.total' reads a variable by name. The evaluator searches its scopes
     for it; here the scope is already known, so the search is done now and what
     is left is the measuring. *)
  | Attribute (Identifier "Length", name) ->
      Printf.sprintf "(length_of_value %s %s)" (quoted name)
        (expression scope (Identifier name))
  | Attribute (target, name) ->
      Printf.sprintf "(attribute_of %s %s)" (expression scope target) (quoted name)
  | Range (from_expr, to_expr) ->
      sequenced (operands scope from_expr to_expr) (fun names ->
          Printf.sprintf "range_value %s" (String.concat " " names))
  (* A declared type is checked where the value is assigned, by the function the
     evaluator calls. When the analysis already knows the value is what was
     declared there is nothing to check, and nothing is emitted. *)
  | Annotated (declared, name, inner) -> (
      match (declared, numeric_kind scope [] inner) with
      | "int", Some `Int | "float", Some `Float -> expression scope inner
      | _ ->
          Printf.sprintf "(check_annotation %s %s %s)" (quoted declared) (quoted name)
            (expression scope inner))
  (* '++' yields what the variable held before the step. *)
  | Postfix (op, Identifier name) -> (
      let direction = match op with Post_inc -> 1 | Post_dec -> -1 in
      match lookup scope name with
      | Some (IntRef target) ->
          Printf.sprintf "(let previous = !%s in %s := previous + (%d); V_int previous)" target target
            direction
      | Some (FloatRef target) ->
          Printf.sprintf "(let previous = !%s in %s := previous +. %s; V_float previous)" target
            target (float_literal (float_of_int direction))
      | Some binding ->
          Printf.sprintf "(let previous = %s in %s; previous)" (read_binding binding)
            (write_binding binding (Printf.sprintf "step_value (%d) previous" direction))
      | None -> unsupported (Printf.sprintf "'++' on '%s', which is not a variable here" name))
  | Postfix _ -> unsupported "'++' or '--' on anything but a plain variable"
  (* A closure over OCaml refs, which is what a Suchu closure already is: the
     refs it reads belong to the enclosing scope, so it sees later changes to
     them rather than a copy taken at the point it was written. *)
  | FunctionLiteral literal ->
      Printf.sprintf "(%s)"
        (String.trim (compile_lambda scope 0 "anonymous function" literal.params literal.body))

(* --- statements ----------------------------------------------------------

   Everything is written into a buffer with an explicit indentation level, so
   the generated file is laid out like hand-written OCaml. *)

and emit buffer level text = Buffer.add_string buffer (String.make (level * 2) ' ' ^ text ^ "\n")

(* Every name -- variable or function -- is a ref, so a 'let rec ... and ...'
   group whose shape would have to match the source order is not needed.

   Each function in the block gets its ref up front, which is what lets two
   functions call each other. The bodies are filled in where they were written,
   and not before: a body has to see the variables that were in scope at that
   point, and only those. Filling them all in first made 'passed = 0' followed by
   a function that increments it compile the function against a scope where
   'passed' did not exist yet. *)
and compile_block ?(tail = false) buffer scope level statements =
  let scope = push scope in
  let declarations = List.filter_map (function FunctionDef d -> Some d | _ -> None) statements in
  List.iter
    (fun (declaration : function_decl) ->
      emit buffer level (Printf.sprintf "let %s = ref V_null in" (declare scope declaration.name)))
    declarations;
  let outer_numerics = !numeric_names in
  numeric_names := numeric_locals scope statements;
  Fun.protect
    ~finally:(fun () -> numeric_names := outer_numerics)
    (fun () -> compile_sequence ~tail buffer scope level statements)

(* One body for both a 'fun' declaration and a function literal: they differ only
   in whether the result is bound to a name, and in what an arity complaint calls
   them. Parameters become refs like any other local, which is what lets a
   function assign to its own parameter without the caller seeing it. *)
and compile_lambda scope level label params body =
  let buffer = Buffer.create 256 in
  let inner = push scope in
  Buffer.add_string buffer "V_native (fun arguments ->\n";
  (* One match does the counting and the taking apart at once, where it used to
     be List.length followed by a List.nth for each parameter -- three walks over
     the same short list. *)
  let assigned, stepped = assignments_and_steps body in
  let assigned_names = List.map fst assigned @ stepped in
  let incoming =
    List.map (fun parameter -> "in_" ^ ocaml_name parameter) params |> String.concat "; "
  in
  emit buffer (level + 1) (Printf.sprintf "match arguments with");
  emit buffer (level + 1) (Printf.sprintf "| [ %s ] ->" incoming);
  List.iter
    (fun parameter ->
      let name = declare inner parameter in
      (* A parameter a function never writes to does not need a cell of its own;
         it is already a value sitting in a register. Only one it assigns has to
         be a ref, so that writing it stays local to the call. *)
      if List.mem parameter assigned_names then
        emit buffer (level + 2) (Printf.sprintf "let %s = ref in_%s in" name name)
      else begin
        let name = declare_value inner parameter in
        emit buffer (level + 2) (Printf.sprintf "let %s = in_%s in" name name)
      end)
    params;
  (* A body whose only 'return' is its last statement, or which has none, hands
     its value straight back. The exception is only needed to leave from the
     middle -- out of a loop, out of a branch -- and raising and catching one on
     every call was costing more than the call itself. *)
  (* Delimit the match branch. Otherwise a generated `try ... with` captures
     the following `| _ -> arity error` as an exception handler, hiding the
     real body error (and leaving the argument match non-exhaustive). *)
  emit buffer (level + 2) "begin";
  if returns_only_at_the_end body then compile_block ~tail:true buffer inner (level + 3) body
  else begin
    emit buffer (level + 3) "try";
    compile_block buffer inner (level + 4) body;
    emit buffer (level + 4) ";";
    emit buffer (level + 4) "V_null";
    emit buffer (level + 3) "with Return_signal result -> result"
  end;
  emit buffer (level + 2) "end";
  (* Any other shape of argument list is the wrong number of them. *)
  emit buffer (level + 1) "| _ ->";
  emit buffer (level + 2)
    (Printf.sprintf "raise (Runtime_error %s))"
       (quoted (Printf.sprintf "%s expects %d argument(s)" label (List.length params))));
  Buffer.contents buffer

and compile_function buffer scope level (declaration : function_decl) =
  let binding = Option.get (lookup scope declaration.name) in
  let lambda =
    String.trim (compile_lambda scope level declaration.name declaration.params declaration.body)
  in
  match binding with
  (* A 'fun' always makes its name in the environment it is written in, as the
     evaluator does, whatever the importer happens to have called something. *)
  | Slot (env_name, suchu_name) ->
      emit buffer level (Printf.sprintf "env_define %s %S (%s)" env_name suchu_name lambda)
  | _ -> emit buffer level (write_binding binding lambda)

and compile_sequence ?(tail = false) buffer scope level statements =
  match statements with
  (* In tail position the block is an expression whose value the caller wants,
     so it ends with a value rather than with unit. *)
  | [] -> emit buffer level (if tail then "V_null" else "()")
  | [ Return None ] when tail -> emit buffer level "V_null"
  | [ Return (Some expr) ] when tail -> emit buffer level (expression scope expr)
  (* The first assignment to a name introduces its ref, and everything after it
     lives inside that 'let ... in'. That is Suchu's rule that a variable made
     in a block does not outlive the block, expressed in OCaml's own scoping. *)
  | Assign (Identifier name, value_expr) :: rest when lookup scope name = None ->
      (* The kind the block decided for this name, but only if what it is being
         given right now agrees -- the analysis speaks for the block, not for one
         statement. *)
      let kind =
        match List.assoc_opt name !numeric_names with
        | Some wanted when numeric_kind scope [] value_expr = Some wanted -> Some wanted
        | _ -> None
      in
      let value =
        match kind with
        | Some wanted -> numeric_code scope wanted value_expr
        | None -> expression scope value_expr
      in
      let scope = push scope in
      let declared =
        match kind with
        | Some wanted -> declare_numeric scope wanted name
        | None -> declare scope name
      in
      emit buffer level (Printf.sprintf "let %s = ref %s in" declared value);
      compile_sequence ~tail buffer scope level rest
  (* An import binds a name, so like an assignment it owns everything after it. *)
  | Import specification :: rest -> (
      let alias = Runtime.module_alias specification in
      if String.equal alias "" then unsupported "an import with an empty module name";
      let value = import_value specification in
      match lookup scope alias with
      (* Already bound: the top level of a module, where names are slots. *)
      | Some binding ->
          emit buffer level (write_binding binding value);
          emit buffer level ";";
          compile_sequence ~tail buffer scope level rest
      | None ->
          let scope = push scope in
          emit buffer level (Printf.sprintf "let %s = ref %s in" (declare scope alias) value);
          compile_sequence ~tail buffer scope level rest)
  (* Below the cases that bind a name, since those own everything after them and
     a single one of them is still a declaration rather than a last statement. *)
  | [ statement ] when tail ->
      compile_statement buffer scope level statement;
      emit buffer level ";";
      emit buffer level "V_null"
  | [ statement ] -> compile_statement buffer scope level statement
  | statement :: rest ->
      compile_statement buffer scope level statement;
      emit buffer level ";";
      compile_sequence ~tail buffer scope level rest

and compile_statement buffer scope level statement =
  match statement with
  | Assign (Identifier name, value_expr) -> (
      match lookup scope name with
      (* An integer variable given an integer expression never leaves the
         integers: no box is made and none is taken apart. *)
      | Some (IntRef target) when numeric_kind scope [] value_expr = Some `Int ->
          emit buffer level (Printf.sprintf "%s := %s" target (numeric_code scope `Int value_expr))
      | Some (FloatRef target) when numeric_kind scope [] value_expr <> None ->
          emit buffer level (Printf.sprintf "%s := %s" target (numeric_code scope `Float value_expr))
      | Some binding -> emit buffer level (write_binding binding (expression scope value_expr))
      | None -> assert false)
  (* items[0] = x and user["name"] = x. The container and the key are evaluated
     before the value, as in the evaluator. *)
  | Assign (Index (target, index), value_expr) ->
      emit buffer level
        (Printf.sprintf "(let container = %s in let key = %s in index_set container key %s)"
           (expression scope target) (expression scope index) (expression scope value_expr))
  | Assign (Attribute (Identifier "Length", name), _) ->
      unsupported (Printf.sprintf "assigning to Length.%s, which reads a size and never sets one" name)
  (* user.age = 22, and a name that is not there yet adds the field. *)
  | Assign (Attribute (target, name), value_expr) ->
      emit buffer level
        (Printf.sprintf
           "(match %s with V_record fields -> record_set fields %s %s | other -> raise \
            (Runtime_error (Printf.sprintf \"Cannot assign field '%s' on a %%s\" (type_of_value \
            other))))"
           (expression scope target) (quoted name) (expression scope value_expr) name)
  | Assign _ -> unsupported "assigning to anything but a variable, an index or a field"
  | ExprStmt expr -> emit buffer level (Printf.sprintf "ignore %s" (expression scope expr))
  | Return None -> emit buffer level "raise (Return_signal V_null)"
  | Return (Some expr) ->
      emit buffer level (Printf.sprintf "raise (Return_signal %s)" (expression scope expr))
  | Break -> emit buffer level "raise Break_signal"
  | Continue -> emit buffer level "raise Continue_signal"
  | IfStmt (condition, then_block, else_block) ->
      emit buffer level (Printf.sprintf "if %s then begin" (condition_code scope condition));
      compile_block buffer scope (level + 1) then_block;
      emit buffer level "end else begin";
      (match else_block with
      | Some block -> compile_block buffer scope (level + 1) block
      | None -> emit buffer (level + 1) "()");
      emit buffer level "end"
  | WhileStmt (condition, body) ->
      (* break and continue travel as exceptions here too, so they unwind out
         of nested blocks without any construct threading a status around. *)
      emit buffer level "(try";
      emit buffer (level + 1)
        (Printf.sprintf "while %s do" (condition_code scope condition));
      emit buffer (level + 2) "(try";
      compile_block buffer scope (level + 3) body;
      emit buffer (level + 2) "with Continue_signal -> ())";
      emit buffer (level + 1) "done";
      emit buffer level "with Break_signal -> ())"
  (* A case is only evaluated if no earlier one matched, so the cases become a
     chain of 'else if' rather than a list to be searched. *)
  | MatchStmt (target_expr, branches, default_block) ->
      emit buffer level (Printf.sprintf "(let target = %s in" (expression scope target_expr));
      List.iteri
        (fun index (case_expr, case_block) ->
          emit buffer (level + 1)
            (Printf.sprintf "%sif value_equals target %s then begin"
               (if index = 0 then "" else "end else ")
               (expression scope case_expr));
          compile_block buffer scope (level + 2) case_block)
        branches;
      if branches <> [] then emit buffer (level + 1) "end else begin";
      (match default_block with
      | Some block -> compile_block buffer scope (level + 2) block
      | None -> emit buffer (level + 2) "()");
      emit buffer (level + 1) "end)"
  (* Only a Suchu error is caught. An OCaml exception escaping from here would be
     a bug in the runtime, not something a program should be able to swallow.
     Break, Continue and Return travel as exceptions too, and must pass through:
     a 'return' inside a try returns, it does not fire the handler. *)
  | TryStmt (attempted, binding, handler) ->
      emit buffer level "(try begin";
      compile_block buffer scope (level + 1) attempted;
      emit buffer level "end with Runtime_error message -> begin";
      let handler_scope = push scope in
      (match binding with
      | Some name ->
          emit buffer (level + 1)
            (Printf.sprintf "let %s = ref (V_record (ref [ (\"message\", V_string message) ])) in"
               (declare handler_scope name))
      | None -> ());
      compile_block buffer handler_scope (level + 1) handler;
      emit buffer level "end)"
  (* The loop variable is one binding reused on every turn, not a fresh one each
     time -- so a closure made inside the body sees the last value, exactly as it
     does in the evaluator. *)
  | ForStmt (name, iterable_expr, body) ->
      emit buffer level
        (Printf.sprintf "(let values = collect_iterable %s in" (expression scope iterable_expr));
      let loop_scope = push scope in
      emit buffer (level + 1)
        (Printf.sprintf "let %s = ref V_null in" (declare loop_scope name));
      emit buffer (level + 1) "try";
      emit buffer (level + 2) "List.iter (fun value ->";
      emit buffer (level + 3)
        (write_binding (Option.get (lookup loop_scope name)) "value" ^ ";");
      emit buffer (level + 3) "(try begin";
      compile_block buffer loop_scope (level + 4) body;
      emit buffer (level + 3) "end with Continue_signal -> ())) values";
      emit buffer (level + 1) "with Break_signal -> ())"
  (* The ref already exists, put there by compile_block; this fills it in. *)
  | FunctionDef declaration -> compile_function buffer scope level declaration
  | Import _ -> assert false (* bound by compile_sequence, which owns the scope *)
  (* A window is written out as the block the parser produced, with its handlers
     replaced by the position of a compiled closure. The block itself is data --
     names, sizes, colours -- and building the tree from it costs one pass when
     the window opens. The handlers are the part that runs over and over, and
     those are compiled: a closure over the very refs the rest of the program
     uses, so 'counter = counter + 1' in an onClick reaches the same 'counter'. *)
  | Gui block ->
      uses_gui := true;
      let handlers = ref [] in
      let literal = gui_block_literal scope handlers block in
      emit buffer level
        (Printf.sprintf
           "ignore (Suchu.Gui_backend.run_compiled_window ~interpreter ~handlers:[| %s |] ~should_run:%b\n%s   (%s))"
           (List.rev !handlers |> List.map (Printf.sprintf "(%s)") |> String.concat ";\n   ")
           (* The evaluator opens a window only when it is at global scope, so a
              window inside any block -- an if, a function, a module -- builds its
              widgets and returns. Level 2 is the program's own top level; every
              nested block is deeper. *)
           (level = 2 && not modules.inside_module)
           (String.make (level * 2) ' ')
           literal)

(* --- a window, as data ----------------------------------------------------

   The gui_block the parser made is written back out as an OCaml value. It is a
   small tree of names, literals and properties, so this is a transcription
   rather than a translation -- except for the handlers, which are compiled and
   left behind as an index into an array. *)
and gui_literal_of = function
  | GuiString text -> Printf.sprintf "GuiString %s" (quoted text)
  | GuiBool value -> Printf.sprintf "GuiBool %b" value
  | GuiNumber value -> Printf.sprintf "GuiNumber %s" (float_literal value)
  | GuiLength value -> Printf.sprintf "GuiLength %d" value
  | GuiPercent value -> Printf.sprintf "GuiPercent %s" (float_literal value)
  | GuiDuration value -> Printf.sprintf "GuiDuration %d" value
  | GuiColor value -> Printf.sprintf "GuiColor %s" (quoted value)
  | GuiIdent value -> Printf.sprintf "GuiIdent %s" (quoted value)

and gui_block_literal scope handlers (GuiBlock (name, arguments, properties, children)) =
  let property (property_name, value) =
    match value with
    | GuiPropLiteral literal ->
        Printf.sprintf "(%s, GuiPropLiteral (%s))" (quoted property_name) (gui_literal_of literal)
    | GuiPropEvent source ->
        (* The handler's text is parsed here, at compile time, and translated
           like any other block. Its two parameters are the names a handler
           reads the event through. *)
        let body =
          try Parser.parse source
          with Lexical.Lexing_error message | Parser.Parse_error message ->
            unsupported (Printf.sprintf "the '%s' handler: %s" property_name message)
        in
        let index = List.length !handlers in
        handlers :=
          String.trim
            (compile_lambda scope 0
               (Printf.sprintf "the '%s' handler" property_name)
               [ "event_widget"; "event_value" ]
               body)
          :: !handlers;
        Printf.sprintf "(%s, GuiPropEvent %s)" (quoted property_name)
          (quoted (string_of_int index))
  in
  Printf.sprintf "GuiBlock (%s, [%s], [%s], [%s])" (quoted name)
    (arguments |> List.map gui_literal_of |> String.concat "; ")
    (properties |> List.map property |> String.concat "; ")
    (children |> List.map (gui_block_literal scope handlers) |> String.concat "; ")

(* 'import "modules/math.suchu"' translates that file too, so the finished binary
   carries it and does not go looking for the .suchu beside itself. 'import json'
   names a module that is already OCaml, and needs nothing translating. *)
and import_value specification =
  match Runtime.find_native_module specification with
  | Some _ ->
      if String.equal specification "browser" then (uses_browser := true; uses_gui := true);
      if List.mem specification ["platform";"fileio";"textbytes"] then (uses_platform := true; uses_gui := true);
      if String.equal specification "http" then uses_http := true;
      if String.equal specification "audio" then uses_audio := true;
      Printf.sprintf "(native_module %s)" (quoted specification)
  | None -> Printf.sprintf "(Lazy.force %s)" (compile_module specification)

and compile_module specification =
  let path = Runtime.resolve_module_path modules.directory specification in
  (* Asked for a module whose own top level is still being translated: the import
     graph has a cycle. Tested before the cache, because the cache already holds
     the name of the module we are inside -- finding it there would produce a
     lazy that forces itself. Instead this is a fresh one that raises when forced,
     which is where the evaluator raises too. *)
  if List.mem path modules.in_progress then begin
    modules.counter <- modules.counter + 1;
    let name = Printf.sprintf "module_%d" modules.counter in
    modules.definitions <-
      modules.definitions
      @ [ Printf.sprintf "(* %s -- reached in a cycle *)\n%s = lazy (raise (Runtime_error %s))" path
            name
            (quoted (Printf.sprintf "Circular import detected while loading '%s'" specification)) ];
    name
  end
  else
    match List.assoc_opt path modules.emitted with
    | Some name -> name
    | None ->
        modules.counter <- modules.counter + 1;
        let name = Printf.sprintf "module_%d" modules.counter in
        (* Recorded before the body is translated, so the same file imported from
           two places is translated once, as the evaluator caches it once. *)
        modules.emitted <- (path, name) :: modules.emitted;
        begin
        let source =
          try
            let channel = open_in_bin path in
            Fun.protect
              ~finally:(fun () -> close_in channel)
              (fun () -> really_input_string channel (in_channel_length channel))
          with Sys_error message ->
            unsupported
              (Printf.sprintf "the import '%s', which could not be read (%s)" specification message)
        in
        let program =
          try Parser.parse source with
          | Lexical.Lexing_error message | Parser.Parse_error message ->
              unsupported (Printf.sprintf "the import '%s': %s" specification message)
        in
        let previous_directory = modules.directory in
        let previous_inside = modules.inside_module in
        modules.in_progress <- path :: modules.in_progress;
        modules.directory <- Filename.dirname path;
        modules.inside_module <- true;
        let body = Buffer.create 1024 in
        (* A module's top level lives in a real environment rather than in refs,
           so 'math.pi' reads the binding the module's own functions read. *)
        let scope = push (new_scope ()) in
        List.iter
          (fun statement ->
            match statement with
            | FunctionDef declaration -> declare_slot scope "module_env" declaration.name
            | Assign (Identifier assigned, _) -> declare_slot scope "module_env" assigned
            | Import inner -> declare_slot scope "module_env" (Runtime.module_alias inner)
            | _ -> ())
          program;
        compile_sequence body scope 2 program;
        modules.directory <- previous_directory;
        modules.inside_module <- previous_inside;
        modules.in_progress <- List.filter (fun other -> other <> path) modules.in_progress;
        modules.definitions <-
          modules.definitions
          @ [ Printf.sprintf
                "(* %s *)\n%s = lazy (\n  let module_env = child_env interpreter.Interpreter.globals in\n%s;\n  V_module module_env)"
                path name (Buffer.contents body) ];
        name
      end

(* --- the file ------------------------------------------------------------ *)

let prelude () =
  let bindings =
    Hashtbl.fold (fun name () acc -> name :: acc) globals_used []
    |> List.sort String.compare
    |> List.map (fun name -> Printf.sprintf "let g_%s = lazy (builtin %s)" name (quoted name))
    |> String.concat "\n"
  in
  let direct =
    Hashtbl.fold (fun name () acc -> name :: acc) direct_used []
    |> List.sort String.compare
    |> List.map (fun name -> Printf.sprintf "let builtin_%s = native_builtin %s" name (quoted name))
    |> String.concat "\n"
  in
  let bindings = if direct = "" then bindings else bindings ^ "\n\n" ^ direct in
  String.concat "\n"
    [ "(* Generated from Suchu source by suchu build.";
      "";
      "   Every operator called below is the one the Suchu evaluator calls, so the";
      "   two cannot disagree about arithmetic. Values stay boxed as Runtime.value";
      "   because Suchu is dynamically typed; what the translation removes is the";
      "   lookup of each name and the walk over each AST node. *)";
      "";
      "open Runtime";
      "";
      "exception Return_signal = Interpreter.Return_signal";
      "exception Break_signal = Interpreter.Break_signal";
      "exception Continue_signal = Interpreter.Continue_signal";
      "";
      "(* Built-ins still live in the interpreter's global table. Each one this";
      "   program mentions is fetched once, at its first use rather than at every";
      "   use -- and not before, so a name that is missing, or that 'eval' has yet";
      "   to define, is looked for at the moment the program asks for it. *)";
      "let () = Suchu_stdlib.register ()";
      (if !uses_gui then
         "\n(* Registered before the interpreter is made, because that is when the\n\
         \   window helpers -- set_text, get_value and the rest -- are added. Only\n\
         \   programs that open a window mention this, since naming it is what makes\n\
         \   the linker bring in raylib and the typeface. *)\n\
          let () = Suchu.Gui_backend.register ()"
       else "");
      (if !uses_audio then
         "\n(* The audio module is not part of the core library: it brings the sound\n\
         \   card with it, so only a program that imports it registers it. *)\n\
          let () = Suchu.Audio_backend.register ()"
       else "");
      (if !uses_browser then "let () = Suchu.Browser_backend.register ()" else "");
      (if !uses_platform then "let () = Suchu.Platform_backend.register ()" else "");
      (if !uses_http then
         "\n(* The http module is not part of the core library: it brings sockets and TLS\n\
         \   with it, so only a program that imports it registers it. *)\n\
          let () = Suchu.Http_backend.register ()"
       else "");
      "let interpreter = Interpreter.create ()";
      "let builtin name = env_lookup interpreter.Interpreter.globals name";
      "";
      (if Hashtbl.length direct_used = 0 then ""
       else
         "(* A built-in the program calls and never redefines is bound once, to the\n\
         \   function inside it, so a call to it is just a call. *)\n\
          let native_builtin name =\n\
         \  match builtin name with\n\
         \  | V_native fn -> fn\n\
         \  | _ -> raise (Runtime_error (name ^ \" is not a function\"))\n");
      "let call_value callee arguments =";
      "  match callee with";
      "  | V_native fn -> fn arguments";
      "  | other -> raise (Runtime_error (value_to_string other ^ \" is not a function\"))";
      "";
      "(* 'import json' names a module that is already OCaml. Its bindings are put";
      "   into an environment of their own, as the evaluator does. *)";
      "let native_module name =";
      "  match find_native_module name with";
      "  | Some bindings ->";
      "      let module_env = child_env interpreter.Interpreter.globals in";
      "      List.iter (fun (field, value) -> env_define module_env field value) bindings;";
      "      V_module module_env";
      "  | None -> raise (Runtime_error (\"Cannot import '\" ^ name ^ \"'\"))";
      "";
      bindings;
      "" ]

let program ?(directory = ".") statements =
  Hashtbl.reset globals_used;
  Hashtbl.reset direct_used;
  reset_modules directory;
  uses_gui := false;
  uses_browser := false;
  uses_platform := false;
  uses_http := false;
  uses_audio := false;
  let body = Buffer.create 4096 in
  (* The body is compiled first: compiling it is what discovers which globals the
     program mentions and which modules it imports, both of which have to be
     written out ahead of it. *)
  compile_block body (new_scope ()) 2 statements;
  (* One recursive group, so a module may refer to another whichever order they
     were finished in -- which two modules importing each other do. *)
  let imported =
    match modules.definitions with
    | [] -> ""
    | definitions -> "let rec " ^ String.concat "\n\nand " definitions ^ "\n"
  in
  (* An error that no 'try' caught ends the program the way 'suchu run' ends it:
     the same sentence on standard error and the same status. Letting the OCaml
     exception escape instead would report a program's own mistake as though the
     runtime had broken. *)
  Printf.sprintf
    "%s\n%slet () =\n%slet () =\n  try\n%s\n  with Runtime_error message ->\n    prerr_endline (\"Runtime error: \" \
     ^ message);\n    exit 3\n"
    (prelude ()) imported
    (* The same store 'suchu run' fills, so system.args answers the same thing
       whether the program was interpreted or compiled. *)
    "  Runtime.program_arguments :=\n\
    \    (match Array.to_list Sys.argv with _ :: rest -> rest | [] -> [])\n\n"
    (Buffer.contents body)
