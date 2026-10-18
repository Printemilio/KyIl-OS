#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "../runtime.h"
typedef struct {uint64_t pattern;uint32_t alive,failed,tid,cpu_mask;} context;
static uint64_t read_gs(void) {uint64_t x;__asm__ volatile("movq %%gs:0,%0":"=r"(x));return x;}
static void worker(void *p) {
    context *c=p;vex_thread_context_req_t initial={.size=sizeof(initial)};
    if(vex_syscall1(VEX_SYS_THREAD_CONTEXT,(uintptr_t)&initial) || initial.gs_base || wr_set_gs(c))c->failed=1;
    for(unsigned i=0;i<300;i++) {
        uint32_t cpu;if(read_gs()!=c->pattern || wr_context(0,0,&cpu))c->failed=1;
        if(cpu<32)c->cpu_mask|=1u<<cpu;wr_yield();
    }
    vex_thread_context_req_t r={.size=sizeof(r)};
    if(vex_syscall1(VEX_SYS_THREAD_CONTEXT,(uintptr_t)&r) || r.tid!=c->tid || r.gs_base!=(uintptr_t)c)c->failed=1;
    vex_syscall1(VEX_SYS_THREAD_EXIT,0);wr_exit(125);
}
int wr_main(uint64_t argc,char **argv) {
    vex_thread_context_req_t r={.size=sizeof(r)};
    if(vex_syscall1(VEX_SYS_THREAD_CONTEXT,(uintptr_t)&r))goto fail;
    if(argc>1) {if(r.gs_base)goto fail;wr_log("native-context: exec clears GS PASS\n");return 0;}
    context main={.pattern=0x1234567812345678ull};
    if(wr_set_gs(&main) || read_gs()!=main.pattern)goto fail;
    static const vex_thread_context_req_t ro={.size=sizeof(ro)};
    if((int64_t)vex_syscall1(VEX_SYS_THREAD_CONTEXT,(uintptr_t)&ro)!=-14)goto fail;
    r=(vex_thread_context_req_t){.size=sizeof(r),.operation=1,.mask=2,.gs_base=0xffff800000000000ull};
    if((int64_t)vex_syscall1(VEX_SYS_THREAD_CONTEXT,(uintptr_t)&r)!=-22 || read_gs()!=main.pattern)goto fail;
    context *contexts=wr_map(0,4096,PE_R|PE_W);void *stacks[4];if(!contexts)goto fail;
    for(unsigned i=0;i<4;i++) {
        contexts[i].pattern=0xabcdef0000000000ull+i;contexts[i].alive=1;
        stacks[i]=wr_map(0,65536,PE_R|PE_W);if(!stacks[i])goto fail;
        if(wr_start_thread(worker,&contexts[i],(uint8_t *)stacks[i]+65536,&contexts[i].tid,&contexts[i].alive)<0)goto fail;
    }
    for(unsigned i=0;i<4;i++) {
        while(__atomic_load_n(&contexts[i].alive,__ATOMIC_ACQUIRE))wr_futex(&contexts[i].alive,0,1,0);
        if(contexts[i].failed || read_gs()!=main.pattern)goto fail;wr_unmap(stacks[i],65536);
    }
    int64_t child=(int64_t)vex_syscall0(VEX_SYS_PROCESS_FORK);
    if(child<0)goto fail;
    if(!child) {
        if(read_gs()!=main.pattern)goto fail;
        char *args[]={"/usr/native-tests/native_context_test","exec-child",0};
        vex_process_exec_req_t e={args[0],args,0};
        vex_syscall1(VEX_SYS_PROCESS_EXEC,(uintptr_t)&e);goto fail;
    }
    int status=-1;vex_process_wait_req_t wait={(int32_t)child,&status,0};
    if(vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait)!=(uint64_t)child || status || read_gs()!=main.pattern)goto fail;
    wr_set_gs(0);wr_unmap(contexts,4096);
    wr_log("native-context: four GS contexts / fork-exec / rejected high-RO pointers PASS\n");return 0;
fail:
    wr_log("native-context: FAIL\n");return 1;
}
