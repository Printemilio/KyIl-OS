open Ast

type native_object = {
  kind : string;
  payload : Obj.t;
}

type value =
  | V_null
  | V_int of int
  | V_float of float
  | V_bool of bool
  | V_string of string
  (* A growable array rather than an OCaml list.

     It used to be a 'value list ref', and appending meant '!items @ [value]',
     which copies the whole list. Building a list of n elements cost n squared
     copies: eighty thousand pushes took a minute. Now a push writes one slot and
     doubles the storage when it runs out, so building n elements costs n.

     A set keeps the list, since it is small by nature and every operation on it
     already walks the whole thing to keep the elements unique. *)
  | V_list of list_buffer
  | V_set of value list ref
  (* An association list rather than a hashtable so fields keep insertion
     order: printing and JSON round-trips stay predictable, and records are
     small enough that lookup cost does not matter. *)
  | V_record of (string * value) list ref
  | V_function of function_value
  | V_native of native_function
  | V_module of environment
  | V_object of native_object
  | V_length

(* [slots] is longer than the list; only the first [count] entries are the
   list's, and the rest is room to grow. The record is the list's identity, so
   two names bound to one list see each other's changes exactly as they did when
   this was a ref. *)
and list_buffer = {
  mutable slots : value array;
  mutable count : int;
}

and function_value = {
  params : string list;
  body : block;
  env : environment;
}

and native_function = value list -> value

and environment = {
  table : (string, value) Hashtbl.t;
  parent : environment option;
}

exception Runtime_error of string

(* --- lists ----------------------------------------------------------------

   [list_values] and [list_assign] are the shape the rest of the code already
   spoke when a list was a ref, so most of it reads the same. They cost a walk,
   which is fine: every caller of them was walking the list anyway. What matters
   is that appending, indexing and counting no longer do. *)

let list_of_values values =
  let slots = Array.of_list values in
  { slots; count = Array.length slots }

let empty_list () = { slots = [||]; count = 0 }
let list_length buffer = buffer.count
let list_values buffer = Array.to_list (Array.sub buffer.slots 0 buffer.count)

let list_assign buffer values =
  let slots = Array.of_list values in
  buffer.slots <- slots;
  buffer.count <- Array.length slots

(* Doubling, so n appends cost n moves in total rather than n squared. *)
let list_reserve buffer wanted =
  if wanted > Array.length buffer.slots then begin
    let capacity = max 8 (max wanted (2 * Array.length buffer.slots)) in
    let slots = Array.make capacity V_null in
    Array.blit buffer.slots 0 slots 0 buffer.count;
    buffer.slots <- slots
  end

let list_push buffer value =
  list_reserve buffer (buffer.count + 1);
  buffer.slots.(buffer.count) <- value;
  buffer.count <- buffer.count + 1

let list_get buffer index = buffer.slots.(index)
let list_set buffer index value = buffer.slots.(index) <- value

(* The last element off the end. The slot is cleared rather than left holding the
   value, so a list that has been emptied does not keep it alive. *)
let list_pop_last buffer =
  if buffer.count = 0 then None
  else begin
    let value = buffer.slots.(buffer.count - 1) in
    buffer.slots.(buffer.count - 1) <- V_null;
    buffer.count <- buffer.count - 1;
    Some value
  end

let list_append_buffer buffer other =
  let addition = Array.sub other.slots 0 other.count in
  list_reserve buffer (buffer.count + Array.length addition);
  Array.blit addition 0 buffer.slots buffer.count (Array.length addition);
  buffer.count <- buffer.count + Array.length addition

let list_iter f buffer =
  for index = 0 to buffer.count - 1 do
    f buffer.slots.(index)
  done

let empty_env () = { table = Hashtbl.create 32; parent = None }

let child_env parent = { table = Hashtbl.create 16; parent = Some parent }

let rec env_lookup env name =
  match Hashtbl.find_opt env.table name with
  | Some value -> value
  | None -> (
      match env.parent with
      | Some parent -> env_lookup parent name
      | None -> raise (Runtime_error (Printf.sprintf "Undefined variable '%s'" name)))

let rec env_assign env name value =
  if Hashtbl.mem env.table name then Hashtbl.replace env.table name value
  else
    match env.parent with
    | Some parent -> env_assign parent name value
    | None -> Hashtbl.replace env.table name value

let env_define env name value = Hashtbl.replace env.table name value

