#include <stdint.h>
__declspec(dllimport) void *CreateEventW(void *,int,int,const uint16_t *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) int SetEvent(void *),ResetEvent(void *),CloseHandle(void *),RegisterWaitForSingleObject(void **,void *,void (*)(void *,uint8_t),void *,unsigned,unsigned),UnregisterWaitEx(void *,void *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),SleepEx(unsigned,int),QueueUserAPC(void (*)(uintptr_t),void *,uintptr_t),GetCurrentThreadId(void),GetLastError(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int yes,const char *text){if(!yes){OutputDebugStringA(text);ExitProcess(1);}}
static void *done,*entered,*release_callback;static unsigned calls,timed,blocking,callback_tid;static uintptr_t apc_value;
static void callback(void *argument,uint8_t timeout) {
    check(argument==(void *)42,"async-contract: callback context FAIL\n");
    __atomic_add_fetch(&calls,1,__ATOMIC_RELAXED);timed=timeout;
    if(blocking){SetEvent(entered);check(!WaitForSingleObject(release_callback,5000),"async-contract: callback release FAIL\n");}
    SetEvent(done);
}
static void apc(uintptr_t value){apc_value=value;callback_tid=GetCurrentThreadId();}
static unsigned apc_thread(void *unused){(void)unused;check(apc_value==345 && callback_tid==GetCurrentThreadId(),"async-contract: startup APC target FAIL\n");return 0;}
void mainCRTStartup(void) {
    void *event=CreateEventW(0,0,0,0);done=CreateEventW(0,1,0,0);entered=CreateEventW(0,1,0,0);release_callback=CreateEventW(0,1,0,0);
    check(event && done && entered && release_callback,"async-contract: event allocation FAIL\n");
    void *registration=0;check(RegisterWaitForSingleObject(&registration,event,callback,(void *)42,UINT32_MAX,8) && registration,"async-contract: actual registration FAIL\n");
    check(SetEvent(event) && !WaitForSingleObject(done,5000) && calls==1 && !timed && UnregisterWaitEx(registration,(void *)-1),"async-contract: signaled callback and drain FAIL\n");
    check(!UnregisterWaitEx(registration,0) && GetLastError()==6,"async-contract: stale registration FAIL\n");
    ResetEvent(done);check(RegisterWaitForSingleObject(&registration,event,callback,(void *)42,10,8),"async-contract: timeout registration FAIL\n");
    check(!WaitForSingleObject(done,5000) && calls==2 && timed && UnregisterWaitEx(registration,(void *)-1),"async-contract: actual timer callback FAIL\n");
    ResetEvent(done);blocking=1;void *completion=CreateEventW(0,1,0,0);
    check(completion && RegisterWaitForSingleObject(&registration,event,callback,(void *)42,UINT32_MAX,8) && SetEvent(event) && !WaitForSingleObject(entered,5000),"async-contract: active callback setup FAIL\n");
    check(!UnregisterWaitEx(registration,completion) && GetLastError()==997 && WaitForSingleObject(completion,0)==258,"async-contract: cancellation must not fake completion FAIL\n");
    check(SetEvent(release_callback) && !WaitForSingleObject(completion,5000) && calls==3,"async-contract: real drained completion event FAIL\n");
    unsigned self=GetCurrentThreadId();check(QueueUserAPC(apc,(void *)-2,123) && !SleepEx(0,0) && !apc_value && SleepEx(0,1)==0xc0 && apc_value==123 && callback_tid==self,"async-contract: alertability and own APC FAIL\n");
    apc_value=0;void *thread=CreateThread(0,0,apc_thread,0,4,0);check(thread && QueueUserAPC(apc,thread,345),"async-contract: queue to paused thread FAIL\n");
    __declspec(dllimport) unsigned ResumeThread(void *);check(ResumeThread(thread)==1 && !WaitForSingleObject(thread,5000) && CloseHandle(thread),"async-contract: remote APC lifecycle FAIL\n");
    check(CloseHandle(event) && CloseHandle(done) && CloseHandle(entered) && CloseHandle(release_callback) && CloseHandle(completion),"async-contract: cleanup FAIL\n");
    OutputDebugStringA("windows-async-contract: true callbacks / timeout / cancellation / completion / stale handles / target-thread APC / alertability PASS\n");ExitProcess(0);
}
