#include <stdint.h>
typedef struct {void *data;unsigned bytes;unsigned char overhead,region;uint16_t flags;unsigned char detail[24];} entry;
__declspec(dllimport) void *GetProcessHeap(void),*HeapAlloc(void *,unsigned,uint64_t),*LocalAlloc(unsigned,uint64_t),*LocalFree(void *);
__declspec(dllimport) int HeapFree(void *,unsigned,void *),HeapLock(void *),HeapUnlock(void *),HeapWalk(void *,entry *),CloseHandle(void *);
__declspec(dllimport) void *CreateEventW(void *,int,int,const uint16_t *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) int SetEvent(void *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void *heap,*started;static unsigned finished;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static unsigned worker(void *unused) {
    (void)unused;SetEvent(started);void *p=HeapAlloc(heap,0,29);
    if(!p || !HeapFree(heap,0,p))return 1;__atomic_store_n(&finished,1,__ATOMIC_RELEASE);return 0;
}
void mainCRTStartup(void) {
    heap=GetProcessHeap();void *a=HeapAlloc(heap,0,17),*b=HeapAlloc(heap,8,39);check(a && b,"heap-walk: actual blocks FAIL\n");
    check(HeapLock(heap) && HeapLock(heap),"heap-walk: recursive explicit lock FAIL\n");
    started=CreateEventW(0,1,0,0);void *thread=CreateThread(0,0,worker,0,0,0);check(started && thread && !WaitForSingleObject(started,5000),"heap-walk: blocked allocator starts FAIL\n");
    check(!__atomic_load_n(&finished,__ATOMIC_ACQUIRE) && WaitForSingleObject(thread,0)==258,"heap-walk: real exclusion FAIL\n");
    entry e={0};unsigned seen=0;
    while(HeapWalk(heap,&e)){check((e.flags&4) && e.overhead && (e.data==a || e.data==b),"heap-walk: actual busy descriptors FAIL\n");seen++;}
    check(seen==2 && GetLastError()==259,"heap-walk: complete enumeration/end FAIL\n");
    check(HeapUnlock(heap) && !finished && HeapUnlock(heap) && !WaitForSingleObject(thread,5000) && finished,"heap-walk: unlock releases allocation FAIL\n");
    check(!HeapUnlock(heap) && GetLastError()==158 && CloseHandle(thread) && CloseHandle(started),"heap-walk: ownership/cleanup FAIL\n");
    check(HeapFree(heap,0,a) && HeapFree(heap,0,b),"heap-walk: real block frees FAIL\n");
    unsigned char *local=LocalAlloc(0x40,31);check(local!=0,"heap-walk: real local allocation FAIL\n");
    for(unsigned i=0;i<31;i++)check(local[i]==0,"heap-walk: local zeroed bytes FAIL\n");
    check(!LocalFree(local) && LocalFree(local)==local && GetLastError()==6,"heap-walk: local lifetime/stale FAIL\n");
    check(!LocalAlloc(2,31) && GetLastError()==50,"heap-walk: unsupported movable local contract FAIL\n");
    OutputDebugStringA("windows-heap-walk: real busy blocks / recursive lock / concurrent allocator exclusion / ownership / local lifetime / zeroing / stale PASS\n");ExitProcess(0);
}
