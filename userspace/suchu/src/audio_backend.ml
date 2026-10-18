(* The 'audio' module: playing music, and knowing what it looks like.

   raylib does the sound. It reads MP3, WAV, OGG, FLAC, XM and MOD, plays them
   through the machine's audio device and answers where it is in them. What it does
   not give is any way to hear what is being played, so the picture of a track --
   its waveform, and its spectrum at each moment -- is worked out ahead of time
   from the decoded samples (Audio_analysis) and looked up as it plays.

   A track is a stream, not a sample: it is read from the file as it plays, so a
   long track costs no memory until it is analysed, and any number can be open.
   A stream has to be fed every so often or it runs dry and stutters, which is
   what 'update' does. A window does it by itself on every frame; a program with
   no window calls audio.update() in its own loop. *)

open Runtime
module R = Raylib
module A = Audio_analysis

type state = Stopped | Playing | Paused

type track = {
  path : string;
  music : R.Music.t;
  length : float;
  mutable state : state;
  mutable finished : bool;  (* it reached its end by itself, and nobody has asked since *)
  mutable analysis : A.analysis option;
}

let tracks : track list ref = ref []

let formats = [ ".mp3"; ".wav"; ".ogg"; ".flac"; ".xm"; ".mod" ]

(* Analysing a track decodes all of it into memory, at four bytes a sample. Past
   this length that is gigabytes, for something drawn a few hundred pixels wide. *)
let longest_analysis = 20. *. 60.

let fail verb message = raise (Runtime_error (Printf.sprintf "audio.%s: %s" verb message))

(* --- file names ------------------------------------------------------------- *)

(* The name of a file in the form raylib can open. OCaml opens a UTF-8 name
   correctly; raylib does not, on Windows, and "Café été.mp3" would be a file it
   cannot find. The stub turns it into the machine's own code page, or its 8.3
   short name when a character does not fit there. See suchu_path_stubs.c. *)
external ansi_path : string -> string option = "suchu_ansi_path"

let is_ascii text = String.for_all (fun c -> Char.code c < 128) text

let raylib_path path =
  if Sys.win32 && not (is_ascii path) then
    match ansi_path path with
    | Some usable -> usable
    | None -> path
  else path

(* --- the device ------------------------------------------------------------- *)

let shutdown () =
  List.iter (fun track -> R.unload_music_stream track.music) !tracks;
  tracks := [];
  if R.is_audio_device_ready () then R.close_audio_device ()

let exit_hook_set = ref false

(* Opened when the first track is, not at start: a program that never plays
   anything does not claim the sound card. *)
let ensure_device verb =
  if not (R.is_audio_device_ready ()) then begin
    R.init_audio_device ();
    if not (R.is_audio_device_ready ()) then
      fail verb "no audio device could be opened. Is there a speaker or headphones, and is another program holding it?"
  end;
  if not !exit_hook_set then begin
    exit_hook_set := true;
    at_exit shutdown
  end

(* --- feeding the streams ---------------------------------------------------- *)

let update_all () =
  List.iter
    (fun track ->
      if track.state = Playing then begin
        R.update_music_stream track.music;
        (* raylib stops a stream that reached its end, and puts it back at the
           start. Playing in our books and not playing in its means it ended. *)
        if not (R.is_music_stream_playing track.music) then begin
          track.state <- Stopped;
          track.finished <- true
        end
      end)
    !tracks

(* --- values ----------------------------------------------------------------- *)

let track_of verb value =
  match value with
  | V_object _ -> expect_object (Printf.sprintf "audio.%s" verb) "track" value
  | other ->
      fail verb (Printf.sprintf "expects a track from audio.open, got %s" (describe_value other))

let number verb value =
  match value with
  | V_int n -> float_of_int n
  | V_float f -> f
  | other -> fail verb (Printf.sprintf "expects a number, got %s" (describe_value other))

let clamp low high value = Float.max low (Float.min high value)

let floats_value values =
  V_list (list_of_values (Array.to_list (Array.map (fun v -> V_float v) values)))

(* --- opening ---------------------------------------------------------------- *)

