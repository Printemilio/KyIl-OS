(* What can be said about a piece of music without a sound card.

   The waveform of a track, the spectrum at any moment of it, and the title and
   artist written inside the file. All of it works on numbers and bytes handed in,
   so it has no raylib in it and is tested without playing anything: a sine wave
   goes in and has to come out at the right frequency.

   The spectrum is computed ahead of time, for the whole track, rather than as it
   plays. raylib gives no way to hear what is being played -- there is no callback
   on the stream -- but it does decode a file to samples, and from samples a
   spectrum for every thirtieth of a second costs a second or so. Looking one up
   while the track plays is then free, and it cannot fall behind the music the way
   a calculation done on the fly could. *)

(* --- the Fourier transform ------------------------------------------------- *)

(* cos and sin of 2*pi*k/n for k below n/2, computed once per size: a track is
   thousands of transforms of the same length, and a table is the difference
   between calling cos a million times and not. *)
let twiddle_tables : (int, float array * float array) Hashtbl.t = Hashtbl.create 4

let twiddles n =
  match Hashtbl.find_opt twiddle_tables n with
  | Some tables -> tables
  | None ->
      let half = n / 2 in
      let cosines = Array.init half (fun k -> cos (2. *. Float.pi *. float_of_int k /. float_of_int n)) in
      let sines = Array.init half (fun k -> sin (2. *. Float.pi *. float_of_int k /. float_of_int n)) in
      Hashtbl.replace twiddle_tables n (cosines, sines);
      (cosines, sines)

let is_power_of_two n = n > 0 && n land (n - 1) = 0

(* In place, radix 2. [re] and [im] hold the real and imaginary parts and have the
   same length, which has to be a power of two. *)
let fft re im =
  let n = Array.length re in
  if not (is_power_of_two n) || Array.length im <> n then
    invalid_arg "Audio_analysis.fft: the length must be a power of two";
  (* Put the samples in bit-reversed order. *)
  let j = ref 0 in
  for i = 0 to n - 1 do
    if i < !j then begin
      let r = re.(i) and m = im.(i) in
      re.(i) <- re.(!j);
      im.(i) <- im.(!j);
      re.(!j) <- r;
      im.(!j) <- m
    end;
    let m = ref (n lsr 1) in
    while !m >= 1 && !j >= !m do
      j := !j - !m;
      m := !m lsr 1
    done;
    j := !j + !m
  done;
  let cosines, sines = twiddles n in
  let size = ref 2 in
  while !size <= n do
    let half = !size / 2 in
    let step = n / !size in
    let start = ref 0 in
    while !start < n do
      for k = 0 to half - 1 do
        let wr = cosines.(k * step) and wi = -.sines.(k * step) in
        let a = !start + k and b = !start + k + half in
        let tr = (re.(b) *. wr) -. (im.(b) *. wi) and ti = (re.(b) *. wi) +. (im.(b) *. wr) in
        re.(b) <- re.(a) -. tr;
        im.(b) <- im.(a) -. ti;
        re.(a) <- re.(a) +. tr;
        im.(a) <- im.(a) +. ti
      done;
      start := !start + !size
    done;
    size := !size * 2
  done

(* --- bands ----------------------------------------------------------------- *)

let lowest_hertz = 40.
let highest_hertz = 16000.

(* Where each band starts, as an index into the transform's bins, spaced the way
   hearing is: by ratio, not by distance, so a band at the bottom is a few bins
   wide and one at the top is a few hundred. Every band gets at least one bin, and
   the last index is one past the final bin. The array has [bands + 1] entries. *)
let band_edges ~bands ~sample_rate ~size =
  let nyquist_bins = size / 2 in
  let top = Float.min highest_hertz (0.95 *. float_of_int sample_rate /. 2.) in
  let bin_of hertz = int_of_float (Float.round (hertz *. float_of_int size /. float_of_int sample_rate)) in
  let edges =
    Array.init (bands + 1) (fun i ->
        let ratio = float_of_int i /. float_of_int bands in
        bin_of (lowest_hertz *. ((top /. lowest_hertz) ** ratio)))
  in
  edges.(0) <- max 1 edges.(0);
  for i = 1 to bands do
    edges.(i) <- max edges.(i) (edges.(i - 1) + 1)
  done;
  (* Squeezed back under the last bin if the spacing pushed past it. *)
  if edges.(bands) > nyquist_bins then begin
    edges.(bands) <- nyquist_bins;
    for i = bands - 1 downto 0 do
      edges.(i) <- min edges.(i) (edges.(i + 1) - 1)
    done
  end;
  edges

