(* OCaml 5 runtime on native Vex: startup, GC, exceptions, effects,
   domains, systhreads, Unix files and processes. Results go to stderr,
   which Vex mirrors to the serial log. *)

let failures = ref 0
let checks = ref 0

let check name cond =
  incr checks;
  if not cond then begin
    incr failures;
    Printf.eprintf "  FAIL %s\n%!" name
  end

(* The child role of the process tests below. *)
let () = if Array.length Sys.argv > 1 && Sys.argv.(1) = "child" then exit 17

(* Startup: argv, environment, executable name. *)
let () =
  check "argv" (Array.length Sys.argv >= 1 && Sys.argv.(0) <> "");
  check "os_type" (Sys.os_type = "Unix");
  check "word size" (Sys.word_size = 64);
  check "getenv absent" (Sys.getenv_opt "VEX_NO_SUCH_VARIABLE" = None)

(* Allocation and collection: minor and major heaps, finalisers, weak
   pointers, compaction. *)
let () =
  let keep = ref [] in
  for i = 1 to 200_000 do
    let block = Array.make (i mod 64 + 1) i in
    if i mod 1000 = 0 then keep := block :: !keep
  done;
  check "kept blocks" (List.length !keep = 200);
  let finalised = ref 0 in
  for _ = 1 to 100 do
    let r = ref 0 in
    Gc.finalise (fun _ -> incr finalised) r
  done;
  Gc.full_major ();
  check "finalisers ran" (!finalised > 0);
  let big = Bytes.make (64 * 1024 * 1024) 'x' in
  check "64 MiB string" (Bytes.length big = 64 * 1024 * 1024 && Bytes.get big 12345 = 'x');
  Gc.compact ();
  let stats = Gc.quick_stat () in
  check "gc counters" (stats.Gc.minor_collections > 0 && stats.Gc.major_collections > 0)

(* Exceptions, deep recursion, effects. *)
type _ Effect.t += Ask : int Effect.t

let () =
  check "exception" (try raise Exit with Exit -> true);
  check "division by zero" (try ignore (1 / (Sys.opaque_identity 0)); false
                            with Division_by_zero -> true);
  let rec depth n = if n = 0 then 0 else 1 + depth (n - 1) in
  check "deep recursion" (depth 1_000_000 = 1_000_000);
  check "stack overflow" (try let rec f n = 1 + f (n + 1) in ignore (f 0); false
                          with Stack_overflow -> true);
  let open Effect.Deep in
  let r = try_with (fun () -> Effect.perform Ask + Effect.perform Ask) ()
      { effc = fun (type a) (e : a Effect.t) ->
          match e with
          | Ask -> Some (fun (k : (a, _) continuation) -> continue k 21)
          | _ -> None } in
  check "effects" (r = 42)

(* Formatting and floats. *)
let () =
  check "printf" (Printf.sprintf "%d %s %.3f %x" 42 "vex" 3.14159 255 = "42 vex 3.142 ff");
  check "float_of_string" (float_of_string "0.1" = 0.1);
  check "string_of_float" (string_of_float 1e100 = "1e+100");
  check "Int64" (Int64.to_string Int64.max_int = "9223372036854775807")

(* Processes: fork, exec of this very program, pipes, exit codes. They run
   before any domain is spawned: OCaml 5 forbids Unix.fork after that. *)
let () =
  let pid = Unix.create_process Sys.executable_name [| Sys.executable_name; "child" |]
      Unix.stdin Unix.stdout Unix.stderr in
  (match Unix.waitpid [] pid with
   | _, Unix.WEXITED 17 -> check "create_process" true
   | _ -> check "create_process" false);
  let r, w = Unix.pipe () in
  (match Unix.fork () with
   | 0 -> ignore (Unix.write_substring w "from child" 0 10); Unix._exit 0
   | child ->
     Unix.close w;
     let buf = Bytes.create 32 in
     let n = Unix.read r buf 0 32 in
     ignore (Unix.waitpid [] child);
     check "fork + pipe" (Bytes.sub_string buf 0 n = "from child"))

(* Domains run in parallel on several CPUs. *)
let () =
  let n = Domain.recommended_domain_count () in
  Printf.eprintf "  recommended domains: %d\n%!" n;
  let work k = let s = ref 0 in for i = 1 to 5_000_000 do s := !s + (i mod (k + 2)) done; !s in
  let t0 = Unix.gettimeofday () in
  let ds = List.init 4 (fun k -> Domain.spawn (fun () -> work k)) in
  let results = List.map Domain.join ds in
  let t1 = Unix.gettimeofday () in
  check "domains" (List.length results = 4 && List.for_all (fun x -> x > 0) results);
  Printf.eprintf "  4 domains: %.0f ms\n%!" ((t1 -. t0) *. 1000.);
  let counter = Atomic.make 0 in
  let ds = List.init 4 (fun _ -> Domain.spawn (fun () ->
      for _ = 1 to 100_000 do Atomic.incr counter done)) in
  List.iter Domain.join ds;
  check "atomics" (Atomic.get counter = 400_000);
  let m = Mutex.create () and total = ref 0 in
  let ds = List.init 4 (fun _ -> Domain.spawn (fun () ->
      for _ = 1 to 10_000 do Mutex.lock m; incr total; Mutex.unlock m done)) in
  List.iter Domain.join ds;
  check "mutex across domains" (!total = 40_000)

(* Systhreads inside one domain. *)
let () =
  let c = Condition.create () and m = Mutex.create () and ready = ref 0 in
  let ts = List.init 8 (fun _ -> Thread.create (fun () ->
      Mutex.lock m; incr ready; Condition.broadcast c; Mutex.unlock m) ()) in
  List.iter Thread.join ts;
  check "threads" (!ready = 8);
  let t0 = Unix.gettimeofday () in
  Thread.delay 0.05;
  let waited = Unix.gettimeofday () -. t0 in
  if waited < 0.05 then Printf.eprintf "  Thread.delay 0.05 took %.6f s
%!" waited;
  check "Thread.delay" (waited >= 0.05)

(* Files and directories through Unix and Stdlib. *)
let () =
  let dir = "/tmp/vex-ocaml-test" in
  (try Unix.mkdir dir 0o755 with Unix.Unix_error (Unix.EEXIST, _, _) -> ());
  let path = Filename.concat dir "data.txt" in
  let oc = open_out path in
  for i = 1 to 1000 do Printf.fprintf oc "line %d\n" i done;
  close_out oc;
  let ic = open_in path in
  let n = ref 0 in
  (try while true do ignore (input_line ic); incr n done with End_of_file -> ());
  close_in ic;
  check "file lines" (!n = 1000);
  let st = Unix.stat path in
  check "stat size" (st.Unix.st_size = 8893);
  check "readdir" (Array.mem "data.txt" (Sys.readdir dir));
  let tmp = Filename.temp_file ~temp_dir:dir "vex" ".tmp" in
  check "temp_file" (Sys.file_exists tmp);
  Sys.remove tmp;
  Sys.remove path;
  Unix.rmdir dir;
  check "rmdir" (not (Sys.file_exists dir))

let () =
  Printf.eprintf "ocaml-runtime: %s (%d checks, %d failed)\n%!"
    (if !failures = 0 then "PASS" else "FAIL") !checks !failures;
  exit (if !failures = 0 then 0 else 1)
