#include <stdint.h>
__declspec(dllimport) unsigned GetFileVersionInfoSizeW(const uint16_t *,unsigned *);
__declspec(dllimport) int GetFileVersionInfoW(const uint16_t *,unsigned,unsigned,void *),VerQueryValueW(const void *,const uint16_t *,void **,unsigned *);
__declspec(dllimport) unsigned GetLastError(void),GetModuleFileNameW(void *,uint16_t *,unsigned);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(align(4)) static unsigned char bytes[4096];
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static int same(const uint16_t *a,const uint16_t *b){while(*a && *a==*b){a++;b++;}return *a==*b;}
void mainCRTStartup(void) {
    uint16_t path[512];check(GetModuleFileNameW(0,path,512)>0,"version: actual PE path FAIL\n");
    unsigned unused=99,size=GetFileVersionInfoSizeW(path,&unused);
    check(size>=52 && size<=sizeof(bytes) && !unused,"version: actual resource size FAIL\n");
    bytes[0]=0x39;check(!GetFileVersionInfoW(path,0,16,bytes) && GetLastError()==122 && bytes[0]==0x39,"version: short buffer refused FAIL\n");
    check(GetFileVersionInfoW(path,0,sizeof(bytes),bytes),"version: confined resource extraction FAIL\n");
    void *out=0;unsigned length;
    check(VerQueryValueW(bytes,L"\\",&out,&length) && length==52,"version: fixed resource root FAIL\n");
    const uint32_t *fixed=out;
    check(fixed[0]==0xfeef04bd && fixed[2]==(37u<<16|2) && fixed[3]==(5u<<16|9),"version: real binary version values FAIL\n");
    check(VerQueryValueW(bytes,L"\\VarFileInfo\\Translation",&out,&length) && length==4 &&
        ((const uint16_t *)out)[0]==0x409 && ((const uint16_t *)out)[1]==1200,"version: actual translation array FAIL\n");
    check(VerQueryValueW(bytes,L"\\stringfileinfo\\040904B0\\CompanyName",&out,&length) && length==7 && same(out,L"KyIlOS"),"version: casefold string query FAIL\n");
    check(VerQueryValueW(bytes,L"\\StringFileInfo\\040904b0\\FileVersion",&out,&length) && same(out,L"37.2.5.9"),"version: actual version string FAIL\n");
    check(!VerQueryValueW(bytes,L"\\StringFileInfo\\040904b0\\missing",&out,&length) && !out && !length,"version: missing field refused FAIL\n");
    unsigned char invalid[8]={4,0};check(!VerQueryValueW(invalid,L"\\",&out,&length) && GetLastError()==1812,"version: malformed node refused FAIL\n");
    check(!GetFileVersionInfoSizeW(L"A:\\missing.exe",0) && GetLastError()==2,"version: missing file refused FAIL\n");
    check(!GetFileVersionInfoSizeW(L"C:\\..\\outside.exe",0) && GetLastError()==5,"version: namespace confinement FAIL\n");
    OutputDebugStringA("windows-version: real PE RT_VERSION / fixed values / translation / UTF16 strings / borrowed buffer / malformed / missing / confinement PASS\n");ExitProcess(0);
}
