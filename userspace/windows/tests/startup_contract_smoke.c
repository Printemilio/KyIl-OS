#include <stdint.h>
typedef uint16_t W;
typedef struct {void *debug;int32_t count,recursion;uintptr_t owner;void *semaphore;uintptr_t spin;} critical;
typedef struct link {struct link *next,*previous;} link;
typedef struct {uint16_t type,creator;uint32_t pad;critical *section;link links;uint32_t entries,contentions,flags;uint16_t high,spare;} debug;
__declspec(dllimport) void *LoadLibraryW(const W *),*GetProcAddress(void *,const char *),*LocalFree(void *),*OpenProcess(unsigned,int,unsigned),*GetCurrentProcess(void),*VirtualAllocEx(void *,void *,uint64_t,unsigned,unsigned);
__declspec(dllimport) unsigned GetCurrentProcessId(void),GetLastError(void),GetModuleFileNameW(void *,W *,unsigned),GetProcessId(void *),GetHandleInformation(void *,unsigned *);
__declspec(dllimport) unsigned GetFileAttributesW(const W *);
__declspec(dllimport) int FreeLibrary(void *),CloseHandle(void *),InitializeCriticalSectionEx(critical *,unsigned,unsigned),DuplicateHandle(void *,void *,void *,void **,unsigned,int,unsigned),ReadProcessMemory(void *,const void *,void *,uint64_t,uint64_t *),WriteProcessMemory(void *,void *,const void *,uint64_t,uint64_t *),VirtualFreeEx(void *,void *,uint64_t,unsigned),VirtualProtectEx(void *,void *,uint64_t,unsigned,unsigned *);
__declspec(dllimport) void EnterCriticalSection(critical *),LeaveCriticalSection(critical *),DeleteCriticalSection(critical *),OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) void *GetStdHandle(int);
__declspec(dllimport) int WriteFile(void *,const void *,unsigned,unsigned *,void *);
static void check(int ok,const char *why){if(!ok){unsigned n=0,done;while(why[n])n++;WriteFile(GetStdHandle(-12),why,n,&done,0);OutputDebugStringA(why);ExitProcess(1);}}
static int same(const W *a,const W *b){while(*a && *a==*b){a++;b++;}return *a==*b;}
void mainCRTStartup(void){
#ifdef KYIL_NATIVE_TEST
    for(int stream=-10;stream>=-12;stream--){unsigned inherited=0;
        check(GetHandleInformation(GetStdHandle(stream),&inherited) && (inherited&1),"startup-contract: delegated standard stream inheritance FAIL\n");}
#endif
    void *shell=LoadLibraryW(L"shell32.dll");check(shell!=0,"startup-contract: real Shell32 provider FAIL\n");
    W **(*parse)(const W *,int *)=GetProcAddress(shell,"CommandLineToArgvW");check(parse!=0,"startup-contract: parser binding FAIL\n");
    uint32_t (*folder)(void *,int,void *,unsigned,W *)=GetProcAddress(shell,"SHGetFolderPathW");check(folder!=0,"startup-contract: folder binding FAIL\n");
    const int folders[]={0x10,5,0x1a,0x1c,0x28,0x24,0x25};
    for(unsigned i=0;i<sizeof(folders)/sizeof(*folders);i++){W path[260];check(!folder(0,folders[i],0,0,path) && path[0],"startup-contract: profile path FAIL\n");
        unsigned a=GetFileAttributesW(path);check(a!=UINT32_MAX && (a&0x10),"startup-contract: actual profile directory FAIL\n");}
    for(unsigned cycle=0;cycle<64;cycle++){
        int argc=-1;W **v=parse(L"\"C:\\Program Files\\a.exe\" one \"two three\" a\\\"b \"a\"\"b\"",&argc);
        check(v && argc==5 && same(v[0],L"C:\\Program Files\\a.exe") && same(v[1],L"one") && same(v[2],L"two three") && same(v[3],L"a\"b") && same(v[4],L"a\"b") && !LocalFree(v),"startup-contract: quote/backslash/LocalFree FAIL\n");
        v=parse(L"",&argc);W image[512];unsigned image_size=GetModuleFileNameW(0,image,512);
        int image_count=0;W **image_args=image_size?parse(image,&image_count):0;
        check(v && image_args && argc==image_count,"startup-contract: empty uses module command line FAIL\n");
        for(int i=0;i<argc;i++)check(same(v[i],image_args[i]),"startup-contract: empty module command argument FAIL\n");
        check(!LocalFree(v) && !LocalFree(image_args),"startup-contract: empty command lifetime FAIL\n");
        v=parse(L" \tp \"\"",&argc);check(v && argc==3 && !v[0][0] && same(v[1],L"p") && !v[2][0] && !LocalFree(v),"startup-contract: leading whitespace and empty argument FAIL\n");
    }
    check(FreeLibrary(shell),"startup-contract: Shell32 release FAIL\n");
    for(unsigned cycle=0;cycle<64;cycle++){critical a,b;check(InitializeCriticalSectionEx(&a,0,0x10000000) && InitializeCriticalSectionEx(&b,0,0x10000000),"startup-contract: forced debug initialization FAIL\n");
        debug *x=a.debug,*y=b.debug;check(x && y && x->section==&a && y->section==&b && x->links.next->previous==&x->links && y->links.previous->next==&y->links,"startup-contract: real debug structures and list FAIL\n");
        EnterCriticalSection(&a);EnterCriticalSection(&a);check(a.recursion==2,"startup-contract: debug lock recursion FAIL\n");LeaveCriticalSection(&a);LeaveCriticalSection(&a);
        DeleteCriticalSection(&a);check(y->links.next->previous==&y->links && y->links.previous->next==&y->links,"startup-contract: debug unlink lifetime FAIL\n");DeleteCriticalSection(&b);
    }
    unsigned pid=GetCurrentProcessId(),flags=0;void *h=OpenProcess(0x1f0fff,1,pid);check(h && GetProcessId(h)==pid && GetHandleInformation(h,&flags) && (flags&1),"startup-contract: full self process/inherit FAIL\n");
    unsigned char *bytes=VirtualAllocEx(h,0,4096,0x3000,4);uint64_t done=0;unsigned char in=73,out=0;
    check(bytes && WriteProcessMemory(h,bytes,&in,1,&done) && done==1 && ReadProcessMemory(h,bytes,&out,1,&done) && out==in,"startup-contract: real self process memory FAIL\n");
    unsigned old=0;check(VirtualProtectEx(h,bytes,4096,2,&old),"startup-contract: real readonly protection FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!WriteProcessMemory(h,bytes,&in,1,&done) && !done,"startup-contract: native self handle preserves readonly FAIL\n");
#endif
    check(VirtualFreeEx(h,bytes,0,0x8000) && CloseHandle(h),"startup-contract: full handle cleanup FAIL\n");
#ifdef KYIL_NATIVE_TEST
    shell=LoadLibraryW(L"shell32.dll");parse=GetProcAddress(shell,"CommandLineToArgvW");int argc=0;
    check(!parse((const W *)0xffff800000000000ull,&argc) && GetLastError()==998,"startup-contract: parser rejects kernel pointer FAIL\n");
    check(!parse(L"p",(int *)1) && GetLastError()==998,"startup-contract: parser output fault rollback FAIL\n");
    bytes=VirtualAllocEx(GetCurrentProcess(),0,8192,0x3000,4);check(bytes!=0,"startup-contract: parser boundary backing FAIL\n");
    bytes[4092]='p';bytes[4093]=bytes[4094]=bytes[4095]=0;
    check(VirtualProtectEx(GetCurrentProcess(),bytes+4096,4096,1,&old),"startup-contract: parser inaccessible neighbour FAIL\n");
    W **edge=parse((const W *)(bytes+4092),&argc);check(edge && argc==1 && same(edge[0],L"p") && !LocalFree(edge),"startup-contract: parser stops at page NUL FAIL\n");
    check(!parse((const W *)(bytes+4095),&argc) && GetLastError()==998,"startup-contract: split UTF16 character fault FAIL\n");
    check(VirtualFreeEx(GetCurrentProcess(),bytes,0,0x8000) && FreeLibrary(shell),"startup-contract: parser boundary cleanup FAIL\n");
#endif
    h=OpenProcess(0x1000,0,pid);void *copy=0;check(h && DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),&copy,0,1,2),"startup-contract: limited duplicate FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!VirtualAllocEx(copy,0,4096,0x3000,4) && GetLastError()==50,"startup-contract: no rights enlargement through duplicate FAIL\n");
#else
    check(!VirtualAllocEx(copy,0,4096,0x3000,4),"startup-contract: no rights enlargement through duplicate FAIL\n");
#endif
    check(CloseHandle(copy) && CloseHandle(h),"startup-contract: limited cleanup FAIL\n");
    OutputDebugStringA("windows-startup-contract: Shell32 real parser / LocalFree / forced debug list lifecycle / full self process / real memory / readonly / attenuation PASS\n");ExitProcess(0);
}