(* --- what the transform says, as a level between 0 and 1 ------------------ *)

(* A sine of amplitude 1 gives a bin of size/4 under a Hann window, so dividing by
   that makes 0 dB the loudest a sample can be. Seventy-two decibels below it is
   where the display bottoms out. *)
let floor_decibels = -72.

let level_of magnitude ~size =
  let relative = magnitude /. (float_of_int size /. 4.) in
  let decibels = 20. *. Float.log10 (Float.max relative 1e-9) in
  Float.max 0. (Float.min 1. ((decibels -. floor_decibels) /. -.floor_decibels))

let hann size = Array.init size (fun i -> 0.5 -. (0.5 *. cos (2. *. Float.pi *. float_of_int i /. float_of_int size)))

(* --- a whole track --------------------------------------------------------- *)

type analysis = {
  fps : int;  (* how many spectra a second *)
  bands : int;
  spectra : float array array;  (* one row per moment, [bands] levels each *)
  peaks : float array;  (* the loudest sample in each slice of the track, 0 to 1 *)
}

let peak_count = 2000

(* [sample i] is the mono value of frame [i], for any [i] -- zero outside the
   track -- so this knows nothing about channels or sample formats. *)
let analyze ~frame_count ~sample_rate ~sample ~fps ~bands ~window =
  if frame_count <= 0 || sample_rate <= 0 then { fps; bands; spectra = [||]; peaks = [||] }
  else begin
    let window_function = hann window in
    let edges = band_edges ~bands ~sample_rate ~size:window in
    let moments = max 1 (int_of_float (Float.ceil (float_of_int frame_count /. float_of_int sample_rate *. float_of_int fps))) in
    let re = Array.make window 0. and im = Array.make window 0. in
    let spectra =
      Array.init moments (fun moment ->
          let centre = moment * sample_rate / fps in
          let first = centre - (window / 2) in
          for i = 0 to window - 1 do
            re.(i) <- sample (first + i) *. window_function.(i);
            im.(i) <- 0.
          done;
          fft re im;
          Array.init bands (fun band ->
              let loudest = ref 0. in
              for bin = edges.(band) to edges.(band + 1) - 1 do
                let magnitude = sqrt ((re.(bin) *. re.(bin)) +. (im.(bin) *. im.(bin))) in
                if magnitude > !loudest then loudest := magnitude
              done;
              level_of !loudest ~size:window))
    in
    (* A slice that is long is not read sample by sample: sixty-four looks inside
       it find the peak well enough for something drawn a pixel wide. *)
    let per_peak = max 1 (frame_count / peak_count) in
    let stride = max 1 (per_peak / 64) in
    let peaks =
      Array.init (min peak_count frame_count) (fun slice ->
          let from = slice * per_peak in
          let loudest = ref 0. in
          let i = ref from in
          while !i < from + per_peak do
            let value = Float.abs (sample !i) in
            if value > !loudest then loudest := value;
            i := !i + stride
          done;
          Float.min 1. !loudest)
    in
    { fps; bands; spectra; peaks }
  end

(* [count] values describing the peaks, however many were stored: the loudest of
   each group, so a thin spike survives being drawn smaller. *)
let resample_peaks peaks count =
  let stored = Array.length peaks in
  if stored = 0 || count <= 0 then Array.make (max 0 count) 0.
  else
    Array.init count (fun slot ->
        let from = slot * stored / count in
        let upto = max (from + 1) ((slot + 1) * stored / count) in
        let loudest = ref 0. in
        for i = from to min (stored - 1) (upto - 1) do
          if peaks.(i) > !loudest then loudest := peaks.(i)
        done;
        !loudest)

