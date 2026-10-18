(* Vex build: Cli is linked as a plain module (upstream: Suchu.Cli, from the
   wrapped dune library). *)
let () =
  let exit_code = Cli.main Sys.argv in
  exit exit_code
