let fail name message =
  Printf.eprintf "[FAIL] %s: %s\n%!" name message;
  exit 1

let pass name = Printf.printf "[PASS] %s\n%!" name

let check name condition message =
  if condition then pass name else fail name message

let contains text needle =
  let rec loop index =
    if index + String.length needle > String.length text then false
    else if String.sub text index (String.length needle) = needle then true
    else loop (index + 1)
  in
  loop 0

let eval ?base_dir source =
  let interpreter = Interpreter.create ?base_dir () in
  Interpreter.run_program interpreter (Parser.parse source)

(* Shipped broken until 0.7: both operands were evaluated before the operator
   was consulted, so the commonest guard in programming -- check a length, then
   index -- raised instead of short-circuiting. *)
let test_short_circuit () =
  let guarded = "xs = []; if len(xs) > 0 && xs[0] == 1 { 1; } else { 2; }" in
  (match eval guarded with
  | Runtime.V_int 2 -> pass "&& short-circuits"
  | value -> fail "&& short-circuits" ("expected 2, got " ^ Runtime.value_to_string value));
  (* The right-hand side must not run at all, not merely be ignored. *)
  let counted = "n = 0; fun bump() { n = n + 1; return true; } false && bump(); true || bump(); n;" in
  match eval counted with
  | Runtime.V_int 0 -> pass "&& and || leave the right side alone"
  | value ->
      fail "&& and || leave the right side alone"
        ("the right-hand side ran; n = " ^ Runtime.value_to_string value)

(* 2 ** 3 used to be 8.0: the operator always went through floats. *)
let test_integer_power () =
  (match eval "2 ** 3;" with
  | Runtime.V_int 8 -> pass "integer power stays integer"
  | value -> fail "integer power stays integer" ("expected 8, got " ^ Runtime.value_to_string value));
  (match eval "2 ** 40;" with
  | Runtime.V_int 1099511627776 -> pass "integer power is exact"
  | value -> fail "integer power is exact" ("expected 1099511627776, got " ^ Runtime.value_to_string value));
  (* A negative exponent and an overflowing result both have to leave the integers. *)
  (match eval "2 ** (0 - 1);" with
  | Runtime.V_float f when Float.abs (f -. 0.5) < 1e-12 -> pass "negative exponent gives a float"
  | value -> fail "negative exponent gives a float" ("expected 0.5, got " ^ Runtime.value_to_string value));
  match eval "2 ** 200;" with
  | Runtime.V_float _ -> pass "overflowing power gives a float"
  | value -> fail "overflowing power gives a float" ("expected a float, got " ^ Runtime.value_to_string value)

let test_arithmetic () =
  match eval "2 + 3 * 4;" with
  | Runtime.V_int 14 -> pass "arithmetic"
  | value -> fail "arithmetic" ("expected 14, got " ^ Runtime.value_to_string value)

let test_closures () =
  let source =
    {|
fun make_adder(value) {
  return fun(other) { return value + other; };
}
add_five = make_adder(5);
add_five(7);
|}
  in
  match eval source with
  | Runtime.V_int 12 -> pass "closures"
  | value -> fail "closures" ("expected 12, got " ^ Runtime.value_to_string value)

let test_collections () =
  let source =
    {|
values = [3, 1, 2];
values.sort();
push(values, 4);
values.sum();
|}
  in
  match eval source with
  | Runtime.V_int 10 -> pass "collections"
  | value -> fail "collections" ("expected 10, got " ^ Runtime.value_to_string value)

let test_parse_location () =
  try
    ignore (Parser.parse "value = 1 +");
    fail "parse location" "invalid source was accepted"
  with
  | Parser.Parse_error message ->
      check "parse location" (contains message "line 1" && contains message "column 12")
        ("missing location in: " ^ message)

let test_file_helpers () =
  let path = Filename.temp_file "suchu-test-" ".txt" in
  Fun.protect
    ~finally:(fun () -> if Sys.file_exists path then Sys.remove path)
    (fun () ->
      let source =
        Printf.sprintf
          "write_file(%S, \"hello\");\nread_file(%S);"
          path path
      in
      match eval source with
      | Runtime.V_string "hello" -> pass "file helpers"
      | value -> fail "file helpers" ("unexpected value: " ^ Runtime.value_to_string value))

let test_truncate_utf8 () =
  match eval {|truncate("éééééééé", 9);|} with
  | Runtime.V_string text ->
      check "UTF-8 truncate" (String.equal text "ééé...")
        ("unexpected truncation: " ^ text)
  | value -> fail "UTF-8 truncate" ("unexpected value: " ^ Runtime.value_to_string value)