(* The spectrum at a moment, in [count] bands. Between two stored moments it takes
   the nearer one: a thirtieth of a second is below what an eye separates. *)
let spectrum_at analysis ~seconds ~count =
  let moments = Array.length analysis.spectra in
  if moments = 0 || count <= 0 then Array.make (max 0 count) 0.
  else begin
    let moment = max 0 (min (moments - 1) (int_of_float (Float.round (seconds *. float_of_int analysis.fps)))) in
    let row = analysis.spectra.(moment) in
    Array.init count (fun slot ->
        let from = slot * analysis.bands / count in
        let upto = max (from + 1) ((slot + 1) * analysis.bands / count) in
        let loudest = ref 0. in
        for i = from to min (analysis.bands - 1) (upto - 1) do
          if row.(i) > !loudest then loudest := row.(i)
        done;
        !loudest)
  end

(* --- tags ------------------------------------------------------------------ *)

type tags = {
  title : string;
  artist : string;
  album : string;
  year : string;
}

let no_tags = { title = ""; artist = ""; album = ""; year = "" }

let utf8_of_codepoint buffer code =
  match Uchar.of_int code with
  | uchar -> Buffer.add_utf_8_uchar buffer uchar
  | exception Invalid_argument _ -> Buffer.add_char buffer '?'

let latin1_to_utf8 text =
  let buffer = Buffer.create (String.length text) in
  String.iter (fun c -> utf8_of_codepoint buffer (Char.code c)) text;
  Buffer.contents buffer

(* UTF-16, with surrogate pairs. [big_endian] is the byte order unless the text
   opens with a byte-order mark, which then decides. *)
let utf16_to_utf8 ~big_endian text =
  let length = String.length text in
  let big_endian, start =
    if length >= 2 && text.[0] = '\xff' && text.[1] = '\xfe' then (false, 2)
    else if length >= 2 && text.[0] = '\xfe' && text.[1] = '\xff' then (true, 2)
    else (big_endian, 0)
  in
  let unit_at i =
    let a = Char.code text.[i] and b = Char.code text.[i + 1] in
    if big_endian then (a lsl 8) lor b else (b lsl 8) lor a
  in
  let buffer = Buffer.create length in
  let i = ref start in
  while !i + 1 < length do
    let first = unit_at !i in
    i := !i + 2;
    if first >= 0xD800 && first < 0xDC00 && !i + 1 < length then begin
      let second = unit_at !i in
      if second >= 0xDC00 && second < 0xE000 then begin
        i := !i + 2;
        utf8_of_codepoint buffer (0x10000 + ((first - 0xD800) lsl 10) + (second - 0xDC00))
      end
      else utf8_of_codepoint buffer first
    end
    else utf8_of_codepoint buffer first
  done;
  Buffer.contents buffer

let strip_nulls text =
  let stop = ref (String.length text) in
  while !stop > 0 && (text.[!stop - 1] = '\000' || text.[!stop - 1] = ' ') do
    decr stop
  done;
  String.sub text 0 !stop

(* A text frame: one byte saying how the text is written, then the text. *)
let decode_text_frame body =
  if body = "" then ""
  else
    let rest = String.sub body 1 (String.length body - 1) in
    let text =
      match body.[0] with
      | '\000' -> latin1_to_utf8 rest
      | '\001' -> utf16_to_utf8 ~big_endian:false rest
      | '\002' -> utf16_to_utf8 ~big_endian:true rest
      | _ -> rest
    in
    (* A frame can hold several values separated by a null; the first is the one. *)
    let first =
      match String.index_opt text '\000' with Some i -> String.sub text 0 i | None -> text
    in
    String.trim (strip_nulls first)

let synchsafe data offset =
  ((Char.code data.[offset] land 0x7f) lsl 21)
  lor ((Char.code data.[offset + 1] land 0x7f) lsl 14)
  lor ((Char.code data.[offset + 2] land 0x7f) lsl 7)
  lor (Char.code data.[offset + 3] land 0x7f)

