#include <stdint.h>
#include "../handle_value.h"
__declspec(dllimport) void *CreateEventW(void *,int,int,const uint16_t *);
__declspec(dllimport) int CloseHandle(void *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
void mainCRTStartup(void){void *first=0,*last=0;
    for(unsigned i=0;i<WR_HANDLE_GENERATION_MAX+2;i++){
        void *h=CreateEventW(0,0,1,0);if(!i)first=h;last=h;
        check(h && (uintptr_t)h==(uintptr_t)(int32_t)(uintptr_t)h && !((uintptr_t)h&3),"handle-values: LONG roundtrip/alignment FAIL\n");
        check(!WaitForSingleObject(h,0) && CloseHandle(h),"handle-values: actual event use/close FAIL\n");
    }
    check(wr_handle_slot((uintptr_t)first)!=wr_handle_slot((uintptr_t)last),"handle-values: exhausted slot not retired FAIL\n");
    check(WaitForSingleObject(first,0)==UINT32_MAX && GetLastError()==6,"handle-values: no stale revival after generation exhaustion FAIL\n");
    check(WaitForSingleObject((void *)((uintptr_t)last|(1ull<<32)),0)==UINT32_MAX && GetLastError()==6,"handle-values: upper bits rejected FAIL\n");
    OutputDebugStringA("windows-handle-values: 16385 real event lifecycles / LONG roundtrip / aligned / generation retirement / stale and upper-bit rejection PASS\n");ExitProcess(0);
}