let with_temp_modules action =
  let marker = Filename.temp_file "suchu-modules-" ".tmp" in
  let directory = Filename.dirname marker in
  let prefix = Filename.basename marker |> Filename.chop_extension in
  Sys.remove marker;
  let path name = Filename.concat directory (prefix ^ "-" ^ name ^ ".suchu") in
  let write path contents =
    let channel = open_out_bin path in
    Fun.protect
      ~finally:(fun () -> close_out channel)
      (fun () -> output_string channel contents)
  in
  let created = ref [] in
  let create name contents =
    let module_path = path name in
    write module_path contents;
    created := module_path :: !created;
    module_path
  in
  Fun.protect
    ~finally:(fun () ->
      List.iter (fun file -> if Sys.file_exists file then Sys.remove file) !created)
    (fun () -> action directory prefix create)

let test_import_namespace () =
  with_temp_modules (fun directory prefix create ->
      ignore (create "math" "answer = 40 + 2;\nfun twice(value) { return value * 2; }");
      let module_name = prefix ^ "-math.suchu" in
      let alias = Runtime.module_alias module_name in
      let source =
        Printf.sprintf "import %S;\n%s.twice(%s.answer);"
          module_name alias alias
      in
      match eval ~base_dir:directory source with
      | Runtime.V_int 84 -> pass "import namespace"
      | value ->
          fail "import namespace"
            ("expected 84, got " ^ Runtime.value_to_string value))

let test_nested_import () =
  with_temp_modules (fun directory prefix create ->
      ignore (create "base" "value = 21;");
      ignore
        (create "feature"
           (let module_name = prefix ^ "-base.suchu" in
            Printf.sprintf "import %S;\nresult = %s.value * 2;"
              module_name (Runtime.module_alias module_name)));
      let module_name = prefix ^ "-feature.suchu" in
      let source =
        Printf.sprintf "import %S;\n%s.result;"
          module_name (Runtime.module_alias module_name)
      in
      match eval ~base_dir:directory source with
      | Runtime.V_int 42 -> pass "nested import"
      | value ->
          fail "nested import"
            ("expected 42, got " ^ Runtime.value_to_string value))

let test_circular_import () =
  with_temp_modules (fun directory prefix create ->
      ignore
        (create "a"
           (Printf.sprintf "import %S;\nvalue = 1;" (prefix ^ "-b.suchu")));
      ignore
        (create "b"
           (Printf.sprintf "import %S;\nvalue = 2;" (prefix ^ "-a.suchu")));
      try
        ignore
          (eval ~base_dir:directory
             (Printf.sprintf "import %S;" (prefix ^ "-a.suchu")));
        fail "circular import" "cycle was accepted"
      with
      | Runtime.Runtime_error message ->
          check "circular import" (contains message "Circular import")
            ("unexpected error: " ^ message))

let test_native_module () =
  Native_api.register_module "test_native"
    [
      Native_api.function2 "add" (fun left right ->
          Native_api.int (Native_api.as_int left + Native_api.as_int right));
      ("name", Native_api.string "OCaml");
    ];
  match eval "import test_native;\ntest_native.add(20, 22);" with
  | Runtime.V_int 42 -> pass "native module API"
  | value ->
      fail "native module API"
        ("expected 42, got " ^ Runtime.value_to_string value)

let test_standard_modules () =
  match
    eval
      "import json;\nimport system;\nencoded = json.encode([1, true, \"ok\"]);\n\
       decoded = json.decode(encoded);\n[encoded, decoded, system.os];"
  with
  | Runtime.V_list values -> (
      match Runtime.list_values values with
      | [ Runtime.V_string "[1,true,\"ok\"]"; Runtime.V_list decoded; Runtime.V_string os ] ->
          check "standard modules"
            (Runtime.list_length decoded = 3 && not (String.equal os ""))
            "unexpected decoded values or platform"
      | _ -> fail "standard modules" "unexpected module result")
  | value ->
      fail "standard modules"
        ("unexpected value: " ^ Runtime.value_to_string value)

(* Found by the differential suite on its first run: a function with no return
   handed back the value of its last statement, so 'fun f(n) { x = n * 2; }'
   answered with x. The documentation has always said none. *)
