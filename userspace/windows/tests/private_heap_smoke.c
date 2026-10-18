#include <stdint.h>
__declspec(dllimport) void *GetProcessHeap(void),*HeapCreate(unsigned,uint64_t,uint64_t),*HeapAlloc(void *,unsigned,uint64_t),*HeapReAlloc(void *,unsigned,void *,uint64_t),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) int HeapDestroy(void *),HeapFree(void *,unsigned,void *),HeapLock(void *),HeapUnlock(void *),HeapSetInformation(void *,unsigned,const void *,uint64_t),CloseHandle(void *),GetExitCodeThread(void *,unsigned *);
__declspec(dllimport) unsigned GetProcessHeaps(unsigned,void **),GetLastError(void),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) uint64_t HeapSize(void *,unsigned,void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static void *shared;
static unsigned worker(void *argument){
    uintptr_t tag=(uintptr_t)argument;
    for(unsigned i=0;i<80;i++){
        uint64_t size=17+((i*73+tag*31)%3072);unsigned char *a=HeapAlloc(shared,8,size);if(!a || ((uintptr_t)a&15))return 1;
        for(unsigned k=0;k<size;k++)if(a[k])return 2;
        for(unsigned k=0;k<size;k++)a[k]=(unsigned char)(k+tag);
        unsigned char *b=HeapReAlloc(shared,8,a,size+131);if(!b)return 3;
        for(unsigned k=0;k<size;k++)if(b[k]!=(unsigned char)(k+tag))return 4;
        for(unsigned k=(unsigned)size;k<size+131;k++)if(b[k])return 5;
        if(HeapSize(shared,0,b)<size+131 || !HeapFree(shared,0,b))return 6;
    }return 0;
}
void mainCRTStartup(void){
    check(HeapSetInformation(0,1,0,0),"private-heap: actual corruption policy FAIL\n");
#ifdef HEAP_CORRUPT
    void *h=HeapCreate(0,0,0);unsigned char *p=h?HeapAlloc(h,0,32):0;check(p!=0,"private-heap: corruption fixture allocation FAIL\n");
    p[32]^=1;HeapFree(h,0,p);OutputDebugStringA("private-heap: corruption was not fatal FAIL\n");ExitProcess(1);
#else
    unsigned before=GetProcessHeaps(0,0);shared=HeapCreate(0,4096,0);check(shared && GetProcessHeaps(0,0)==before+1,"private-heap: actual object inventory FAIL\n");
    void *list[258];unsigned count=GetProcessHeaps(258,list),found=0;for(unsigned i=0;i<count;i++)if(list[i]==shared)found++;
    check(found==1,"private-heap: private heap enumeration FAIL\n");
    check(HeapLock(shared) && HeapLock(shared),"private-heap: recursive lock FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!HeapDestroy(shared) && GetLastError()==170,"private-heap: locked heap retained FAIL\n");
#endif
    check(HeapUnlock(shared) && HeapUnlock(shared),"private-heap: balanced lock pins FAIL\n");
    void *threads[4];for(unsigned i=0;i<4;i++){threads[i]=CreateThread(0,0,worker,(void *)(uintptr_t)(i+1),0,0);check(threads[i]!=0,"private-heap: concurrent workers FAIL\n");}
    for(unsigned i=0;i<4;i++){unsigned result=99;check(!WaitForSingleObject(threads[i],30000) && GetExitCodeThread(threads[i],&result) && !result && CloseHandle(threads[i]),"private-heap: concurrent realloc/data/zeroing FAIL\n");}
    void *live=HeapAlloc(shared,0,131072);check(live && HeapSize(shared,0,live)>=131072 && HeapDestroy(shared),"private-heap: grow / free all live blocks FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!HeapAlloc(shared,0,8) && GetLastError()==6 && !HeapDestroy(shared) && GetLastError()==6,"private-heap: stale generation refusal FAIL\n");
    check(!HeapDestroy(GetProcessHeap()) && GetLastError()==5,"private-heap: process heap destruction refused FAIL\n");
#endif
    for(unsigned cycle=0;cycle<24;cycle++){
        void *fixed=HeapCreate(0,4096,16384);check(fixed!=0,"private-heap: bounded reservation FAIL\n");
        void *a=HeapAlloc(fixed,0,6000),*b=HeapAlloc(fixed,0,6000);check(a && b && !HeapAlloc(fixed,0,6000),"private-heap: actual fixed capacity FAIL\n");
        check(HeapFree(fixed,0,a) && (a=HeapAlloc(fixed,0,5900)) && HeapFree(fixed,0,a) && HeapFree(fixed,0,b),"private-heap: reuse and coalesce FAIL\n");
        a=HeapAlloc(fixed,0,12000);check(a && HeapDestroy(fixed),"private-heap: merged capacity / exact destruction FAIL\n");
    }
    check(GetProcessHeaps(0,0)==before,"private-heap: no surviving objects FAIL\n");
#ifdef KYIL_NATIVE_TEST
    unsigned lfh=2;check(!HeapSetInformation(GetProcessHeap(),0,&lfh,4) && GetLastError()==50,"private-heap: LFH not falsely advertised FAIL\n");
    check(!HeapCreate(0x40000,0,0) && GetLastError()==5,"private-heap: executable heap rejected FAIL\n");
#endif
    OutputDebugStringA("windows-private-heap: four concurrent workers / grow / fixed capacity / reuse / coalesce / lock pins / destroy / stale / NX PASS\n");ExitProcess(0);
#endif
}