let big_endian32 data offset =
  (Char.code data.[offset] lsl 24)
  lor (Char.code data.[offset + 1] lsl 16)
  lor (Char.code data.[offset + 2] lsl 8)
  lor Char.code data.[offset + 3]

(* The size of the whole ID3v2 tag at the start of a file, header included, from
   the ten bytes that open it. None when the file does not start with one. *)
let id3v2_size header =
  if String.length header >= 10 && String.sub header 0 3 = "ID3" then Some (10 + synchsafe header 6)
  else None

(* ID3v2.2, 2.3 and 2.4, given the tag from its first byte. *)
let parse_id3v2 data =
  match id3v2_size data with
  | None -> None
  | Some _ ->
      let version = Char.code data.[3] in
      let flags = Char.code data.[5] in
      let stop = min (String.length data) (10 + synchsafe data 6) in
      let position = ref 10 in
      (* An extended header sits between the tag's header and its first frame. *)
      if version >= 3 && flags land 0x40 <> 0 && !position + 4 <= stop then
        position := !position + (if version = 4 then synchsafe data !position else 4 + big_endian32 data !position);
      let found = ref no_tags in
      let record id body =
        let text = decode_text_frame body in
        match id with
        | "TIT2" | "TT2" -> found := { !found with title = text }
        | "TPE1" | "TP1" -> found := { !found with artist = text }
        | "TALB" | "TAL" -> found := { !found with album = text }
        | "TYER" | "TYE" | "TDRC" ->
            found := { !found with year = (if String.length text > 4 then String.sub text 0 4 else text) }
        | _ -> ()
      in
      let continue = ref true in
      while !continue do
        if version = 2 then
          if !position + 6 > stop || data.[!position] = '\000' then continue := false
          else begin
            let id = String.sub data !position 3 in
            let size = (Char.code data.[!position + 3] lsl 16) lor (Char.code data.[!position + 4] lsl 8) lor Char.code data.[!position + 5] in
            if size < 0 || !position + 6 + size > stop then continue := false
            else begin
              if id.[0] = 'T' then record id (String.sub data (!position + 6) size);
              position := !position + 6 + size
            end
          end
        else if !position + 10 > stop || data.[!position] = '\000' then continue := false
        else begin
          let id = String.sub data !position 4 in
          let size = if version = 4 then synchsafe data (!position + 4) else big_endian32 data (!position + 4) in
          if size < 0 || !position + 10 + size > stop then continue := false
          else begin
            if id.[0] = 'T' then record id (String.sub data (!position + 10) size);
            position := !position + 10 + size
          end
        end
      done;
      Some !found

(* ID3v1: the last 128 bytes of the file. *)
let parse_id3v1 data =
  if String.length data = 128 && String.sub data 0 3 = "TAG" then
    let field offset width = String.trim (strip_nulls (latin1_to_utf8 (String.sub data offset width))) in
    Some { title = field 3 30; artist = field 33 30; album = field 63 30; year = field 93 4 }
  else None

(* Whichever there is, version 2 first, filling in from version 1 what it lacks. *)
let merge_tags primary fallback =
  let pick a b = if a <> "" then a else b in
  { title = pick primary.title fallback.title;
    artist = pick primary.artist fallback.artist;
    album = pick primary.album fallback.album;
    year = pick primary.year fallback.year }

(* --- the cover picture --------------------------------------------------------- *)

(* The picture an ID3v2 tag carries (APIC, or PIC in 2.2): its bytes and the file
   extension it wants, "jpg" or "png". The first picture wins, which is the front
   cover in practice. None when there is none, or the tag is cut off inside it. *)
let parse_picture data =
  match id3v2_size data with
  | None -> None
  | Some _ ->
      let version = Char.code data.[3] in
      let flags = Char.code data.[5] in
      let stop = min (String.length data) (10 + synchsafe data 6) in
      let position = ref 10 in
      if version >= 3 && flags land 0x40 <> 0 && !position + 4 <= stop then
        position := !position + (if version = 4 then synchsafe data !position else 4 + big_endian32 data !position);
      (* Past the text encoding byte, a NUL-terminated string, which is two NULs
         on a two-byte boundary in the UTF-16 encodings. *)
      let skip_text body from encoding =
        let n = String.length body in
        let i = ref from in
        if encoding = 1 || encoding = 2 then begin
          while !i + 1 < n && not (body.[!i] = '\000' && body.[!i + 1] = '\000') do i := !i + 2 done;
          !i + 2
        end
        else begin
          while !i < n && body.[!i] <> '\000' do incr i done;
          !i + 1
        end
      in
      let sniff bytes =
        if String.length bytes >= 8 && String.sub bytes 0 4 = "\x89PNG" then Some "png"
        else if String.length bytes >= 3 && String.sub bytes 0 3 = "\xff\xd8\xff" then Some "jpg"
        else None
      in
      let picture id body =
        if String.length body < 4 then None
        else
          let encoding = Char.code body.[0] in
          let after_description =
            if id = "PIC" then skip_text body 5 encoding
            else
              let after_mime = skip_text body 1 0 in
              skip_text body (after_mime + 1) encoding
          in
          if after_description >= String.length body then None
          else
            let bytes = String.sub body after_description (String.length body - after_description) in
            Option.map (fun extension -> (bytes, extension)) (sniff bytes)
      in
      let result = ref None in
      let continue = ref true in
      while !continue && !result = None do
        let header = if version = 2 then 6 else 10 in
        if !position + header > stop || data.[!position] = '\000' then continue := false
        else begin
          let id_length = if version = 2 then 3 else 4 in
          let id = String.sub data !position id_length in
          let size =
            if version = 2 then
              (Char.code data.[!position + 3] lsl 16) lor (Char.code data.[!position + 4] lsl 8)
              lor Char.code data.[!position + 5]
            else if version = 4 then synchsafe data (!position + 4)
            else big_endian32 data (!position + 4)
          in
          if size < 0 || !position + header + size > stop then continue := false
          else begin
            if id = "APIC" || id = "PIC" then result := picture id (String.sub data (!position + header) size);
            position := !position + header + size
          end
        end
      done;
      !result

(* The colours a picture is made of, as the 24-bit numbers the draw module takes,
   most prominent first: [pixels] is width * height packed colours. Each colour is
   put in a box of sixteen shades a channel; a box counts for how many pixels fall
   in it, and more for being vivid, because a cover that is mostly grey with a red
   stripe is remembered as red. Boxes that are near an already chosen colour are
   skipped, so five colours are five different ones. *)
let palette_of_pixels pixels ~count =
  let boxes = Hashtbl.create 256 in
  Array.iter
    (fun packed ->
      let r = (packed lsr 16) land 0xff and g = (packed lsr 8) land 0xff and b = packed land 0xff in
      let key = ((r lsr 4) lsl 8) lor ((g lsr 4) lsl 4) lor (b lsr 4) in
      let n, sr, sg, sb = try Hashtbl.find boxes key with Not_found -> (0, 0, 0, 0) in
      Hashtbl.replace boxes key (n + 1, sr + r, sg + g, sb + b))
    pixels;
  let scored =
    Hashtbl.fold
      (fun _ (n, sr, sg, sb) acc ->
        let r = sr / n and g = sg / n and b = sb / n in
        let high = max r (max g b) and low = min r (min g b) in
        let saturation = if high = 0 then 0. else float_of_int (high - low) /. float_of_int high in
        let lightness = float_of_int high /. 255. in
        (* Near-black and near-white are rarely the colour of an album. *)
        let usable = if lightness < 0.18 then 0.15 else if lightness > 0.97 && saturation < 0.1 then 0.3 else 1. in
        let weight = float_of_int n *. (0.25 +. saturation) *. usable in
        (weight, (r lsl 16) lor (g lsl 8) lor b) :: acc)
      boxes []
  in
  let distance a b =
    let channel x = ((x lsr 16) land 0xff, (x lsr 8) land 0xff, x land 0xff) in
    let r1, g1, b1 = channel a and r2, g2, b2 = channel b in
    abs (r1 - r2) + abs (g1 - g2) + abs (b1 - b2)
  in
  let chosen = ref [] in
  List.iter
    (fun (_, colour) ->
      if List.length !chosen < count && List.for_all (fun c -> distance c colour > 90) !chosen then
        chosen := colour :: !chosen)
    (List.sort (fun (a, _) (b, _) -> compare b a) scored);
  List.rev !chosen

(* --- FLAC, Ogg and WAV ---------------------------------------------------------- *)

let little_endian32 data offset =
  Char.code data.[offset]
  lor (Char.code data.[offset + 1] lsl 8)
  lor (Char.code data.[offset + 2] lsl 16)
  lor (Char.code data.[offset + 3] lsl 24)

(* The comments of a Vorbis, Opus or FLAC file: a vendor string, then any number of
   NAME=value lines, all lengths little-endian. Reads as far as the data goes, so a
   cut-off one gives what came before the cut. *)
let parse_vorbis_comment body =
  let n = String.length body in
  let found = ref no_tags in
  (try
     let position = ref (4 + little_endian32 body 0) in
     let count = little_endian32 body !position in
     position := !position + 4;
     for _ = 1 to min count 512 do
       let length = little_endian32 body !position in
       if length < 0 || !position + 4 + length > n then raise Exit;
       let entry = String.sub body (!position + 4) length in
       position := !position + 4 + length;
       match String.index_opt entry '=' with
       | None -> ()
       | Some at ->
           let name = String.uppercase_ascii (String.sub entry 0 at) in
           let value = String.trim (String.sub entry (at + 1) (String.length entry - at - 1)) in
           (match name with
           | "TITLE" -> found := { !found with title = value }
           | "ARTIST" -> found := { !found with artist = value }
           | "ALBUM" -> found := { !found with album = value }
           | "DATE" | "YEAR" ->
               found := { !found with year = (if String.length value > 4 then String.sub value 0 4 else value) }
           | _ -> ())
     done
   with _ -> ());
  if !found = no_tags then None else Some !found

(* A FLAC PICTURE block: the picture's bytes and extension. *)
let parse_flac_picture body =
  try
    let mime_length = big_endian32 body 4 in
    let at = 8 + mime_length in
    let description_length = big_endian32 body at in
    let at = at + 4 + description_length + 16 in
    let length = big_endian32 body at in
    let bytes = String.sub body (at + 4) length in
    if String.length bytes >= 8 && String.sub bytes 0 4 = "\x89PNG" then Some (bytes, "png")
    else if String.length bytes >= 3 && String.sub bytes 0 3 = "\xff\xd8\xff" then Some (bytes, "jpg")
    else None
  with _ -> None

(* An Ogg file's comments, from its first bytes: they follow the marker of the
   Vorbis ("\003vorbis") or Opus ("OpusTags") comment header. *)
let parse_ogg data =
  let find marker =
    let n = String.length data and m = String.length marker in
    let rec scan i = if i + m > n then None else if String.sub data i m = marker then Some (i + m) else scan (i + 1) in
    scan 0
  in
  let after = match find "\003vorbis" with Some i -> Some i | None -> find "OpusTags" in
  match after with
  | None -> None
  | Some i -> parse_vorbis_comment (String.sub data i (String.length data - i))

(* A WAV file's LIST/INFO chunk: INAM title, IART artist, IPRD album, ICRD date. *)
let parse_wav_info body =
  let found = ref no_tags in
  let position = ref 4 in
  let n = String.length body in
  (try
     while !position + 8 <= n do
       let id = String.sub body !position 4 in
       let size = little_endian32 body (!position + 4) in
       if size < 0 || !position + 8 + size > n then raise Exit;
       let value = String.trim (strip_nulls (String.sub body (!position + 8) size)) in
       (match id with
       | "INAM" -> found := { !found with title = value }
       | "IART" -> found := { !found with artist = value }
       | "IPRD" -> found := { !found with album = value }
       | "ICRD" -> found := { !found with year = (if String.length value > 4 then String.sub value 0 4 else value) }
       | _ -> ());
       position := !position + 8 + size + (size land 1)
     done
   with _ -> ());
  if !found = no_tags then None else Some !found
