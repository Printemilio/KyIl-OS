(* Build-time translation only. Declare available native modules; the emitted
 * binary registers their real target implementations before evaluating Suchu. *)
let () =
  Suchu_stdlib.register ();
  List.iter (fun name -> Runtime.register_native_module name [])
    ["browser";"draw";"keys";"mouse";"chrome";"platform";"fileio";"textbytes";"clipboard"];
  let source=Sys.argv.(1) and destination=Sys.argv.(2) in
  let channel=open_in_bin source in
  let text=Fun.protect ~finally:(fun () -> close_in channel)
    (fun () -> really_input_string channel (in_channel_length channel)) in
  let program=Parser.parse text in
  let generated=Transpiler.program ~directory:(Filename.dirname source) program in
  let channel=open_out_bin destination in
  Fun.protect ~finally:(fun () -> close_out channel) (fun () -> output_string channel generated)
