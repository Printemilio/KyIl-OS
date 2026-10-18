#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "../runtime.h"
static volatile unsigned entered;
static void worker(void *p){(void)p;entered=1;for(;;)__asm__ volatile("pause");}
__attribute__((naked)) static void no_stack(void *p __attribute__((unused))){__asm__ volatile("movl $1,(%rdi); 1: pause; jmp 1b");}
static int call(vex_thread_control_req_t *r,unsigned op,uint32_t tid){r->size=sizeof(*r);r->operation=op;r->tid=tid;r->value=op==VEX_THREAD_PAUSE?127u:0u;return (int)(int64_t)vex_syscall1(VEX_SYS_THREAD_CONTROL,(uintptr_t)r);}
int wr_main(uint64_t argc,char **argv){(void)argc;(void)argv;
    vex_thread_control_req_t r={0};
    if((int64_t)vex_syscall1(VEX_SYS_THREAD_CONTROL,(uintptr_t)&r)!=-22)goto fail;
    if((int64_t)vex_syscall1(VEX_SYS_THREAD_CONTROL,0xffff800000000000ull)!=-14)goto fail;
    static const vex_thread_control_req_t ro={.size=sizeof(ro),.operation=VEX_THREAD_YIELD};
    if((int64_t)vex_syscall1(VEX_SYS_THREAD_CONTROL,(uintptr_t)&ro)!=-14)goto fail;
    if(call(&r,VEX_THREAD_PAUSE,3)!=-1)goto fail; /* GUI is not our Process. */
    for(unsigned k=0;k<32;k++) {
        void *stack=wr_map(0,65536,PE_R|PE_W);uint32_t alive=1,tid=0;entered=0;
        if(!stack || wr_start_thread_flags(worker,0,(uint8_t *)stack+65536,&tid,&alive,VEX_THREAD_START_PAUSED)<0)goto fail;
        if(call(&r,VEX_THREAD_SNAPSHOT,tid) || !r.context.rip || !r.context.rsp || entered)goto fail;
        for(unsigned i=1;i<3;i++) {
            r=(vex_thread_control_req_t){.size=sizeof(r),.operation=VEX_THREAD_PAUSE,.tid=tid,.value=3};
            if(vex_syscall1(VEX_SYS_THREAD_CONTROL,(uintptr_t)&r) || r.value!=i)goto fail;
        }
        r=(vex_thread_control_req_t){.size=sizeof(r),.operation=VEX_THREAD_PAUSE,.tid=tid,.value=3};
        if((int64_t)vex_syscall1(VEX_SYS_THREAD_CONTROL,(uintptr_t)&r)!=-75)goto fail;
        if(call(&r,VEX_THREAD_RESUME,tid) || r.value!=3 || call(&r,VEX_THREAD_RESUME,tid) || r.value!=2)goto fail;
        for(unsigned i=1;i<127;i++)if(call(&r,VEX_THREAD_PAUSE,tid) || r.value!=i)goto fail;
        if(call(&r,VEX_THREAD_PAUSE,tid)!=-75)goto fail;
        for(unsigned i=127;i>1;i--)if(call(&r,VEX_THREAD_RESUME,tid) || r.value!=i)goto fail;
        if(entered || call(&r,VEX_THREAD_SNAPSHOT,tid))goto fail;
        /* Terminating a parked context must not require a resume, nor leave
           a task lease / FPU snapshot / wait registration behind. */
        if(wr_stop_thread(tid,17))goto fail;
        while(__atomic_load_n(&alive,__ATOMIC_ACQUIRE))wr_futex(&alive,0,1,0);
        if(entered || wr_unmap(stack,65536))goto fail;
    }
    /* The scheduler must own the bootstrap frame, even if the caller unmaps
       the user stack before resuming a thread that doesn't access that stack. */
    void *stack=wr_map(0,65536,PE_R|PE_W);uint32_t alive=1,tid=0;entered=0;
    if(!stack || wr_start_thread_flags(no_stack,(void *)&entered,(uint8_t *)stack+65536,&tid,&alive,VEX_THREAD_START_PAUSED)<0)goto fail;
    if(call(&r,VEX_THREAD_SNAPSHOT,tid) || wr_unmap(stack,65536) || call(&r,VEX_THREAD_RESUME,tid))goto fail;
    while(!entered && __atomic_load_n(&alive,__ATOMIC_ACQUIRE))wr_yield();
    if(!entered || wr_stop_thread(tid,0))goto fail;
    while(__atomic_load_n(&alive,__ATOMIC_ACQUIRE))wr_futex(&alive,0,1,0);
    wr_log("native-thread-control: invalid RO/kernel pointers / foreign TID / caller count limits / 32 parked kills / unmapped start stack PASS\n");return 0;
fail:wr_log("native-thread-control: FAIL\n");return 1;
}
