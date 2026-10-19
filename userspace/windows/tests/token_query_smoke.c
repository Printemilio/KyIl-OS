#include <stdint.h>
typedef uint16_t W;
__declspec(dllimport) void *LoadLibraryW(const W *),*GetProcAddress(void *,const char *),*GetCurrentProcess(void),*GetCurrentThread(void),*GetStdHandle(unsigned),*OpenProcess(unsigned,int,unsigned),*CreateThread(void *,uint64_t,void *,void *,unsigned,unsigned *);
__declspec(dllimport) unsigned GetLastError(void),GetCurrentProcessId(void),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) int CloseHandle(void *),GetHandleInformation(void *,unsigned *),SetHandleInformation(void *,unsigned,unsigned),DuplicateHandle(void *,void *,void *,void **,unsigned,int,unsigned),WriteFile(void *,const void *,unsigned,unsigned *,void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static int (*open_token)(void *,unsigned,void **),(*thread_token)(void *,unsigned,int,void **),(*information)(void *,unsigned,void *,unsigned,unsigned *),(*valid_sid)(void *),(*equal_sid)(void *,void *);
static void check(int ok,const char *why){if(!ok){unsigned n=0,done;while(why[n])n++;WriteFile(GetStdHandle(-12),why,n,&done,0);OutputDebugStringA(why);ExitProcess(1);}}
static void *shared;
static unsigned worker(void *arg){(void)arg;for(unsigned i=0;i<128;i++){void *h=0;check(DuplicateHandle(GetCurrentProcess(),shared,GetCurrentProcess(),&h,0,0,2),"token-query: concurrent duplicate FAIL\n");
    uint8_t bytes[256];unsigned used=0;check(information(h,1,bytes,sizeof(bytes),&used) && valid_sid(*(void **)bytes) && CloseHandle(h),"token-query: concurrent query/release FAIL\n");}return 0;}
void mainCRTStartup(void){void *m=LoadLibraryW(L"advapi32.dll");check(m!=0,"token-query: provider FAIL\n");
#define B(var,name) var=GetProcAddress(m,name);check(var!=0,"token-query: export " name " FAIL\n")
    B(open_token,"OpenProcessToken");B(thread_token,"OpenThreadToken");B(information,"GetTokenInformation");B(valid_sid,"IsValidSid");B(equal_sid,"EqualSid");
#undef B
    void *p=OpenProcess(0x1000,0,GetCurrentProcessId()),*h=0;check(p && open_token(p,8,&h) && h && CloseHandle(p),"token-query: process query right and subject FAIL\n");
    unsigned used=0,flags=9;check(!information(h,1,0,0,&used) && GetLastError()==122 && used>=24 && used<=256,"token-query: exact buffer sizing FAIL\n");
    uint8_t data[256];check(information(h,1,data,sizeof(data),&used),"token-query: identity query FAIL\n");void *sid=*(void **)data;
    check((uintptr_t)sid>=(uintptr_t)data && (uintptr_t)sid<(uintptr_t)data+used && valid_sid(sid),"token-query: relocated real SID FAIL\n");
    check(GetHandleInformation(h,&flags) && !flags,"token-query: non-inheritable token FAIL\n");
    shared=h;void *a=CreateThread(0,0,worker,0,0,0),*b=CreateThread(0,0,worker,0,0,0);check(a && b && !WaitForSingleObject(a,20000) && !WaitForSingleObject(b,20000) && CloseHandle(a) && CloseHandle(b),"token-query: actual concurrent threads FAIL\n");
    void *copy=0;check(DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),&copy,0,0,2) && CloseHandle(h),"token-query: retained duplicate FAIL\n");
    uint8_t other[256];check(information(copy,1,other,sizeof(other),&used) && equal_sid(sid,*(void **)other),"token-query: creator close preserves identity FAIL\n");
#ifdef KYIL_NATIVE_TEST
    /* The Windows reference host raises STATUS_INVALID_HANDLE on this stale
       NT handle (verified separately); KyIlOS must return ERROR_INVALID_HANDLE. */
    check(!information(h,1,other,sizeof(other),&used) && GetLastError()==6,"token-query: stale generation rejected FAIL\n");
#endif
    void *empty=0;check(open_token(GetCurrentProcess(),2,&empty) && !information(empty,1,other,sizeof(other),&used) && GetLastError()==5 && CloseHandle(empty),"token-query: query access enforced FAIL\n");
    empty=0;check(!open_token(GetCurrentProcess(),0,&empty) && GetLastError()==5 && !empty,"token-query: zero desired access rejected FAIL\n");
    empty=0;check(!thread_token(GetCurrentThread(),8,0,&empty) && GetLastError()==1008 && !empty,"token-query: no phantom impersonation token FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!open_token(GetCurrentProcess(),0x20,&empty) && GetLastError()==50,"token-query: privilege adjustment not granted FAIL\n");
    check(!information(copy,25,other,sizeof(other),&used) && GetLastError()==50,"token-query: no fabricated integrity policy FAIL\n");
    check(!information(copy,1,(void *)0xffff800000000000ull,sizeof(other),&used) && GetLastError()==998,"token-query: kernel pointer rejected FAIL\n");
    check(!SetHandleInformation(copy,1,1) && GetLastError()==50,"token-query: unsupported inheritance not falsely accepted FAIL\n");
    check(SetHandleInformation(copy,2,2) && !CloseHandle(copy) && GetLastError()==5 && SetHandleInformation(copy,2,0),"token-query: protected close FAIL\n");
    unsigned value=99;check(information(copy,20,&value,4,&used) && !value,"token-query: no NT elevation granted FAIL\n");
#endif
    check(CloseHandle(copy),"token-query: last reference FAIL\n");OutputDebugStringA("windows-token-query: authenticated UID projection / real SID / rights / concurrent retain-query-release / stale handles / no impersonation or invented integrity PASS\n");ExitProcess(0);
}