let test_falling_off_the_end () =
  (match eval "fun f(n) { x = n * 2; } f(4);" with
  | Runtime.V_null -> pass "a function without a return yields none"
  | value ->
      fail "a function without a return yields none"
        ("expected none, got " ^ Runtime.value_to_string value));
  (* The top level still has a value: the REPL prints it. *)
  match eval "1 + 1;" with
  | Runtime.V_int 2 -> pass "the top level still has a value"
  | value ->
      fail "the top level still has a value" ("expected 2, got " ^ Runtime.value_to_string value)

(* Also from the differential suite. Prefix '-' was compiled as 'zero minus',
   which turns -0.0 into 0.0; both sides now negate through one function. *)
let test_negation () =
  (match eval "0 - 7;" with
  | Runtime.V_int (-7) -> pass "integer negation"
  | value -> fail "integer negation" ("expected -7, got " ^ Runtime.value_to_string value));
  match eval "-0.0;" with
  | Runtime.V_float f when Float.sign_bit f -> pass "negative zero keeps its sign"
  | value ->
      fail "negative zero keeps its sign"
        ("expected -0.0, got " ^ Runtime.value_to_string value)

(* Suchu evaluates left to right. OCaml does not promise that for the arguments
   of a function, and in practice goes the other way, so the compiled form used
   to run the operands of every call and every operator backwards. *)
let test_evaluation_order () =
  let source =
    "order = \"\";\n\
     fun note(letter) { order = order @ letter; return 1; }\n\
     fun three(a, b, c) { return 0; }\n\
     three(note(\"a\"), note(\"b\"), note(\"c\"));\n\
     note(\"d\") + note(\"e\");\n\
     order;"
  in
  match eval source with
  | Runtime.V_string "abcde" -> pass "operands are evaluated left to right"
  | value ->
      fail "operands are evaluated left to right"
        ("expected \"abcde\", got " ^ Runtime.value_to_string value)

(* --- http: the wire format, with no network ---------------------------------

   Http_wire reads a response from any function that hands over the next bytes,
   so each of these feeds it a string. The splits are the point: a real socket
   hands over whatever arrived, which can end in the middle of a header, of a
   chunk size, or of a line break, and that is where a reader goes wrong. *)

let wire_fails name f =
  match f () with
  | _ -> fail name "expected an error and got a result"
  | exception Http_wire.Http_error message -> message

(* The pieces, one after another, then "" for the end. *)
let pieces_of chunks =
  let remaining = ref chunks in
  fun () ->
    match !remaining with
    | [] -> ""
    | chunk :: rest ->
        remaining := rest;
        chunk

(* The same text as one piece, as pieces of a given size, and a byte at a time. *)
let splits text =
  let cut size =
    let rec go start acc =
      if start >= String.length text then List.rev acc
      else go (start + size) (String.sub text start (min size (String.length text - start)) :: acc)
    in
    go 0 []
  in
  [ ("whole", [ text ]); ("in sevens", cut 7); ("byte by byte", cut 1) ]

let read ?(meth = "GET") chunks = Http_wire.read_response ~meth (Http_wire.make_stream (pieces_of chunks))

let test_http_urls () =
  let open Http_wire in
  let u = parse_url "https://Example.COM/a/b?x=1#frag" in
  check "http url: scheme, host, path and query"
    (u.secure && u.host = "example.com" && u.port = 443 && u.target = "/a/b?x=1")
    "https://Example.COM/a/b?x=1#frag was read wrongly";
  let u = parse_url "http://localhost:8080" in
  check "http url: a port, and no path"
    ((not u.secure) && u.host = "localhost" && u.port = 8080 && u.target = "/")
    "http://localhost:8080 was read wrongly";
  check "http url: a query with no path" ((parse_url "http://h?q=1").target = "/?q=1")
    "http://h?q=1 should ask for /?q=1";
  check "http url: the port is left out of Host when it is the default"
    (host_header (parse_url "https://h:443/") = "h" && host_header (parse_url "https://h:444/") = "h:444")
    "Host header wrong";
  List.iter
    (fun text ->
      ignore (wire_fails ("http url refused: " ^ text) (fun () -> parse_url text)))
    [ "ftp://example.com/"; "example.com"; "http://"; "http://h:0/"; "http://h:99999/"; "http://h:x/";
      "http://u:p@h/"; "http://[::1]/"; "http://h/a b"; "http://h/a\r\nHost: evil" ];
  pass "http url: bad addresses are refused"

let test_http_redirects_resolve () =
  let base = Http_wire.parse_url "https://h.example/a/b/page?q=1" in
  let resolve = Http_wire.resolve_location base in
  check "location: a whole address" (resolve "http://other/x" = "http://other/x") "absolute";
  check "location: a path on the same host" (resolve "/root" = "https://h.example/root") "rooted";
  check "location: relative to the page" (resolve "next" = "https://h.example/a/b/next") "relative";
  check "location: scheme-relative" (resolve "//cdn.example/x" = "https://cdn.example/x") "scheme-relative"

