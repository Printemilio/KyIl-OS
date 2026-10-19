#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef unsigned short W;
__declspec(dllimport) void *CreateFileW(const W *,unsigned,unsigned,void *,unsigned,unsigned,void *);
__declspec(dllimport) unsigned GetTempPathW(unsigned,W *),GetCurrentProcessId(void),GetLastError(void);
__declspec(dllimport) int ReadFile(void *,void *,unsigned,unsigned *,void *),WriteFile(void *,const void *,unsigned,unsigned *,void *);
__declspec(dllimport) int FlushFileBuffers(void *),CloseHandle(void *),GetFileSizeEx(void *,long long *);
__declspec(dllimport) int SetFilePointerEx(void *,long long,long long *,unsigned);
__declspec(dllimport) unsigned GetFileAttributesW(const W *),GetFileAttributesA(const char *);
__declspec(dllimport) int GetFileAttributesExW(const W *,unsigned,void *),DeleteFileW(const W *),RemoveDirectoryW(const W *);
__declspec(dllimport) int CreateDirectoryW(const W *,void *);
__declspec(dllimport) unsigned GetFileSize(void *,unsigned *);
__declspec(dllimport) int GetFileTime(void *,void *,void *,void *);
__declspec(dllimport) void *CreateFileA(const char *,unsigned,unsigned,void *,unsigned,unsigned,void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static unsigned char data[65537],copy[65537];
static void check(int ok,const char *reason) {if(!ok){OutputDebugStringA(reason);ExitProcess(1);}}
void mainCRTStartup(void) {
    W path[512];unsigned n=GetTempPathW(512,path),pid=GetCurrentProcessId();
    check(n && n<450,"windows-files: temp FAIL\n");
    const W suffix[]=L"file-contract-";unsigned i=0;while(suffix[i])path[n++]=suffix[i++];
    for(i=0;i<8;i++)path[n++]=L"0123456789abcdef"[(pid>>(28-i*4))&15];path[n]=0;
    void *h=CreateFileW(path,0xc0000000u,7,0,1,0x80,0);
    /* Same VM boot repeated test runs have distinct PIDs; an already existing
       disk file is opened honestly instead of truncating with guessed status. */
    if(h==(void *)-1 && GetLastError()==80)h=CreateFileW(path,0xc0000000u,7,0,5,0x80,0);
    check(h!=(void *)-1,"windows-files: real CreateFileW FAIL\n");
    for(i=0;i<sizeof(data);i++)data[i]=(unsigned char)(i*37u+19u);
    unsigned written=0;long long size=0,pos=0;
    check(WriteFile(h,data,sizeof(data),&written,0) && written==sizeof(data) && FlushFileBuffers(h),"windows-files: write/flush FAIL\n");
    struct {unsigned attributes,times[6],high,low;} metadata;
    check(GetFileAttributesW(path)==0x80 && GetFileAttributesExW(path,0,&metadata) && metadata.low==sizeof(data) && !metadata.high,
        "windows-files: real path metadata FAIL\n");
    check(!GetFileAttributesExW(path,1,&metadata) && GetLastError()==87,"windows-files: attribute class refusal FAIL\n");
    /* This backend explicitly refuses Windows delete-pending on live objects;
       it must never report success and recreate the pathname on final flush. */
    check(!DeleteFileW(path) && GetLastError()==50,"windows-files: live-object delete refusal FAIL\n");
    check(GetFileSizeEx(h,&size) && size==sizeof(data),"windows-files: size FAIL\n");
    unsigned high=99;unsigned long long birth=99,at=0,mt=0;
    check(GetFileSize(h,&high)==sizeof(data) && !high && !GetLastError() && GetFileTime(h,&birth,&at,&mt) && !birth && mt,
        "windows-files: size/time adapters FAIL\n");
    check(SetFilePointerEx(h,0,&pos,0) && pos==0 && ReadFile(h,copy,sizeof(copy),&written,0) && written==sizeof(copy),"windows-files: seek/read FAIL\n");
    for(i=0;i<sizeof(data);i++)check(data[i]==copy[i],"windows-files: bytes FAIL\n");
    check(!SetFilePointerEx(h,0x7fffffffffffffffLL,0,1) && GetLastError()==87,"windows-files: seek overflow FAIL\n");
    check(CloseHandle(h) && !CloseHandle(h) && GetLastError()==6,"windows-files: stale handle FAIL\n");
    h=CreateFileW(path,0x80000000u,7,0,3,0x80,0);check(h!=(void *)-1,"windows-files: reopen FAIL\n");
    check(ReadFile(h,copy,sizeof(copy),&written,0) && written==sizeof(copy),"windows-files: persisted read FAIL\n");
    for(i=0;i<sizeof(data);i++)check(copy[i]==data[i],"windows-files: persisted bytes FAIL\n");
    check(!WriteFile(h,data,1,&written,0) && GetLastError()==5,"windows-files: readonly rights FAIL\n");
    check(!ReadFile(h,(void *)0xffff800000001000ull,1,&written,0) && GetLastError()==998,"windows-files: invalid buffer FAIL\n");
    check(CloseHandle(h),"windows-files: close FAIL\n");
    h=CreateFileW(path,0x80000000u,3,0,3,0x80,0);
    check(h!=(void *)-1,"windows-files: KSW protected share=3 FAIL\n");
    check(!DeleteFileW(path) && GetLastError()==5,"windows-files: native name protection FAIL\n");
    check(ReadFile(h,copy,1,&written,0) && written==1 && copy[0]==data[0] && CloseHandle(h),"windows-files: protected actual bytes/close FAIL\n");
    check(DeleteFileW(path) && GetFileAttributesW(path)==0xffffffffu && GetLastError()==2,
        "windows-files: actual confined deletion FAIL\n");
    check(!DeleteFileW(path) && GetLastError()==2,"windows-files: repeated deletion FAIL\n");
    check(CreateDirectoryW(path,0) && (GetFileAttributesW(path)&0x10),"windows-files: actual directory attributes FAIL\n");
    check(!DeleteFileW(path) && GetLastError()==5 && RemoveDirectoryW(path) && GetFileAttributesW(path)==0xffffffffu,
        "windows-files: file vs directory removal FAIL\n");
    check(GetFileAttributesA("A:\\windows-native-smoke.exe")!=0xffffffffu &&
        !DeleteFileW(L"A:\\windows-native-smoke.exe") && GetLastError()==5,"windows-files: package metadata/deletion rights FAIL\n");
    h=CreateFileA("A:\\windows-native-smoke.exe",0x80000000u,7,0,3,0x80,0);
    check(h!=(void *)-1 && CloseHandle(h),"windows-files: actual UTF8 file adapter FAIL\n");
    check(CreateFileW(L"A:\\write-refused",0x40000000u,7,0,1,0x80,0)==(void *)-1 && GetLastError()==5,"windows-files: package RO FAIL\n");
    check(CreateFileW(path,0x80000000u,1,0,3,0x80,0)==(void *)-1 && GetLastError()==50,"windows-files: unsupported sharing refusal FAIL\n");
    vex_open_ex_req_t outside={VEX_AT_FDCWD,O_RDONLY,0,0,"/usr/pe-tests/windows-native-smoke.exe"};
    check((long long)vex_syscall1(VEX_SYS_OPEN_EX,(uintptr_t)&outside)==-13,"windows-files: native bypass FAIL\n");
    OutputDebugStringA("windows-files: 65537 real bytes / reopen / seek / rights / stale / native confinement PASS\n");ExitProcess(0);
}
