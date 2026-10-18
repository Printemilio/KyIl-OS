open Runtime

type suchu_value = value
type binding = string * suchu_value

let null = V_null
let int value = V_int value
let float value = V_float value
let bool value = V_bool value
let string value = V_string value
let list values = V_list (list_of_values values)
let set values = make_set values
let native fn = V_native fn
let object_value kind value = make_object kind value

let as_int = function
  | V_int value -> value
  | value ->
      raise
        (Runtime_error
           (Printf.sprintf "Expected int, got %s" (type_of_value value)))

let as_float = function
  | V_float value -> value
  | V_int value -> float_of_int value
  | value ->
      raise
        (Runtime_error
           (Printf.sprintf "Expected number, got %s" (type_of_value value)))

let as_bool = function
  | V_bool value -> value
  | value ->
      raise
        (Runtime_error
           (Printf.sprintf "Expected bool, got %s" (type_of_value value)))

let as_string = function
  | V_string value -> value
  | value ->
      raise
        (Runtime_error
           (Printf.sprintf "Expected string, got %s" (type_of_value value)))

let as_list = function
  | V_list values -> list_values values
  | value ->
      raise
        (Runtime_error
           (Printf.sprintf "Expected list, got %s" (type_of_value value)))

let register_module = register_native_module

let function0 name fn =
  ( name,
    native (function
      | [] -> fn ()
      | _ -> raise (Runtime_error (name ^ " expects no arguments"))) )

let function1 name fn =
  ( name,
    native (function
      | [ value ] -> fn value
      | _ -> raise (Runtime_error (name ^ " expects one argument"))) )

let function2 name fn =
  ( name,
    native (function
      | [ left; right ] -> fn left right
      | _ -> raise (Runtime_error (name ^ " expects two arguments"))) )
