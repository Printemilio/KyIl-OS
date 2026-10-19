#include <stdint.h>
__declspec(dllimport) void *LoadLibraryExW(const uint16_t *,void *,unsigned),*GetProcAddress(void *,const char *),*GetModuleHandleW(const uint16_t *);
__declspec(dllimport) unsigned GetModuleFileNameW(void *,uint16_t *,unsigned),GetLastError(void);
__declspec(dllimport) int SetDefaultDllDirectories(unsigned),FreeLibrary(void *),GetModuleHandleExW(unsigned,const uint16_t *,void **);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) int WriteFile(void *,const void *,unsigned,unsigned *,void *);
static void check(int ok,const char *why){if(!ok){unsigned n=0,written;while(why[n])n++;WriteFile(GetStdHandle(-12),why,n,&written,0);OutputDebugStringA(why);ExitProcess(1);}}
static int same(const uint16_t *a,const uint16_t *b){while(*a && *a==*b){a++;b++;}return *a==*b;}
#ifdef HOST_REFERENCE
static uint16_t top_path[512],leaf_path[512],absent_path[512];
static void path(uint16_t *out,const uint16_t *suffix){unsigned n=GetModuleFileNameW(0,out,512);check(n && n<400,"altered-search: host module path FAIL\n");
    while(n && out[n-1]!='\\')n--;unsigned i=0;while((out[n+i]=suffix[i]))i++;}
#else
#define top_path L"A:\\altered\\top.dll"
#define leaf_path L"A:\\altered\\kyil-altered-leaf.dll"
#define absent_path L"A:\\absent\\top.dll"
#endif
void mainCRTStartup(void){
#ifdef HOST_REFERENCE
    path(top_path,L"altered\\top.dll");path(leaf_path,L"altered\\kyil-altered-leaf.dll");path(absent_path,L"absent\\top.dll");
#endif
    check(SetDefaultDllDirectories(0x800),"altered-search: system-only default FAIL\n");
    for(unsigned i=0;i<4;i++)check(!LoadLibraryExW(absent_path,0,8) && GetLastError()==126,"altered-search: missing absolute rollback FAIL\n");
    check(!LoadLibraryExW(L"kyil-altered-leaf.dll",0,0x800),"altered-search: failed load leaked search directory FAIL\n");
    void *m=LoadLibraryExW(top_path,0,8);check(m!=0,"altered-search: actual nested DLL load FAIL\n");
    unsigned (*value)(void)=GetProcAddress(m,"ActualValue");check(value && value()==322,"altered-search: actual transitive implementation FAIL\n");
    uint16_t path[512];void *leaf=GetModuleHandleW(L"kyil-altered-leaf.dll");
    check(leaf && GetModuleFileNameW(leaf,path,512) && same(path,leaf_path),"altered-search: loaded DLL directory wins over executable shadow FAIL\n");
#ifdef KYIL_NATIVE_TEST
    /* Forbidden by the documented contract. The Windows reference ignores
       this combination on its already-loaded-module fast path. */
    check(!LoadLibraryExW(top_path,0,0x808) && GetLastError()==87,"altered-search: incompatible flags rejected FAIL\n");
#endif
    void *pinned=0;check(GetModuleHandleExW(1,L"top.dll",&pinned) && pinned==m && FreeLibrary(pinned) && FreeLibrary(m) && SetDefaultDllDirectories(0xa00),"altered-search: explicit pin lifetime and policy restoration FAIL\n");
    OutputDebugStringA("windows-altered-search: actual absolute DLL / transitive dependency / package shadow exclusion / missing rollback / scoped search state PASS\n");ExitProcess(0);
}