let test_http_request () =
  let url = Http_wire.parse_url "http://h:81/p?q=1" in
  let request = Http_wire.build_request ~meth:"get" ~url ~headers:[] ~body:"" in
  check "request: line, Host, and nothing it should not send"
    (contains request "GET /p?q=1 HTTP/1.1\r\nHost: h:81\r\n"
    && contains request "Connection: close\r\n"
    && not (contains request "Content-Length"))
    request;
  let post = Http_wire.build_request ~meth:"POST" ~url ~headers:[ ("Content-Type", "text/plain") ] ~body:"héllo" in
  check "request: a body has its length in bytes, not characters"
    (contains post "Content-Length: 6\r\n\r\nhéllo" && contains post "Content-Type: text/plain\r\n")
    post;
  check "request: an empty POST still says it is empty"
    (contains (Http_wire.build_request ~meth:"POST" ~url ~headers:[] ~body:"") "Content-Length: 0\r\n")
    "no Content-Length on an empty POST";
  check "request: a header of the program's own replaces a default"
    (let r = Http_wire.build_request ~meth:"GET" ~url ~headers:[ ("user-agent", "mine") ] ~body:"" in
     contains r "user-agent: mine" && not (contains r "User-Agent: suchu"))
    "default User-Agent was sent as well";
  List.iter
    (fun (label, headers) ->
      ignore (wire_fails ("request refused: " ^ label)
                (fun () -> Http_wire.build_request ~meth:"GET" ~url ~headers ~body:"")))
    [ ("a line break in a value", [ ("X", "a\r\nInjected: 1") ]);
      ("a colon in a name", [ ("X:Y", "v") ]);
      ("Host", [ ("Host", "evil") ]);
      ("Content-Length", [ ("content-length", "5") ]);
      ("Accept-Encoding", [ ("Accept-Encoding", "gzip") ]) ];
  pass "request: headers that would corrupt it are refused"

let test_http_responses () =
  let plain = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello" in
  List.iter
    (fun (label, chunks) ->
      let r = read chunks in
      check ("response: content-length, " ^ label)
        (r.status = 200 && r.reason = "OK" && r.body = "hello"
        && Http_wire.header r "Content-Type" = Some "text/plain")
        "status, body or header wrong")
    (splits plain);
  let chunked =
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n7;ext=1\r\n, world\r\n0\r\nX-Trailer: t\r\n\r\n"
  in
  List.iter
    (fun (label, chunks) ->
      check ("response: chunked, " ^ label) ((read chunks).body = "hello, world") "chunks not joined")
    (splits chunked);
  let until_close = "HTTP/1.0 200 OK\r\n\r\nno length, the end is the close" in
  List.iter
    (fun (label, chunks) ->
      check ("response: body until the connection closes, " ^ label)
        ((read chunks).body = "no length, the end is the close") "body cut short")
    (splits until_close);
  check "response: repeated headers become one list"
    (let r = read [ "HTTP/1.1 200 OK\r\nSet-Cookie: a=1\r\nset-cookie: b=2\r\nContent-Length: 0\r\n\r\n" ] in
     Http_wire.header r "set-cookie" = Some "a=1, b=2")
    "repeats not merged";
  check "response: HEAD, 204 and 304 have no body even when they name a length"
    ((read ~meth:"HEAD" [ "HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\n" ]).body = ""
    && (read [ "HTTP/1.1 204 No Content\r\nContent-Length: 50\r\n\r\n" ]).body = ""
    && (read [ "HTTP/1.1 304 Not Modified\r\n\r\n" ]).body = "")
    "a body was waited for";
  check "response: a body with no bytes is fine"
    ((read [ "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n" ]).body = "")
    "empty body"

let test_http_response_errors () =
  let fails label chunks =
    ignore (wire_fails ("response refused: " ^ label) (fun () -> read chunks))
  in
  fails "closed before the body is whole" [ "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort" ];
  fails "closed in the middle of the headers" [ "HTTP/1.1 200 OK\r\nContent-Ty" ];
  fails "nothing at all" [];
  fails "not HTTP" [ "SSH-2.0-OpenSSH\r\n" ];
  fails "a status that is not a number" [ "HTTP/1.1 abc OK\r\n\r\n" ];
  fails "a bad content length" [ "HTTP/1.1 200 OK\r\nContent-Length: ten\r\n\r\n" ];
  fails "a bad chunk size" [ "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nhello\r\n0\r\n\r\n" ];
  fails "a compressed body" [ "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 0\r\n\r\n" ];
  fails "a header with no colon" [ "HTTP/1.1 200 OK\r\nthis is wrong\r\n\r\n" ];
  fails "more headers than anyone sends"
    [ "HTTP/1.1 200 OK\r\n" ^ String.concat "" (List.init 300 (fun i -> Printf.sprintf "X-%d: v\r\n" i)) ^ "\r\n" ];
  pass "response: every way it can be wrong is an error, not a short read"

