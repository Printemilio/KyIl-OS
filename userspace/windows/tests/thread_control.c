#include <stdint.h>
__declspec(dllimport) void *CreateThread(void *,uint64_t,void *,void *,unsigned,unsigned *),*CreateEventA(void *,int,int,const char *);
__declspec(dllimport) unsigned SuspendThread(void *),ResumeThread(void *),WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) int SetEvent(void *),CloseHandle(void *),GetThreadContext(void *,void *),QueryThreadCycleTime(void *,uint64_t *),SwitchToThread(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
typedef struct {
    uint64_t home[6];uint32_t flags,mxcsr;uint16_t cs,ds,es,fs,gs,ss;uint32_t eflags;
    uint64_t debug[6],rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15,rip;
    uint8_t fp[512],tail[0x1d0];
} __declspec(align(16)) context;
typedef struct {volatile unsigned stop,ready;uint64_t ticks;void *event;} job;
unsigned spin_registers(job *);
static unsigned worker(void *arg){job *j=arg;SetEvent(j->event);return spin_registers(j);}
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
void mainCRTStartup(void) {
    for(unsigned n=0;n<32;n++) {
        job j={0};j.event=CreateEventA(0,1,0,0);check(j.event!=0,"threads: event FAIL\n");
        unsigned tid=0;void *h=CreateThread(0,0,worker,&j,4,&tid);check(h && tid,"threads: create suspended FAIL\n");
        check(WaitForSingleObject(j.event,0)==258,"threads: created thread ran FAIL\n");
        check(SuspendThread(h)==1 && ResumeThread(h)==2,"threads: initial nested count FAIL\n");
        static context c;c.flags=0x10000b;
        check(GetThreadContext(h,&c) && c.rip && c.rsp && c.cs && c.ss,"threads: initial context FAIL\n");
        check(ResumeThread(h)==1 && WaitForSingleObject(j.event,5000)==0,"threads: start resume FAIL\n");
        while(!__atomic_load_n(&j.ready,__ATOMIC_ACQUIRE))SwitchToThread();
        uint64_t first=0,last=0;check(QueryThreadCycleTime(h,&first),"threads: cycles FAIL\n");
        for(unsigned k=0;k<3;k++) {
            check(SuspendThread(h)==0 && SuspendThread(h)==1,"threads: nested pause FAIL\n");c.flags=0x10000b;
            check(GetThreadContext(h,&c),"threads: running context FAIL\n");
            check(c.r12==0x1234567887654321ull && c.r13==0xfedcba9801234567ull,"threads: GPR snapshot FAIL\n");
            check(*(uint64_t *)(c.fp+160+6*16)==0x9988776655443322ull && *(uint64_t *)(c.fp+168+6*16)==0x1122334455667788ull,"threads: XMM snapshot FAIL\n");
            uint64_t before=j.ticks;for(unsigned i=0;i<16;i++)SwitchToThread();
            check(j.ticks==before,"threads: paused execution advanced FAIL\n");
            check(QueryThreadCycleTime(h,&last) && last>=first,"threads: cycles monotonic FAIL\n");
            first=last;check(ResumeThread(h)==2,"threads: partial resume FAIL\n");
            for(unsigned i=0;i<8;i++)SwitchToThread();check(j.ticks==before,"threads: nested pause released early FAIL\n");
            check(ResumeThread(h)==1,"threads: final resume FAIL\n");
        }
        __atomic_store_n(&j.stop,1,__ATOMIC_RELEASE);check(WaitForSingleObject(h,5000)==0,"threads: exit FAIL\n");
        check(CloseHandle(h),"threads: close FAIL\n");
#ifndef THREAD_HOST_REFERENCE
        check(SuspendThread(h)==~0u && GetLastError()==6,"threads: stale handle FAIL\n");
#endif
        CloseHandle(j.event);
    }
#ifndef THREAD_HOST_REFERENCE
    check(SuspendThread((void *)123)==~0u && GetLastError()==6,"threads: invalid handle FAIL\n");
#endif
    OutputDebugStringA("windows-thread-control: 32 cycles / counted pause / GPR-XMM / no progress / cycles / stale handles PASS\n");ExitProcess(0);
}
