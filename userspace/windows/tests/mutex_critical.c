#include <stdint.h>
typedef struct {void *debug;int32_t count,recursion;uintptr_t owner;void *semaphore;uintptr_t spin;} critical;
__declspec(dllimport) void *CreateMutexW(void *,int,const uint16_t *);
__declspec(dllimport) int ReleaseMutex(void *),CloseHandle(void *);
__declspec(dllimport) unsigned GetLastError(void),WaitForSingleObject(void *,unsigned),GetCurrentThreadId(void);
__declspec(dllimport) void InitializeCriticalSection(critical *),EnterCriticalSection(critical *),LeaveCriticalSection(critical *),DeleteCriticalSection(critical *);
__declspec(dllimport) int TryEnterCriticalSection(critical *),InitializeCriticalSectionEx(critical *,unsigned,unsigned);
__declspec(dllimport) unsigned SetCriticalSectionSpinCount(critical *,unsigned);
__declspec(dllimport) void *CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) void ExitProcess(unsigned),OutputDebugStringA(const char *),Sleep(unsigned);
static void *mutex;static critical lock;static unsigned value,mutex_value,failed;
static void check(int ok,const char *reason){if(!ok){OutputDebugStringA(reason);ExitProcess(1);}}
static unsigned worker(void *arg) {
    (void)arg;
    for(unsigned i=0;i<2000;i++) {
        EnterCriticalSection(&lock);EnterCriticalSection(&lock);unsigned old=value;Sleep(0);value=old+1;
        LeaveCriticalSection(&lock);LeaveCriticalSection(&lock);
        if(WaitForSingleObject(mutex,5000)) {__atomic_fetch_add(&failed,1,__ATOMIC_RELAXED);return 1;}
        old=mutex_value;Sleep(0);mutex_value=old+1;
        if(!ReleaseMutex(mutex))__atomic_fetch_add(&failed,1,__ATOMIC_RELAXED);
    }return 0;
}
static unsigned abandoning(void *arg) {
    (void)arg;return WaitForSingleObject(mutex,5000)?1:0;
}
void mainCRTStartup(void) {
    mutex=CreateMutexW(0,1,0);check(mutex!=0,"mutex: initial owner FAIL\n");
    check(!WaitForSingleObject(mutex,0) && ReleaseMutex(mutex) && ReleaseMutex(mutex),"mutex: recursion FAIL\n");
    check(!ReleaseMutex(mutex) && GetLastError()==288,"mutex: wrong owner FAIL\n");
    check(!CreateMutexW(0,0,L"unimplemented-name") && GetLastError()==50,"mutex: honest namespace refusal FAIL\n");
    check(InitializeCriticalSectionEx(&lock,32,0x1000000),"critical: initialization FAIL\n");
    check(SetCriticalSectionSpinCount(&lock,64)==32,"critical: spin hint FAIL\n");
    check(TryEnterCriticalSection(&lock) && TryEnterCriticalSection(&lock) && lock.count==1 && lock.recursion==2 && lock.owner==GetCurrentThreadId(),"critical: recursive layout FAIL\n");
    LeaveCriticalSection(&lock);LeaveCriticalSection(&lock);
    void *threads[8];for(unsigned i=0;i<8;i++){threads[i]=CreateThread(0,0,worker,0,0,0);check(threads[i]!=0,"mutex/critical: thread FAIL\n");}
    for(unsigned i=0;i<8;i++)check(!WaitForSingleObject(threads[i],30000) && CloseHandle(threads[i]),"mutex/critical: join FAIL\n");
    check(!failed && value==16000 && mutex_value==16000 && lock.count==-1 && !lock.owner && !lock.recursion,"mutex/critical: exclusion FAIL\n");
    void *t=CreateThread(0,0,abandoning,0,0,0);check(t && !WaitForSingleObject(t,5000) && CloseHandle(t),"mutex: owner exit FAIL\n");
    check(WaitForSingleObject(mutex,0)==128 && ReleaseMutex(mutex) && CloseHandle(mutex),"mutex: WAIT_ABANDONED FAIL\n");
    check(WaitForSingleObject(mutex,0)==UINT32_MAX && GetLastError()==6,"mutex: stale FAIL\n");
    DeleteCriticalSection(&lock);
    OutputDebugStringA("windows-mutex-critical: recursion / x64 layout / 32000 protected increments / blocking / abandoned owner / stale PASS\n");ExitProcess(0);
}
