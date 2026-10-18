#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "../runtime.h"
static uint64_t cap;
static unsigned failure_line;
static uint32_t alive,started,done,entered,release_worker,tid;
static int64_t result;
static int64_t call(unsigned op,unsigned count,uint64_t token,uint64_t ns) {
    vex_execution_req_t r={sizeof(r),op,count,0,token,ns};
    return (int64_t)vex_syscall1(VEX_SYS_EXECUTION_GATE,(uintptr_t)&r);
}
static void worker(void *arg) {
    __atomic_store_n(&started,1,__ATOMIC_RELEASE);
    result=call(VEX_EXECUTION_ENTER,0,cap,(uintptr_t)arg);
    if(!result) {
        __atomic_store_n(&entered,1,__ATOMIC_RELEASE);
        __atomic_store_n(&done,1,__ATOMIC_RELEASE);wr_futex(&done,1,1,0);
        while(!__atomic_load_n(&release_worker,__ATOMIC_ACQUIRE))wr_yield();
        call(VEX_EXECUTION_LEAVE,0,0,0);
    }
    vex_syscall1(VEX_SYS_THREAD_EXIT,0);wr_exit(125);
}
static int spin(uint32_t *word) {
    uint64_t start=wr_ticks();while(!__atomic_load_n(word,__ATOMIC_ACQUIRE)) {
        if(wr_ticks()-start>5000)return -1;wr_yield();
    }return 0;
}
static int registered(void) {
    uint64_t start=wr_ticks();while(call(VEX_EXECUTION_WAITERS,0,cap,0)!=1) {
        if(wr_ticks()-start>5000)return -1;wr_yield();
    }return 0;
}
int wr_main(uint64_t argc,char **argv) {
    (void)argc;(void)argv;
    cap=call(VEX_EXECUTION_CREATE,1,0,0);if((int64_t)cap<=0){failure_line=__LINE__;goto fail;}
    if(call(VEX_EXECUTION_RESUME,0,cap,0) || call(VEX_EXECUTION_QUERY,0,cap,0)!=1 || call(VEX_EXECUTION_LEAVE,0,0,0)){failure_line=__LINE__;goto fail;}
    if(call(VEX_EXECUTION_ENTER,0,cap,0) || call(VEX_EXECUTION_QUERY,0,cap,0)!=1 || call(VEX_EXECUTION_CAPACITY,0,cap,0)!=1){failure_line=__LINE__;goto fail;}
    void *stack=wr_map(0,65536,3);if(!stack){failure_line=__LINE__;goto fail;}
    alive=1;if(wr_start_thread(worker,(void *)30000000,(char *)stack+65536,&tid,&alive)<0){failure_line=__LINE__;goto fail;}
    /* Yield/preemption are still runnable; they must NOT donate the slot. */
    uint64_t start=wr_ticks();while(__atomic_load_n(&alive,__ATOMIC_ACQUIRE)) {
        if(wr_ticks()-start>5000){failure_line=__LINE__;goto fail;}wr_yield();
    }
    if(result!=-110 || entered){failure_line=__LINE__;goto fail;}
    started=done=entered=release_worker=0;alive=1;result=-999;
    if(wr_start_thread(worker,0,(char *)stack+65536,&tid,&alive)<0 || spin(&started) || registered()){failure_line=__LINE__;goto fail;}
    /* A different wait, not an ENTER wait: scheduler blocking frees capacity. */
    while(!__atomic_load_n(&done,__ATOMIC_ACQUIRE))if(wr_futex(&done,0,0,5000000000ull)==-110){failure_line=__LINE__;goto fail;}
    if(result || !entered || call(VEX_EXECUTION_QUERY,0,cap,0)!=2){failure_line=__LINE__;goto fail;}
    if(call(VEX_EXECUTION_LEAVE,0,0,0)){failure_line=__LINE__;goto fail;}
    __atomic_store_n(&release_worker,1,__ATOMIC_RELEASE);
    while(__atomic_load_n(&alive,__ATOMIC_ACQUIRE))wr_futex(&alive,0,1,0);
    if(call(VEX_EXECUTION_QUERY,0,cap,0)){failure_line=__LINE__;goto fail;}
    /* Pending member killed while the caller retains the only active slot. */
    if(call(VEX_EXECUTION_ENTER,0,cap,0)){failure_line=__LINE__;goto fail;}
    started=entered=0;alive=1;
    if(wr_start_thread(worker,0,(char *)stack+65536,&tid,&alive)<0 || spin(&started) || registered()){failure_line=__LINE__;goto fail;}
    if(wr_stop_thread(tid,17)){failure_line=__LINE__;goto fail;}
    while(__atomic_load_n(&alive,__ATOMIC_ACQUIRE))wr_futex(&alive,0,1,0);
    if(entered || call(VEX_EXECUTION_LEAVE,0,0,0)){failure_line=__LINE__;goto fail;}
    int64_t pid=(int64_t)vex_syscall0(VEX_SYS_PROCESS_FORK);if(pid<0){failure_line=__LINE__;goto fail;}
    if(!pid)wr_exit(call(VEX_EXECUTION_ENTER,0,cap,1)==-9?0:1);
    int status=-1;vex_process_wait_req_t wait={(int32_t)pid,&status,0};
    if((int64_t)vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&wait)!=pid || status){failure_line=__LINE__;goto fail;}
    if(call(VEX_EXECUTION_ENTER,0,cap,0)){failure_line=__LINE__;goto fail;}
    started=0;alive=1;result=-999;
    if(wr_start_thread(worker,0,(char *)stack+65536,&tid,&alive)<0 || spin(&started) || registered()){failure_line=__LINE__;goto fail;}
    if(call(VEX_EXECUTION_CLOSE,0,cap,0)){failure_line=__LINE__;goto fail;}
    while(__atomic_load_n(&alive,__ATOMIC_ACQUIRE))wr_futex(&alive,0,1,0);
    if(result!=-125 || call(VEX_EXECUTION_ENTER,0,cap,1)!=-9 || call(VEX_EXECUTION_LEAVE,0,0,0)){failure_line=__LINE__;goto fail;}
    wr_unmap(stack,65536);
    wr_log("native-execution: runnable quota / yield / unrelated block / reactivation / registered remote kill / close wake / foreign token / stale PASS\n");return 0;
fail:wr_log("native-execution: FAIL line=");char line[]="0000\n";unsigned value=failure_line;for(unsigned i=0;i<4;i++){line[3-i]=(char)('0'+value%10);value/=10;}wr_log(line);return 1;
}

