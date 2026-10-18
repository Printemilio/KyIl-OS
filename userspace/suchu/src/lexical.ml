open Util

type token_kind =
  | Fun_kw
  | If_kw
  | Else_kw
  | Return_kw
  | Break_kw
  | Continue_kw
  | Try_kw
  | While_kw
  | For_kw
  | In_kw
  | Then_kw
  | Match_kw
  | Case_kw
  | Default_kw
  | Import_kw
  | True_kw
  | False_kw
  | None_kw
  | Nan_kw
  | Inf_kw
  (* Since 0.5 only `window` still opens a GUI block from statement position.
     row / column / label / button / input / id are contextual: they are plain
     identifiers everywhere, and the GUI parser recognises them by text. That is
     what makes `input(...)`, `id = 7` and `label = "x"` legal again. *)
  | Window_kw
  | Set_kw
  | Onclick_kw
  | Oninput_kw
  | Ident of string
  | Int_lit of int
  | Float_lit of float
  | String_lit of string
  | Length_lit of int
  (* A span of time, always normalised to milliseconds: 100ms, 2s. *)
  | Duration_lit of int
  | Color_lit of string
  | Lparen
  | Rparen
  | Lbrace
  | Rbrace
  | Lbracket
  | Rbracket
  | Comma
  | Colon
  | Semicolon
  | Dot
  | Assign
  | Plus
  | Minus
  | Star
  | Slash
  | Percent
  | Bang
  | At
  | Hash
  | Pipe
  | Ampersand
  | Eq_eq
  | Bang_eq
  | Lt
  | Gt
  | Le
  | Ge
  | And_kw
  | Or_kw
  | Inc
  | Dec
  | Pow
  | Range
  | Newline
  | Eof

type token = {
  kind : token_kind;
  lexeme : string option;
  line : int;
  column : int;
}

exception Lexing_error of string

let token ?(lexeme = None) ~kind ~line ~column () = { kind; lexeme; line; column }

let keywords =
  [
    ("fun", Fun_kw);
    ("if", If_kw);
    ("else", Else_kw);
    ("return", Return_kw);
    ("break", Break_kw);
    ("continue", Continue_kw);
    ("try", Try_kw);
    ("while", While_kw);
    ("for", For_kw);
    ("in", In_kw);
    ("then", Then_kw);
    ("match", Match_kw);
    ("case", Case_kw);
    ("default", Default_kw);
    ("import", Import_kw);
    ("true", True_kw);
    ("false", False_kw);
    ("none", None_kw);
    ("NaN", Nan_kw);
    ("nan", Nan_kw);
    ("inf", Inf_kw);
    ("set", Set_kw);
    ("window", Window_kw);
    ("Window", Window_kw);
    ("onClick", Onclick_kw);
    ("onInput", Oninput_kw);
  ]

let keyword_lookup = Hashtbl.create 16

let () =
  List.iter (fun (lex, kind) -> Hashtbl.add keyword_lookup lex kind) keywords

let make_identifier name =
  match Hashtbl.find_opt keyword_lookup name with
  | Some kind -> kind
  | None -> Ident name

type state = {
  source : string;
  length : int;
  mutable index : int;
  mutable line : int;
  mutable column : int;
}