(* --- audio: what can be known without a sound card -------------------------

   A sine wave goes in and has to come out at the right frequency; tags are built
   byte by byte, the way a file would carry them, and read back. Nothing here
   plays anything. *)

let near a b tolerance = Float.abs (a -. b) <= tolerance

let sine_samples ~hertz ~rate ~seconds ~amplitude =
  let count = int_of_float (float_of_int rate *. seconds) in
  Array.init count (fun i -> amplitude *. sin (2. *. Float.pi *. hertz *. float_of_int i /. float_of_int rate))

let test_fft () =
  let n = 256 in
  let re = Array.init n (fun i -> sin (2. *. Float.pi *. 16. *. float_of_int i /. float_of_int n)) in
  let im = Array.make n 0. in
  Audio_analysis.fft re im;
  let magnitude k = sqrt ((re.(k) *. re.(k)) +. (im.(k) *. im.(k))) in
  check "fft: a sine lands in its own bin"
    (near (magnitude 16) (float_of_int n /. 2.) 0.001) "bin 16 is not n/2";
  let others = ref 0. in
  for k = 0 to (n / 2) - 1 do
    if k <> 16 then others := Float.max !others (magnitude k)
  done;
  check "fft: and nowhere else" (!others < 0.001) "energy leaked into other bins";
  (* A constant has all its energy at zero. *)
  let re = Array.make 8 1. and im = Array.make 8 0. in
  Audio_analysis.fft re im;
  check "fft: a constant is all in bin zero" (near re.(0) 8. 1e-9 && near re.(3) 0. 1e-9) "constant wrong";
  match Audio_analysis.fft (Array.make 6 0.) (Array.make 6 0.) with
  | () -> fail "fft: refuses a length that is not a power of two" "no error"
  | exception Invalid_argument _ -> pass "fft: refuses a length that is not a power of two"

let test_audio_bands () =
  List.iter
    (fun (bands, rate) ->
      let edges = Audio_analysis.band_edges ~bands ~sample_rate:rate ~size:1024 in
      let increasing = ref true in
      for i = 1 to bands do
        if edges.(i) <= edges.(i - 1) then increasing := false
      done;
      check
        (Printf.sprintf "bands: %d at %d Hz are ordered, each at least one bin, inside the transform" bands rate)
        (Array.length edges = bands + 1 && !increasing && edges.(bands) <= 512 && edges.(0) >= 1)
        "edges are wrong")
    [ (16, 44100); (48, 44100); (64, 48000); (32, 22050); (64, 8000) ]

let test_audio_analysis () =
  let rate = 44100 in
  let samples = sine_samples ~hertz:1000. ~rate ~seconds:1.0 ~amplitude:1.0 in
  let sample i = if i >= 0 && i < Array.length samples then samples.(i) else 0. in
  let result =
    Audio_analysis.analyze ~frame_count:(Array.length samples) ~sample_rate:rate ~sample ~fps:30 ~bands:32
      ~window:1024
  in
  check "analysis: one spectrum for each thirtieth of a second"
    (Array.length result.Audio_analysis.spectra = 30) "wrong number of moments";
  let row = result.Audio_analysis.spectra.(15) in
  let edges = Audio_analysis.band_edges ~bands:32 ~sample_rate:rate ~size:1024 in
  let bin = int_of_float (Float.round (1000. *. 1024. /. float_of_int rate)) in
  let band = ref 0 in
  for i = 0 to 31 do
    if bin >= edges.(i) && bin < edges.(i + 1) then band := i
  done;
  check "analysis: a full-scale 1 kHz sine reads as full level in the band that holds 1 kHz"
    (row.(!band) > 0.9) (Printf.sprintf "level %.3f" row.(!band));
  check "analysis: and well down in a band far from it" (row.(31) < 0.3 && row.(0) < 0.3)
    (Printf.sprintf "far bands %.3f and %.3f" row.(0) row.(31));
  check "analysis: peaks follow the amplitude"
    (Array.length result.Audio_analysis.peaks > 0 && near result.Audio_analysis.peaks.(10) 1.0 0.05)
    "peak of a full-scale sine is not 1";
  (* Silence is silence. *)
  let quiet =
    Audio_analysis.analyze ~frame_count:44100 ~sample_rate:rate ~sample:(fun _ -> 0.) ~fps:30 ~bands:16 ~window:1024
  in
  check "analysis: silence reads as zero everywhere"
    (Array.for_all (fun row -> Array.for_all (fun v -> v = 0.) row) quiet.Audio_analysis.spectra
    && Array.for_all (fun v -> v = 0.) quiet.Audio_analysis.peaks)
    "silence is not zero";
  (* Half the amplitude: peaks halve. *)
  let half = sine_samples ~hertz:440. ~rate ~seconds:1.0 ~amplitude:0.5 in
  let soft =
    Audio_analysis.analyze ~frame_count:(Array.length half) ~sample_rate:rate
      ~sample:(fun i -> if i >= 0 && i < Array.length half then half.(i) else 0.)
      ~fps:30 ~bands:16 ~window:1024
  in
  check "analysis: half the amplitude is half the peak" (near soft.Audio_analysis.peaks.(10) 0.5 0.05) "peak wrong"

