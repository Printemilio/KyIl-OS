(* General byte/UTF-8, file-descriptor and native event primitives. Document,
   piece-table, undo, find/replace and dialog policy are entirely Suchu. *)
open Runtime
external install : bool -> unit = "kyil_platform_install"
external cancel_close : unit -> unit = "kyil_platform_cancel_close"
external context : int -> string = "kyil_platform_context"
external events : unit -> (int*int*int*int*int*int) list = "kyil_platform_events"
external bench_start : string -> unit = "kyil_bench_start"
external bench_tag : int -> unit = "kyil_bench_tag"
external bench_run : unit -> unit = "kyil_bench_run"
external bench_idle : unit -> unit = "kyil_bench_idle"
external bench_report : int -> int -> unit = "kyil_bench_report"
external wake_after : int -> unit = "kyil_platform_wake_after"
external open_path : string -> int -> unit = "kyil_platform_open_path"
external spawn : string -> int = "kyil_platform_spawn"
external spawn_caps : string -> int -> int = "kyil_platform_spawn_caps"
external wait_child : int -> int * int = "kyil_platform_wait_child"
external console_attach : unit -> bool = "kyil_console_attach"
external console_read : unit -> string * int = "kyil_console_read"
external console_submit : string -> unit = "kyil_console_submit"
let string=Native_api.as_string and int=Native_api.as_int
let values xs=V_list(list_of_values xs)
let guard f=try f() with
 | Unix.Unix_error(e,fn,p)->raise(Runtime_error(fn^" "^p^": "^Unix.error_message e))
 | Sys_error s | Failure s->raise(Runtime_error s)
type descriptor={fd:Unix.file_descr;mutable closed:bool}
let descriptor v=let d : descriptor=expect_object "fileio" "file_descriptor" v in
 if d.closed then raise(Runtime_error "File descriptor is closed");d
let validate_utf8 s=
 let rec walk i= if i=String.length s then true else
   let decoded=String.get_utf_8_uchar s i in
   Uchar.utf_decode_is_valid decoded && walk(i+Uchar.utf_decode_length decoded)
 in walk 0
