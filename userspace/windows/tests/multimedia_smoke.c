#include <stdint.h>
__declspec(dllimport) void *LoadLibraryExW(const uint16_t *,void *,unsigned),*GetProcAddress(void *,const char *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) int FreeLibrary(void *),CloseHandle(void *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) void Sleep(unsigned),OutputDebugStringA(const char *),ExitProcess(unsigned);
static unsigned (__attribute__((ms_abi)) *clock_ms)(void);
static unsigned failed;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static unsigned worker(void *ignored){(void)ignored;unsigned previous=clock_ms();for(unsigned i=0;i<256;i++){unsigned now=clock_ms();if((int32_t)(now-previous)<0)__atomic_store_n(&failed,1,__ATOMIC_RELAXED);previous=now;}return 0;}
void mainCRTStartup(void){void *library=LoadLibraryExW(L"winmm",0,0x800);
    check(library && (clock_ms=GetProcAddress(library,"timeGetTime")),"multimedia: genuine WinMM clock provider FAIL\n");
    unsigned before=clock_ms();Sleep(70);unsigned elapsed=clock_ms()-before;
    check(elapsed>=30 && elapsed<5000,"multimedia: real elapsed milliseconds FAIL\n");
    void *threads[4];for(unsigned i=0;i<4;i++){threads[i]=CreateThread(0,0,worker,0,0,0);check(threads[i]!=0,"multimedia: actual caller threads FAIL\n");}
    for(unsigned i=0;i<4;i++)check(!WaitForSingleObject(threads[i],10000) && CloseHandle(threads[i]),"multimedia: completed callers FAIL\n");
    check(!failed && FreeLibrary(library),"multimedia: shared monotonic source and release FAIL\n");
    OutputDebugStringA("windows-multimedia: genuine WinMM provider / elapsed milliseconds / four concurrent monotonic callers PASS\n");ExitProcess(0);
}
