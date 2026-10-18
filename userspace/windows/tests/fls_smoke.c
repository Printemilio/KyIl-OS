#include <stdint.h>
__declspec(dllimport) unsigned FlsAlloc(void (*)(void *)),GetLastError(void),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) void *FlsGetValue(unsigned),*CreateEventW(void *,int,int,const uint16_t *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) int FlsSetValue(unsigned,void *),FlsFree(unsigned),SetEvent(void *),CloseHandle(void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static unsigned slot,ready,calls,sum;static void *go,*all;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static void cleanup(void *value){__atomic_add_fetch(&calls,1,__ATOMIC_RELAXED);__atomic_add_fetch(&sum,(unsigned)(uintptr_t)value,__ATOMIC_RELAXED);}
static unsigned worker(void *value) {
    check(FlsSetValue(slot,value) && FlsGetValue(slot)==value,"fls: thread-local values FAIL\n");
    if(__atomic_add_fetch(&ready,1,__ATOMIC_RELEASE)==8)SetEvent(all);
    check(!WaitForSingleObject(go,5000),"fls: actual wait FAIL\n");return 0;
}
void mainCRTStartup(void) {
    for(unsigned phase=0;phase<2;phase++) {
        ready=0;slot=FlsAlloc(cleanup);check(slot!=UINT32_MAX,"fls: actual allocation FAIL\n");
        check(!FlsGetValue(slot) && !GetLastError(),"fls: initial zero FAIL\n");
        go=CreateEventW(0,1,0,0);all=CreateEventW(0,1,0,0);check(go && all,"fls: event gates FAIL\n");
        void *threads[8];for(unsigned i=0;i<8;i++){threads[i]=CreateThread(0,0,worker,(void *)(uintptr_t)(i+1),0,0);check(threads[i]!=0,"fls: real threads FAIL\n");}
        check(!WaitForSingleObject(all,5000),"fls: thread values registered FAIL\n");
        if(!phase) {
            check(FlsSetValue(slot,(void *)9) && FlsFree(slot) && calls==9 && sum==45,"fls: process-wide detach and callbacks FAIL\n");
            check(!FlsGetValue(slot) && GetLastError()==87 && !FlsSetValue(slot,(void *)1),"fls: stale index FAIL\n");
        }
        check(SetEvent(go),"fls: release actual threads FAIL\n");
        for(unsigned i=0;i<8;i++)check(!WaitForSingleObject(threads[i],5000) && CloseHandle(threads[i]),"fls: cleanup completion FAIL\n");
        if(phase)check(calls==17 && sum==81 && FlsFree(slot),"fls: thread-exit callback exactly once FAIL\n");
        else check(calls==9 && sum==45,"fls: no callbacks duplicated at exit FAIL\n");
        check(CloseHandle(go) && CloseHandle(all),"fls: gate lifetime FAIL\n");
    }
    check(!FlsFree(slot) && GetLastError()==87,"fls: repeat free FAIL\n");
    OutputDebugStringA("windows-fls: 8 thread-local values / free vs exit ownership / actual callbacks / index reuse / stale / exactly once PASS\n");ExitProcess(0);
}