let register ()=
 let f=Native_api.function0 and f1=Native_api.function1 and f2=Native_api.function2 in
 Native_api.register_module "platform"
 [f1 "install" (fun v->install(truthy v);V_null);
  f1 "bench_start" (fun v->bench_start(string v);V_null);
  f1 "bench_tag" (fun v->bench_tag(int v);V_null);
  f "bench_run" (fun()->bench_run();V_null);
  f "bench_idle" (fun()->bench_idle();V_null);
  f2 "bench_report" (fun n h->bench_report(int n)(int h);V_null);
  f1 "wake_after" (fun n->wake_after(int n);V_null);
  f1 "context" (fun v->guard(fun()->V_string(context(int v))));
  f "events" (fun()->guard(fun()->events() |> List.map(fun(a,b,c,d,e,f)->values(List.map(fun n->V_int n)[a;b;c;d;e;f])) |> values));
  f "focused" (fun()->V_bool(Raylib.is_window_focused()));
  f "pid" (fun()->V_int(Unix.getpid()));
  f "monotonic" (fun()->V_float(Raylib.get_time()));
  f2 "open_path" (fun p kind->guard(fun()->open_path(string p)(int kind);V_null));
  f1 "spawn" (fun p->guard(fun()->V_int(spawn(string p))));
  f2 "spawn_caps" (fun p caps->guard(fun()->V_int(spawn_caps(string p)(int caps))));
  f1 "wait_child" (fun p->guard(fun()->let pid,status=wait_child(int p) in values[V_int pid;V_int status]));
  f "console_attach" (fun()->guard(fun()->V_bool(console_attach())));
  f "console_read" (fun()->guard(fun()->let text,flags=console_read() in values[V_string text;V_int flags]));
  f1 "console_submit" (fun text->guard(fun()->console_submit(string text);V_null));
  f "cancel_close" (fun()->cancel_close();V_null)];
 Native_api.register_module "textbytes"
 [f1 "fail" (fun s->raise(Runtime_error(string s)));
  f2 "byte" (fun s n->let s=string s and n=int n in
    if n<0 || n>=String.length s then raise(Runtime_error "Byte index out of bounds");V_int(Char.code s.[n]));
  ("sub",V_native(function [s;a;b]->let s=string s and a=int a and b=int b in
     if a<0 || b<0 || a>String.length s-b then raise(Runtime_error "Byte slice out of bounds");V_string(String.sub s a b)
    | _->raise(Runtime_error "textbytes.sub expects string, offset, length")));
  f1 "valid_utf8" (fun s->V_bool(validate_utf8(string s)));
  f2 "next" (fun s n->V_int(Gui_backend.utf8_next (string s)(int n)));
  f2 "prev" (fun s n->V_int(Gui_backend.utf8_prev (string s)(int n)));
  f1 "character" (fun n->let n=int n in if not(Uchar.is_valid n) then raise(Runtime_error "Invalid code point");
    V_string(Gui_backend.utf8_of_uchar(Uchar.of_int n)));
  ("find",V_native(function [s;needle;start]->let s=string s and needle=string needle and start=int start in
      if start<0 || start>String.length s then raise(Runtime_error "Search offset out of bounds");
      V_int(match Runtime.find_substring s needle start with Some i->i | None-> -1)
    | _->raise(Runtime_error "textbytes.find expects string, pattern, offset")));
  f2 "count" (fun s ch->let s=string s and ch=string ch in if String.length ch<>1 then raise(Runtime_error "count expects one byte");
    V_int(String.fold_left(fun n c->if c=ch.[0] then n+1 else n)0 s))];
 Native_api.register_module "fileio"
 [f1 "stat" (fun p->guard(fun()->let s=Unix.LargeFile.lstat(string p) in
     let kind=match s.Unix.LargeFile.st_kind with Unix.S_DIR->"directory" | Unix.S_REG->"file" | Unix.S_LNK->"link" | _->"other" in
     if s.Unix.LargeFile.st_size<0L || s.Unix.LargeFile.st_size>Int64.of_int max_int then raise(Runtime_error "Unrepresentable file size");
     values[V_string kind;V_int(Int64.to_int s.Unix.LargeFile.st_size);V_int s.Unix.LargeFile.st_perm;V_int s.Unix.LargeFile.st_uid;V_int s.Unix.LargeFile.st_gid]));
  f1 "exists" (fun p->guard(fun()->let p=string p in
     try ignore(Unix.lstat p);V_bool true with Unix.Unix_error(Unix.ENOENT,_,_)->V_bool false));
  f2 "mkdir" (fun p mode->guard(fun()->Unix.mkdir(string p)(int mode);V_null));
  f1 "rmdir" (fun p->guard(fun()->Unix.rmdir(string p);V_null));
  f1 "readlink" (fun p->guard(fun()->V_string(Unix.readlink(string p))));
  f2 "symlink" (fun target path->guard(fun()->Unix.symlink(string target)(string path);V_null));
  f2 "open" (fun path mode->guard(fun()->let path=string path in
    if String.contains path '\000' then raise(Runtime_error "NUL in path");
    let flags=match string mode with "r"->[Unix.O_RDONLY] | "x"->[Unix.O_WRONLY;Unix.O_CREAT;Unix.O_EXCL]
       | _->raise(Runtime_error "Mode must be r or exclusive-create x") in
    let d={fd=Unix.openfile path flags 0o600;closed=false} in
    Gc.finalise(fun d->if not d.closed then (d.closed<-true;try Unix.close d.fd with _->()))d;
    make_object "file_descriptor" d));
  f2 "read" (fun fd size->guard(fun()->let size=int size in
     if size<0 || size>1048576 then raise(Runtime_error "Read size out of bounds");
     let b=Bytes.create size in let n=Unix.read(descriptor fd).fd b 0 size in V_string(Bytes.sub_string b 0 n)));
  f2 "write" (fun fd s->guard(fun()->let d=descriptor fd and s=string s in
     let rec write offset=if offset<String.length s then
       let n=Unix.write_substring d.fd s offset (String.length s-offset) in
       if n=0 then raise(Runtime_error "Zero-length write before completion") else write(offset+n) in write 0;V_null));
  f1 "sync" (fun fd->guard(fun()->Unix.fsync(descriptor fd).fd;V_null));
  f1 "size" (fun fd->guard(fun()->let n=(Unix.LargeFile.fstat(descriptor fd).fd).Unix.LargeFile.st_size in
     if n<0L || n>Int64.of_int max_int then raise(Runtime_error "File size cannot be represented");V_int(Int64.to_int n)));
  f1 "mode" (fun p->guard(fun()->V_int((Unix.stat(string p)).Unix.st_perm)));
  f2 "chmod" (fun p n->guard(fun()->Unix.chmod(string p)(int n);V_null));
  f1 "close" (fun fd->guard(fun()->let d=descriptor fd in d.closed<-true;Unix.close d.fd;V_null));
  f2 "rename" (fun a b->guard(fun()->Unix.rename(string a)(string b);V_null));
  f1 "remove" (fun s->guard(fun()->Unix.unlink(string s);V_null))]