let open_track path =
  ensure_device "open";
  if not (Sys.file_exists path) then fail "open" (Printf.sprintf "there is no file '%s'" path);
  let music = R.load_music_stream (raylib_path path) in
  if not (R.is_music_valid music) then
    fail "open"
      (Printf.sprintf "'%s' could not be read as audio. MP3, WAV, OGG, FLAC, XM and MOD files can be" path);
  (* raylib loops a stream unless told otherwise. A player has to know when a track
     ends, and repeating is its own decision. *)
  R.Music.set_looping music false;
  let track =
    { path; music; length = R.get_music_time_length music; state = Stopped; finished = false; analysis = None }
  in
  tracks := track :: !tracks;
  track

let close_track track =
  R.unload_music_stream track.music;
  tracks := List.filter (fun other -> other != track) !tracks

(* --- analysing -------------------------------------------------------------- *)

(* The number of frames in a Wave. The bindings expose its rate, its width and its
   channels but not its length, which is the first field of the C struct, an
   unsigned int. It is read from there, and the field after it -- the sample rate,
   which the bindings do give -- is read as well and has to agree, so that a layout
   that is not what this assumes is an error here and not a read past the end of the
   samples. *)
let wave_frame_count wave =
  let fields = Ctypes.(coerce (ptr R.Wave.t) (ptr uint) (addr wave)) in
  let frames = Unsigned.UInt.to_int Ctypes.(!@fields) in
  let rate_in_struct = Unsigned.UInt.to_int Ctypes.(!@(fields +@ 1)) in
  let rate = Unsigned.UInt.to_int (R.Wave.sample_rate wave) in
  if rate_in_struct <> rate || frames < 0 then
    fail "analyze" "this build of raylib lays its audio data out differently from what was expected";
  frames

let analyze track =
  match track.analysis with
  | Some analysis -> analysis
  | None ->
      if track.length > longest_analysis then
        fail "analyze"
          (Printf.sprintf "this track is %d minutes long, and analysis stops at %d" (int_of_float (track.length /. 60.))
             (int_of_float (longest_analysis /. 60.)));
      let wave = R.load_wave (raylib_path track.path) in
      if not (R.is_wave_valid wave) then fail "analyze" (Printf.sprintf "'%s' could not be decoded" track.path);
      let channels = max 1 (Unsigned.UInt.to_int (R.Wave.channels wave)) in
      let rate = Unsigned.UInt.to_int (R.Wave.sample_rate wave) in
      let frames = wave_frame_count wave in
      let samples = R.load_wave_samples wave in
      let sample frame =
        if frame < 0 || frame >= frames then 0.
        else if channels = 1 then Ctypes.(!@(samples +@ frame))
        else begin
          let total = ref 0. in
          for channel = 0 to channels - 1 do
            total := !total +. Ctypes.(!@(samples +@ ((frame * channels) + channel)))
          done;
          !total /. float_of_int channels
        end
      in
      let analysis =
        A.analyze ~frame_count:frames ~sample_rate:rate ~sample ~fps:30 ~bands:64 ~window:1024
      in
      R.unload_wave_samples samples;
      R.unload_wave wave;
      track.analysis <- Some analysis;
      analysis

(* --- tags ------------------------------------------------------------------- *)

(* How much of a tag is read. A tag can be megabytes, nearly all of it the cover
   picture, and the text fields come before the picture in practice. Reading the
   first half megabyte finds them, and a tag cut off there is read as far as it goes
   (Audio_analysis never raises on a short tag). Across a library of thousands of
   files the difference is gigabytes not read. *)
let biggest_tag = 512 * 1024

let read_id3_tags path =
  match open_in_bin path with
  | exception Sys_error message -> fail "tags" message
  | channel ->
      Fun.protect
        ~finally:(fun () -> close_in channel)
        (fun () ->
          let length = in_channel_length channel in
          let from_start =
            try
              let header = really_input_string channel (min 10 length) in
              match A.id3v2_size header with
              | Some size when size <= length ->
                  seek_in channel 0;
                  A.parse_id3v2 (really_input_string channel (min size biggest_tag))
              | _ -> None
            with End_of_file | Sys_error _ -> None
          in
          let from_end =
            try
              if length >= 128 then begin
                seek_in channel (length - 128);
                A.parse_id3v1 (really_input_string channel 128)
              end
              else None
            with End_of_file | Sys_error _ -> None
          in
          match (from_start, from_end) with
          | Some a, Some b -> A.merge_tags a b
          | Some a, None -> a
          | None, Some b -> b
          | None, None -> A.no_tags)

