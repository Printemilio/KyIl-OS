open Ast
open Lexical
open Parser_utils

let token_lexeme token =
  match token.lexeme with
  | Some lex -> lex
  | None -> (
      match token.kind with
      | Window_kw -> "window"
      | Onclick_kw -> "onclick"
      | Oninput_kw -> "oninput"
      | Import_kw -> "import"
      | _ -> "token")

let lower_string s = String.lowercase_ascii s

let option_default default = function
  | Some value -> value
  | None -> default

(* Property names arrive as plain identifiers since 0.5; the few words still
   reserved elsewhere in the language are accepted here too, so `window:` or
   `onClick:` remain usable as property names. *)
let is_property_segment = function
  | Ident _
  | Window_kw
  | Onclick_kw
  | Oninput_kw -> true
  | _ -> false

let rec looks_like_property state offset expect_segment =
  if offset + state.index >= state.length then false
  else
    let token = state.tokens.(state.index + offset) in
    match token.kind with
    | Colon when not expect_segment -> true
    | Minus when not expect_segment -> looks_like_property state (offset + 1) true
    | kind when is_property_segment kind -> looks_like_property state (offset + 1) false
    | Newline | Semicolon -> false
    | Lbrace | Lparen | Lt -> false
    | _ -> false

let is_property_entry state = looks_like_property state 0 true

let parse_property_name state =
  let buffer = Buffer.create 32 in
  let rec gather first_segment =
    let token =
      expect state
        (fun kind -> is_property_segment kind)
        "Expected property name"
    in
    let segment = token_lexeme token |> lower_string in
    if not first_segment then Buffer.add_char buffer '-';
    Buffer.add_string buffer segment;
    match peek state with
    | { kind = Minus; _ } ->
        ignore (advance state);
        gather false
    | _ -> Buffer.contents buffer
  in
  gather true

let gui_literal_of_token token =
  match token.kind with
  | String_lit value -> GuiString value
  | True_kw -> GuiBool true
  | False_kw -> GuiBool false
  | Int_lit value -> GuiNumber (float_of_int value)
  | Float_lit value -> GuiNumber value
  | Length_lit value -> GuiLength value
  | Duration_lit value -> GuiDuration value
  | Color_lit value -> GuiColor ("#" ^ value)
  | Ident name -> GuiIdent (lower_string name)
  | Window_kw ->

      let lexeme = token_lexeme token in
      GuiIdent (lower_string lexeme)
  | _ ->
      let display =
        match token.lexeme with
        | Some lex -> lex
        | None -> "value"
      in
      raise
        (Parse_error
           (Printf.sprintf "Unsupported literal '%s' in GUI property (line %d, column %d)" display
              token.line token.column))

(* '%' is the modulo operator, so it cannot become a unit in the lexer without
   changing what '50%3' means everywhere else. Instead a percentage is read
   here, where we already know we are in a GUI property, and only when the '%'
   sits immediately against the number: '50%' is a width, '50 % 3' is still a
   remainder. *)
let percent_follows state token =
  match token.lexeme with
  | None -> false
  | Some lexeme ->
      let next = peek state in
      next.kind = Percent && next.line = token.line
      && next.column = token.column + String.length lexeme

let parse_gui_literal state =
  let token =
    expect state
      (function
        | String_lit _
        | True_kw
        | False_kw
        | Int_lit _
        | Float_lit _
        | Length_lit _
        | Duration_lit _
        | Color_lit _
        | Ident _
        | Window_kw -> true
        | _ -> false)
      "Expected literal value"
  in
  match token.kind with
  | (Int_lit _ | Float_lit _) when percent_follows state token ->
      ignore (advance state);
      let value =
        match token.kind with
        | Int_lit value -> float_of_int value
        | Float_lit value -> value
        | _ -> 0.
      in
      GuiPercent value
  | _ -> gui_literal_of_token token

let token_text token =
  match token.kind with
  | String_lit value -> "\"" ^ String.escaped value ^ "\""
  | Int_lit value -> string_of_int value
  | Float_lit value -> option_default (string_of_float value) token.lexeme
  | Length_lit value -> option_default (string_of_int value ^ "px") token.lexeme
  | Duration_lit value -> option_default (string_of_int value ^ "ms") token.lexeme
  | Color_lit value -> "#" ^ value
  | Ident _ -> option_default "" token.lexeme
  | True_kw -> "true"
  | False_kw -> "false"
  | Window_kw -> token_lexeme token
  | Fun_kw -> "fun"
  | If_kw -> "if"
  | Else_kw -> "else"
  | Return_kw -> "return"
  | While_kw -> "while"
  | For_kw -> "for"
  | In_kw -> "in"
  | Then_kw -> "then"
  | Match_kw -> "match"
  | Case_kw -> "case"
  | Default_kw -> "default"
  | Import_kw -> "import"
  | None_kw -> "none"
  | Nan_kw -> "NaN"
  | Inf_kw -> "inf"
  | Onclick_kw -> token_lexeme token
  | Oninput_kw -> token_lexeme token
  | Lparen -> "("
  | Rparen -> ")"
  | Lbrace -> "{"
  | Rbrace -> "}"
  | Lbracket -> "["
  | Rbracket -> "]"
  | Comma -> ","
  | Colon -> ":"
  | Semicolon -> ";"
  | Dot -> "."
  | Assign -> "="
  | Plus -> "+"
  | Minus -> "-"
  | Star -> "*"
  | Slash -> "/"
  | Percent -> "%"
  | Bang -> "!"
  | At -> "@"
  | Hash -> "#"
  | Pipe -> "|"
  | Ampersand -> "&"
  | Eq_eq -> "=="
  | Bang_eq -> "!="
  | Lt -> "<"
  | Gt -> ">"
  | Le -> "<="
  | Ge -> ">="
  | And_kw -> "&&"
  | Or_kw -> "||"
  | Inc -> "++"
  | Dec -> "--"
  | Pow -> "**"
  | Range -> ".."
  | Newline -> "\n"
  | Eof -> ""
  | _ -> option_default "" token.lexeme

