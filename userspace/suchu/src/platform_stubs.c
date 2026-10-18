/* Native ABI/context and input observation only. No document/editor policy. */
#define VEX_HOSTED_BUILD 1
#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <caml/fail.h>
#include "vex_app.h"
#include "vex_input_observer.h"
#include "vex_bench.h"
#include "vex_canvas_cache.h"
#include <stdio.h>
#include <string.h>
static void check_path(value path){
    if(caml_string_length(path)!=strlen(String_val(path)))caml_failwith("NUL in path");
}
static uint64_t console_token;
CAMLprim value kyil_console_attach(value unit){
    CAMLparam1(unit);vex_console_req_t r={.operation=VEX_CONSOLE_ATTACH,.window_id=vex_raylib_window_id()};
    int64_t rc=vex_syscall1(VEX_SYS_CONSOLE,(uintptr_t)&r);
    if(rc<0){char text[80];snprintf(text,sizeof(text),"Native console attach failed (%lld)",(long long)rc);caml_failwith(text);}
    console_token=r.token;CAMLreturn(Val_bool(rc==0));
}
CAMLprim value kyil_console_read(value unit){
    CAMLparam1(unit);CAMLlocal2(pair,text);char buffer[4096];
    vex_console_req_t r={.operation=VEX_CONSOLE_READ,.window_id=vex_raylib_window_id(),.token=console_token,.buffer=buffer,.size=sizeof(buffer)};
    int64_t rc=vex_syscall1(VEX_SYS_CONSOLE,(uintptr_t)&r);
    if(rc<0 || rc>(int64_t)sizeof(buffer))caml_failwith("Native console read failed");
    text=caml_alloc_initialized_string(rc,buffer);pair=caml_alloc_tuple(2);
    Store_field(pair,0,text);Store_field(pair,1,Val_long(r.flags));CAMLreturn(pair);
}
CAMLprim value kyil_console_submit(value text){
    CAMLparam1(text);check_path(text);
    vex_console_req_t r={.operation=VEX_CONSOLE_SUBMIT,.window_id=vex_raylib_window_id(),.token=console_token,.buffer=(char *)String_val(text),.size=caml_string_length(text)};
    int64_t rc=vex_syscall1(VEX_SYS_CONSOLE,(uintptr_t)&r);
    if(rc<0){char error[80];snprintf(error,sizeof(error),"Command not submitted (%lld)",(long long)rc);caml_failwith(error);}
    CAMLreturn(Val_unit);
}
CAMLprim value kyil_platform_open_path(value path,value kind){
    CAMLparam2(path,kind);check_path(path);
    vex_open_path_req_t r={.path=String_val(path),.type=Int_val(kind)};
    if((int64_t)vex_syscall1(VEX_SYS_OPEN_PATH,(uintptr_t)&r)<0)caml_failwith("Native open-path request failed");
    CAMLreturn(Val_unit);
}
CAMLprim value kyil_platform_spawn(value path){
    CAMLparam1(path);check_path(path);
    vex_process_spawn_req_t r={.path=String_val(path)};
    int64_t rc=vex_syscall1(VEX_SYS_PROCESS_SPAWN,(uintptr_t)&r);
    if(rc<0 || !r.pid){char text[96];snprintf(text,sizeof(text),"Spawn failed (%lld): %.45s",(long long)rc,String_val(path));caml_failwith(text);}
    CAMLreturn(Val_long(r.pid));
}
CAMLprim value kyil_platform_wait_child(value pid){
    CAMLparam1(pid);CAMLlocal1(pair);int status=0;
    vex_process_wait_req_t r={.pid=Int_val(pid),.status=&status,.flags=VEX_PROCESS_WAIT_NOHANG};
    int64_t rc=vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&r);
    if(rc<0 && rc!=-10)caml_failwith("Wait-child failed");
    pair=caml_alloc_tuple(2);Store_field(pair,0,Val_long(rc));Store_field(pair,1,Val_int(status));CAMLreturn(pair);
}
CAMLprim value kyil_platform_spawn_caps(value path,value caps){
    CAMLparam2(path,caps);check_path(path);intnat mask=Long_val(caps);
    if(mask<0 || (uint64_t)mask>VEX_CAP_ALL)caml_failwith("Invalid capability mask");
    vex_process_spawn_req_t r={.path=String_val(path),.flags=VEX_PROCESS_SPAWN_CAPABILITIES|((uint32_t)mask<<VEX_PROCESS_SPAWN_CAP_SHIFT)};
    int64_t rc=vex_syscall1(VEX_SYS_PROCESS_SPAWN,(uintptr_t)&r);
    if(rc<0 || !r.pid)caml_failwith("Requested launch capabilities denied, or spawn failed");
    CAMLreturn(Val_long(r.pid));
}
CAMLprim value kyil_canvas_capture(value old,value x,value y,value w,value h){
    CAMLparam5(old,x,y,w,h);
    CAMLlocal1(result);result=caml_copy_nativeint(0);
    void *p=vex_raylib_canvas_capture((void *)Nativeint_val(old),Int_val(x),Int_val(y),Int_val(w),Int_val(h));
    Nativeint_val(result)=(intnat)p;CAMLreturn(result);
}
CAMLprim value kyil_canvas_restore(value saved,value x,value y,value w,value h){
    return Val_bool(vex_raylib_canvas_restore((void *)Nativeint_val(saved),Int_val(x),Int_val(y),Int_val(w),Int_val(h)));
}
CAMLprim value kyil_canvas_drop(value saved){vex_raylib_canvas_drop((void *)Nativeint_val(saved));return Val_unit;}
CAMLprim value kyil_bench_frame_begin(value u){vex_bench_begin();return Val_unit;}
CAMLprim value kyil_bench_start(value s){vex_bench_start(String_val(s));return Val_unit;}
CAMLprim value kyil_bench_tag(value n){vex_bench_tag(Int_val(n));return Val_unit;}
CAMLprim value kyil_bench_run(value u){vex_bench_run();return Val_unit;}
CAMLprim value kyil_bench_idle(value u){vex_bench_idle();return Val_unit;}
CAMLprim value kyil_bench_report(value n,value h){vex_bench_report(Int_val(n),Long_val(h));return Val_unit;}
CAMLprim value kyil_platform_wake_after(value ms){
    int64_t n=Long_val(ms);if(n<1 || n>655350)caml_failwith("Wake deadline outside range");
    vex_raylib_set_event_deadline((uint16_t)((n+9)/10));return Val_unit;
}
static vex_app_window_event_t queue[256];
static unsigned used;
static int overflow;
static void observe(const vex_app_window_event_t *e,void *data){
    (void)data;
    if(used && e->type==VEX_APP_EVENT_POINTER && !e->w &&
       queue[used-1].type==e->type && !queue[used-1].w && queue[used-1].flags==e->flags){queue[used-1]=*e;return;}
    if(used==256){overflow=1;return;}
    queue[used++]=*e;
}
CAMLprim value kyil_platform_install(value confirm){
    CAMLparam1(confirm);used=0;overflow=0;
    vex_raylib_set_input_observer(observe,NULL);vex_raylib_set_close_confirmation(Bool_val(confirm));
    CAMLreturn(Val_unit);
}
CAMLprim value kyil_platform_cancel_close(value unit){CAMLparam1(unit);vex_raylib_cancel_close();CAMLreturn(Val_unit);}
CAMLprim value kyil_platform_context(value kind){
    CAMLparam1(kind);char path[256]={0};
    vex_open_context_req_t r={.application=Int_val(kind),.buffer=path,.size=sizeof(path)};
    int result=(int64_t)vex_syscall1(VEX_SYS_OPEN_CONTEXT,(uintptr_t)&r);
    /* Native OPEN_CONTEXT returns -1 when no path was supplied. That is the
     * normal desktop New-document launch, not a failed file read. */
    if(result<0)path[0]=0;
    CAMLreturn(caml_copy_string(path));
}
CAMLprim value kyil_platform_events(value unit){
    CAMLparam1(unit);CAMLlocal3(list,item,node);list=Val_emptylist;
    if(overflow){overflow=0;used=0;caml_failwith("Native input observer overflow");}
    for(unsigned i=used;i>0;--i){
        const vex_app_window_event_t *e=&queue[i-1];item=caml_alloc_tuple(6);
        Store_field(item,0,Val_int(e->type));Store_field(item,1,Val_long(e->flags));
        Store_field(item,2,Val_long((int64_t)e->x));Store_field(item,3,Val_long((int64_t)e->y));
        Store_field(item,4,Val_long((int64_t)e->w));Store_field(item,5,Val_long((int64_t)e->h));
        node=caml_alloc(2,0);Store_field(node,0,item);Store_field(node,1,list);list=node;
    }
    used=0;CAMLreturn(list);
}
