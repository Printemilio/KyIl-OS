#include <stdio.h>
#include <stdint.h>
#include "vex_app.h"
static const vex_console_req_t readonly_request={.operation=VEX_CONSOLE_ATTACH};
static const char readonly_buffer[]="read-only";
static int call(vex_console_req_t *r){return (int64_t)vex_syscall1(VEX_SYS_CONSOLE,(uintptr_t)r);}
int main(void) {
    vex_app_window_t window={0};char bytes[16];int failed=0;
    if(vex_app_window_open(&window,"Console boundary test",160,90,0))return 1;
    vex_console_req_t r={.operation=VEX_CONSOLE_READ,.window_id=window.id,.token=UINT64_MAX,.buffer=bytes,.size=sizeof(bytes)};
    failed|=call(&r)!=-13; /* A valid own window is not a console lease. */
    r.buffer=(char *)(uintptr_t)0xffff800000001000ull;failed|=call(&r)!=-14;
    r.buffer=(char *)(uintptr_t)0x444;failed|=call(&r)!=-14;
    r.buffer=(char *)(uintptr_t)0x00007ffffffffffeull;r.size=16;failed|=call(&r)!=-14;
    r.buffer=(char *)readonly_buffer;r.size=4;failed|=call(&r)!=-14;
    failed|=(int64_t)vex_syscall1(VEX_SYS_CONSOLE,(uintptr_t)&readonly_request)!=-14;
    r.operation=VEX_CONSOLE_SUBMIT;r.buffer=(char *)(uintptr_t)0xffff800000001000ull;r.size=8;failed|=call(&r)!=-14;
    r.operation=999;r.buffer=bytes;r.size=sizeof(bytes);failed|=call(&r)!=-22;
    vex_process_spawn_req_t spawn={.path="/usr/bin/kyil-terminal",.flags=VEX_PROCESS_SPAWN_CAPABILITIES|(VEX_CAP_ALL<<VEX_PROCESS_SPAWN_CAP_SHIFT),.pid=42};
    failed|=(int64_t)vex_syscall1(VEX_SYS_PROCESS_SPAWN,(uintptr_t)&spawn)!=-13 || spawn.pid!=0;
    spawn.flags=0x80000000;spawn.pid=42;
    failed|=(int64_t)vex_syscall1(VEX_SYS_PROCESS_SPAWN,(uintptr_t)&spawn)!=-22 || spawn.pid!=0;
    vex_app_window_close(&window);
    puts(failed?"native-console-boundary: FAIL":"native-console-boundary: PASS ten pointer/rights/delegation checks");
    return failed;
}
