#include <stdint.h>
__declspec(dllimport) void *CreateEventW(void *,int,int,const uint16_t *),*CreateMutexW(void *,int,const uint16_t *),*CreateSemaphoreW(void *,int,int,const uint16_t *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) unsigned WaitForMultipleObjects(unsigned,const void *,int,unsigned),WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) int SetEvent(void *),ResetEvent(void *),CloseHandle(void *),ReleaseMutex(void *),ReleaseSemaphore(void *,int,int *),GetExitCodeThread(void *,unsigned *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void *ready,*input[2],*abandoned;static unsigned result;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static unsigned wait_thread(void *all){SetEvent(ready);result=WaitForMultipleObjects(2,input,all!=0,10000);return result;}
static unsigned abandon(void *unused){(void)unused;return WaitForSingleObject(abandoned,5000);}
static void join(void *thread,unsigned expected){unsigned status=99;check(!WaitForSingleObject(thread,5000) && GetExitCodeThread(thread,&status) && status==expected && result==expected && CloseHandle(thread),"multiple-wait: actual waiter result FAIL\n");}
void mainCRTStartup(void){
    ready=CreateEventW(0,1,0,0);input[0]=CreateEventW(0,0,0,0);input[1]=CreateEventW(0,0,0,0);check(ready && input[0] && input[1],"multiple-wait: real objects FAIL\n");
    check(SetEvent(input[0]) && WaitForMultipleObjects(2,input,1,0)==258 && !WaitForSingleObject(input[0],0),"multiple-wait: wait-all must not consume partially FAIL\n");
    void *thread=CreateThread(0,0,wait_thread,(void *)1,0,0);check(thread && !WaitForSingleObject(ready,5000),"multiple-wait: waiter starts FAIL\n");
#ifdef WAIT_EXIT
    OutputDebugStringA("windows-multiple-wait: exiting with registered waitset\n");ExitProcess(17);
#endif
    check(SetEvent(input[1]) && SetEvent(input[0]),"multiple-wait: signal both FAIL\n");join(thread,0);
    check(WaitForSingleObject(input[0],0)==258 && WaitForSingleObject(input[1],0)==258,"multiple-wait: exact auto-reset consumption FAIL\n");
    for(unsigned i=0;i<24;i++){
        check(ResetEvent(ready),"multiple-wait: ready reset FAIL\n");thread=CreateThread(0,0,wait_thread,0,0,0);
        check(thread && !WaitForSingleObject(ready,5000) && SetEvent(input[i&1]),"multiple-wait: concurrent registration/signal FAIL\n");join(thread,i&1);
    }
    check(SetEvent(input[1]) && SetEvent(input[0]) && !WaitForMultipleObjects(2,input,0,0) && WaitForMultipleObjects(2,input,0,0)==1,"multiple-wait: lowest index / exact tokens FAIL\n");
    check(CloseHandle(input[0]) && CloseHandle(input[1]),"multiple-wait: actual close FAIL\n");
    input[0]=CreateSemaphoreW(0,1,4,0);input[1]=CreateMutexW(0,0,0);check(input[0] && input[1] && !WaitForMultipleObjects(2,input,1,0),"multiple-wait: semaphore and mutex atomic acquire FAIL\n");
    check(WaitForSingleObject(input[0],0)==258 && ReleaseMutex(input[1]),"multiple-wait: ownership and semaphore count FAIL\n");int previous=-1;
    check(ReleaseSemaphore(input[0],1,&previous) && !previous && !WaitForMultipleObjects(2,input,1,0) && ReleaseMutex(input[1]) && CloseHandle(input[0]) && CloseHandle(input[1]),"multiple-wait: real semaphore release / mutex ownership FAIL\n");
    abandoned=CreateMutexW(0,0,0);thread=CreateThread(0,0,abandon,0,0,0);unsigned status=99;
    check(abandoned && thread && !WaitForSingleObject(thread,5000) && GetExitCodeThread(thread,&status) && !status && CloseHandle(thread),"multiple-wait: abandoned fixture FAIL\n");
    input[0]=abandoned;input[1]=ready;check(SetEvent(ready) && WaitForMultipleObjects(2,input,1,0)==128 && ReleaseMutex(abandoned),"multiple-wait: abandoned mutex in wait-all FAIL\n");
    void *duplicates[2]={ready,ready};check(WaitForMultipleObjects(2,duplicates,1,0)==UINT32_MAX && GetLastError()==87,"multiple-wait: duplicate handles refused FAIL\n");
    check(CloseHandle(abandoned) && CloseHandle(ready),"multiple-wait: final reference cleanup FAIL\n");
    OutputDebugStringA("windows-multiple-wait: atomic all / ordered any / no partial consumption / real blocking / mutex ownership / abandonment / concurrent signals / cleanup PASS\n");ExitProcess(0);
}
