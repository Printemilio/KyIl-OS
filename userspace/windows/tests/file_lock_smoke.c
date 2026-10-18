#include <stdint.h>
typedef struct {uintptr_t status,bytes;uint32_t offset,high;void *event;} overlap;
typedef uint16_t W;
__declspec(dllimport) void *CreateFileW(const W *,unsigned,unsigned,void *,unsigned,unsigned,void *),*CreateEventW(void *,int,int,const W *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *),*GetCurrentProcess(void);
__declspec(dllimport) int LockFileEx(void *,unsigned,unsigned,unsigned,unsigned,overlap *),UnlockFileEx(void *,unsigned,unsigned,unsigned,overlap *),ReadFile(void *,void *,unsigned,unsigned *,void *),WriteFile(void *,const void *,unsigned,unsigned *,void *),SetFilePointerEx(void *,int64_t,int64_t *,unsigned),FlushFileBuffers(void *),CloseHandle(void *),DuplicateHandle(void *,void *,void *,void **,unsigned,int,unsigned),DeleteFileW(const W *),SetEvent(void *);
__declspec(dllimport) unsigned GetLastError(void),GetTempPathW(unsigned,W *),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int yes,const char *why){if(!yes){OutputDebugStringA(why);unsigned n=0,written;while(why[n])n++;WriteFile(GetStdHandle((unsigned)-12),why,n,&written,0);ExitProcess(1);}}
static void *other,*entered,*done;
static unsigned waiter(void *unused) {
    (void)unused;overlap o={0};SetEvent(entered);
    check(LockFileEx(other,2,0,8,0,&o),"file-lock: blocking grant FAIL\n");
    check(UnlockFileEx(other,0,8,0,&o),"file-lock: blocking unlock FAIL\n");SetEvent(done);return 0;
}
void mainCRTStartup(void) {
    static W path[512];unsigned n=GetTempPathW(480,path);const W *suffix=(const W *)L"mandatory-lock.txt";
    check(n && n<480,"file-lock: temporary root FAIL\n");while(*suffix)path[n++]=*suffix++;path[n]=0;DeleteFileW(path);
    void *h=CreateFileW(path,0xc0000000u,7,0,1,0x80,0);unsigned bytes;
    check(h!=(void *)-1 && WriteFile(h,"0123456789abcdef",16,&bytes,0) && bytes==16 && FlushFileBuffers(h),"file-lock: actual file FAIL\n");
    other=CreateFileW(path,0xc0000000u,7,0,3,0x80,0);check(other!=(void *)-1,"file-lock: second opening FAIL\n");
    overlap o={0},wrong={.offset=1};char buffer[16];
    check(LockFileEx(h,3,0,8,0,&o),"file-lock: exclusive grant FAIL\n");
    check(!LockFileEx(h,3,0,8,0,&o) && GetLastError()==33,"file-lock: own overlap denial FAIL\n");
    check(!ReadFile(other,buffer,1,&bytes,0) && GetLastError()==33 && !WriteFile(other,"x",1,&bytes,0) && GetLastError()==33,"file-lock: mandatory read/write FAIL\n");
    check(SetFilePointerEx(other,8,0,0) && ReadFile(other,buffer,8,&bytes,0) && bytes==8,"file-lock: adjacent unaffected FAIL\n");
    check(!UnlockFileEx(h,0,8,0,&wrong) && GetLastError()==158,"file-lock: exact unlock FAIL\n");
    check(LockFileEx(h,1,0,8,0,&o) && UnlockFileEx(h,0,8,0,&o),"file-lock: overlapping shared/exclusive order FAIL\n");
    check(SetFilePointerEx(other,0,0,0) && ReadFile(other,buffer,1,&bytes,0) && SetFilePointerEx(h,0,0,0) && !WriteFile(h,"x",1,&bytes,0) && GetLastError()==33 && UnlockFileEx(h,0,8,0,&o),"file-lock: shared denies owner writes FAIL\n");
    void *duplicate=0,*self=GetCurrentProcess();
    check(LockFileEx(h,3,0,8,0,&o) && DuplicateHandle(self,h,self,&duplicate,0,0,2) && CloseHandle(h),"file-lock: duplicate lifetime setup FAIL\n");
    check(!ReadFile(other,buffer,1,&bytes,0) && GetLastError()==33,"file-lock: lock survives original close FAIL\n");
    check(CloseHandle(duplicate) && ReadFile(other,buffer,1,&bytes,0),"file-lock: last local description release FAIL\n");
    h=CreateFileW(path,0xc0000000u,7,0,3,0x80,0);entered=CreateEventW(0,1,0,0);done=CreateEventW(0,1,0,0);
    check(h!=(void *)-1 && entered && done && LockFileEx(h,3,0,8,0,&o),"file-lock: waiter setup FAIL\n");
    void *thread=CreateThread(0,0,waiter,0,0,0);
    check(thread && !WaitForSingleObject(entered,5000) && WaitForSingleObject(done,30)==258 && UnlockFileEx(h,0,8,0,&o) && !WaitForSingleObject(done,5000) && !WaitForSingleObject(thread,5000),"file-lock: sleep/wake actual completion FAIL\n");
    check(CloseHandle(thread) && CloseHandle(entered) && CloseHandle(done) && CloseHandle(h) && CloseHandle(other) && DeleteFileW(path),"file-lock: cleanup FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!LockFileEx(h,3,0,8,0,&o) && GetLastError()==6 && !LockFileEx((void *)-1,4,0,1,0,&o) && GetLastError()==87,"file-lock: invalid handle/flags FAIL\n");
#endif
    OutputDebugStringA("windows-file-lock: mandatory ranges / exact unlock / shared and exclusive / duplicate lifetime / scheduler wake / stale handles PASS\n");ExitProcess(0);
}
