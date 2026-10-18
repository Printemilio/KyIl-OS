#include <stdint.h>
typedef struct {void *debug;int32_t count,recursion;uintptr_t owner;void *semaphore;uintptr_t spin;} critical;
__declspec(dllimport) void InitializeSRWLock(uint64_t *),AcquireSRWLockExclusive(uint64_t *),ReleaseSRWLockExclusive(uint64_t *);
__declspec(dllimport) void AcquireSRWLockShared(uint64_t *),ReleaseSRWLockShared(uint64_t *),InitializeConditionVariable(uint64_t *);
__declspec(dllimport) int SleepConditionVariableSRW(uint64_t *,uint64_t *,unsigned,unsigned);
__declspec(dllimport) int SleepConditionVariableCS(uint64_t *,critical *,unsigned);
__declspec(dllimport) void WakeConditionVariable(uint64_t *),WakeAllConditionVariable(uint64_t *);
__declspec(dllimport) void InitializeCriticalSection(critical *),EnterCriticalSection(critical *),LeaveCriticalSection(critical *),DeleteCriticalSection(critical *);
__declspec(dllimport) void InitOnceInitialize(uint64_t *);
__declspec(dllimport) int InitOnceBeginInitialize(uint64_t *,unsigned,int *,void **),InitOnceComplete(uint64_t *,unsigned,void *);
__declspec(dllimport) int InitOnceExecuteOnce(uint64_t *,int (*)(uint64_t *,void *,void **),void *,void **);
__declspec(dllimport) void *CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) void *CreateEventW(void *,int,int,const uint16_t *);
__declspec(dllimport) int SetEvent(void *),CloseHandle(void *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static uint64_t lock,condition,ack_condition,once;
static unsigned credit,acknowledged,ready,stop,once_calls,once_value;
static void *initialize_started,*initialize_allowed;
static void check(int ok,const char *reason){if(!ok){OutputDebugStringA(reason);ExitProcess(1);}}
static unsigned consumer(void *unused) {
    (void)unused;
    for(unsigned i=0;i<400;i++) {
        AcquireSRWLockExclusive(&lock);
        while(!credit)check(SleepConditionVariableSRW(&condition,&lock,5000,0),"condition: consumer wake FAIL\n");
        credit--;acknowledged++;WakeConditionVariable(&ack_condition);ReleaseSRWLockExclusive(&lock);
    }return 0;
}
static unsigned shared_reader(void *unused) {
    (void)unused;AcquireSRWLockShared(&lock);__atomic_add_fetch(&ready,1,__ATOMIC_RELEASE);WakeAllConditionVariable(&ack_condition);
    while(!stop)check(SleepConditionVariableSRW(&condition,&lock,5000,1),"condition: shared wake/reacquire FAIL\n");
    ReleaseSRWLockShared(&lock);return 0;
}
static int initialize(uint64_t *object,void *parameter,void **context) {
    (void)object;check(parameter==&once_value,"once: actual parameter FAIL\n");
    __atomic_add_fetch(&once_calls,1,__ATOMIC_RELAXED);SetEvent(initialize_started);
    check(!WaitForSingleObject(initialize_allowed,5000),"once: initializer wait FAIL\n");
    once_value=0x39175;*context=&once_value;return 1;
}
static unsigned once_worker(void *unused) {
    (void)unused;void *context=0;
    for(unsigned i=0;i<100;i++)check(InitOnceExecuteOnce(&once,initialize,&once_value,&context) && context==&once_value && once_value==0x39175,
        "once: concurrent context/publication FAIL\n");return 0;
}
void mainCRTStartup(void) {
    InitializeSRWLock(&lock);InitializeConditionVariable(&condition);InitializeConditionVariable(&ack_condition);
    AcquireSRWLockExclusive(&lock);
    check(!SleepConditionVariableSRW(&condition,&lock,0,0) && GetLastError()==1460,"condition: timeout and reacquire FAIL\n");
    check(!SleepConditionVariableSRW(&condition,&lock,0,2) && GetLastError()==87,"condition: invalid flags FAIL\n");
    ReleaseSRWLockExclusive(&lock);
    void *thread=CreateThread(0,0,consumer,0,0,0);check(thread!=0,"condition: consumer start FAIL\n");
    for(unsigned i=0;i<400;i++) {
        AcquireSRWLockExclusive(&lock);credit++;WakeConditionVariable(&condition);
        while(acknowledged<=i)check(SleepConditionVariableSRW(&ack_condition,&lock,5000,0),"condition: ack/lost wake FAIL\n");
        ReleaseSRWLockExclusive(&lock);
    }
    check(!WaitForSingleObject(thread,5000) && CloseHandle(thread) && acknowledged==400 && !credit,"condition: actual handoffs FAIL\n");
    void *threads[8];for(unsigned i=0;i<8;i++){threads[i]=CreateThread(0,0,shared_reader,0,0,0);check(threads[i]!=0,"condition: shared reader start FAIL\n");}
    AcquireSRWLockExclusive(&lock);
    while(__atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=8)check(SleepConditionVariableSRW(&ack_condition,&lock,5000,0),"condition: shared registration FAIL\n");
    stop=1;WakeAllConditionVariable(&condition);ReleaseSRWLockExclusive(&lock);
    for(unsigned i=0;i<8;i++)check(!WaitForSingleObject(threads[i],5000) && CloseHandle(threads[i]),"condition: shared joins FAIL\n");
    critical cs;InitializeCriticalSection(&cs);EnterCriticalSection(&cs);
    check(!SleepConditionVariableCS(&condition,&cs,1) && GetLastError()==1460 && cs.recursion==1,"condition: CS timeout ownership FAIL\n");
    LeaveCriticalSection(&cs);DeleteCriticalSection(&cs);
    InitOnceInitialize(&once);int pending=0;void *context=0;
    check(!InitOnceBeginInitialize(&once,1,&pending,&context) && GetLastError()==31,"once: uninitialized check only FAIL\n");
    check(InitOnceBeginInitialize(&once,0,&pending,&context) && pending && InitOnceComplete(&once,4,0),"once: failed attempt rollback FAIL\n");
    initialize_started=CreateEventW(0,1,0,0);initialize_allowed=CreateEventW(0,1,0,0);
    check(initialize_started && initialize_allowed,"once: actual test gates FAIL\n");
    for(unsigned i=0;i<8;i++){threads[i]=CreateThread(0,0,once_worker,0,0,0);check(threads[i]!=0,"once: contenders start FAIL\n");}
    check(!WaitForSingleObject(initialize_started,5000) && SetEvent(initialize_allowed),"once: initializer release FAIL\n");
    for(unsigned i=0;i<8;i++)check(!WaitForSingleObject(threads[i],5000) && CloseHandle(threads[i]),"once: contenders joins FAIL\n");
    check(once_calls==1 && InitOnceBeginInitialize(&once,1,&pending,&context) && !pending && context==&once_value,"once: exactly once FAIL\n");
    check(CloseHandle(initialize_started) && CloseHandle(initialize_allowed),"once: gate cleanup FAIL\n");
    uint64_t asynchronous=0;
    check(InitOnceBeginInitialize(&asynchronous,2,&pending,0) && pending && InitOnceBeginInitialize(&asynchronous,2,&pending,0) && pending,
        "once: genuine concurrent async attempts FAIL\n");
    check(!InitOnceBeginInitialize(&asynchronous,0,&pending,0) && GetLastError()==87,"once: mixed mode refusal FAIL\n");
    check(!InitOnceComplete(&asynchronous,2,(void *)1) && GetLastError()==87 && InitOnceComplete(&asynchronous,2,&once_value) &&
        !InitOnceComplete(&asynchronous,2,0) && GetLastError()==31,"once: alignment and completion winner FAIL\n");
    OutputDebugStringA("windows-condition-once: 400 real SRW handoffs / shared wake-all / CS reacquire / timeout / 8 contenders / retry / async CAS / published context PASS\n");ExitProcess(0);
}