(* An assignment as the language means it: a name that already exists, here or in an
   enclosing environment, is changed where it is; a new one is made in [env] itself.
   Not env_assign, which hands a new name to the outermost environment -- that is
   right for nothing but the outermost one, and made every module's new names
   everybody's. *)
let env_set env name value =
  match env_lookup env name with
  | _ -> env_assign env name value
  | exception Runtime_error _ -> env_define env name value

let env_has_local env name = Hashtbl.mem env.table name

let native_modules : (string, (string * value) list) Hashtbl.t = Hashtbl.create 16

let register_native_module name bindings =
  if String.equal (String.trim name) "" then
    raise (Invalid_argument "Native module name cannot be empty");
  Hashtbl.replace native_modules name bindings

let find_native_module name = Hashtbl.find_opt native_modules name

let make_object kind value = V_object { kind; payload = Obj.repr value }

let expect_object name expected_kind = function
  | V_object object_value when String.equal object_value.kind expected_kind ->
      Obj.obj object_value.payload
  | V_object object_value ->
      raise
        (Runtime_error
           (Printf.sprintf "%s expects a %s, got %s" name expected_kind object_value.kind))
  | _ -> raise (Runtime_error (Printf.sprintf "%s expects a %s" name expected_kind))

(* --- record helpers ------------------------------------------------------ *)

let record_get fields name = List.assoc_opt name !fields

let record_has fields name = List.mem_assoc name !fields

(* Updating keeps the field in place; a new field is appended, so the order a
   record is written in is the order it prints in. *)
let record_set fields name value =
  if record_has fields name then
    fields :=
      List.map (fun (key, old) -> if String.equal key name then (key, value) else (key, old)) !fields
  else fields := !fields @ [ (name, value) ]

let rec value_to_string = function
  | V_null -> "none"
  | V_int n -> string_of_int n
  | V_float f ->
      (* The trailing '.0' exists so 2. reads as 2.0. Infinity and NaN have no
         decimal part to complete, and appending one produced 'inf.0' -- text
         the language cannot read back, since its literals are inf and NaN. *)
      if Float.is_nan f then "NaN"
      else if f = Float.infinity then "inf"
      else if f = Float.neg_infinity then "-inf"
      else
        (* string_of_float always leaves a dot, so the old test for one never
           fired and every whole float printed as '4.' instead of '4.0'. *)
        let s = string_of_float f in
        if String.length s > 0 && s.[String.length s - 1] = '.' then s ^ "0" else s
  | V_bool true -> "true"
  | V_bool false -> "false"
  | V_string s -> s
  | V_list items ->
      let data = list_values items |> List.map value_to_string |> String.concat ", " in
      "[" ^ data ^ "]"
  | V_set items ->
      let data = !(items) |> List.map value_to_string |> String.concat ", " in
      "{" ^ data ^ "}"
  | V_record fields ->
      let data =
        !fields
        |> List.map (fun (name, value) -> name ^ ": " ^ value_to_string value)
        |> String.concat ", "
      in
      "{" ^ data ^ "}"
  | V_function _ -> "<function>"
  | V_native _ -> "<native>"
  | V_module _ -> "<module>"
  | V_object object_value -> "<" ^ object_value.kind ^ ">"
  | V_length -> "<Length>"

let truthy = function
  | V_null -> false
  | V_bool b -> b
  | V_int n -> n <> 0
  | V_float f -> f <> 0.0
  | V_string s -> s <> ""
  | V_list items -> list_length items <> 0
  | V_set items -> !(items) <> []
  | V_record fields -> !fields <> []
  | V_function _ -> true
  | V_native _ -> true
  | V_module _ -> true
  | V_object _ -> true
  | V_length -> true

let type_of_value = function
  | V_null -> "none"
  | V_int _ -> "int"
  | V_float _ -> "float"
  | V_bool _ -> "bool"
  | V_string _ -> "string"
  | V_list _ -> "list"
  | V_set _ -> "set"
  | V_record _ -> "record"
  | V_function _ -> "function"
  | V_native _ -> "native"
  | V_module _ -> "module"
  | V_object object_value -> object_value.kind
  | V_length -> "length"

(* A value as an error names it: what kind it is, and what it holds when that is
   short enough to help. "Expected an integer" with nothing after it leaves the
   reader to work out which of the program's values it meant. *)
let describe_value value =
  let kind = type_of_value value in
  match value with
  | V_string text when String.length text <= 30 -> Printf.sprintf "%s \"%s\"" kind text
  | V_string _ -> kind
  | V_int _ | V_float _ | V_bool _ -> Printf.sprintf "%s %s" kind (value_to_string value)
  | V_null -> "none"
  | _ -> kind

let is_number = function V_int _ | V_float _ -> true | _ -> false

let expect_number value =
  match value with
  | V_int n -> (float_of_int n, `Int)
  | V_float f -> (f, `Float)
  | other -> raise (Runtime_error (Printf.sprintf "Expected a number, got %s" (describe_value other)))

(* The two operands of an arithmetic operator, as numbers, or an error that says
   which operator and what it was given. This used to be 'Expected numeric value',
   which named neither. *)
let number_operands operator left right =
  if is_number left && is_number right then (fst (expect_number left), fst (expect_number right))
  else
    raise
      (Runtime_error
         (Printf.sprintf "'%s' needs two numbers, but it was given %s and %s" operator
            (describe_value left) (describe_value right)))

(* --- declared types --------------------------------------------------------

   'int n = 5;' holds n to an int from then on. The check is made where a value
   is assigned, and it is this one function, shared by the interpreter and by
   compiled programs, so both refuse the same things in the same words.

   A float accepts an int and widens it, as most languages do: 'float x = 1;'
   holds 1.0. An int does not accept a float, which would lose the decimals
   without saying so. A char is a string of exactly one character. *)
let annotation_types = [ "int"; "float"; "string"; "char"; "bool"; "list"; "record" ]

let character_count text =
  let count = ref 0 in
  String.iter (fun c -> if Char.code c land 0xC0 <> 0x80 then incr count) text;
  !count

let check_annotation declared name value =
  match (declared, value) with
  | "float", V_int n -> V_float (float_of_int n)
  | "int", V_int _ | "float", V_float _ | "string", V_string _ | "bool", V_bool _
  | "list", V_list _ | "record", V_record _ ->
      value
  | "char", V_string text when character_count text = 1 -> value
  | _ ->
      let hint =
        match (declared, value) with
        | "char", V_string _ -> " (a char is exactly one character)"
        | "int", V_float _ -> " (round() turns a float into an int)"
        | _ -> ""
      in
      raise
        (Runtime_error
           (Printf.sprintf "%s is declared as %s, but it was given %s%s" name declared
              (describe_value value) hint))

let expect_int = function
  | V_int n -> n
  | other -> raise (Runtime_error (Printf.sprintf "Expected an integer, got %s" (describe_value other)))

let expect_list name value =
  match value with
  | V_list items -> items
  | _ -> raise (Runtime_error (Printf.sprintf "%s expects a list" name))

let expect_string name value =
  match value with
  | V_string s -> s
  | _ -> raise (Runtime_error (Printf.sprintf "%s expects a string" name))

let expect_set name value =
  match value with
  | V_set items -> items
  | _ -> raise (Runtime_error (Printf.sprintf "%s expects a set" name))

let rec value_equals left right =
  match (left, right) with
  | V_null, V_null -> true
  | V_int a, V_int b -> a = b
  | V_float a, V_float b -> a = b
  | V_int a, V_float b -> float_of_int a = b
  | V_float a, V_int b -> a = float_of_int b
  | V_bool a, V_bool b -> a = b
  | V_string a, V_string b -> String.equal a b
  | V_list a, V_list b ->
      list_length a = list_length b && List.for_all2 value_equals (list_values a) (list_values b)
  | V_set a, V_set b ->
      let left_items = !(a) in
      let right_items = !(b) in
      let contains lst value = List.exists (fun item -> value_equals item value) lst in
      List.length left_items = List.length right_items
      && List.for_all (fun item -> contains right_items item) left_items
      && List.for_all (fun item -> contains left_items item) right_items
  (* Field order is presentation, not identity: two records are equal when they
     hold the same names bound to equal values. *)
  | V_record a, V_record b ->
      List.length !a = List.length !b
      && List.for_all
           (fun (name, value) ->
             match List.assoc_opt name !b with
             | Some other -> value_equals value other
             | None -> false)
           !a
  | V_object a, V_object b ->
      String.equal a.kind b.kind && a.payload == b.payload
  | V_module a, V_module b -> a == b
  | V_length, V_length -> true
  | _ -> false

let normalize_set values =
  List.fold_left
    (fun acc item -> if List.exists (fun existing -> value_equals existing item) acc then acc else acc @ [ item ])
    [] values

let make_set values = V_set (ref (normalize_set values))

let set_union left right =
  match (left, right) with
  | V_set a, V_set b -> make_set (!(a) @ !(b))
  | _ -> raise (Runtime_error "Set union expects two sets")

let set_intersection left right =
  match (left, right) with
  | V_set a, V_set b ->
      let filtered =
        !(a) |> List.filter (fun value -> List.exists (fun other -> value_equals value other) !(b))
      in
      make_set filtered
  | _ -> raise (Runtime_error "Set intersection expects two sets")

let set_difference left right =
  match (left, right) with
  | V_set a, V_set b ->
      let filtered =
        !(a) |> List.filter (fun value -> not (List.exists (fun other -> value_equals value other) !(b)))
      in
      make_set filtered
  | _ -> raise (Runtime_error "Set difference expects two sets")

let numeric_binary op left right =
  let (lv, lt) = expect_number left in
  let (rv, rt) = expect_number right in
  match op (lv, lt) (rv, rt) with
  | `Int result -> V_int result
  | `Float result -> V_float result

let plus_operator left right =
  match (left, right) with
  | V_int a, V_int b -> V_int (a + b)
  | V_string a, V_string b -> V_string (a ^ b)
  | V_string a, v -> V_string (a ^ value_to_string v)
  | V_list items, V_list other ->
      list_append_buffer items other;
      V_list items
  | V_list items, v ->
      list_push items v;
      V_list items
  | _ ->
      let a, b = number_operands "+" left right in
      V_float (a +. b)

let minus_operator left right =
  match (left, right) with
  | V_int a, V_int b -> V_int (a - b)
  | V_set _, V_set _ -> set_difference left right
  | V_set _, _ -> raise (Runtime_error "Set difference expects a set on the right")
  | _ ->
      let a, b = number_operands "-" left right in
      V_float (a -. b)

(* Prefix '-' is not 'zero minus', which would turn -0.0 into 0.0 and would
   widen an integer to a float. It lives here so the evaluator and the compiler
   negate through the same function. *)
let negate_operator = function
  | V_int n -> V_int (-n)
  | V_float f -> V_float (-.f)
  | _ -> raise (Runtime_error "Unary '-' expects a number")

let multiply_operator left right =
  match (left, right) with
  | V_int a, V_int b -> V_int (a * b)
  | _ ->
      let a, b = number_operands "*" left right in
      V_float (a *. b)

let divide_operator left right =
  let a, b = number_operands "/" left right in
  V_float (a /. b)

(* '@' concatenates. It lives here rather than in the evaluator's operator
   table so that the compiler emits a call to the same function instead of a
   second copy of the rule. *)
let at_operator left right =
  match left with
  | V_list items ->
      (match right with
      | V_list other -> list_append_buffer items other
      | _ -> list_push items right);
      V_list items
  | V_string text -> V_string (text ^ value_to_string right)
  | _ -> raise (Runtime_error "'@' operator expects list or string on left")

let modulo_operator left right =
  match (left, right) with
  (* 'a mod 0' raises OCaml's Division_by_zero, which escapes as a fatal error
     no Suchu program can catch and no reader can act on. *)
  | V_int _, V_int 0 -> raise (Runtime_error "Modulo by zero")
  | V_int a, V_int b -> V_int (a mod b)
  | _ -> raise (Runtime_error "Modulo expects integer operands")

let pow_operator left right =
  match (left, right) with
  (* Two integers give an integer: 2 ** 3 is 8, not 8.0. A negative exponent
     cannot, and neither can a result past the 63-bit range -- rather than
     wrap silently into nonsense, those fall back to a float. *)
  | V_int base, V_int exponent when exponent >= 0 ->
      let approximate = float_of_int base ** float_of_int exponent in
      if Float.abs approximate > 4.6e18 then V_float approximate
      else begin
        let rec power acc factor remaining =
          if remaining = 0 then acc
          else
            power (if remaining land 1 = 1 then acc * factor else acc) (factor * factor)
              (remaining asr 1)
        in
        V_int (power 1 base exponent)
      end
  | _ ->
      let a, b = number_operands "**" left right in
      V_float (a ** b)

let compare_values left right =
  match (left, right) with
  | V_int a, V_int b -> compare a b
  | V_float a, V_float b -> Float.compare a b
  | V_int a, V_float b -> Float.compare (float_of_int a) b
  | V_float a, V_int b -> Float.compare a (float_of_int b)
  | V_string a, V_string b -> String.compare a b
  | V_bool a, V_bool b -> Bool.compare a b
  | _ -> raise (Runtime_error "Values are not comparable")

(* '<' and friends promised string comparison and delivered 'Expected numeric
   value': they went through expect_number, while compare_values right below
   already knew how to order strings and booleans. Numbers keep going through
   floats so that IEEE holds -- every comparison against NaN stays false. *)
let compare_operator ordering left right =
  let result =
    match (left, right) with
    | (V_int _ | V_float _), (V_int _ | V_float _) ->
        let (a, _), (b, _) = (expect_number left, expect_number right) in
        if Float.is_nan a || Float.is_nan b then None else Some (Float.compare a b)
    | _ -> Some (compare_values left right)
  in
  match result with
  | None -> V_bool false
  | Some c ->
      V_bool
        (match ordering with
        | `Lt -> c < 0
        | `Le -> c <= 0
        | `Gt -> c > 0
        | `Ge -> c >= 0)

let rec find_substring text pattern start =
  let text_len = String.length text in
  let pattern_len = String.length pattern in
  if pattern_len = 0 then Some start
  else if start + pattern_len > text_len then None
  else
    let rec matches i =
      if i = pattern_len then true
      else if text.[start + i] <> pattern.[i] then false
      else matches (i + 1)
    in
    if matches 0 then Some start else find_substring text pattern (start + 1)

let starts_with ~prefix text =
  let prefix_len = String.length prefix in
  let text_len = String.length text in
  prefix_len <= text_len && String.sub text 0 prefix_len = prefix

let ends_with ~suffix text =
  let suffix_len = String.length suffix in
  let text_len = String.length text in
  suffix_len <= text_len && String.sub text (text_len - suffix_len) suffix_len = suffix

let replace_substring text pattern replacement =
  if pattern = "" then text
  else
    let buffer = Buffer.create (String.length text) in
    let pattern_len = String.length pattern in
    let rec loop index =
      if index >= String.length text then ()
      else
        match find_substring text pattern index with
        | None ->
            Buffer.add_substring buffer text index (String.length text - index)
        | Some found ->
            Buffer.add_substring buffer text index (found - index);
            Buffer.add_string buffer replacement;
            loop (found + pattern_len)
    in
    loop 0;
    Buffer.contents buffer

(* The characters of a string, not its bytes: 'é' is two bytes and one
   character, and splitting between them produces two pieces that are not
   text at all.

   A byte that is not a continuation starts a character; the continuations that
   follow belong to it. Written as a scan forward rather than a length read off
   the leading byte so that malformed input still comes back byte for byte
   instead of losing some -- a stray continuation byte becomes its own piece,
   and joining the result reproduces the original whatever it contained. *)
let utf8_characters text =
  let length = String.length text in
  let is_continuation index = Char.code text.[index] land 0xC0 = 0x80 in
  let rec loop index acc =
    if index >= length then List.rev acc
    else begin
      let stop = ref (index + 1) in
      while !stop < length && is_continuation !stop do
        incr stop
      done;
      loop !stop (String.sub text index (!stop - index) :: acc)
    end
  in
  loop 0 []

let split_string text delimiter =
  if delimiter = "" then utf8_characters text
  else
    let delimiter_len = String.length delimiter in
    let rec loop acc index =
      match find_substring text delimiter index with
      | None ->
          let rest = String.sub text index (String.length text - index) in
          List.rev (rest :: acc)
      | Some found ->
          let part = String.sub text index (found - index) in
          loop (part :: acc) (found + delimiter_len)
    in
    if String.length text = 0 then [ "" ] else loop [] 0


(* --- indexing -------------------------------------------------------------

   items[0] on lists and strings, user["name"] on records. Sets are excluded on
   purpose: their order is not guaranteed, so an index would mean nothing. *)

let index_out_of_bounds kind position length =
  raise
    (Runtime_error
       (Printf.sprintf "%s index %d is out of bounds (length %d)" kind position length))

let index_get container key =
  match (container, key) with
  | V_list items, V_int position ->
      let length = list_length items in
      if position < 0 || position >= length then index_out_of_bounds "List" position length
      else list_get items position
  | V_string text, V_int position ->
      let length = String.length text in
      if position < 0 || position >= length then index_out_of_bounds "String" position length
      else V_string (String.make 1 text.[position])
  | V_record fields, V_string name -> (
      match record_get fields name with
      | Some value -> value
      | None -> raise (Runtime_error (Printf.sprintf "Record has no field '%s'" name)))
  | V_record _, other ->
      raise (Runtime_error ("Record keys must be strings, got " ^ type_of_value other))
  | (V_list _ | V_string _), other ->
      raise (Runtime_error ("Index must be an int, got " ^ type_of_value other))
  | V_set _, _ -> raise (Runtime_error "Sets are unordered and cannot be indexed")
  | other, _ -> raise (Runtime_error ("Cannot index a " ^ type_of_value other))

let index_set container key value =
  match (container, key) with
  | V_list items, V_int position ->
      let length = list_length items in
      if position < 0 || position >= length then index_out_of_bounds "List" position length
      else list_set items position value
  | V_record fields, V_string name -> record_set fields name value
  | V_record _, other ->
      raise (Runtime_error ("Record keys must be strings, got " ^ type_of_value other))
  | V_list _, other ->
      raise (Runtime_error ("Index must be an int, got " ^ type_of_value other))
  | V_string _, _ ->
      raise (Runtime_error "Strings are immutable; build a new one instead")
  | V_set _, _ -> raise (Runtime_error "Sets are unordered and cannot be indexed")
  | other, _ -> raise (Runtime_error ("Cannot assign into a " ^ type_of_value other))

(* Split from [length_of] so the compiler can use it. 'Length.x' reads a
   variable by name, which the evaluator does at run time and the compiler
   already knows at compile time; both end up measuring here. The name is
   carried only to say which one was not countable. *)
let length_of_value name = function
  | V_string s -> V_int (String.length s)
  | V_list items -> V_int (list_length items)
  | V_set items -> V_int (List.length !items)
  | V_record fields -> V_int (List.length !fields)
  | _ -> raise (Runtime_error (Printf.sprintf "Length.%s is not countable" name))

let length_of env name = length_of_value name (env_lookup env name)

(* 1..5, and 5..1 counting back down. Shared, so the compiler cannot disagree
   about whether the bound is included -- it is. *)
let range_value from_value to_value =
  let bound = function
    | V_int n -> n
    | _ ->
        raise
          (Runtime_error
             (Printf.sprintf "a range needs two integers, but it was given %s and %s"
                (describe_value from_value) (describe_value to_value)))
  in
  let start = bound from_value and finish = bound to_value in
  let step = if finish >= start then 1 else -1 in
  let rec build acc current =
    if step > 0 && current > finish then List.rev acc
    else if step < 0 && current < finish then List.rev acc
    else build (current :: acc) (current + step)
  in
  V_list (list_of_values (build [] start |> List.map (fun n -> V_int n)))

(* What 'for (x in thing)' walks over. Shared, so a compiled loop cannot end up
   iterating something the evaluator would refuse, or in a different order. *)
let collect_iterable = function
  | V_list items -> list_values items
  | V_set items -> !items
  (* Iterating a record yields its field names, in declaration order. *)
  | V_record fields -> List.map (fun (name, _) -> V_string name) !fields
  | V_string text -> List.init (String.length text) (fun i -> V_string (String.make 1 text.[i]))
  | V_int _ | V_float _ | V_bool _ | V_null | V_native _ | V_function _ | V_length | V_module _
  | V_object _ ->
      raise (Runtime_error "Value is not iterable")

(* How an import spells the name it binds, and where it looks for the file. Both
   are needed by the evaluator, which resolves an import while running, and by the
   compiler, which resolves it while translating -- so they live here rather than
   in either one. *)
let module_alias module_spec =
  let basename = Filename.basename module_spec in
  let stem =
    if Filename.check_suffix basename ".suchu" then Filename.chop_suffix basename ".suchu"
    else basename
  in
  String.map
    (fun ch -> match ch with 'a' .. 'z' | 'A' .. 'Z' | '0' .. '9' | '_' -> ch | _ -> '_')
    stem

let resolve_module_path current_dir module_spec =
  let with_extension =
    if Filename.extension module_spec = "" then module_spec ^ ".suchu" else module_spec
  in
  if Filename.is_relative with_extension then Filename.concat current_dir with_extension
  else with_extension

(* Prefix '@' takes the last element off a list and hands it back. Shared, so
   both sides agree that it is the last and not the first. *)
let pull_operator = function
  | V_list items -> (
      match list_pop_last items with
      | None -> raise (Runtime_error "Cannot pull from empty list")
      | Some value -> value)
  | _ -> raise (Runtime_error "Prefix '@' expects a list")

(* The step taken by '++' and '--'. They yield the value from before the step,
   which is the caller's business; this only says what the next one is. *)
let step_value direction = function
  | V_int n -> V_int (n + direction)
  | V_float f -> V_float (f +. float_of_int direction)
  | _ -> raise (Runtime_error "Postfix operator expects numeric variable")

(* Reading a field, and the methods of lists, strings and sets. Moved out of the
   evaluator for the same reason '@' was: the compiler emits a call to this, so
   there is one definition of what '.sort()' does rather than two that have to be
   kept in agreement.

   'Length' does not appear here. It reads a variable by name, which needs a
   scope, and the evaluator settles it before calling this. *)
let attribute_of obj name =
  let expect_no_args method_name args =
    match args with
    | [] -> ()
    | _ -> raise (Runtime_error (Printf.sprintf "%s() expects no arguments" method_name))
  in
  let expect_one_arg method_name args =
    match args with
    | [ value ] -> value
    | _ -> raise (Runtime_error (Printf.sprintf "%s() expects one argument" method_name))
  in
  let expect_two_args method_name args =
    match args with
    | [ a; b ] -> (a, b)
    | _ -> raise (Runtime_error (Printf.sprintf "%s() expects two arguments" method_name))
  in
  match obj with
  | V_module module_env -> env_lookup module_env name
  | V_record fields -> (
      match record_get fields name with
      | Some value -> value
      | None -> raise (Runtime_error (Printf.sprintf "Record has no field '%s'" name)))
  | V_list items -> (
      match name with
      | "reverse" ->
          V_native
            (fun args ->
              expect_no_args "reverse" args;
              list_assign items (List.rev (list_values items));
              V_list items)
      | "sort" ->
          V_native
            (fun args ->
              expect_no_args "sort" args;
              list_assign items (List.sort compare_values (list_values items));
              V_list items)
      | "clear" ->
          V_native
            (fun args ->
              expect_no_args "clear" args;
              list_assign items [];
              V_list items)
      | "sum" ->
          V_native
            (fun args ->
              expect_no_args "sum" args;
              let int_total, float_total_opt =
                List.fold_left
                  (fun (int_acc, float_opt) value ->
                    match (value, float_opt) with
                    | V_int n, None -> (int_acc + n, None)
                    | V_int n, Some f -> (0, Some (f +. float_of_int n))
                    | V_float f, None -> (0, Some (float_of_int int_acc +. f))
                    | V_float f, Some acc -> (0, Some (acc +. f))
                    | _ -> raise (Runtime_error "sum() expects numeric elements"))
                  (0, None) (list_values items)
              in
              match float_total_opt with Some total -> V_float total | None -> V_int int_total)
      | "pop" ->
          V_native
            (fun args ->
              match args with
              | [] -> (
                  match list_pop_last items with
                  | None -> raise (Runtime_error "pop() on empty list")
                  | Some value -> value)
              | [ index ] ->
                  let idx = expect_int index in
                  let rec extract i acc remaining =
                    match remaining with
                    | [] -> raise (Runtime_error "pop() index out of bounds")
                    | hd :: tl -> if i = 0 then (List.rev acc @ tl, hd) else extract (i - 1) (hd :: acc) tl
                  in
                  let updated, value = extract idx [] (list_values items) in
                  list_assign items updated;
                  value
              | _ -> raise (Runtime_error "pop() expects zero or one argument"))
      | _ -> raise (Runtime_error (Printf.sprintf "Unknown list attribute '%s'" name)))
  | V_string text -> (
      match name with
      | "split" ->
          V_native
            (fun args ->
              let delimiter = expect_string "split" (expect_one_arg "split" args) in
              let parts = split_string text delimiter |> List.map (fun part -> V_string part) in
              V_list (list_of_values parts))
      | "join" ->
          V_native
            (fun args ->
              let list_value = expect_one_arg "join" args in
              let list_ref = expect_list "join" list_value in
              let parts =
                list_values list_ref
                |> List.map (function
                     | V_string s -> s
                     | _ -> raise (Runtime_error "join() expects strings"))
              in
              V_string (String.concat text parts))
      | "replace" ->
          V_native
            (fun args ->
              let old_v, new_v = expect_two_args "replace" args in
              let old_sub = expect_string "replace" old_v in
              let new_sub = expect_string "replace" new_v in
              V_string (replace_substring text old_sub new_sub))
      | "startswith" ->
          V_native
            (fun args ->
              let prefix = expect_string "startswith" (expect_one_arg "startswith" args) in
              V_bool (starts_with ~prefix text))
      | "endswith" ->
          V_native
            (fun args ->
              let suffix = expect_string "endswith" (expect_one_arg "endswith" args) in
              V_bool (ends_with ~suffix text))
      (* Letters A to Z and a to z only. An accented letter is left as it is: a
         string is bytes, and 'É' is two of them that no one-byte rule can map. *)
      | "lower" ->
          V_native
            (fun args ->
              expect_no_args "lower" args;
              V_string (String.lowercase_ascii text))
      | "upper" ->
          V_native
            (fun args ->
              expect_no_args "upper" args;
              V_string (String.uppercase_ascii text))
      (* Spaces, tabs and line breaks off both ends. *)
      | "trim" ->
          V_native
            (fun args ->
              expect_no_args "trim" args;
              V_string (String.trim text))
      | "contains" ->
          V_native
            (fun args ->
              let needle = expect_string "contains" (expect_one_arg "contains" args) in
              V_bool (needle = "" || find_substring text needle 0 <> None))
      | _ -> raise (Runtime_error (Printf.sprintf "Unknown string attribute '%s'" name)))
  | V_set items -> (
      match name with
      | "clear" ->
          V_native
            (fun args ->
              expect_no_args "clear" args;
              items := [];
              V_set items)
      | _ -> raise (Runtime_error (Printf.sprintf "Unknown set attribute '%s'" name)))
  | _ -> raise (Runtime_error "Unknown attribute access")

let builtin_print args =
  let output = args |> List.map value_to_string |> String.concat " " in
  print_endline output;
  V_null

let builtin_input args =
  (match args with
  | [] -> ()
  | [ prompt ] ->
      print_string (value_to_string prompt);
      flush stdout
  | _ -> raise (Runtime_error "input expects At most one argument"));
  (* Reaching the end of stdin is an ordinary thing to happen -- a piped script,
     a closed terminal -- and used to escape as a bare OCaml End_of_file, which
     is not a message any Suchu program can act on or a reader can understand. *)
  match read_line () with
  | line -> V_string line
  | exception End_of_file ->
      raise (Runtime_error "input reached the end of stdin with nothing to read")

let builtin_push args =
  match args with
  | [ target; value ] ->
      let items = expect_list "push" target in
      list_push items value;
      target
  | _ -> raise (Runtime_error "push expects (list, value)")

let builtin_pull args =
  match args with
  | [ target ] ->
      let items = expect_list "pull" target in
      (match list_pop_last items with
      | None -> raise (Runtime_error "pull on empty list")
      | Some value -> value)
  | _ -> raise (Runtime_error "pull expects (list)")

let builtin_insert args =
  match args with
  | [ target; index; value ] ->
      let items = expect_list "insert" target in
      let idx = expect_int index in
      let before, after =
        let rec split i acc rest =
          match (i, rest) with
          | _, [] -> (List.rev acc, [])
          | 0, _ -> (List.rev acc, rest)
          | n, hd :: tl -> split (n - 1) (hd :: acc) tl
        in
        split idx [] (list_values items)
      in
      list_assign items (before @ (value :: after));
      target
  | _ -> raise (Runtime_error "insert expects (list, index, value)")

let builtin_remove args =
  match args with
  | [ target; index ] ->
      let items = expect_list "remove" target in
      let idx = expect_int index in
      let rec remove i acc = function
        | [] -> raise (Runtime_error "remove index out of bounds")
        | hd :: tl ->
            if i = 0 then List.rev acc @ tl else remove (i - 1) (hd :: acc) tl
      in
      list_assign items (remove idx [] (list_values items));
      target
  | _ -> raise (Runtime_error "remove expects (list, index)")

let with_file_error operation path action =
  try action () with
  | Sys_error message ->
      raise
        (Runtime_error
           (Printf.sprintf "%s failed for '%s': %s" operation path message))

let builtin_read_file args =
  match args with
  | [ path_value ] ->
      let path = expect_string "read_file" path_value in
      with_file_error "read_file" path (fun () ->
          let channel = open_in_bin path in
          Fun.protect
            ~finally:(fun () -> close_in channel)
            (fun () -> V_string (really_input_string channel (in_channel_length channel))))
  | _ -> raise (Runtime_error "read_file expects (path)")

let builtin_write_file args =
  match args with
  | [ path_value; content ] ->
      let path = expect_string "write_file" path_value in
      with_file_error "write_file" path (fun () ->
          let channel = open_out_bin path in
          Fun.protect
            ~finally:(fun () -> close_out channel)
            (fun () -> output_string channel (value_to_string content));
          V_null)
  | _ -> raise (Runtime_error "write_file expects (path, content)")

(* What the program was started with, after its own name. Set once by whoever
   is about to run the program -- the CLI for 'suchu run', the generated main
   for a compiled binary -- so that both answer the same thing. A program that
   opens a file handed to it by the desktop reads it here. *)
let program_arguments : string list ref = ref []

let builtin_file_exists args =
  match args with
  | [ path_value ] ->
      let path = expect_string "file_exists" path_value in
      V_bool (Sys.file_exists path)
  | _ -> raise (Runtime_error "file_exists expects (path)")

(* The names inside a directory, without '.' and '..'.

   Sorted, because Sys.readdir hands them back in whatever order the filesystem
   stored them: alphabetical on one machine, creation order on another. A file
   tree that reshuffles itself between runs is a bug nobody would think to look
   for here, so the order is decided once, here. *)
let builtin_list_directory args =
  match args with
  | [ path_value ] ->
      let path = expect_string "list_directory" path_value in
      with_file_error "list_directory" path (fun () ->
          let entries = Sys.readdir path in
          Array.sort compare entries;
          V_list (list_of_values (Array.to_list (Array.map (fun name -> V_string name) entries))))
  | _ -> raise (Runtime_error "list_directory expects (path)")

(* Asked separately rather than returned by list_directory, so that walking a
   tree stays two plain calls instead of a record the language would have to
   describe. A path that does not exist is not a directory, rather than an
   error: 'is it one?' has an answer either way. *)
let builtin_is_directory args =
  match args with
  | [ path_value ] ->
      let path = expect_string "is_directory" path_value in
      V_bool (try Sys.is_directory path with Sys_error _ -> false)
  | _ -> raise (Runtime_error "is_directory expects (path)")

let utf8_safe_prefix text limit =
  let stop = min (String.length text) (max 0 limit) in
  let rec find_boundary index =
    if index <= 0 || index >= String.length text then index
    else
      let byte = Char.code text.[index] in
      if byte land 0xC0 = 0x80 then find_boundary (index - 1) else index
  in
  String.sub text 0 (find_boundary stop)

let builtin_truncate args =
  match args with
  | [ text_value; max_value ] ->
      let text = expect_string "truncate" text_value in
      let max_length = expect_int max_value in
      if max_length < 0 then raise (Runtime_error "truncate length cannot be negative");
      if String.length text <= max_length then V_string text
      else if max_length <= 3 then V_string (utf8_safe_prefix text max_length)
      else V_string (utf8_safe_prefix text (max_length - 3) ^ "...")
  | _ -> raise (Runtime_error "truncate expects (text, max_length)")

(* What a snippet handed to eval() is allowed to see.

   Named one by one, and anything not named is refused. A list of what to forbid
   would quietly let every built-in added later through, which is the wrong way
   round for a decision about what untrusted text may do.

   Everything here computes and returns. Nothing here touches a file, reads the
   keyboard, opens a window or reaches the program that called eval. A snippet
   that should be able to affect its caller is handed a record to work on -- the
   caller decides what goes in it. *)
let sandbox_builtins =
  [ "len"; "type"; "assert"; "truncate";
    "sqrt"; "Pow"; "abs"; "min"; "max"; "round"; "range";
    "sin"; "cos"; "tan"; "asin"; "acos"; "atan"; "atan2"; "log"; "exp"; "floor"; "ceil"; "pi";
    "push"; "pull"; "insert"; "remove"; "map"; "filter"; "reduce" ]

let create_global_environment () =
  let env = empty_env () in
  env_define env "print" (V_native builtin_print);
  env_define env "input" (V_native builtin_input);
  env_define env "sqrt"
    (V_native (function
      | [ value ] ->
          let (num, _) = expect_number value in
          V_float (sqrt num)
      | _ -> raise (Runtime_error "sqrt expects one argument")));
  (* Angles in radians, as everywhere else that has these. Added when the 3D
     scene arrived: a camera going round something needs them, and so does
     anything drawn on a curve. *)
  List.iter
    (fun (name, fn) ->
      env_define env name
        (V_native (function
          | [ value ] ->
              let num, _ = expect_number value in
              V_float (fn num)
          | _ -> raise (Runtime_error (name ^ " expects one argument")))))
    [ ("sin", sin); ("cos", cos); ("tan", tan); ("asin", asin); ("acos", acos);
      ("atan", atan); ("log", log); ("exp", exp); ("floor", Float.floor); ("ceil", Float.ceil) ];
  env_define env "atan2"
    (V_native (function
      | [ y; x ] ->
          let y, _ = expect_number y and x, _ = expect_number x in
          V_float (atan2 y x)
      | _ -> raise (Runtime_error "atan2 expects two arguments")));
  env_define env "pi" (V_float (4.0 *. atan 1.0));
  env_define env "Pow"
    (V_native (function
      | [ a; b ] -> pow_operator a b
      | _ -> raise (Runtime_error "Pow expects two arguments")));
  env_define env "abs"
    (V_native (function
      | [ v ] -> (
          match v with
          | V_int n -> V_int (abs n)
          | V_float f -> V_float (abs_float f)
          | _ -> raise (Runtime_error "abs expects a number"))
      | _ -> raise (Runtime_error "abs expects one argument")));
  let fold_min_max name args selector =
    match args with
    | [] -> raise (Runtime_error (name ^ " expects at least one argument"))
    | first :: rest ->
        List.fold_left
          (fun acc value ->
            let cmp = compare_values value acc in
            if selector cmp then value else acc)
          first rest
  in
  env_define env "min"
    (V_native (fun args ->
         let result = fold_min_max "min" args (fun cmp -> cmp < 0) in
         result));
  env_define env "max"
    (V_native (fun args ->
         let result = fold_min_max "max" args (fun cmp -> cmp > 0) in
         result));
  env_define env "round"
    (V_native (function
      | [ value ] -> (
          match value with
          | V_int _ as v -> v
          | V_float f ->
              if Float.is_nan f || Float.is_infinite f then
                raise (Runtime_error "round cannot handle NaN or infinity")
              else
                let rounded =
                  if f >= 0.0 then Float.floor (f +. 0.5) else Float.ceil (f -. 0.5)
                in
                V_int (int_of_float rounded)
          | _ -> raise (Runtime_error "round expects a numeric value"))
      | _ -> raise (Runtime_error "round expects one argument")));
  env_define env "type"
    (V_native (function
      | [ value ] -> V_string (type_of_value value)
      | _ -> raise (Runtime_error "type expects one argument")));
  env_define env "range"
    (V_native (function
      | [ start_v; stop_v ] ->
          let start_i = expect_int start_v in
          let stop_i = expect_int stop_v in
          let step = if stop_i >= start_i then 1 else -1 in
          let rec build acc current =
            if (step > 0 && current >= stop_i) || (step < 0 && current <= stop_i) then List.rev acc
            else build (current :: acc) (current + step)
          in
          V_list (list_of_values (build [] start_i |> List.map (fun n -> V_int n)))
      | [ start_v; stop_v; step_v ] ->
          let start_i = expect_int start_v in
          let stop_i = expect_int stop_v in
          let step_i = expect_int step_v in
          if step_i = 0 then raise (Runtime_error "range step cannot be zero");
          let forward = step_i > 0 in
          let rec build acc current =
            if (forward && current >= stop_i) || ((not forward) && current <= stop_i) then List.rev acc
            else build (current :: acc) (current + step_i)
          in
          V_list (list_of_values (build [] start_i |> List.map (fun n -> V_int n)))
      | _ -> raise (Runtime_error "range expects two or three integer arguments")));
  env_define env "assert"
    (V_native (function
      | [ condition ] ->
          if truthy condition then V_null else raise (Runtime_error "Assertion failed")
      | _ -> raise (Runtime_error "assert expects one argument")));
  env_define env "push" (V_native builtin_push);
  env_define env "pull" (V_native builtin_pull);
  env_define env "insert" (V_native builtin_insert);
  env_define env "remove" (V_native builtin_remove);
  env_define env "read_file" (V_native builtin_read_file);
  env_define env "write_file" (V_native builtin_write_file);
  env_define env "file_exists" (V_native builtin_file_exists);
  env_define env "truncate" (V_native builtin_truncate);
  (* len(x) works on any value with a size, and unlike Length.name it takes an
     expression: len(build_list()) is legal, Length.(...) never was. *)
  env_define env "len"
    (V_native (function
      | [ V_string text ] -> V_int (String.length text)
      | [ V_list items ] -> V_int (list_length items)
      | [ V_set items ] -> V_int (List.length !items)
      | [ V_record fields ] -> V_int (List.length !fields)
      | [ other ] ->
          raise (Runtime_error ("len() does not apply to a " ^ type_of_value other))
      | _ -> raise (Runtime_error "len expects one argument")));
  (* Kept so 0.4 sources still run; len() is the documented form. *)
  env_define env "Length" V_length;
  env