let test_audio_lookups () =
  let result =
    { Audio_analysis.fps = 10; bands = 4;
      spectra = [| [| 0.1; 0.2; 0.3; 0.4 |]; [| 0.9; 0.0; 0.0; 0.0 |]; [| 0.0; 0.0; 0.0; 0.5 |] |];
      peaks = [| 0.1; 0.9; 0.1; 0.1; 0.2; 0.2 |] }
  in
  check "lookup: the spectrum at a moment is the nearest stored one"
    (Audio_analysis.spectrum_at result ~seconds:0.1 ~count:4 = [| 0.9; 0.0; 0.0; 0.0 |]) "wrong row";
  check "lookup: past the end it holds the last one, before the start the first"
    (Audio_analysis.spectrum_at result ~seconds:99. ~count:4 = [| 0.0; 0.0; 0.0; 0.5 |]
    && Audio_analysis.spectrum_at result ~seconds:(-5.) ~count:4 = [| 0.1; 0.2; 0.3; 0.4 |])
    "ends wrong";
  check "lookup: fewer bands keep the loudest of each group"
    (Audio_analysis.spectrum_at result ~seconds:0. ~count:2 = [| 0.2; 0.4 |]) "grouping wrong";
  check "lookup: peaks keep a thin spike when drawn smaller"
    (Audio_analysis.resample_peaks result.Audio_analysis.peaks 2 = [| 0.9; 0.2 |]) "spike lost";
  check "lookup: nothing analysed is quiet, not an error"
    (Audio_analysis.spectrum_at { result with Audio_analysis.spectra = [||] } ~seconds:1. ~count:3 = [| 0.; 0.; 0. |]
    && Audio_analysis.resample_peaks [||] 3 = [| 0.; 0.; 0. |])
    "empty analysis"

(* The bytes of a tag, the way a file holds them. *)
let synchsafe_bytes n =
  String.init 4 (fun i -> Char.chr ((n lsr (7 * (3 - i))) land 0x7f))

let big_endian_bytes n =
  String.init 4 (fun i -> Char.chr ((n lsr (8 * (3 - i))) land 0xff))

let frame23 ?(v4 = false) id body =
  id ^ (if v4 then synchsafe_bytes (String.length body) else big_endian_bytes (String.length body)) ^ "\000\000" ^ body

let utf16le text =
  let buffer = Buffer.create 16 in
  Buffer.add_string buffer "\xff\xfe";
  String.iter (fun c -> Buffer.add_char buffer c; Buffer.add_char buffer '\000') text;
  Buffer.contents buffer

let id3v2 ~version frames =
  let body = String.concat "" frames in
  "ID3" ^ String.make 1 (Char.chr version) ^ "\000\000" ^ synchsafe_bytes (String.length body) ^ body

