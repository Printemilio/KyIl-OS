(* Native transport/Surface bindings only; application policy is Suchu. *)
open Runtime
external open_transport : unit -> unit = "kyil_browser_open"
external close_transport : unit -> unit = "kyil_browser_close"
external time : unit -> int = "kyil_browser_time"
external normalize : string -> string = "kyil_browser_normalize"
external downloads : unit -> (int * int * int * string * string * string * string * string) list = "kyil_browser_downloads"
external command : int -> int -> string -> int = "kyil_browser_command"
external input : int -> int array -> int = "kyil_browser_input"
external poll : unit -> (int * int * int * string * string * string) list = "kyil_browser_poll"
external events : unit -> (int * int * int * int * int * int) list = "kyil_browser_events"
external draw : int -> int array -> bool = "kyil_browser_draw"
let integer = Native_api.as_int
let text = Native_api.as_string
let values xs = V_list (list_of_values xs)
let opened = ref false
let close () = if !opened then (opened := false; close_transport ())
let utf8_prefix string count =
  let rec next offset left =
    if left=0 || offset=String.length string then offset else
    let c=Char.code string.[offset] in
    let bytes=if c<128 then 1 else if c<224 then 2 else if c<240 then 3 else 4 in
    next (min (String.length string) (offset+bytes)) (left-1)
  in String.sub string 0 (next 0 (max 0 count))
let register () =
  Gui_backend.register_close_callback close;
  Native_api.register_module "browser"
    [ Native_api.function0 "open" (fun () -> open_transport (); opened := true; V_null);
      Native_api.function0 "close" (fun () -> close (); V_null);
      Native_api.function0 "time" (fun () -> V_int (time ()));
      Native_api.function1 "normalize" (fun value ->
        try V_string (normalize (text value)) with Invalid_argument message -> raise (Runtime_error message));
      Native_api.function0 "downloads" (fun () -> downloads () |> List.map (fun (id,tab,status,received,total,name,path,error) ->
        values [V_int id;V_int tab;V_int status;V_string received;V_string total;V_string name;V_string path;V_string error]) |> values);
      Native_api.function2 "prefix" (fun value count -> V_string (utf8_prefix (text value) (integer count)));
      ("command",V_native (function
        | [op;id;uri] -> V_int (command (integer op) (integer id) (text uri))
        | _ -> raise (Runtime_error "browser.command expects operation, page id, URI")));
      Native_api.function2 "input" (fun id fields ->
        V_int (input (integer id) (Array.of_list (List.map integer (Native_api.as_list fields)))));
      Native_api.function2 "draw" (fun id rect ->
        V_bool (draw (integer id) (Array.of_list (List.map integer (Native_api.as_list rect)))));
      Native_api.function0 "poll" (fun () ->
        try poll () |> List.rev |> List.map (fun (op,id,flags,uri,title,error) ->
          values [V_int op;V_int id;V_int flags;V_string uri;V_string title;V_string error]) |> values
        with Failure message -> raise (Runtime_error message));
      Native_api.function0 "events" (fun () ->
        events () |> List.map (fun (kind,flags,x,y,w,h) ->
          values [V_int kind;V_int flags;V_int x;V_int y;V_int w;V_int h]) |> values);
    ]
