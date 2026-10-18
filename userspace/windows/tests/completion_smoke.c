#include <stdint.h>
__declspec(dllimport) void *CreateIoCompletionPort(void *,void *,uintptr_t,unsigned);
__declspec(dllimport) int PostQueuedCompletionStatus(void *,unsigned,uintptr_t,void *);
__declspec(dllimport) int GetQueuedCompletionStatus(void *,unsigned *,uintptr_t *,void **,unsigned);
__declspec(dllimport) unsigned GetLastError(void),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) int CloseHandle(void *),SetEvent(void *);
__declspec(dllimport) void *CreateEventW(void *,int,int,const uint16_t *);
__declspec(dllimport) void *CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) void Sleep(unsigned),ExitProcess(unsigned),OutputDebugStringA(const char *);
static void *port,*pause_event;static unsigned ready,got,failed;static uintptr_t sum;
static void check(int ok,const char *reason){if(!ok){OutputDebugStringA(reason);ExitProcess(1);}}
static unsigned consumer(void *arg) {
    __atomic_fetch_add(&ready,1,__ATOMIC_RELEASE);
    unsigned bytes=0;uintptr_t key=0;void *overlap=0;
    if(!GetQueuedCompletionStatus(port,&bytes,&key,&overlap,5000))__atomic_fetch_add(&failed,1,__ATOMIC_RELAXED);
    else {
        if(bytes!=key+7 || overlap!=(void *)(key+0x1000))__atomic_fetch_add(&failed,1,__ATOMIC_RELAXED);
        __atomic_fetch_add(&sum,key,__ATOMIC_RELAXED);__atomic_fetch_add(&got,1,__ATOMIC_RELEASE);
        /* Real event blocking must release a runnable-consumer quota slot. */
        if((uintptr_t)arg && WaitForSingleObject(pause_event,5000))__atomic_fetch_add(&failed,1,__ATOMIC_RELAXED);
    }
    return 0;
}
static unsigned abandoned(void *arg) {
    (void)arg;unsigned bytes=77;uintptr_t key=88;void *overlap=(void *)99;
    __atomic_fetch_add(&ready,1,__ATOMIC_RELEASE);
    if(GetQueuedCompletionStatus(port,&bytes,&key,&overlap,UINT32_MAX) || overlap || GetLastError()!=735)
        __atomic_fetch_add(&failed,1,__ATOMIC_RELAXED);
    return 0;
}
static unsigned quota_timeout(void *arg) {
    (void)arg;unsigned bytes=0;uintptr_t key=0;void *overlap=0;
    if(GetQueuedCompletionStatus(port,&bytes,&key,&overlap,0) || overlap || GetLastError()!=258)
        __atomic_fetch_add(&failed,1,__ATOMIC_RELAXED);
    __atomic_store_n(&ready,1,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&got,__ATOMIC_ACQUIRE))Sleep(0);
    return 0;
}
void mainCRTStartup(void) {
#ifdef IOCP_EXIT_RACE
    port=CreateIoCompletionPort((void *)-1,0,0,1);check(port!=0,"iocp-exit: create FAIL\n");
    for(unsigned i=0;i<8;i++)check(CreateThread(0,0,abandoned,0,0,0)!=0,"iocp-exit: thread FAIL\n");
    while(__atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=8)Sleep(1);
    Sleep(20);OutputDebugStringA("windows-iocp-exit: stop 8 completion waiters\n");ExitProcess(23);
#endif
    port=CreateIoCompletionPort((void *)-1,0,0,1);check(port!=0,"iocp: create FAIL\n");
    unsigned bytes=77;uintptr_t key=88;void *overlap=(void *)99;
    check(!GetQueuedCompletionStatus(port,&bytes,&key,&overlap,0) && !overlap && GetLastError()==258 && bytes==77 && key==88,"iocp: empty timeout FAIL\n");
    for(unsigned i=0;i<512;i++)check(PostQueuedCompletionStatus(port,i+7,i,(void *)(uintptr_t)(i+0x1000)),"iocp: post FAIL\n");
    for(unsigned i=0;i<512;i++)check(GetQueuedCompletionStatus(port,&bytes,&key,&overlap,0) && bytes==i+7 && key==i && overlap==(void *)(uintptr_t)(i+0x1000),"iocp: FIFO/full pointer FAIL\n");
    check(PostQueuedCompletionStatus(port,99,0x1234567887654321ull,(void *)0xabcddcba12345678ull),"iocp: wide packet FAIL\n");
    void *quota=CreateThread(0,0,quota_timeout,0,0,0);check(quota!=0,"iocp: quota thread FAIL\n");
    while(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE))Sleep(0);
    check(!failed && !GetQueuedCompletionStatus(port,&bytes,&key,&overlap,0) && !overlap && GetLastError()==258,"iocp: resumed quota enforcement FAIL\n");
    __atomic_store_n(&got,1,__ATOMIC_RELEASE);
    check(!WaitForSingleObject(quota,5000) && CloseHandle(quota),"iocp: quota join FAIL\n");
    check(GetQueuedCompletionStatus(port,&bytes,&key,&overlap,0) && bytes==99 && key==0x1234567887654321ull && overlap==(void *)0xabcddcba12345678ull,"iocp: wide packet retention FAIL\n");
    ready=got=0;
    /* Switch association so main is no longer an active consumer of port. */
    void *other=CreateIoCompletionPort((void *)-1,0,0,0);check(other!=0,"iocp: default capacity FAIL\n");
    check(PostQueuedCompletionStatus(other,1,0,0) && GetQueuedCompletionStatus(other,&bytes,&key,&overlap,0),"iocp: reassociation FAIL\n");
    pause_event=CreateEventW(0,1,0,0);check(pause_event!=0,"iocp: event FAIL\n");
    void *threads[8];for(unsigned i=0;i<8;i++){threads[i]=CreateThread(0,0,consumer,(void *)1,0,0);check(threads[i]!=0,"iocp: thread FAIL\n");}
    while(__atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=8)Sleep(1);
    for(unsigned i=0;i<8;i++)check(PostQueuedCompletionStatus(port,i+7,i,(void *)(uintptr_t)(i+0x1000)),"iocp: concurrent post FAIL\n");
    /* Joining before signaling the event would deadlock if blocked consumers
       erroneously held all admission. Only the test deadline is timed. */
    unsigned waited=0;while(__atomic_load_n(&got,__ATOMIC_ACQUIRE)!=8 && waited++<5000)Sleep(1);
    check(got==8 && sum==28 && !failed,"iocp: scheduler blocking admission FAIL\n");
    check(SetEvent(pause_event),"iocp: release event FAIL\n");
    for(unsigned i=0;i<8;i++)check(!WaitForSingleObject(threads[i],5000) && CloseHandle(threads[i]),"iocp: join FAIL\n");
    ready=0;void *t=CreateThread(0,0,abandoned,0,0,0);check(t!=0,"iocp: abandoned thread FAIL\n");
    while(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE))Sleep(1);
    /* Give a finite registration window only in this test; close/registration
       is also exercised repeatedly below and native wake is not timed. */
    Sleep(20);void *stale=port;check(CloseHandle(port),"iocp: close FAIL\n");
    check(!WaitForSingleObject(t,5000) && CloseHandle(t) && !failed,"iocp: abandoned waiter FAIL\n");
#ifndef IOCP_HOST_REFERENCE
    check(!PostQueuedCompletionStatus(stale,0,0,0) && GetLastError()==6 && !CloseHandle(stale),"iocp: stale FAIL\n");
#endif
    for(unsigned i=0;i<128;i++){void *p=CreateIoCompletionPort((void *)-1,0,0,2);
        check(p && p!=stale && PostQueuedCompletionStatus(p,7,0,0) && CloseHandle(p),"iocp: queued teardown FAIL\n");}
#ifndef IOCP_HOST_REFERENCE
    check(!CreateIoCompletionPort((void *)123,0,0,1) && GetLastError()==6,"iocp: invalid file FAIL\n");
#endif
    check(!CreateIoCompletionPort((void *)-1,other,0,1) && GetLastError()==87,"iocp: invalid create combination FAIL\n");
    check(CloseHandle(other) && CloseHandle(pause_event),"iocp: cleanup FAIL\n");
    OutputDebugStringA("windows-iocp: FIFO / timeout / reassociation / 8 consumers / blocking quota / close-wake / stale / 128 queued teardown cycles PASS\n");ExitProcess(0);
}