(* Notepad and PowerShell save UTF-8 files with a byte order mark. It carries no
   meaning here, but the lexer would otherwise report an unreadable "unexpected
   character" on the very first column of every file written on Windows. *)
let bom = "\xEF\xBB\xBF"

let starts_with_bom source =
  String.length source >= 3 && String.sub source 0 3 = bom

let create_state source =
  let index = if starts_with_bom source then 3 else 0 in
  { source; length = String.length source; index; line = 1; column = 1 }

let peek state =
  if state.index >= state.length then None else Some state.source.[state.index]

let peek_next state =
  if state.index + 1 >= state.length then None else Some state.source.[state.index + 1]

(* Needed to tell a unit from the start of a name: '100s' is a duration,
   '100start' is not. *)
let peek_at state offset =
  if state.index + offset >= state.length then None else Some state.source.[state.index + offset]

let advance state =
  match peek state with
  | None -> None
  | Some ch ->
      state.index <- state.index + 1;
      if ch = '\n' then (
        state.line <- state.line + 1;
        state.column <- 1)
      else state.column <- state.column + 1;
      Some ch

let emit tokens kind ?lexeme state =
  let tok = token ~kind ~line:state.line ~column:state.column ?lexeme () in
  tok :: tokens

let rec skip_comment state =
  match advance state with
  | None
  | Some '\n' -> ()
  | _ -> skip_comment state

let read_identifier state start_line start_column first_char =
  let buffer = Buffer.create 16 in
  Buffer.add_char buffer first_char;
  let rec loop () =
    match peek state with
    | Some ch when is_alphanum ch ->
        Buffer.add_char buffer ch;
        ignore (advance state);
        loop ()
    | _ -> ()
  in
  loop ();
  let name = Buffer.contents buffer in
  token ~kind:(make_identifier name) ~line:start_line ~column:start_column ~lexeme:(Some name) ()

let read_number state start_line start_column first_digit =
  let buffer = Buffer.create 16 in
  Buffer.add_char buffer first_digit;
  let rec consume_digits () =
    match peek state with
    | Some ch when is_digit ch ->
        Buffer.add_char buffer ch;
        ignore (advance state);
        consume_digits ()
    | _ -> ()
  in
  consume_digits ();
  let is_float = ref false in
  (match peek state, peek_next state with
  | Some '.', Some next when is_digit next ->
      is_float := true;
      Buffer.add_char buffer '.';
      ignore (advance state);
      (match advance state with
      | Some digit ->
          Buffer.add_char buffer digit;
          consume_digits ()
      | None -> ())
  | _ -> ());
  let literal = Buffer.contents buffer in
  if !is_float then
    let value = float_of_string literal in
    token ~kind:(Float_lit value) ~line:start_line ~column:start_column ~lexeme:(Some literal) ()
  else (
    (* A unit only counts when nothing name-like follows it, so that '100s' is
       a duration while '100start' still splits into a number and a word. *)
    let free_at offset =
      match peek_at state offset with
      | Some ch when is_alphanum ch || ch = '_' -> false
      | _ -> true
    in
    let value = int_of_string literal in
    let unit_token kind suffix count =
      for _ = 1 to count do
        ignore (advance state)
      done;
      token ~kind ~line:start_line ~column:start_column ~lexeme:(Some (literal ^ suffix)) ()
    in
    match (peek state, peek_next state) with
    | Some 'p', Some 'x' when free_at 2 -> unit_token (Length_lit value) "px" 2
    | Some 'm', Some 's' when free_at 2 -> unit_token (Duration_lit value) "ms" 2
    | Some 's', _ when free_at 1 -> unit_token (Duration_lit (value * 1000)) "s" 1
    | _ -> token ~kind:(Int_lit value) ~line:start_line ~column:start_column ~lexeme:(Some literal) ())

let read_string state start_line start_column =
  let buffer = Buffer.create 32 in
  let rec loop () =
    match advance state with
    | None -> raise (Lexing_error "Unterminated string literal")
    | Some '"' ->
        let value = Buffer.contents buffer in
        token ~kind:(String_lit value) ~line:start_line ~column:start_column ~lexeme:(Some value) ()
    | Some '\\' -> (
        match advance state with
        | None -> raise (Lexing_error "Unterminated escape sequence")
        | Some 'n' ->
            Buffer.add_char buffer '\n';
            loop ()
        | Some 'r' ->
            Buffer.add_char buffer '\r';
            loop ()
        | Some 't' ->
            Buffer.add_char buffer '\t';
            loop ()
        | Some '"' ->
            Buffer.add_char buffer '"';
            loop ()
        | Some '\\' ->
            Buffer.add_char buffer '\\';
            loop ()
        | Some other ->
            Buffer.add_char buffer other;
            loop ())
    | Some ch ->
        Buffer.add_char buffer ch;
        loop ()
  in
  loop ()

(* 'b' is a char: one character, written between apostrophes. It is the string "b"
   -- the language has one text type, and a char is a string that is exactly one
   character long -- so what it adds is a way to say so, not another kind of
   value. The annotation 'char c = ...' is what checks the length.

   The errors say what is wrong with what was written. The commonest way to meet
   this is to write 'hello' with apostrophes, and "unexpected character" for that
   sends the reader looking for a typo that is not there. *)
let read_char state start_line start_column =
  let position = Printf.sprintf "line %d, column %d" start_line start_column in
  let buffer = Buffer.create 4 in
  let unterminated () =
    raise
      (Lexing_error
         (Printf.sprintf "A char is not closed (%s): it is written between apostrophes, 'a'" position))
  in
  let rec loop () =
    match peek state with
    | None | Some '\n' -> unterminated ()
    | Some '\'' -> ignore (advance state)
    | Some '\\' -> (
        ignore (advance state);
        match advance state with
        | None -> unterminated ()
        | Some 'n' -> Buffer.add_char buffer '\n'; loop ()
        | Some 'r' -> Buffer.add_char buffer '\r'; loop ()
        | Some 't' -> Buffer.add_char buffer '\t'; loop ()
        | Some other -> Buffer.add_char buffer other; loop ())
    | Some ch ->
        ignore (advance state);
        Buffer.add_char buffer ch;
        loop ()
  in
  loop ();
  let text = Buffer.contents buffer in
  (* Characters, not bytes: a continuation byte belongs to the one before it. *)
  let count = ref 0 in
  String.iter (fun c -> if Char.code c land 0xC0 <> 0x80 then incr count) text;
  if !count = 0 then
    raise
      (Lexing_error
         (Printf.sprintf "A char cannot be empty (%s). For an empty string, write \"\"" position))
  else if !count > 1 then
    raise
      (Lexing_error
         (Printf.sprintf
            "A char holds exactly one character, but '%s' has %d (%s). For text, use double quotes: \"%s\""
            text !count position text))
  else token ~kind:(String_lit text) ~line:start_line ~column:start_column ~lexeme:(Some text) ()

let lex source =
  let state = create_state source in
  let rec loop tokens =
    match peek state with
    | None ->
        let eof_token = token ~kind:Eof ~line:state.line ~column:state.column () in
        List.rev (eof_token :: tokens)
    | Some ch ->
        let start_line = state.line in
        let start_column = state.column in
        ignore (advance state);
        match ch with
        | ' ' | '\t' | '\r' -> loop tokens
        (* Since 0.5 a newline is plain whitespace. Statements are terminated by
           ';', so an expression may be wrapped across as many lines as you like
           and indentation is entirely free. *)
        | '\n' -> loop tokens
        | '/' -> (
            match peek state with
            | Some '/' ->
                ignore (advance state);
                skip_comment state;
                loop tokens
            | _ ->
                let tok = token ~kind:Slash ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '(' ->
            let tok = token ~kind:Lparen ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | ')' ->
            let tok = token ~kind:Rparen ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | '[' ->
            let tok = token ~kind:Lbracket ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | ']' ->
            let tok = token ~kind:Rbracket ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | '{' ->
            let tok = token ~kind:Lbrace ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | '}' ->
            let tok = token ~kind:Rbrace ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | ',' ->
            let tok = token ~kind:Comma ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | ':' ->
            let tok = token ~kind:Colon ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | ';' ->
            let tok = token ~kind:Semicolon ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | '.' -> (
            match peek state with
            | Some '.' ->
                ignore (advance state);
                let tok = token ~kind:Range ~line:start_line ~column:start_column () in
                loop (tok :: tokens)
            | _ ->
                let tok = token ~kind:Dot ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '+' -> (
            match peek state with
            | Some '+' ->
                ignore (advance state);
                let tok = token ~kind:Inc ~line:start_line ~column:start_column () in
                loop (tok :: tokens)
            | _ ->
                let tok = token ~kind:Plus ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '-' -> (
            match peek state with
            | Some '-' ->
                ignore (advance state);
                let tok = token ~kind:Dec ~line:start_line ~column:start_column () in
                loop (tok :: tokens)
            | _ ->
                let tok = token ~kind:Minus ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '*' -> (
            match peek state with
            | Some '*' ->
                ignore (advance state);
                let tok = token ~kind:Pow ~line:start_line ~column:start_column () in
                loop (tok :: tokens)
            | _ ->
                let tok = token ~kind:Star ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '%' ->
            let tok = token ~kind:Percent ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | '#' -> (
            let buffer = Buffer.create 6 in
            let rec gather_hex count =
              if count >= 6 then ()
              else
                match peek state with
                | Some ch when is_hex_digit ch ->
                    ignore (advance state);
                    Buffer.add_char buffer ch;
                    gather_hex (count + 1)
                | _ -> ()
            in
            match peek state with
            | Some ch when is_hex_digit ch ->
                ignore (advance state);
                Buffer.add_char buffer ch;
                gather_hex 1;
                let length = Buffer.length buffer in
                if length = 3 || length = 6 then
                  let value = Buffer.contents buffer in
                  let lexeme = "#" ^ value in
                  let tok =
                    token ~kind:(Color_lit value) ~line:start_line ~column:start_column ~lexeme:(Some lexeme) ()
                  in
                  loop (tok :: tokens)
                else
                  let message =
                    Printf.sprintf "Invalid color literal starting at line %d column %d" start_line start_column
                  in
                  raise (Lexing_error message)
            | _ ->
                let tok = token ~kind:Hash ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '@' ->
            let tok = token ~kind:At ~line:start_line ~column:start_column () in
            loop (tok :: tokens)
        | '=' -> (
            match peek state with
            | Some '=' ->
                ignore (advance state);
                let tok = token ~kind:Eq_eq ~line:start_line ~column:start_column () in
                loop (tok :: tokens)
            | _ ->
                let tok = token ~kind:Assign ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '!' -> (
            match peek state with
            | Some '=' ->
                ignore (advance state);
                let tok = token ~kind:Bang_eq ~line:start_line ~column:start_column () in
                loop (tok :: tokens)
            | _ ->
                let tok = token ~kind:Bang ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '<' -> (
            match peek state with
            | Some '=' ->
                ignore (advance state);
                let tok = token ~kind:Le ~line:start_line ~column:start_column () in
                loop (tok :: tokens)
            | _ ->
                let tok = token ~kind:Lt ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '>' -> (
            match peek state with
            | Some '=' ->
                ignore (advance state);
                let tok = token ~kind:Ge ~line:start_line ~column:start_column () in
                loop (tok :: tokens)
            | _ ->
                let tok = token ~kind:Gt ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '&' -> (
            match peek state with
            | Some '&' ->
                ignore (advance state);
                let tok = token ~kind:And_kw ~line:start_line ~column:start_column () in
                loop (tok :: tokens)
            | _ ->
                let tok = token ~kind:Ampersand ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '|' -> (
            match peek state with
            | Some '|' ->
                ignore (advance state);
                let tok = token ~kind:Or_kw ~line:start_line ~column:start_column () in
                loop (tok :: tokens)
            | _ ->
                let tok = token ~kind:Pipe ~line:start_line ~column:start_column () in
                loop (tok :: tokens))
        | '"' ->
            let string_token = read_string state start_line start_column in
            loop (string_token :: tokens)
        | '\'' ->
            let char_token = read_char state start_line start_column in
            loop (char_token :: tokens)
        | ch when is_alpha ch ->
            let ident_token = read_identifier state start_line start_column ch in
            loop (ident_token :: tokens)
        | ch when is_digit ch ->
            let num_token = read_number state start_line start_column ch in
            loop (num_token :: tokens)
        | ch ->
            let msg = Printf.sprintf "Unexpected character '%c' At line %d column %d" ch state.line state.column in
            raise (Lexing_error msg)
  in
  loop []
