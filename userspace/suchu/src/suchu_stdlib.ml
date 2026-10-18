open Runtime

let native0 name fn =
  V_native (function
    | [] -> fn ()
    | _ -> raise (Runtime_error (name ^ " expects no arguments")))

let native1 name fn =
  V_native (function
    | [ value ] -> fn value
    | _ -> raise (Runtime_error (name ^ " expects one argument")))

let json_escape text =
  let buffer = Buffer.create (String.length text + 8) in
  String.iter
    (function
      | '"' -> Buffer.add_string buffer "\\\""
      | '\\' -> Buffer.add_string buffer "\\\\"
      | '\n' -> Buffer.add_string buffer "\\n"
      | '\r' -> Buffer.add_string buffer "\\r"
      | '\t' -> Buffer.add_string buffer "\\t"
      | ch when Char.code ch < 0x20 ->
          Buffer.add_string buffer (Printf.sprintf "\\u%04x" (Char.code ch))
      | ch -> Buffer.add_char buffer ch)
    text;
  Buffer.contents buffer

let rec json_encode = function
  | V_null -> "null"
  | V_bool value -> if value then "true" else "false"
  | V_int value -> string_of_int value
  | V_float value ->
      if Float.is_finite value then string_of_float value
      else raise (Runtime_error "json.encode cannot encode NaN or infinity")
  | V_string value -> "\"" ^ json_escape value ^ "\""
  | V_list values ->
      "[" ^ String.concat "," (List.map json_encode (list_values values)) ^ "]"
  | V_set values ->
      "[" ^ String.concat "," (List.map json_encode !values) ^ "]"
  | V_record fields ->
      let field (name, value) = "\"" ^ json_escape name ^ "\":" ^ json_encode value in
      "{" ^ String.concat "," (List.map field !fields) ^ "}"
  | value ->
      raise
        (Runtime_error
           ("json.encode cannot encode a value of type " ^ type_of_value value))

type json_state = {
  source : string;
  mutable position : int;
}

let json_error state message =
  raise
    (Runtime_error
       (Printf.sprintf "json.decode: %s at character %d" message
          (state.position + 1)))

let json_skip_space state =
  while
    state.position < String.length state.source
    &&
    match state.source.[state.position] with
    | ' '
    | '\t'
    | '\r'
    | '\n' -> true
    | _ -> false
  do
    state.position <- state.position + 1
  done

let json_expect_word state word value =
  let length = String.length word in
  if
    state.position + length <= String.length state.source
    && String.sub state.source state.position length = word
  then (
    state.position <- state.position + length;
    value)
  else json_error state ("expected " ^ word)

let json_string state =
  let buffer = Buffer.create 16 in
  state.position <- state.position + 1;
  let rec loop () =
    if state.position >= String.length state.source then
      json_error state "unterminated string";
    match state.source.[state.position] with
    | '"' ->
        state.position <- state.position + 1;
        V_string (Buffer.contents buffer)
    | '\\' ->
        state.position <- state.position + 1;
        if state.position >= String.length state.source then
          json_error state "unterminated escape";
        let escaped =
          match state.source.[state.position] with
          | '"' -> '"'
          | '\\' -> '\\'
          | '/' -> '/'
          | 'b' -> '\b'
          | 'f' -> '\012'
          | 'n' -> '\n'
          | 'r' -> '\r'
          | 't' -> '\t'
          | _ -> json_error state "unsupported escape"
        in
        Buffer.add_char buffer escaped;
        state.position <- state.position + 1;
        loop ()
    | ch ->
        Buffer.add_char buffer ch;
        state.position <- state.position + 1;
        loop ()
  in
  loop ()

let json_number state =
  let start = state.position in
  while
    state.position < String.length state.source
    &&
    match state.source.[state.position] with
    | '0' .. '9'
    | '-'
    | '+'
    | '.'
    | 'e'
    | 'E' -> true
    | _ -> false
  do
    state.position <- state.position + 1
  done;
  let token = String.sub state.source start (state.position - start) in
  try
    if String.contains token '.' || String.contains token 'e' || String.contains token 'E'
    then V_float (float_of_string token)
    else V_int (int_of_string token)
  with Failure _ -> json_error state "invalid number"

let rec json_value state =
  json_skip_space state;
  if state.position >= String.length state.source then
    json_error state "expected a value";
  match state.source.[state.position] with
  | '"' -> json_string state
  | '[' -> json_array state
  | 't' -> json_expect_word state "true" (V_bool true)
  | 'f' -> json_expect_word state "false" (V_bool false)
  | 'n' -> json_expect_word state "null" V_null
  | '-' | '0' .. '9' -> json_number state
  | '{' -> json_object state
  | _ -> json_error state "unexpected character"

(* JSON objects decode to records, which is what records were added for. Keys
   keep their document order, so decode then encode round-trips faithfully. *)
and json_object state =
  state.position <- state.position + 1;
  json_skip_space state;
  if state.position < String.length state.source && state.source.[state.position] = '}'
  then (
    state.position <- state.position + 1;
    V_record (ref []))
  else
    let fields = ref [] in
    let rec entries () =
      json_skip_space state;
      if state.position >= String.length state.source || state.source.[state.position] <> '"' then
        json_error state "expected a quoted field name";
      let key =
        match json_string state with
        | V_string name -> name
        | _ -> json_error state "expected a quoted field name"
      in
      json_skip_space state;
      if state.position >= String.length state.source || state.source.[state.position] <> ':' then
        json_error state "expected ':' after a field name";
      state.position <- state.position + 1;
      let value = json_value state in
      record_set fields key value;
      json_skip_space state;
      if state.position >= String.length state.source then
        json_error state "unterminated object";
      match state.source.[state.position] with
      | ',' ->
          state.position <- state.position + 1;
          entries ()
      | '}' ->
          state.position <- state.position + 1;
          V_record fields
      | _ -> json_error state "expected ',' or '}'"
    in
    entries ()

