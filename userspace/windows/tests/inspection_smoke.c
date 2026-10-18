#include <stdint.h>
typedef uint16_t W;
typedef struct {uint16_t length,maximum;unsigned pad;W *buffer;} unicode;
typedef struct {unsigned status,pad;void *peb;uint64_t affinity;int priority;unsigned pad2;uint64_t pid,parent;} process_info;
typedef struct {uint64_t created,access,write,change;unsigned attrs,pad;} basic_file;
typedef struct {int64_t allocated,size;unsigned links;unsigned char pending,directory,pad[2];} standard_file;
__declspec(dllimport) unsigned NtQueryInformationProcess(void *,unsigned,void *,unsigned,unsigned *),NtQueryObject(void *,unsigned,void *,unsigned,unsigned *),RtlGetLastNtStatus(void),RtlNtStatusToDosError(unsigned);
__declspec(dllimport) void RtlSetLastWin32ErrorAndNtStatusFromNtStatus(unsigned);
__declspec(dllimport) void *GetCurrentProcess(void),*GetCurrentThread(void),*CreateEventW(void *,int,int,const W *),*CreateMutexW(void *,int,const W *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *),*CreateFileW(const W *,unsigned,unsigned,void *,unsigned,unsigned,void *),*VirtualAllocEx(void *,void *,uint64_t,unsigned,unsigned);
__declspec(dllimport) int CloseHandle(void *),SetHandleInformation(void *,unsigned,unsigned),GetExitCodeThread(void *,unsigned *),GetFileInformationByHandleEx(void *,unsigned,void *,unsigned),SetFileInformationByHandle(void *,unsigned,const void *,unsigned),WriteFile(void *,const void *,unsigned,unsigned *,void *),ReadFile(void *,void *,unsigned,unsigned *,void *),SetFilePointerEx(void *,int64_t,int64_t *,unsigned),DeleteFileW(const W *),PrefetchVirtualMemory(void *,uint64_t,const void *,unsigned),VirtualFreeEx(void *,void *,uint64_t,unsigned),K32QueryWorkingSetEx(void *,void *,unsigned),Wow64GetThreadContext(void *,void *);
__declspec(dllimport) unsigned GetCurrentProcessId(void),GetLastError(void),WaitForSingleObject(void *,unsigned),GetTempPathW(unsigned,W *);
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) int GetProcessHandleCount(void *,unsigned *);
__declspec(dllimport) void SetLastError(unsigned),OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){unsigned length=0,written;while(why[length])length++;OutputDebugStringA(why);WriteFile(GetStdHandle((unsigned)-12),why,length,&written,0);ExitProcess(1);}}
static int same(const W *a,const W *b,unsigned units){for(unsigned i=0;i<units;i++)if(a[i]!=b[i])return 0;return 1;}
static unsigned status_thread(void *unused){
    (void)unused;RtlSetLastWin32ErrorAndNtStatusFromNtStatus(0xc0000022u);
    return RtlGetLastNtStatus()==0xc0000022u && GetLastError()==5?0:1;
}
static void type(void *h,const W *expected,unsigned units){
    unsigned used=0;unsigned char buffer[256];
    check(NtQueryObject(h,2,buffer,1,&used)==0xc0000004u && used>=106,"inspection: NT object size contract FAIL\n");
    check(!NtQueryObject(h,2,buffer,sizeof(buffer),&used),"inspection: actual NT object type FAIL\n");
    unicode *name=(void *)buffer;check(name->length==units*2 && name->buffer>=(W *)buffer && (uint8_t *)name->buffer+name->maximum<=buffer+sizeof(buffer) && same(name->buffer,expected,units),"inspection: bounded NT typename FAIL\n");
}
void mainCRTStartup(void){
    void *self=GetCurrentProcess();process_info info;unsigned used=0;
    RtlSetLastWin32ErrorAndNtStatusFromNtStatus(0xc0000008u);
    check(RtlGetLastNtStatus()==0xc0000008u && GetLastError()==6 && RtlNtStatusToDosError(0xdeadbeef)==317,"inspection: real TEB status and status mapping FAIL\n");
    check(RtlGetLastNtStatus()==0xdeadbeef,"inspection: status conversion records NT cause FAIL\n");RtlSetLastWin32ErrorAndNtStatusFromNtStatus(0xc0000008u);
    uint32_t raw_status;__asm__ volatile("movl %%gs:0x1250,%0":"=r"(raw_status));check(raw_status==0xc0000008u,"inspection: AMD64 TEB status layout FAIL\n");
    check(NtQueryInformationProcess(self,0,&info,1,&used)==0xc0000004u && !used,"inspection: PBI sizing FAIL\n");
    check(!NtQueryInformationProcess(self,0,&info,sizeof(info),&used) && info.pid==GetCurrentProcessId() && info.peb && info.affinity && info.status==259,"inspection: native PBI facts FAIL\n");
    void *peb;__asm__ volatile("movq %%gs:0x60,%0":"=r"(peb));check(info.peb==peb,"inspection: actual PEB FAIL\n");
    uint64_t wow=99;check(!NtQueryInformationProcess(self,26,&wow,8,&used) && !wow,"inspection: PE32+ architecture query FAIL\n");
    static unsigned char image[2048];check(!NtQueryInformationProcess(self,27,image,sizeof(image),&used) && ((unicode *)image)->length && ((unicode *)image)->buffer[0]=='\\',"inspection: actual scoped image name FAIL\n");
    check(NtQueryInformationProcess(self,0xffff,0,0,0)==0xc0000003u && RtlGetLastNtStatus()==0xc0000008u,"inspection: NT calls do not overwrite last status FAIL\n");
    void *t=CreateThread(0,0,status_thread,0,0,0);unsigned code=99;
    check(t && !WaitForSingleObject(t,5000) && GetExitCodeThread(t,&code) && !code && CloseHandle(t) && RtlGetLastNtStatus()==0xc0000008u,"inspection: thread-local NT status FAIL\n");
    type(self,L"Process",7);type(GetCurrentThread(),L"Thread",6);
    unsigned baseline,opened,closed;check(GetProcessHandleCount(self,&baseline),"inspection: actual handle inventory FAIL\n");
    void *event=CreateEventW(0,1,0,0),*mutex=CreateMutexW(0,0,0);check(event && mutex,"inspection: object fixtures FAIL\n");type(event,L"Event",5);type(mutex,L"Mutant",6);
    check(GetProcessHandleCount(self,&opened) && opened==baseline+2,"inspection: newly opened handles counted FAIL\n");
    check(CloseHandle(event) && CloseHandle(mutex),"inspection: actual close FAIL\n");
    check(GetProcessHandleCount(self,&closed) && closed==baseline,"inspection: closed handles excluded FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(NtQueryObject(event,2,image,sizeof(image),0)==0xc0000008u,"inspection: stale NT object FAIL\n");
    static unsigned char context[716];*(unsigned *)context=0x10007;check(!Wow64GetThreadContext(GetCurrentThread(),context) && GetLastError()==87,"inspection: no invented x86 context FAIL\n");
#endif
    W path[512];unsigned n=GetTempPathW(470,path);check(n && n<470,"inspection: real prefix temp FAIL\n");const W suffix[]=L"kyil-inspection-test.bin";for(unsigned i=0;i<sizeof(suffix)/2;i++)path[n+i]=suffix[i];
    /* Exercise metadata using the existing supported fully shared open.
       CREATE_ALWAYS and denied-share leases are separate contracts. */
    void *f=CreateFileW(path,0xc0000000u,7,0,1,0x80,0);
    if(f==(void *)-1 && GetLastError()==80)f=CreateFileW(path,0xc0000000u,7,0,5,0x80,0);
    check(f!=(void *)-1,"inspection: file fixture FAIL\n");
    unsigned bytes;check(WriteFile(f,"abcdefgh",8,&bytes,0) && bytes==8,"inspection: actual bytes FAIL\n");
    basic_file basic;standard_file standard;
    check(GetFileInformationByHandleEx(f,0,&basic,sizeof(basic)) && basic.attrs && GetFileInformationByHandleEx(f,1,&standard,sizeof(standard)) && standard.size==8 && standard.allocated>=8 && !standard.directory,"inspection: actual file metadata FAIL\n");
    int64_t end=3;check(SetFileInformationByHandle(f,6,&end,8) && GetFileInformationByHandleEx(f,1,&standard,sizeof(standard)) && standard.size==3,"inspection: handle EOF transaction FAIL\n");
    check(SetFilePointerEx(f,0,0,0),"inspection: file rewind FAIL\n");char text[9]={0};check(ReadFile(f,text,8,&bytes,0) && bytes==3 && text[0]=='a' && text[2]=='c',"inspection: real truncation preserves prefix FAIL\n");
    type(f,L"File",4);check(SetHandleInformation(f,2,2),"inspection: protected handle fixture FAIL\n");
    uint8_t flags[2];check(!NtQueryObject(f,4,flags,2,&used) && !flags[0] && flags[1] && SetHandleInformation(f,2,0),"inspection: real handle flags FAIL\n");
    check(CloseHandle(f) && DeleteFileW(path),"inspection: actual file cleanup FAIL\n");
    unsigned char *range=VirtualAllocEx(self,0,16384,0x3000,4);check(range!=0,"inspection: sparse prefetch fixture FAIL\n");
    struct {void *address;uint64_t length;} entry={range+19,12000};check(PrefetchVirtualMemory(self,1,&entry,0),"inspection: actual prefetch FAIL\n");
    struct {void *address;uint64_t flags;} pages[4];for(unsigned i=0;i<4;i++){pages[i].address=range+4096*i;pages[i].flags=0;}
    check(K32QueryWorkingSetEx(self,pages,sizeof(pages)),"inspection: actual working set inspection FAIL\n");
#ifdef KYIL_NATIVE_TEST
    /* Windows prefetch is advisory. The current Vex implementation really
       faults the sparse pages in; verify that stronger implementation fact
       only in the guest, without inventing a Windows residency guarantee. */
    check((pages[0].flags&1) && (pages[1].flags&1) && (pages[2].flags&1),"inspection: prefetch materializes actual pages FAIL\n");
#endif
    check(VirtualFreeEx(self,range,0,0x8000),"inspection: prefetch cleanup FAIL\n");
    OutputDebugStringA("windows-inspection: native process facts / scoped image / NT types / TEB status / thread isolation / file metadata / actual truncate / sparse prefetch PASS\n");ExitProcess(0);
}
