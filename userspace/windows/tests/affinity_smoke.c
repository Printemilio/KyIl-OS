#include <stdint.h>
__declspec(dllimport) uintptr_t SetThreadAffinityMask(void *,uintptr_t);
__declspec(dllimport) unsigned GetCurrentProcessorNumber(void),WaitForSingleObject(void *,unsigned),GetLastError(void),ResumeThread(void *);
__declspec(dllimport) void *CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) int CloseHandle(void *);
__declspec(dllimport) void Sleep(unsigned),OutputDebugStringA(const char *),ExitProcess(unsigned);
static volatile unsigned failed;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static unsigned worker(void *p){unsigned expected=(unsigned)(uintptr_t)p;
    for(unsigned i=0;i<64;i++){if(GetCurrentProcessorNumber()!=expected)failed=1;Sleep(0);}return 0;}
#ifdef AFFINITY_EXIT
static unsigned churn(void *unused){(void)unused;for(;;){if(!SetThreadAffinityMask((void *)-2,1))failed=1;}return 0;}
#endif
void mainCRTStartup(void){
#ifdef AFFINITY_EXIT
    void *thread=CreateThread(0,0,churn,0,0,0);check(thread!=0,"affinity: churn thread FAIL\n");Sleep(25);
    check(!failed,"affinity: churn operation FAIL\n");ExitProcess(17);
#endif
    uintptr_t all=SetThreadAffinityMask((void *)-2,1);check(all,"affinity: initial allowed set FAIL\n");
    check(!SetThreadAffinityMask((void *)-2,0) && GetLastError()==87,"affinity: empty refused FAIL\n");
    check(!SetThreadAffinityMask((void *)0x1234,1),"affinity: invalid handle refused FAIL\n");
    for(unsigned i=0;i<64;i++)if(all&((uintptr_t)1<<i)){
        check(SetThreadAffinityMask((void *)-2,(uintptr_t)1<<i),"affinity: migration request FAIL\n");
        check(GetCurrentProcessorNumber()==i,"affinity: actual execution placement FAIL\n");
        void *thread=CreateThread(0,0,worker,(void *)(uintptr_t)i,4,0);check(thread!=0,"affinity: real thread FAIL\n");
        check(SetThreadAffinityMask(thread,(uintptr_t)1<<i) && ResumeThread(thread)==1 && !WaitForSingleObject(thread,20000) && CloseHandle(thread) && !failed,"affinity: remote placement / wake / execution FAIL\n");
    }
    check(SetThreadAffinityMask((void *)-2,all),"affinity: restore multi-CPU allowed set FAIL\n");
    for(unsigned i=0;i<32;i++){unsigned cpu=GetCurrentProcessorNumber();check(cpu<64 && (all&((uintptr_t)1<<cpu)),"affinity: allowed-set selection FAIL\n");Sleep(0);}
    OutputDebugStringA("windows-affinity: local migration / remote suspended thread / exact allowed CPU / invalid handles / multi-CPU restore PASS\n");ExitProcess(0);
}
