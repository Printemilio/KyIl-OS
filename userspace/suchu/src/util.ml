let is_alpha = function
  | 'a' .. 'z'
  | 'A' .. 'Z'
  | '_' -> true
  | _ -> false

let is_digit = function
  | '0' .. '9' -> true
  | _ -> false

let is_alphanum ch = is_alpha ch || is_digit ch

let is_hex_digit = function
  | '0' .. '9'
  | 'a' .. 'f'
  | 'A' .. 'F' -> true
  | _ -> false

let string_builder () =
  let buffer = Buffer.create 32 in
  (buffer, Buffer.add_char buffer, Buffer.clear buffer, fun () -> Buffer.contents buffer)