let test_audio_picture () =
  let open Audio_analysis in
  let png = "\x89PNG\r\n\x1a\n" ^ "pixels" in
  let jpg = "\xff\xd8\xff\xe0" ^ "pixels" in
  let apic ?(v4 = false) ~encoding mime picture =
    frame23 ~v4 "APIC"
      (String.make 1 (Char.chr encoding) ^ mime ^ "\000" ^ "\003"
      ^ (if encoding = 1 then "\xff\xfeA\000\000\000" else "front\000")
      ^ picture)
  in
  let check_picture name tag expected =
    check name (parse_picture tag = expected)
      (match parse_picture tag with
      | Some (bytes, extension) -> Printf.sprintf "got %d bytes, %s" (String.length bytes) extension
      | None -> "got none")
  in
  check_picture "picture: APIC in ID3v2.3, a PNG after a text description"
    (id3v2 ~version:3 [ frame23 "TIT2" "\000x"; apic ~encoding:0 "image/png" png ])
    (Some (png, "png"));
  check_picture "picture: APIC in ID3v2.4 with a UTF-16 description"
    (id3v2 ~version:4 [ apic ~v4:true ~encoding:1 "image/jpeg" jpg ])
    (Some (jpg, "jpg"));
  check_picture "picture: PIC in ID3v2.2"
    (id3v2 ~version:2 [ "PIC\000\000\016" ^ "\000JPG\003\000" ^ jpg ])
    (Some (jpg, "jpg"));
  check_picture "picture: a tag without one" (id3v2 ~version:3 [ frame23 "TIT2" "\000x" ]) None;
  check_picture "picture: bytes that are no image"
    (id3v2 ~version:3 [ apic ~encoding:0 "image/png" "plain text, not a picture" ])
    None;
  let cut = id3v2 ~version:3 [ apic ~encoding:0 "image/png" png ] in
  check "picture: a tag cut off inside it does not raise"
    (try
       ignore (parse_picture (String.sub cut 0 (String.length cut - 4)));
       true
     with _ -> false)
    "raised";
  let red = 0xd02020 and blue = 0x2040c0 in
  let pixels = Array.init 100 (fun i -> if i < 60 then red else if i < 90 then blue else 0x101010) in
  let colours = palette_of_pixels pixels ~count:3 in
  check "palette: the two real colours, the more common first"
    (match colours with a :: b :: _ -> a lsr 20 = 0xd && b land 0xff >= 0xb0 | _ -> false)
    (String.concat " " (List.map (Printf.sprintf "%06x") colours));
  check "palette: an empty picture gives no colours" (palette_of_pixels [||] ~count:3 = []) "not empty"

let test_audio_formats () =
  let open Audio_analysis in
  let le32 n = String.init 4 (fun i -> Char.chr ((n lsr (8 * i)) land 0xff)) in
  let be32 n = String.init 4 (fun i -> Char.chr ((n lsr (8 * (3 - i))) land 0xff)) in
  let comment_block entries =
    le32 6 ^ "vendor" ^ le32 (List.length entries)
    ^ String.concat "" (List.map (fun e -> le32 (String.length e) ^ e) entries)
  in
  let block = comment_block [ "title=Caf\xc3\xa9"; "ARTIST=Ana"; "Album=Blue"; "DATE=2019-05-04"; "GENRE=x" ] in
  (match parse_vorbis_comment block with
  | Some t ->
      check "vorbis comments: names in any case, a date cut to its year"
        (t.title = "Caf\xc3\xa9" && t.artist = "Ana" && t.album = "Blue" && t.year = "2019")
        (Printf.sprintf "got %S %S %S %S" t.title t.artist t.album t.year)
  | None -> fail "vorbis comments" "nothing found");
  check "vorbis comments: cut off, or nothing"
    (try parse_vorbis_comment (String.sub block 0 20) = None && parse_vorbis_comment "" = None with _ -> false)
    "raised";
  let png = "\x89PNG\r\n\x1a\n" ^ "pixels" in
  let picture = be32 3 ^ be32 9 ^ "image/png" ^ be32 5 ^ "front" ^ be32 1 ^ be32 1 ^ be32 24 ^ be32 0 ^ be32 (String.length png) ^ png in
  check "flac picture: the bytes after the description" (parse_flac_picture picture = Some (png, "png")) "different";
  check "flac picture: cut off gives none" (parse_flac_picture (String.sub picture 0 30) = None) "not none";
  (match parse_ogg ("OggS\000\002junk\003vorbis" ^ block) with
  | Some t -> check "ogg: comments after the vorbis marker" (t.title = "Caf\xc3\xa9") t.title
  | None -> fail "ogg vorbis" "nothing found");
  (match parse_ogg ("OggS....OpusTags" ^ block) with
  | Some t -> check "ogg: comments after OpusTags" (t.artist = "Ana") t.artist
  | None -> fail "ogg opus" "nothing found");
  check "ogg: no comment header" (parse_ogg "OggS nothing here" = None) "found something";
  let info =
    "INFO" ^ "INAM" ^ le32 5 ^ "Song\000" ^ "\000" ^ "IART" ^ le32 4 ^ "Me\000\000" ^ "ICRD" ^ le32 10 ^ "2020-01-02"
  in
  (match parse_wav_info info with
  | Some t ->
      check "wav: INAM, IART and ICRD, with the padding byte after an odd size"
        (t.title = "Song" && t.artist = "Me" && t.year = "2020")
        (Printf.sprintf "got %S %S %S" t.title t.artist t.year)
  | None -> fail "wav info" "nothing found")