and json_array state =
  state.position <- state.position + 1;
  json_skip_space state;
  if state.position < String.length state.source && state.source.[state.position] = ']'
  then (
    state.position <- state.position + 1;
    V_list (empty_list ()))
  else
    let rec items acc =
      let value = json_value state in
      json_skip_space state;
      if state.position >= String.length state.source then
        json_error state "unterminated array";
      match state.source.[state.position] with
      | ',' ->
          state.position <- state.position + 1;
          items (value :: acc)
      | ']' ->
          state.position <- state.position + 1;
          V_list (list_of_values (List.rev (value :: acc)))
      | _ -> json_error state "expected ',' or ']'"
    in
    items []

let json_decode text =
  let state = { source = text; position = 0 } in
  let value = json_value state in
  json_skip_space state;
  if state.position <> String.length text then
    json_error state "unexpected trailing content";
  value

let register () =
  register_native_module "files"
    [
      ("read", V_native builtin_read_file);
      ("write", V_native builtin_write_file);
      ("exists", V_native builtin_file_exists);
      (* The names inside a directory, sorted, without '.' and '..'. *)
      ("list", V_native builtin_list_directory);
      ("is_directory", V_native builtin_is_directory);
    ];
  register_native_module "time"
    [
      ("now", native0 "time.now" (fun () -> V_float (Unix.gettimeofday ())));
      ("cpu", native0 "time.cpu" (fun () -> V_float (Sys.time ())));
      ( "sleep",
        native1 "time.sleep" (fun value ->
            let seconds, _ = expect_number value in
            if seconds < 0. then
              raise (Runtime_error "time.sleep expects a positive duration");
            Unix.sleepf seconds;
            V_null) );
    ];
  register_native_module "system"
    [
      ("os", V_string Sys.os_type);
      (* What the program was started with, after its own name. A compiled
         application launched by the desktop with a file gets that path here;
         'suchu run prog.suchu a b' gets ["a"; "b"]. Both paths fill the same
         store, so a program cannot tell how it was started. *)
      ( "args",
        native0 "system.args" (fun () ->
            V_list
              (list_of_values (List.map (fun a -> V_string a) !program_arguments))) );
      ("cwd", native0 "system.cwd" (fun () -> V_string (Sys.getcwd ())));
      ( "getenv",
        native1 "system.getenv" (fun value ->
            let name = expect_string "system.getenv" value in
            match Sys.getenv_opt name with
            | Some result -> V_string result
            | None -> V_null) );
      ( "exec",
        native1 "system.exec" (fun value ->
            let command = expect_string "system.exec" value in
            V_int (Sys.command command)) );
    ];
  (* Seeded from the system the first time it is asked for, so a program that never
     calls seed gets different numbers every run. With a seed, the same numbers every
     run, interpreted or compiled: both use this one generator. *)
  let seeded = ref false in
  let ensure_seeded () =
    if not !seeded then begin
      Random.self_init ();
      seeded := true
    end
  in
  let number name value =
    match value with
    | V_int n -> n
    | other ->
        raise
          (Runtime_error
             (Printf.sprintf "%s expects whole numbers, got %s" name (describe_value other)))
  in
  register_native_module "random"
    [
      (* Both ends included: random.int(1, 6) is a die. *)
      ( "int",
        V_native
          (function
          | [ low; high ] ->
              ensure_seeded ();
              let low = number "random.int" low and high = number "random.int" high in
              if low > high then
                raise
                  (Runtime_error
                     (Printf.sprintf "random.int needs the low end first, got %d and %d" low high));
              V_int (Random.int_in_range ~min:low ~max:high)
          | _ -> raise (Runtime_error "random.int expects two arguments")) );
      (* From 0 up to, but not including, 1. *)
      ( "float",
        native0 "random.float" (fun () ->
            ensure_seeded ();
            V_float (Random.float 1.0)) );
      ( "choice",
        native1 "random.choice" (function
          | V_list items when list_length items > 0 ->
              ensure_seeded ();
              list_get items (Random.int (list_length items))
          | V_list _ -> raise (Runtime_error "random.choice cannot choose from an empty list")
          | other ->
              raise
                (Runtime_error
                   (Printf.sprintf "random.choice expects a list, got %s" (describe_value other)))) );
      (* In place, and the list is returned too: every ordering is as likely as any
         other. *)
      ( "shuffle",
        native1 "random.shuffle" (function
          | V_list items ->
              ensure_seeded ();
              for i = list_length items - 1 downto 1 do
                let j = Random.int (i + 1) in
                let held = list_get items i in
                list_set items i (list_get items j);
                list_set items j held
              done;
              V_list items
          | other ->
              raise
                (Runtime_error
                   (Printf.sprintf "random.shuffle expects a list, got %s" (describe_value other)))) );
      (* The same seed gives the same numbers, which is what a test wants. *)
      ( "seed",
        native1 "random.seed" (fun value ->
            seeded := true;
            Random.init (number "random.seed" value);
            V_null) );
    ];
  register_native_module "json"
    [
      ("encode", native1 "json.encode" (fun value -> V_string (json_encode value)));
      ( "decode",
        native1 "json.decode" (fun value ->
            json_decode (expect_string "json.decode" value)) );
    ]
