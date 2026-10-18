(* Domains under contention: stop-the-world collections while other
   domains spin, block or allocate, and repeated domain creation. Each step
   is reported, so that a hang shows where it happened. *)

let step name f =
  Printf.eprintf "  domain step: %s\n%!" name;
  let t0 = Unix.gettimeofday () in
  let r = f () in
  Printf.eprintf "  domain step: %s done (%.0f ms)\n%!" name ((Unix.gettimeofday () -. t0) *. 1000.);
  r

let () =
  let ok = ref true in
  for round = 1 to 3 do
    step (Printf.sprintf "spawn/join round %d" round) (fun () ->
        let ds = List.init 4 (fun k -> Domain.spawn (fun () -> k * 2)) in
        let r = List.map Domain.join ds in
        if r <> [0; 2; 4; 6] then ok := false)
  done;
  step "atomics without allocation" (fun () ->
      let c = Atomic.make 0 in
      let ds = List.init 4 (fun _ -> Domain.spawn (fun () ->
          for _ = 1 to 100_000 do Atomic.incr c done)) in
      List.iter Domain.join ds;
      if Atomic.get c <> 400_000 then ok := false);
  step "allocation in every domain (minor GCs, STW)" (fun () ->
      let ds = List.init 4 (fun k -> Domain.spawn (fun () ->
          let l = ref [] in
          for i = 1 to 200_000 do l := (i + k) :: !l; if i mod 1000 = 0 then l := [] done;
          List.length !l)) in
      List.iter (fun d -> ignore (Domain.join d)) ds);
  step "major GC while domains run" (fun () ->
      let ds = List.init 3 (fun _ -> Domain.spawn (fun () ->
          let a = Array.make 1000 [||] in
          for i = 1 to 20_000 do a.(i mod 1000) <- Array.make 100 i done;
          Array.length a)) in
      Gc.full_major ();
      List.iter (fun d -> ignore (Domain.join d)) ds);
  step "mutex across domains" (fun () ->
      let m = Mutex.create () and total = ref 0 in
      let ds = List.init 4 (fun _ -> Domain.spawn (fun () ->
          for _ = 1 to 10_000 do Mutex.lock m; incr total; Mutex.unlock m done)) in
      List.iter Domain.join ds;
      if !total <> 40_000 then ok := false);
  Printf.eprintf "ocaml-domains: %s\n%!" (if !ok then "PASS" else "FAIL");
  exit (if !ok then 0 else 1)
