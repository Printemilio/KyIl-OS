(* Backend-neutral GUI tree for Suchu.

   Everything here turns the parser's raw [gui_block] into a typed tree and
   reads style properties out of it. None of it knows how anything is drawn,
   which is the point: Gui_backend (Bogue) and Gui_backend_raylib both consume
   this module, so the language surface is defined in exactly one place.

   Extracted from gui_backend.ml when the raylib backend was started. *)

open Ast
open Runtime

(* Alignment is reported symbolically; each backend maps it onto its own
   representation (Bogue's Draw.align, raylib's own layout pass). *)
type align =
  | AlignStart
  | AlignCenter
  | AlignEnd

(* A handler is a callable value taking the widget it fired on and the value
   that came with it, rather than a block of statements.

   It used to be the block, which the backend then ran through the evaluator
   against the environment captured when the window was built. That works while
   the program is being interpreted and cannot work once it is compiled: a
   handler reads the program's variables, and in compiled code those are OCaml
   refs with no environment to look them up in. Holding a value instead lets each
   side build one its own way -- the evaluator closes over the environment, the
   compiler closes over the refs -- while everything from here down stays the
   same. *)
type gui_handler = value

type gui_event =
  | GuiOnClick of gui_handler
  | GuiOnInput of gui_handler
  | GuiOnHover of gui_handler
  (* Fired by widgets whose value changes without text being typed: a checkbox
     being toggled, a slider being dragged, a select choosing an entry. *)
  | GuiOnChange of gui_handler
  (* Fired by a Timer, on its own schedule rather than on any user action.
     It is the only event that does not come from the keyboard or mouse. *)
  | GuiOnTick of gui_handler
  (* Fired by a Canvas, once per frame, while the frame is being drawn. The one
     event that runs inside the drawing rather than before it, because that is
     when the 'draw' verbs it calls have somewhere to put their shapes. *)
  | GuiOnDraw of gui_handler

type gui_styles = (string * gui_literal) list

type layout_orientation =
  | OrientationColumn
  | OrientationRow

type gui_window = {
  title : string;
  id : string option;
  width : int option;
  height : int option;
  orientation : layout_orientation;
  styles : gui_styles;
  children : gui_node list;
}

and gui_container = {
  id : string option;
  styles : gui_styles;
  children : gui_node list;
}

and gui_label = {
  id : string option;
  text : string;
  styles : gui_styles;
}

and gui_button = {
  id : string option;
  label : string;
  styles : gui_styles;
  events : gui_event list;
}

and gui_input = {
  id : string option;
  placeholder : string option;
  styles : gui_styles;
  events : gui_event list;
}

(* The leaf widgets added in 0.6 all have the same shape -- an optional handle,
   one string payload, styles and events -- so they share a record and are told
   apart by [kind] rather than by seven near-identical variants. *)
and gui_simple = {
  id : string option;
  kind : string;
  content : string;
  styles : gui_styles;
  events : gui_event list;
}

and gui_tabs = {
  id : string option;
  styles : gui_styles;
  pages : (string * gui_node) list;
}

and gui_shortcut = {
  key : string;
  events : gui_event list;
}

and gui_select = {
  id : string option;
  styles : gui_styles;
  options : string list;
  selected : int;
  events : gui_event list;
}

and gui_node =
  | GuiWindow of gui_window
  | GuiColumn of gui_container
  | GuiRow of gui_container
  (* A column clipped to a fixed height, with a scrollbar. *)
  | GuiScroll of gui_container
  | GuiLabel of gui_label
  | GuiButton of gui_button
  | GuiInput of gui_input
  | GuiSimple of gui_simple
  | GuiTabs of gui_tabs
  | GuiSelect of gui_select
  (* Invisible: it registers a key binding rather than drawing anything. *)
  | GuiShortcut of gui_shortcut
let lowercase s = String.lowercase_ascii s
let separate_props props =
  List.fold_left
    (fun (literals, events) (name, value) ->
      match value with
      | GuiPropLiteral lit -> ((name, lit) :: literals, events)
      | GuiPropEvent source -> (literals, (name, source) :: events))
    ([], [])
    props
  |> fun (literals, events) -> (List.rev literals, List.rev events)