let no_space_before = function
  | Lparen
  | Lbracket
  | Rparen
  | Rbracket
  | Rbrace
  | Comma
  | Semicolon
  | Colon
  | Dot -> true
  | _ -> false

let no_space_after = function
  | Lparen
  | Lbracket
  | Lbrace
  | Dot -> true
  | _ -> false

let should_insert_space prev_token token =
  match token.kind with
  | Newline -> false
  | _ -> (
      match prev_token with
      | None -> false
      | Some prev when prev.kind = Newline -> false
      | Some prev when no_space_after prev.kind -> false
      | Some _ when no_space_before token.kind -> false
      | Some _ -> true)

let render_event_tokens tokens =
  let buffer = Buffer.create 64 in
  let rec render prev_token = function
    | [] -> ()
    | token :: rest -> (
        match token.kind with
        | Newline ->
            Buffer.add_char buffer '\n';
            render None rest
        | _ ->
            if should_insert_space prev_token token then Buffer.add_char buffer ' ';
            Buffer.add_string buffer (token_text token);
            render (Some token) rest)
  in
  render None tokens;
  Buffer.contents buffer |> String.trim

let capture_event_body state =
  ignore (expect_specific state Lbrace "Expected '{' to start event handler");
  let rec loop depth acc =
    match peek state with
    | { kind = Eof; _ } -> fail state "Unterminated event handler block"
    | token ->
        ignore (advance state);
        (match token.kind with
        | Lbrace -> loop (depth + 1) (token :: acc)
        | Rbrace ->
            if depth = 1 then List.rev acc else loop (depth - 1) (token :: acc)
        | _ -> loop depth (token :: acc))
  in
  let tokens = loop 1 [] in
  render_event_tokens tokens

let parse_event_property state =
  let body = capture_event_body state in
  GuiPropEvent body

let parse_property_value state name =
  skip_newlines state;
  match String.lowercase_ascii name with
  | "onclick" | "onhover" | "oninput" | "onchange" | "onpress" | "ontick" | "ondraw" ->
      parse_event_property state
  | _ -> GuiPropLiteral (parse_gui_literal state)

let parse_property state =
  let name = parse_property_name state in
  skip_newlines state;
  ignore (expect_specific state Colon "Expected ':' after property name");
  skip_newlines state;
  let value = parse_property_value state name in
  skip_newlines state;
  (* Required since 0.5, so a GUI property block and a record literal -- which
     look identical -- follow the same rule. *)
  ignore (expect_specific state Semicolon "Expected ';' after GUI property");
  (name, value)

let parse_argument_list state =
  ignore (expect_specific state Lparen "Expected '(' to start argument list");
  skip_newlines state;
  let rec gather acc =
    match peek state with
    | { kind = Rparen; _ } ->
        ignore (advance state);
        List.rev acc
    | _ ->
        let literal = parse_gui_literal state in
        skip_newlines state;
        let acc = literal :: acc in
        match peek state with
        | { kind = Comma; _ } ->
            ignore (advance state);
            skip_newlines state;
            gather acc
        | { kind = Rparen; _ } ->
            ignore (advance state);
            List.rev acc
        | _ -> fail state "Expected ',' or ')' in argument list"
  in
  gather []

let parse_optional_arguments state =
  skip_newlines state;
  match peek state with
  | { kind = Lparen; _ } -> parse_argument_list state
  | _ -> []

let parse_element_name state =
  let token =
    expect state
      (function
        | Ident _
        | Window_kw -> true
        | _ -> false)
      "Expected element name"
  in
  token_lexeme token

let rec parse_entries state =
  let rec gather props children =
    skip_newlines state;
    match peek state with
    | { kind = Rbrace; _ } ->
        ignore (advance state);
        (List.rev props, List.rev children)
    | { kind = Eof; _ } -> fail state "Unterminated GUI block"
    | { kind = Lt; _ } ->
        let child = parse_angle_element state in
        gather props (child :: children)
    | _ ->
        if is_property_entry state then
          let property = parse_property state in
          gather (property :: props) children
        else
          let child = parse_regular_element state in
          gather props (child :: children)
  in
  gather [] []

and parse_angle_element state =
  ignore (expect_specific state Lt "Expected '<' to start element");
  let name = parse_element_name state in
  ignore (expect_specific state Gt "Expected '>' after element name");
  let args = parse_optional_arguments state in
  skip_newlines state;
  ignore (expect_specific state Lbrace (Printf.sprintf "Expected '{' after element '%s'" name));
  let props, children = parse_entries state in
  GuiBlock (name, args, props, children)

and parse_regular_element state =
  let name = parse_element_name state in
  let args = parse_optional_arguments state in
  skip_newlines state;
  ignore (expect_specific state Lbrace (Printf.sprintf "Expected '{' after element '%s'" name));
  let props, children = parse_entries state in
  GuiBlock (name, args, props, children)

let parse_element state =
  skip_newlines state;
  match peek state with
  | { kind = Lt; _ } -> parse_angle_element state
  | _ -> parse_regular_element state

let parse_root state = parse_element state
