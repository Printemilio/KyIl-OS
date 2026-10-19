(* The KyIlOS system palette.

   The desktop publishes it in /etc/kyil/theme.conf, one "name=#rrggbb" per
   line, and rewrites the file when the person switches between the dark and
   the light theme. A GUI property may name one of those tokens instead of a
   colour, and the window then follows the system:

     Window("Notes", 600px, 400px) { background: window; color: text; }
     Button { background: accent_soft; color: text; }

   draw.* accepts the same names ("muted", "accent"...), and the 'theme'
   module answers questions such as theme.is_dark().

   The file is read at most four times a second, so looking a token up on
   every frame costs a comparison. Where no file was published -- Suchu on
   another system -- the dark KyIlOS palette is used. *)

let path = "/etc/kyil/theme.conf"

let defaults =
  [
    ("mode", "dark");
    ("wallpaper_a", "#893597"); ("wallpaper_b", "#5d429b"); ("wallpaper_c", "#2f589c");
    ("desktop_text", "#ffffff");
    ("bar", "#141414"); ("bar_text", "#ffffff"); ("bar_icon", "#9c9c9c");
    ("window", "#141414"); ("panel", "#1b1a1f"); ("field", "#1a191e");
    ("field_border", "#4a3d57"); ("hover", "#26242c"); ("divider", "#483156");
    ("text", "#f2f2f2"); ("muted", "#9a93a6"); ("faint", "#6b6676");
    ("accent", "#a98bff"); ("accent_soft", "#34264a"); ("on_accent", "#ffffff");
    ("selection", "#4b3670"); ("success", "#44d16e"); ("warning", "#e8c547");
    ("danger", "#ff5a5f"); ("folder", "#c99914"); ("paper", "#d9d9d9");
    ("control_close", "#ff5f57"); ("control_minimize", "#febc2e");
    ("control_maximize", "#28c840"); ("control_idle", "#3a3940");
    ("accent2", "#5aa3fa"); ("border_active_a", "#9c7bff");
    ("border_active_b", "#5aa3fa"); ("border_inactive", "#2a2930");
  ]

let table : (string, string) Hashtbl.t = Hashtbl.create 64
let last_check = ref neg_infinity
let last_text = ref ""
let generation = ref 0

let load_defaults () = List.iter (fun (name, value) -> Hashtbl.replace table name value) defaults

let parse text =
  Hashtbl.reset table;
  load_defaults ();
  String.split_on_char '\n' text
  |> List.iter (fun line ->
         let line = String.trim line in
         if line <> "" && line.[0] <> '#' then
           match String.index_opt line '=' with
           | Some i ->
               let name = String.lowercase_ascii (String.trim (String.sub line 0 i)) in
               let value = String.trim (String.sub line (i + 1) (String.length line - i - 1)) in
               Hashtbl.replace table name value
           | None -> ());
  incr generation

let read_file () =
  try
    let channel = open_in_bin path in
    Fun.protect
      ~finally:(fun () -> close_in_noerr channel)
      (fun () -> Some (really_input_string channel (in_channel_length channel)))
  with _ -> None

let refresh () =
  let now = Unix.gettimeofday () in
  if now -. !last_check >= 0.25 then begin
    last_check := now;
    match read_file () with
    | Some text when text <> !last_text ->
        last_text := text;
        parse text
    | Some _ -> ()
    | None -> if Hashtbl.length table = 0 then (load_defaults (); incr generation)
  end

(* The raw value of a token: "#rrggbb" for colours, "dark"/"light" for mode. *)
let lookup name =
  refresh ();
  Hashtbl.find_opt table (String.lowercase_ascii name)

let mode () = Option.value (lookup "mode") ~default:"dark"

(* Bumped whenever the published palette changed, for caches built from it. *)
let current_generation () =
  refresh ();
  !generation