let find_literal name literals =
  let target = lowercase name in
  List.find_map
    (fun (key, value) -> if lowercase key = target then Some value else None)
    literals

let rec find_first_literal names literals =
  match names with
  | [] -> None
  | key :: rest -> (
      match find_literal key literals with
      | Some literal -> Some literal
      | None -> find_first_literal rest literals)

let filter_out excluded literals =
  List.filter (fun (name, _) -> not (List.exists (fun key -> key = name) excluded)) literals

let round_to_int f =
  if f >= 0. then int_of_float (Float.floor (f +. 0.5))
  else int_of_float (Float.ceil (f -. 0.5))

let text_literal = function
  | GuiString s -> Some s
  | GuiIdent s -> Some s
  | _ -> None

let int_from_literal = function
  | GuiLength px -> Some px
  | GuiNumber f -> Some (round_to_int f)
  | GuiString s -> (try Some (int_of_string s) with _ -> None)
  | _ -> None

let parse_hex_color value =
  let hex =
    let raw =
      if String.length value > 0 && value.[0] = '#' then String.sub value 1 (String.length value - 1)
      else value
    in
    match String.length raw with
    | 3 ->
        let expand c = String.make 2 c in
        expand raw.[0] ^ expand raw.[1] ^ expand raw.[2]
    | 6 | 8 -> raw
    | _ -> raw
  in
  if String.length hex <> 6 && String.length hex <> 8 then None
  else
    try
      let r = int_of_string ("0x" ^ String.sub hex 0 2) in
      let g = int_of_string ("0x" ^ String.sub hex 2 2) in
      let b = int_of_string ("0x" ^ String.sub hex 4 2) in
      (* Eight digits end in the opacity: #ff804080 is half-strength orange. *)
      let a = if String.length hex = 8 then int_of_string ("0x" ^ String.sub hex 6 2) else 255 in
      Some (r, g, b, a)
    with _ -> None

let color_from_literal = function
  | GuiColor hex -> parse_hex_color hex
  | GuiString s when String.length s > 0 && s.[0] = '#' -> parse_hex_color s
  | _ -> None

(* Durations accept a bare number too, so 'interval: 100' keeps meaning 100
   milliseconds as it did before the units existed. *)
let duration_from_literal = function
  | GuiDuration ms -> Some ms
  | GuiLength px -> Some px
  | GuiNumber value -> Some (round_to_int value)
  | _ -> None

let literal_to_string = function
  | GuiString s -> s
  | GuiBool true -> "true"
  | GuiBool false -> "false"
  | GuiNumber f ->
      let int_candidate = int_of_float f in
      if abs_float (f -. float_of_int int_candidate) < 1e-9 then string_of_int int_candidate
      else string_of_float f
  | GuiLength px -> Printf.sprintf "%dpx" px
  | GuiDuration ms -> Printf.sprintf "%dms" ms
  | GuiPercent share ->
      let whole = int_of_float share in
      if abs_float (share -. float_of_int whole) < 1e-9 then Printf.sprintf "%d%%" whole
      else Printf.sprintf "%g%%" share
  | GuiColor hex -> hex
  | GuiIdent name -> name

let ensure_no_children element_name children =
  match children with
  | [] -> ()
  | _ ->
      raise (Runtime_error (Printf.sprintf "'%s' does not accept child elements" element_name))

let parse_event_block name source =
  try Parser.parse source with
  | Parser.Parse_error message ->
      raise (Runtime_error (Printf.sprintf "Invalid handler for '%s': %s" name message))

(* How a handler's text becomes something callable. The evaluator parses it and
   closes over the environment; the compiler has already translated it and only
   has to hand back the closure. Set by whoever is building the tree, just before
   they build it. *)
let build_handler : (string -> string -> gui_handler) ref =
  ref (fun name _ ->
      raise
        (Runtime_error (Printf.sprintf "No way to build the handler for '%s' was installed" name)))

let collect_events entries =
  let rec loop acc = function
    | [] -> List.rev acc
    | (name, source) :: rest ->
        let lower = lowercase name in
        let block = !build_handler lower source in
        let event =
          match lower with
          | "onclick" | "onpress" -> GuiOnClick block
          | "oninput" -> GuiOnInput block
          | "onhover" -> GuiOnHover block
          | "onchange" -> GuiOnChange block
          | "ontick" -> GuiOnTick block
          | "ondraw" -> GuiOnDraw block
          | _ ->
              raise
                (Runtime_error
                   (Printf.sprintf
                      "Unknown GUI event '%s'. Available events: onClick, onInput, onHover, \
                       onChange, onTick, onDraw"
                      name))
        in
        loop (event :: acc) rest
  in
  loop [] entries

let bool_from_literal = function
  | GuiBool value -> Some value
  | GuiIdent name -> (
      match lowercase name with
      | "true" | "yes" | "on" -> Some true
      | "false" | "no" | "off" -> Some false
      | _ -> None)
  | _ -> None

let orientation_of_string value =
  match lowercase value with
  | "row"
  | "horizontal" -> OrientationRow
  | _ -> OrientationColumn

let orientation_from_literals literals =
  match Option.bind (find_literal "layout" literals) text_literal with
  | Some value -> orientation_of_string value
  | None -> OrientationColumn

let id_from_args_and_props args literals =
  match Option.bind (find_literal "id" literals) text_literal with
  | Some id -> Some id
  | None -> (
      match args with
      | first :: _ -> text_literal first
      | [] -> None)
let rec convert_gui_block = function
  | GuiBlock (raw_name, args, props, children_blocks) ->
      let name = lowercase raw_name in
      let literal_props, event_entries = separate_props props in
      let children = List.map convert_gui_block children_blocks in
      let default_label_from_args () =
        match args with
        | first :: _ -> Option.value (text_literal first) ~default:(literal_to_string first)
        | [] -> raw_name
      in
      match name with
      | "window" ->
          if event_entries <> [] then
            raise (Runtime_error "window elements do not accept event handlers");
          let title =
            match args with
            | first :: _ -> Option.value (text_literal first) ~default:(literal_to_string first)
            | [] ->
                (match Option.bind (find_literal "title" literal_props) text_literal with
                | Some t -> t
                | None -> raw_name)
          in
          let width =
            let from_args =
              match args with
              | _ :: second :: _ -> int_from_literal second
              | _ -> None
            in
            match from_args with
            | Some value -> Some value
            | None -> Option.bind (find_literal "width" literal_props) int_from_literal
          in
          let height =
            let from_args =
              match args with
              | _ :: _ :: third :: _ -> int_from_literal third
              | _ -> None
            in
            match from_args with
            | Some value -> Some value
            | None -> Option.bind (find_literal "height" literal_props) int_from_literal
          in
          let id_opt = Option.bind (find_literal "id" literal_props) text_literal in
          let orientation = orientation_from_literals literal_props in
          let styles = filter_out [ "id"; "title"; "layout"; "width"; "height" ] literal_props in
          GuiWindow { title; id = id_opt; width; height; orientation; styles; children }
      | "column" ->
          if event_entries <> [] then raise (Runtime_error "column does not support events");
          let id_opt = id_from_args_and_props args literal_props in
          let styles = filter_out [ "id" ] literal_props in
          GuiColumn { id = id_opt; styles; children }
      | "row" ->
          if event_entries <> [] then raise (Runtime_error "row does not support events");
          let id_opt = id_from_args_and_props args literal_props in
          let styles = filter_out [ "id" ] literal_props in
          GuiRow { id = id_opt; styles; children }
      | "scroll" ->
          if event_entries <> [] then raise (Runtime_error "scroll does not support events");
          let id_opt = id_from_args_and_props args literal_props in
          let styles = filter_out [ "id" ] literal_props in
          GuiScroll { id = id_opt; styles; children }
      | "tabs" ->
          if event_entries <> [] then raise (Runtime_error "tabs does not support events");
          (* Each child must be a Tab, and its title comes from the child's own
             properties -- so the raw blocks are read here, before conversion. *)
          let pages =
            List.map
              (fun child ->
                let (GuiBlock (child_name, child_args, child_props, _)) = child in
                if lowercase child_name <> "tab" then
                  raise
                    (Runtime_error
                       (Printf.sprintf "Tabs only accepts Tab children, got '%s'" child_name));
                let child_literals, _ = separate_props child_props in
                let title =
                  match
                    Option.bind (find_first_literal [ "title"; "text" ] child_literals) text_literal
                  with
                  | Some value -> value
                  | None -> (
                      match child_args with
                      | first :: _ -> Option.value (text_literal first) ~default:"Tab"
                      | [] -> "Tab")
                in
                (title, convert_gui_block child))
              children_blocks
          in
          if pages = [] then raise (Runtime_error "Tabs needs at least one Tab child");
          let id_opt = Option.bind (find_literal "id" literal_props) text_literal in
          GuiTabs { id = id_opt; styles = filter_out [ "id" ] literal_props; pages }
      | "shortcut" ->
          ensure_no_children raw_name children_blocks;
          let key =
            match
              Option.bind (find_first_literal [ "key"; "keys"; "combo" ] literal_props) text_literal
            with
            | Some value -> value
            | None -> (
                match args with
                | first :: _ -> Option.value (text_literal first) ~default:""
                | [] -> raise (Runtime_error "Shortcut needs a key, e.g. key: \"Ctrl+S\";"))
          in
          if key = "" then raise (Runtime_error "Shortcut key cannot be empty");
          GuiShortcut { key; events = collect_events event_entries }
      | "option" ->
          (* Consumed by the enclosing Select, which reads the raw block for its
             text. Children are converted eagerly, so this still needs a case. *)
          ensure_no_children raw_name children_blocks;
          GuiLabel { id = None; text = ""; styles = [] }
      | "tab" ->
          (* Only reachable through Tabs, which strips the title itself. *)
          if event_entries <> [] then raise (Runtime_error "tab does not support events");
          let id_opt = Option.bind (find_literal "id" literal_props) text_literal in
          let styles = filter_out [ "id"; "title"; "text" ] literal_props in
          GuiColumn { id = id_opt; styles; children }
      | "select" ->
          let options =
            List.filter_map
              (fun child ->
                let (GuiBlock (child_name, child_args, child_props, _)) = child in
                if lowercase child_name <> "option" then
                  raise
                    (Runtime_error
                       (Printf.sprintf "Select only accepts Option children, got '%s'" child_name));
                let child_literals, _ = separate_props child_props in
                match
                  Option.bind (find_first_literal [ "text"; "label"; "value" ] child_literals)
                    text_literal
                with
                | Some value -> Some value
                | None -> (
                    match child_args with
                    | first :: _ -> text_literal first
                    | [] -> None))
              children_blocks
          in
          if options = [] then raise (Runtime_error "Select needs at least one Option child");
          let selected =
            Option.value (Option.bind (find_literal "selected" literal_props) int_from_literal)
              ~default:0
          in
          let id_opt = Option.bind (find_literal "id" literal_props) text_literal in
          GuiSelect
            {
              id = id_opt;
              styles = filter_out [ "id"; "selected" ] literal_props;
              options;
              selected = max 0 (min selected (List.length options - 1));
              events = collect_events event_entries;
            }
      | "text" | "html" | "checkbox" | "slider" | "image" | "box" | "spacer" | "timer" | "canvas"
      | "scene" ->
          ensure_no_children raw_name children_blocks;
          let content =
            match
              Option.bind
                (find_first_literal [ "text"; "content"; "src"; "label"; "value" ] literal_props)
                text_literal
            with
            | Some value -> value
            | None -> ( match args with first :: _ -> Option.value (text_literal first) ~default:"" | [] -> "")
          in
          let id_opt = Option.bind (find_literal "id" literal_props) text_literal in
          GuiSimple
            {
              id = id_opt;
              kind = name;
              content;
              styles = filter_out [ "id"; "text"; "content"; "src"; "label" ] literal_props;
              events = collect_events event_entries;
            }
      | "label" ->
          ensure_no_children raw_name children_blocks;
          if event_entries <> [] then raise (Runtime_error "label does not support events");
          let text =
            match Option.bind (find_first_literal [ "text"; "label"; "value"; "content" ] literal_props) text_literal with
            | Some value -> value
            | None -> default_label_from_args ()
          in
          let id_opt = Option.bind (find_literal "id" literal_props) text_literal in
          let styles = filter_out [ "id"; "text"; "label"; "value"; "content" ] literal_props in
          GuiLabel { id = id_opt; text; styles }
      | "button" ->
          ensure_no_children raw_name children_blocks;
          let label =
            match Option.bind (find_first_literal [ "text"; "label" ] literal_props) text_literal with
            | Some value -> value
            | None -> default_label_from_args ()
          in
          let id_opt = Option.bind (find_literal "id" literal_props) text_literal in
          let styles = filter_out [ "id"; "text"; "label" ] literal_props in
          let events = collect_events event_entries in
          GuiButton { id = id_opt; label; styles; events }
      | "input" ->
          ensure_no_children raw_name children_blocks;
          let id_opt = Option.bind (find_literal "id" literal_props) text_literal in
          let placeholder =
            match Option.bind (find_first_literal [ "placeholder"; "prompt"; "text" ] literal_props) text_literal with
            | Some value -> Some value
            | None -> (
                match args with
                | first :: _ -> text_literal first
                | [] -> None)
          in
          let styles = filter_out [ "id"; "placeholder"; "prompt"; "text" ] literal_props in
          let events = collect_events event_entries in
          GuiInput { id = id_opt; placeholder; styles; events }
      (* Previously any unrecognised name silently became a button, so a typo
         like `Labell { text: "x"; }` produced a stray button instead of an
         error. Name the mistake and list what is available. *)
      | _ ->
          raise
            (Runtime_error
               (Printf.sprintf
                  "Unknown GUI element '%s'. Available elements: Window, Row, Column, Scroll, \
                   Tabs, Tab, Select, Option, Label, Button, Input, Text, Html, Checkbox, Slider, \
                   Image, Box, Spacer, Timer, Shortcut"
                  raw_name))

let convert_gui_root block =
  match convert_gui_block block with
  | GuiWindow _ as window -> window
  | _ -> raise (Runtime_error "GUI statement must start with a window element")
let spacing_from_styles styles =
  Option.bind (find_first_literal [ "spacing"; "gap" ] styles) int_from_literal

let margins_from_styles styles =
  Option.bind (find_first_literal [ "padding"; "margin"; "margins" ] styles) int_from_literal

let align_of_name value =
  match lowercase value with
  | "start" | "left" | "min" -> Some AlignStart
  | "end" | "right" | "max" -> Some AlignEnd
  | "center" | "middle" -> Some AlignCenter
  | _ -> None

let align_from_styles styles =
  Option.bind
    (Option.bind
       (find_first_literal [ "align"; "alignment"; "justify-content" ] styles)
       text_literal)
    align_of_name

let text_align_from_styles styles =
  Option.bind
    (Option.bind (find_first_literal [ "text-align"; "textalign" ] styles) text_literal)
    align_of_name

let width_from_styles styles =
  Option.bind (find_first_literal [ "width" ] styles) int_from_literal

let height_from_styles styles =
  Option.bind (find_first_literal [ "height" ] styles) int_from_literal

let text_color_from_styles styles =
  Option.bind (find_first_literal [ "color"; "text-color" ] styles) color_from_literal

let font_size_from_styles styles =
  Option.bind (find_first_literal [ "font-size"; "size" ] styles) int_from_literal
let has_any_style names styles =
  List.exists
    (fun (name, _) -> List.exists (fun expected -> String.equal (lowercase name) expected) names)
    styles

let ensure_style names name value styles =
  if has_any_style names styles then styles else (name, value) :: styles

let modern_window_styles styles =
  styles
  |> ensure_style [ "background"; "background-color" ] "background" (GuiColor "#111827")
  |> ensure_style [ "padding"; "margin"; "margins" ] "padding" (GuiNumber 20.)
  |> ensure_style [ "spacing"; "gap" ] "spacing" (GuiNumber 12.)

let modern_input_styles styles =
  styles
  |> ensure_style [ "background"; "background-color" ] "background" (GuiColor "#24283b")
  |> ensure_style [ "border-radius"; "radius" ] "border-radius" (GuiNumber 10.)
  |> ensure_style [ "padding"; "margin"; "margins" ] "padding" (GuiNumber 12.)
  |> ensure_style [ "height" ] "height" (GuiNumber 46.)

let lighten_color amount (red, green, blue, alpha) =
  let lighten channel = min 255 (channel + amount) in
  (lighten red, lighten green, lighten blue, alpha)