(* FLAC: a run of metadata blocks after "fLaC", each with a one-byte kind (the top
   bit marks the last), a 24-bit length and its body. The comments are kind 4 and the
   picture kind 6; anything else is stepped over without being read. *)
let flac_scan path ~want_picture =
  match open_in_bin path with
  | exception Sys_error message -> fail "tags" message
  | channel ->
      Fun.protect
        ~finally:(fun () -> close_in channel)
        (fun () ->
          let tags = ref None and picture = ref None in
          (try
             if really_input_string channel 4 = "fLaC" then begin
               let continue = ref true in
               while !continue do
                 let header = really_input_string channel 4 in
                 let kind = Char.code header.[0] land 0x7f in
                 let last = Char.code header.[0] land 0x80 <> 0 in
                 let length = (Char.code header.[1] lsl 16) lor (Char.code header.[2] lsl 8) lor Char.code header.[3] in
                 if kind = 4 && length <= 1024 * 1024 then tags := A.parse_vorbis_comment (really_input_string channel length)
                 else if kind = 6 && want_picture && length <= 16 * 1024 * 1024 then
                   picture := A.parse_flac_picture (really_input_string channel length)
                 else seek_in channel (pos_in channel + length);
                 if last || (!tags <> None && (not want_picture || !picture <> None)) then continue := false
               done
             end
           with End_of_file | Sys_error _ -> ());
          (!tags, !picture))

(* A WAV's chunks, read one header at a time; the LIST chunk of kind INFO holds the
   names, and in practice it sits after the sound. *)
let wav_tags path =
  match open_in_bin path with
  | exception Sys_error message -> fail "tags" message
  | channel ->
      Fun.protect
        ~finally:(fun () -> close_in channel)
        (fun () ->
          let found = ref None in
          (try
             let riff = really_input_string channel 12 in
             if String.sub riff 0 4 = "RIFF" && String.sub riff 8 4 = "WAVE" then begin
               while !found = None do
                 let header = really_input_string channel 8 in
                 let size = A.little_endian32 header 4 in
                 if String.sub header 0 4 = "LIST" && size > 4 && size <= 65536 then begin
                   let body = really_input_string channel size in
                   if String.sub body 0 4 = "INFO" then found := A.parse_wav_info body;
                   if !found = None then seek_in channel (pos_in channel + (size land 1))
                 end
                 else seek_in channel (pos_in channel + size + (size land 1))
               done
             end
           with End_of_file | Sys_error _ | Invalid_argument _ -> ());
          !found)

let lowercase_extension path = String.lowercase_ascii (Filename.extension path)

(* Whatever the file says about itself, by what kind of file it is. *)
let read_tags path =
  match lowercase_extension path with
  | ".flac" -> Option.value (fst (flac_scan path ~want_picture:false)) ~default:A.no_tags
  | ".ogg" | ".opus" -> (
      match open_in_bin path with
      | exception Sys_error message -> fail "tags" message
      | channel ->
          Fun.protect
            ~finally:(fun () -> close_in channel)
            (fun () ->
              let data = try really_input_string channel (min 65536 (in_channel_length channel)) with End_of_file -> "" in
              Option.value (A.parse_ogg data) ~default:A.no_tags))
  | ".wav" -> Option.value (wav_tags path) ~default:A.no_tags
  | _ -> read_id3_tags path

let tags_value (tags : A.tags) =
  let fields = ref [] in
  record_set fields "title" (V_string tags.title);
  record_set fields "artist" (V_string tags.artist);
  record_set fields "album" (V_string tags.album);
  record_set fields "year" (V_string tags.year);
  V_record fields

(* --- the cover picture -------------------------------------------------------- *)

(* The most of a tag read looking for its picture. Covers are a few hundred
   kilobytes; a tag larger than this has something else in it. *)
let biggest_cover_tag = 16 * 1024 * 1024

let cover_folder () = Filename.concat (Filename.get_temp_dir_name ()) "suchu_covers"

(* The picture inside a track, written to a file the gui can load and kept there,
   named after the track so it is extracted once. "" when the track carries none. *)