let test_audio_tags () =
  let open Audio_analysis in
  (match parse_id3v2 (id3v2 ~version:3 [ frame23 "TIT2" ("\000Caf\xe9 del Mar"); frame23 "TPE1" ("\001" ^ utf16le "Ana & Ben");
                                          frame23 "TALB" "\000Blue Hour"; frame23 "TYER" "\0002019" ]) with
  | Some t ->
      check "tags: ID3v2.3, latin-1 and UTF-16 text"
        (t.title = "Caf\xc3\xa9 del Mar" && t.artist = "Ana & Ben" && t.album = "Blue Hour" && t.year = "2019")
        (Printf.sprintf "got %S %S %S %S" t.title t.artist t.album t.year)
  | None -> fail "tags: ID3v2.3" "no tag found");
  (match parse_id3v2 (id3v2 ~version:4 [ frame23 ~v4:true "TIT2" "\003\xc3\xa9t\xc3\xa9"; frame23 ~v4:true "TDRC" "\0032021-05-04" ]) with
  | Some t ->
      check "tags: ID3v2.4, UTF-8 text, and a full date reduced to its year"
        (t.title = "\xc3\xa9t\xc3\xa9" && t.year = "2021") (Printf.sprintf "got %S %S" t.title t.year)
  | None -> fail "tags: ID3v2.4" "no tag found");
  (match parse_id3v2 (id3v2 ~version:2 [ "TT2\000\000\008\000Old one"; "TP1\000\000\003\000Me" ]) with
  | Some t -> check "tags: ID3v2.2, three-letter frames" (t.title = "Old one" && t.artist = "Me") "v2.2 wrong"
  | None -> fail "tags: ID3v2.2" "no tag found");
  let v1 =
    let pad s n = s ^ String.make (n - String.length s) '\000' in
    "TAG" ^ pad "Song" 30 ^ pad "Singer" 30 ^ pad "Disc" 30 ^ "1999" ^ String.make 31 '\000'
  in
  check "tags: ID3v1, the last 128 bytes"
    (String.length v1 = 128 && parse_id3v1 v1 = Some { title = "Song"; artist = "Singer"; album = "Disc"; year = "1999" })
    "v1 wrong";
  check "tags: version 2 wins and version 1 fills what it lacks"
    (merge_tags { no_tags with title = "New" } { title = "Old"; artist = "Fallback"; album = ""; year = "" }
     = { title = "New"; artist = "Fallback"; album = ""; year = "" })
    "merge wrong";
  check "tags: a file with none is no tags, not an error"
    (parse_id3v2 "not a tag at all" = None && parse_id3v1 (String.make 128 'x') = None && id3v2_size "RIFF" = None)
    "garbage was read as a tag";
  (* A tag cut off partway, as a half-downloaded file would be. *)
  let whole = id3v2 ~version:3 [ frame23 "TIT2" "\000A long enough title"; frame23 "TPE1" "\000Somebody" ] in
  let survived = ref true in
  for cut = 0 to String.length whole do
    match parse_id3v2 (String.sub whole 0 cut) with _ -> () | exception _ -> survived := false
  done;
  check "tags: a tag cut off at any byte is read as far as it goes, never an exception" !survived "an exception escaped"

let () =
  test_arithmetic ();
  test_short_circuit ();
  test_integer_power ();
  test_falling_off_the_end ();
  test_negation ();
  test_evaluation_order ();
  test_closures ();
  test_collections ();
  test_parse_location ();
  test_file_helpers ();
  test_truncate_utf8 ();
  test_import_namespace ();
  test_nested_import ();
  test_circular_import ();
  test_native_module ();
  test_standard_modules ();
  test_http_urls ();
  test_http_redirects_resolve ();
  test_http_request ();
  test_http_responses ();
  test_http_response_errors ();
  test_fft ();
  test_audio_bands ();
  test_audio_analysis ();
  test_audio_lookups ();
  test_audio_tags ();
  test_audio_picture ();
  test_audio_formats ();
  print_endline "All Suchu tests passed."
