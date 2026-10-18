#include <stdint.h>
__declspec(dllimport) void *GetModuleHandleW(const uint16_t *),*LoadLibraryW(const uint16_t *),*GetProcAddress(void *,const char *);
__declspec(dllimport) int GetModuleHandleExW(unsigned,const uint16_t *,void **),FreeLibrary(void *),CloseHandle(void *);
__declspec(dllimport) unsigned GetModuleFileNameW(void *,uint16_t *,unsigned),GetCurrentProcessId(void),GetLastError(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) int K32GetModuleInformation(void *,void *,void *,unsigned);
__declspec(dllimport) int SetDefaultDllDirectories(unsigned);
__declspec(dllimport) void *LoadLibraryExW(const uint16_t *,void *,unsigned);
typedef struct {void *base;unsigned size,pad;void *entry;} module_info;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static int same(const uint16_t *a,const uint16_t *b){while(*a && *a==*b){a++;b++;}return *a==*b;}
void mainCRTStartup(void) {
    void *main=GetModuleHandleW(0);check(main && *(uint16_t *)main==0x5a4d,"module: actual EXE image FAIL\n");
    module_info metadata;check(K32GetModuleInformation((void *)-1,main,&metadata,sizeof(metadata)) && metadata.base==main && metadata.size && metadata.entry,
        "module: actual image metadata FAIL\n");
    check(!K32GetModuleInformation((void *)-1,(void *)0xffff800000000000ull,&metadata,sizeof(metadata)) && GetLastError()==6,
        "module: invalid metadata handle FAIL\n");
    uint16_t path[512];check(GetModuleFileNameW(main,path,512) && same(path,L"A:\\windows-module-smoke.exe"),"module: guest EXE filename FAIL\n");
    void *library=GetModuleHandleW(L"kernel32.dll");check(library && *(uint16_t *)library==0x5a4d,"module: actual provider PE image FAIL\n");
    check(GetModuleFileNameW(library,path,512) && same(path,L"C:\\Windows\\System32\\kernel32.dll"),"module: real system mapping filename FAIL\n");
    unsigned (__attribute__((ms_abi)) *pid)(void)=GetProcAddress(library,"GetCurrentProcessId");
    check(pid && pid()==GetCurrentProcessId(),"module: real IAT tail-call into native implementation FAIL\n");
    check((uintptr_t)pid>(uintptr_t)library && (uintptr_t)pid<(uintptr_t)library+0x100000,"module: export lives in PE text FAIL\n");
    check(!GetProcAddress(library,"AFunctionThatDoesNotExist") && GetLastError()==127,"module: missing export refused FAIL\n");
    void *by_address=0;check(GetModuleHandleExW(4|2,(const uint16_t *)pid,&by_address) && by_address==library,"module: address lookup FAIL\n");
    for(unsigned i=0;i<8;i++){void *again=LoadLibraryW(L"kernel32.dll");check(again==library && FreeLibrary(again),"module: real provider references FAIL\n");}
    void *without_extension=LoadLibraryExW(L"kernel32",0,0x800);
    check(without_extension==library && FreeLibrary(without_extension),"module: real extensionless provider identity FAIL\n");
    check(!LoadLibraryExW(L"kernel32.",0,0x800) && GetLastError()==126,"module: explicit trailing point suppresses suffix FAIL\n");
    void *api=LoadLibraryExW(L"api-ms-win-core-fibers-l1-1-2",0,0x800);
    unsigned (__attribute__((ms_abi)) *fls_alloc)(void *)=GetProcAddress(api,"FlsAlloc");
    int (__attribute__((ms_abi)) *fls_free)(unsigned)=GetProcAddress(api,"FlsFree");
    check(api && fls_alloc && fls_free,"module: API-set maps real host exports FAIL\n");
    unsigned index=fls_alloc(0);check(index!=UINT32_MAX && fls_free(index) && FreeLibrary(api),"module: API-set executes actual FLS state FAIL\n");
    check(!LoadLibraryW(L"api-ms-win-not-a-real-contract-l9-9-9"),"module: unknown API-set not blanket aliased FAIL\n");
    for(unsigned i=0;i<8;i++)check(!LoadLibraryW(L"missing-module.dll") && GetLastError()==126,"module: missing DLL rollback FAIL\n");
    check(!FreeLibrary((void *)0xffff800000000000ull) && GetLastError()==6,"module: kernel handle refused FAIL\n");
    check(!SetDefaultDllDirectories(0) && GetLastError()==87 && SetDefaultDllDirectories(0x800),"module: actual DLL search policy FAIL\n");
    check(!LoadLibraryW(L"kyil-test.dll") && GetLastError()==126,"module: system-only must not search package FAIL\n");
    void *application=LoadLibraryExW(L"kyil-test.dll",0,0x200);check(application!=0,"module: explicit application search FAIL\n");
    unsigned (__attribute__((ms_abi)) *sum)(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned)=GetProcAddress(application,"SumSix");
    check(sum && sum(1,2,3,4,5,6)==121,"module: actual searched DLL code FAIL\n");
    void *again=LoadLibraryW(L"kyil-test.dll");check(again==application && FreeLibrary(again),"module: loaded-module lookup precedes search FAIL\n");
    check(SetDefaultDllDirectories(0x1000),"module: restore default search FAIL\n");
    OutputDebugStringA("windows-module: actual PE providers / scoped system files / real export execution / handles / refs / missing DLL-export / rollback / W_X PASS\n");ExitProcess(0);
}