let cover_of_id3 path =
  match open_in_bin path with
  | exception Sys_error message -> fail "cover" message
  | channel ->
      let picture =
        Fun.protect
          ~finally:(fun () -> close_in channel)
          (fun () ->
            try
              let length = in_channel_length channel in
              let header = really_input_string channel (min 10 length) in
              match A.id3v2_size header with
              | Some size when size <= length && size <= biggest_cover_tag ->
                  seek_in channel 0;
                  A.parse_picture (really_input_string channel size)
              | _ -> None
            with End_of_file | Sys_error _ -> None)
      in
      match picture with
      | None -> ""
      | Some (bytes, extension) -> (
          let folder = cover_folder () in
          let name = Digest.to_hex (Digest.string (path ^ string_of_int (String.length bytes))) in
          let target = Filename.concat folder (name ^ "." ^ extension) in
          try
            if not (Sys.file_exists folder) then Sys.mkdir folder 0o755;
            if not (Sys.file_exists target) then begin
              let out = open_out_bin target in
              Fun.protect ~finally:(fun () -> close_out out) (fun () -> output_string out bytes)
            end;
            target
          with Sys_error _ -> "")

let write_cover path (bytes, extension) =
  let folder = cover_folder () in
  let name = Digest.to_hex (Digest.string (path ^ string_of_int (String.length bytes))) in
  let target = Filename.concat folder (name ^ "." ^ extension) in
  try
    if not (Sys.file_exists folder) then Sys.mkdir folder 0o755;
    if not (Sys.file_exists target) then begin
      let out = open_out_bin target in
      Fun.protect ~finally:(fun () -> close_out out) (fun () -> output_string out bytes)
    end;
    target
  with Sys_error _ -> ""

(* The picture inside a track: an ID3 tag's for most files, a FLAC's own block for FLAC. *)
let cover_of path =
  if String.equal (lowercase_extension path) ".flac" then
    match snd (flac_scan path ~want_picture:true) with
    | Some picture -> write_cover path picture
    | None -> ""
  else cover_of_id3 path

(* The colours of a picture file, most prominent first, as numbers. A grid of at
   most 32 by 32 samples is looked at: enough to know the colours of a cover,
   and the same cost for a 4000-pixel scan as for a thumbnail. *)
let palette_of path count =
  if not (Sys.file_exists path) then fail "palette" (Printf.sprintf "no such file: %s" path)
  else
    let image = R.load_image path in
    if R.Image.width image <= 0 then []
    else begin
      let w = R.Image.width image and h = R.Image.height image in
      let columns = min 32 w and rows = min 32 h in
      let pixels = Array.make (columns * rows) 0 in
      for row = 0 to rows - 1 do
        for column = 0 to columns - 1 do
          let color = R.get_image_color image (column * w / columns) (row * h / rows) in
          pixels.((row * columns) + column) <-
            (R.Color.r color lsl 16) lor (R.Color.g color lsl 8) lor R.Color.b color
        done
      done;
      R.unload_image image;
      A.palette_of_pixels pixels ~count
    end

(* --- the module ------------------------------------------------------------- *)

