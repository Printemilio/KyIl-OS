#include <stdint.h>
typedef struct {void *debug;int32_t count,recursion;uintptr_t owner;void *semaphore;uintptr_t spin;} critical;
__declspec(dllimport) void *LoadLibraryExW(const uint16_t *,void *,unsigned),*GetProcAddress(void *,const char *),*GetModuleHandleW(const uint16_t *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) int GetModuleHandleExW(unsigned,const uint16_t *,void **),FreeLibrary(void *),CloseHandle(void *);
__declspec(dllimport) unsigned GetCurrentThreadId(void),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) void EnterCriticalSection(critical *),LeaveCriticalSection(critical *),Sleep(unsigned),OutputDebugStringA(const char *),ExitProcess(unsigned);
static unsigned entered;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static critical *loader(void){uintptr_t teb;__asm__ volatile("movq %%gs:0x30,%0":"=r"(teb));uintptr_t peb=*(uintptr_t *)(teb+0x60);return *(critical **)(peb+0x110);}
static unsigned worker(void *ignored){(void)ignored;__atomic_fetch_add(&entered,1,__ATOMIC_RELEASE);
    for(unsigned i=0;i<32;i++)check(GetModuleHandleW(0)!=0,"loader-lock: real concurrent lookup FAIL\n");return 0;}
void mainCRTStartup(void){critical *c=loader();check(c && !c->owner && !c->recursion,"loader-lock: real PEB lock outside loader FAIL\n");
    void *library=LoadLibraryExW(L"kyil-loader-lock-probe.dll",0,0x200);check(library!=0,"loader-lock: actual constructor state FAIL\n");
    void (__attribute__((ms_abi)) *counts)(unsigned *,unsigned *,unsigned *)=GetProcAddress(library,"ProbeCounts");
    check(counts!=0,"loader-lock: actual DLL export FAIL\n");void *pinned=0;
    check(GetModuleHandleExW(1|4,(const uint16_t *)counts,&pinned) && pinned==library,"loader-lock: pinned probe lifetime FAIL\n");
    EnterCriticalSection(c);check(c->owner==GetCurrentThreadId() && c->recursion==1,"loader-lock: actual Windows critical-section ownership FAIL\n");
    EnterCriticalSection(c);check(c->recursion==2 && GetModuleHandleW(0) && c->recursion==2,"loader-lock: canonical reentrant engine FAIL\n");LeaveCriticalSection(c);
    void *threads[4];for(unsigned i=0;i<4;i++){threads[i]=CreateThread(0,0,worker,0,0,0);check(threads[i]!=0,"loader-lock: real native worker FAIL\n");}
    Sleep(40);check(!__atomic_load_n(&entered,__ATOMIC_ACQUIRE),"loader-lock: thread attach bypassed actual lock FAIL\n");LeaveCriticalSection(c);
    for(unsigned i=0;i<4;i++)check(!WaitForSingleObject(threads[i],10000) && CloseHandle(threads[i]),"loader-lock: lost wake or completion FAIL\n");
    unsigned a,d,f;counts(&a,&d,&f);check(entered==4 && a==4 && d==4 && !f && !c->owner && !c->recursion && c->count==-1,"loader-lock: callbacks / release / single engine FAIL\n");
    check(FreeLibrary(library),"loader-lock: pinned reference release FAIL\n");
    OutputDebugStringA("windows-loader-lock: real PEB critical-section / constructor ownership / recursive loader calls / four blocked thread startups / wakeups / detach / idle state PASS\n");ExitProcess(0);
}
