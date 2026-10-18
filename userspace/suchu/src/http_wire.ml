(* HTTP/1.1 on the wire, with no sockets in it.

   What is written to a connection and what is read back from one, kept apart
   from the connection itself. That is what makes it testable without a network:
   a response is read from any function that hands over the next piece of bytes,
   so a test feeds it a string and a real connection feeds it a socket.

   It also keeps this module free of Unix and TLS, which means it compiles
   wherever the rest of the language does -- the web playground included, where
   there are no sockets to be had and 'import http' simply is not offered.

   Deliberately small. HTTP/1.1, one request per connection ('Connection:
   close'), no compression, no IPv6 literals, no proxies. Each of those is a
   refusal with a message, not a silent misreading. *)

exception Http_error of string

let fail format = Printf.ksprintf (fun message -> raise (Http_error message)) format

let lowercase = String.lowercase_ascii

let starts_with ~prefix text =
  String.length text >= String.length prefix
  && String.equal (String.sub text 0 (String.length prefix)) prefix

(* The most of a response held in memory. A server that sends more is either
   broken or hostile, and either way the program would rather be told than run
   out of memory. *)
let max_body = 64 * 1024 * 1024

let max_head = 64 * 1024

(* --- addresses ------------------------------------------------------------- *)

type url = {
  secure : bool;
  host : string;
  port : int;
  target : string;  (* the path and query, as it goes on the request line *)
}

let default_port secure = if secure then 443 else 80

let is_digits text = text <> "" && String.for_all (fun c -> c >= '0' && c <= '9') text

let parse_url text =
  let text = String.trim text in
  (* A space or a control character in an address ends up inside the request
     line, where it would start a second request. Refused at the door. *)
  String.iter
    (fun c ->
      if c <= ' ' || c = '\127' then fail "an address cannot contain spaces or control characters")
    text;
  let lower = lowercase text in
  let secure, rest =
    if starts_with ~prefix:"https://" lower then (true, String.sub text 8 (String.length text - 8))
    else if starts_with ~prefix:"http://" lower then
      (false, String.sub text 7 (String.length text - 7))
    else fail "'%s' is not an http:// or https:// address" text
  in
  (* The fragment is for the reader of the page; it is never sent. *)
  let rest = match String.index_opt rest '#' with Some i -> String.sub rest 0 i | None -> rest in
  let cut =
    let length = String.length rest in
    let rec find i =
      if i >= length then length else match rest.[i] with '/' | '?' -> i | _ -> find (i + 1)
    in
    find 0
  in
  let authority = String.sub rest 0 cut in
  let remainder = String.sub rest cut (String.length rest - cut) in
  let target =
    if remainder = "" then "/" else if remainder.[0] = '?' then "/" ^ remainder else remainder
  in
  if authority = "" then fail "'%s' has no host in it" text;
  if String.contains authority '@' then
    fail "an address with a user name or a password in it is not supported";
  if authority.[0] = '[' then fail "IPv6 addresses are not supported yet";
  let host, port =
    match String.rindex_opt authority ':' with
    | None -> (authority, default_port secure)
    | Some i ->
        let digits = String.sub authority (i + 1) (String.length authority - i - 1) in
        let port = if is_digits digits then int_of_string digits else 0 in
        if port < 1 || port > 65535 then fail "'%s' is not a valid port" digits;
        (String.sub authority 0 i, port)
  in
  if host = "" then fail "'%s' has no host in it" text;
  { secure; host = lowercase host; port; target }

(* What goes after 'Host:': the port only when it is not the scheme's own. *)
let host_header url =
  if url.port = default_port url.secure then url.host else Printf.sprintf "%s:%d" url.host url.port

let url_to_string url =
  Printf.sprintf "%s://%s%s" (if url.secure then "https" else "http") (host_header url) url.target

(* Where a 'Location' header points, as a full address. Servers send all three
   shapes: a whole address, a path on the same host, and a name relative to the
   page that was asked for. *)
let resolve_location base location =
  let lower = lowercase location in
  if starts_with ~prefix:"http://" lower || starts_with ~prefix:"https://" lower then location
  else if starts_with ~prefix:"//" location then
    (if base.secure then "https:" else "http:") ^ location
  else
    let origin = Printf.sprintf "%s://%s" (if base.secure then "https" else "http") (host_header base) in
    if starts_with ~prefix:"/" location then origin ^ location
    else
      let path =
        match String.index_opt base.target '?' with
        | Some i -> String.sub base.target 0 i
        | None -> base.target
      in
      let directory =
        match String.rindex_opt path '/' with
        | Some i -> String.sub path 0 (i + 1)
        | None -> "/"
      in
      origin ^ directory ^ location

(* --- the request ----------------------------------------------------------- *)

(* Set by http itself, and refused when the program tries to set them. Host and
   the framing headers because a wrong one corrupts the exchange; Accept-Encoding
   because a body that arrives compressed is one this cannot read. *)
let reserved_headers = [ "host"; "content-length"; "connection"; "transfer-encoding"; "accept-encoding" ]

let valid_header_name name =
  name <> ""
  && String.for_all
       (fun c -> c > ' ' && c < '\127' && c <> ':' && c <> '(' && c <> ')' && c <> '"')
       name

let build_request ~meth ~url ~headers ~body =
  let meth = String.uppercase_ascii meth in
  if meth = "" || not (String.for_all (fun c -> c >= 'A' && c <= 'Z') meth) then
    fail "'%s' is not an HTTP method" meth;
  List.iter
    (fun (name, value) ->
      if not (valid_header_name name) then fail "'%s' is not a valid header name" name;
      if String.contains value '\r' || String.contains value '\n' then
        fail "the %s header contains a line break" name;
      if List.mem (lowercase name) reserved_headers then
        fail "the %s header is set by http itself" name)
    headers;
  let given name = List.exists (fun (n, _) -> String.equal (lowercase n) name) headers in
  let buffer = Buffer.create 256 in
  let line text = Buffer.add_string buffer text; Buffer.add_string buffer "\r\n" in
  line (Printf.sprintf "%s %s HTTP/1.1" meth url.target);
  line ("Host: " ^ host_header url);
  if not (given "user-agent") then line "User-Agent: suchu";
  if not (given "accept") then line "Accept: */*";
  line "Accept-Encoding: identity";
  line "Connection: close";
  List.iter (fun (name, value) -> line (name ^ ": " ^ value)) headers;
  if body <> "" || List.mem meth [ "POST"; "PUT"; "PATCH" ] then
    line (Printf.sprintf "Content-Length: %d" (String.length body));
  line "";
  Buffer.add_string buffer body;
  Buffer.contents buffer

(* --- the response ---------------------------------------------------------- *)

type response = {
  status : int;
  reason : string;
  headers : (string * string) list;  (* names lowercased; repeats joined with ", " *)
  body : string;
}

let header response name = List.assoc_opt (lowercase name) response.headers

(* Bytes read from the wire and not yet used. [read] gives the next piece, and ""
   once the other side has closed. *)
type stream = {
  read : unit -> string;
  mutable data : string;
  mutable pos : int;
  mutable eof : bool;
}

let make_stream read = { read; data = ""; pos = 0; eof = false }

let available stream = String.length stream.data - stream.pos

(* One more piece from the wire, kept after whatever is unread. False at the end. *)
let refill stream =
  if stream.eof then false
  else begin
    let chunk = stream.read () in
    if chunk = "" then begin
      stream.eof <- true;
      false
    end
    else begin
      stream.data <- String.sub stream.data stream.pos (available stream) ^ chunk;
      stream.pos <- 0;
      true
    end
  end

let find_from text needle start =
  let length = String.length text and width = String.length needle in
  let matches_at i =
    let rec go k = k >= width || (text.[i + k] = needle.[k] && go (k + 1)) in
    go 0
  in
  let rec search i =
    if i + width > length then None else if matches_at i then Some i else search (i + 1)
  in
  search start

let rec read_line stream =
  match find_from stream.data "\r\n" stream.pos with
  | Some stop ->
      let line = String.sub stream.data stream.pos (stop - stream.pos) in
      stream.pos <- stop + 2;
      line
  | None ->
      if available stream > max_head then fail "a line in the response is too long";
      if refill stream then read_line stream
      else fail "the connection closed in the middle of the response"

(* Exactly [count] bytes, onto [out]. *)
let rec take stream count out =
  if count > 0 then begin
    if available stream = 0 && not (refill stream) then
      fail "the connection closed before the whole body arrived";
    let step = min count (available stream) in
    Buffer.add_substring out stream.data stream.pos step;
    stream.pos <- stream.pos + step;
    take stream (count - step) out
  end

let guard_size out =
  if Buffer.length out > max_body then
    fail "the response is larger than %d megabytes" (max_body / (1024 * 1024))

let rec take_everything stream out =
  Buffer.add_substring out stream.data stream.pos (available stream);
  stream.pos <- stream.pos + available stream;
  guard_size out;
  if refill stream then take_everything stream out

let parse_status line =
  match String.index_opt line ' ' with
  | None -> fail "'%s' is not an HTTP status line" line
  | Some i ->
      let version = String.sub line 0 i in
      let rest = String.sub line (i + 1) (String.length line - i - 1) in
      if not (starts_with ~prefix:"HTTP/1." version) then
        fail "the server answered with '%s', which is not HTTP/1.x" version;
      let code, reason =
        match String.index_opt rest ' ' with
        | Some j -> (String.sub rest 0 j, String.sub rest (j + 1) (String.length rest - j - 1))
        | None -> (rest, "")
      in
      if not (is_digits code) then fail "'%s' is not an HTTP status line" line;
      let status = int_of_string code in
      if status < 100 || status > 599 then fail "'%s' is not a status code" code;
      (status, reason)

let max_headers = 256

let read_headers stream =
  let rec go acc =
    match read_line stream with
    | "" -> List.rev acc
    | _ when List.length acc >= max_headers -> fail "the response has more than %d headers" max_headers
    | line -> (
        match String.index_opt line ':' with
        | None -> fail "'%s' is not a header" line
        | Some i ->
            let name = lowercase (String.trim (String.sub line 0 i)) in
            let value = String.trim (String.sub line (i + 1) (String.length line - i - 1)) in
            (* A repeated header is one header with a list of values. *)
            let merged =
              if List.mem_assoc name acc then
                List.map (fun (n, v) -> if String.equal n name then (n, v ^ ", " ^ value) else (n, v)) acc
              else (name, value) :: acc
            in
            go merged)
  in
  go []

let read_chunked stream out =
  let rec chunks () =
    let size_line = read_line stream in
    let digits =
      match String.index_opt size_line ';' with
      | Some i -> String.trim (String.sub size_line 0 i)
      | None -> String.trim size_line
    in
    let size =
      match int_of_string_opt ("0x" ^ digits) with
      | Some size when size >= 0 && digits <> "" -> size
      | _ -> fail "'%s' is not a chunk size" size_line
    in
    if size = 0 then
      (* Trailers, if any, up to the blank line that ends the message. *)
      let rec trailers () = if read_line stream <> "" then trailers () in
      trailers ()
    else begin
      take stream size out;
      guard_size out;
      if read_line stream <> "" then fail "a chunk was not followed by a line break";
      chunks ()
    end
  in
  chunks ()

let read_response ~meth stream =
  let status, reason = parse_status (read_line stream) in
  let headers = read_headers stream in
  let get name = List.assoc_opt name headers in
  (match get "content-encoding" with
  | Some encoding when lowercase encoding <> "identity" ->
      fail "the server compressed the body with '%s', which is not supported yet" encoding
  | _ -> ());
  let out = Buffer.create 4096 in
  let without_body =
    String.uppercase_ascii meth = "HEAD" || status = 204 || status = 304 || (status >= 100 && status < 200)
  in
  if not without_body then begin
    match get "transfer-encoding" with
    | Some encoding when lowercase encoding = "chunked" -> read_chunked stream out
    | Some encoding when lowercase encoding <> "identity" ->
        fail "the server used the transfer encoding '%s', which is not supported" encoding
    | _ -> (
        match get "content-length" with
        | Some text ->
            if not (is_digits (String.trim text)) then fail "'%s' is not a content length" text;
            let length = int_of_string (String.trim text) in
            if length > max_body then
              fail "the response is larger than %d megabytes" (max_body / (1024 * 1024));
            take stream length out
        | None -> take_everything stream out)
  end;
  { status; reason; headers; body = Buffer.contents out }