let register () =
  let one verb handler =
    ( verb,
      V_native
        (function
        | [ value ] -> handler value
        | _ -> fail verb "expects one argument") )
  in
  let two verb handler =
    ( verb,
      V_native
        (function
        | [ a; b ] -> handler a b
        | _ -> fail verb "expects two arguments") )
  in
  register_native_module "audio"
    [
      (* The file formats it can play, as the extensions a file name ends in. *)
      ("formats", V_native (fun _ -> V_list (list_of_values (List.map (fun f -> V_string f) formats))));
      one "open" (function
        | V_string path -> make_object "track" (open_track path)
        | other -> fail "open" (Printf.sprintf "expects a file name, got %s" (describe_value other)));
      one "close" (fun value ->
          close_track (track_of "close" value);
          V_null);
      (* From the start, or from where it was paused. Playing one that is already
         playing does nothing. *)
      one "play" (fun value ->
          let track = track_of "play" value in
          (match track.state with
          | Playing -> ()
          | Paused -> R.resume_music_stream track.music
          | Stopped -> R.play_music_stream track.music);
          track.state <- Playing;
          track.finished <- false;
          V_null);
      one "pause" (fun value ->
          let track = track_of "pause" value in
          if track.state = Playing then begin
            R.pause_music_stream track.music;
            track.state <- Paused
          end;
          V_null);
      (* Back to the start, not playing. *)
      one "stop" (fun value ->
          let track = track_of "stop" value in
          R.stop_music_stream track.music;
          track.state <- Stopped;
          track.finished <- false;
          V_null);
      one "playing" (fun value -> V_bool ((track_of "playing" value).state = Playing));
      one "paused" (fun value -> V_bool ((track_of "paused" value).state = Paused));
      (* True once, when a track has reached its end by itself. Asking is what
         clears it, so a loop that moves on to the next track does not move on
         twice. *)
      one "finished" (fun value ->
          let track = track_of "finished" value in
          let was = track.finished in
          track.finished <- false;
          V_bool was);
      two "seek" (fun value seconds ->
          let track = track_of "seek" value in
          R.seek_music_stream track.music (clamp 0. track.length (number "seek" seconds));
          V_null);
      one "position" (fun value ->
          let track = track_of "position" value in
          V_float (clamp 0. track.length (R.get_music_time_played track.music)));
      one "length" (fun value -> V_float (track_of "length" value).length);
      (* 0 is silence and 1 is the track as it is. *)
      two "volume" (fun value level ->
          R.set_music_volume (track_of "volume" value).music (clamp 0. 1. (number "volume" level));
          V_null);
      (* 1 is normal. Raising it plays faster and higher, lowering it slower and
         lower: it is the speed of the tape, not a change of tempo alone. *)
      two "speed" (fun value rate ->
          R.set_music_pitch (track_of "speed" value).music (clamp 0.25 4. (number "speed" rate));
          V_null);
      (* -1 is all to the left, 0 the middle and 1 all to the right. *)
      two "pan" (fun value side ->
          R.set_music_pan (track_of "pan" value).music ((clamp (-1.) 1. (number "pan" side) +. 1.) /. 2.);
          V_null);
      (* Every track at once, the way the volume knob of the machine would. *)
      one "master" (fun level ->
          ensure_device "master";
          R.set_master_volume (clamp 0. 1. (number "master" level));
          V_null);
      (* Feeds the streams. A window does it on every frame by itself. *)
      ("update", V_native (fun _ -> update_all (); V_null));
      (* Decodes the whole track to work out its waveform and its spectrum. It
         takes about a second for a song, during which nothing else happens, and
         it only has to be done once per track. *)
      one "analyze" (fun value ->
          ignore (analyze (track_of "analyze" value));
          V_null);
      one "analyzed" (fun value -> V_bool ((track_of "analyzed" value).analysis <> None));
      (* The loudest point in each of [count] slices of the whole track, 0 to 1. *)
      two "waveform" (fun value count ->
          let track = track_of "waveform" value in
          let count = max 0 (int_of_float (number "waveform" count)) in
          match track.analysis with
          | None -> floats_value (Array.make count 0.)
          | Some analysis -> floats_value (A.resample_peaks analysis.A.peaks count));
      (* How loud each of [count] bands is at this moment of the track, low pitches
         first, 0 to 1. Zeros until the track has been analysed. It never blocks. *)
      two "spectrum" (fun value count ->
          let track = track_of "spectrum" value in
          let count = max 0 (min 64 (int_of_float (number "spectrum" count))) in
          match track.analysis with
          | None -> floats_value (Array.make count 0.)
          | Some analysis ->
              floats_value
                (A.spectrum_at analysis ~seconds:(R.get_music_time_played track.music) ~count));
      (* The picture a track carries, written to a file that can be shown with draw.image
         or an Image; "" when it has none. Only MP3 tags are read. *)
      one "cover" (function
        | V_string path -> V_string (cover_of path)
        | other -> fail "cover" (Printf.sprintf "expects a file name, got %s" (describe_value other)));
      (* The colours of a picture file, as a list of numbers, the most prominent first:
         audio.palette("cover.jpg", 5). *)
      two "palette" (fun path count ->
          match (path, count) with
          | V_string path, V_int count when count > 0 ->
              V_list (list_of_values (List.map (fun c -> V_int c) (palette_of path count)))
          | _ -> fail "palette" "expects a file name and a count above zero");
      (* The title, artist, album and year written inside the file; "" for what it
         does not say. *)
      one "tags" (function
        | V_string path -> tags_value (read_tags path)
        | other -> fail "tags" (Printf.sprintf "expects a file name, got %s" (describe_value other)));
    ]
