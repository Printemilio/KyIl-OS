#include <stdint.h>
typedef uint16_t W;
typedef struct {void *base,*allocation;unsigned allocation_protect;uint16_t partition,pad;uint64_t size;unsigned state,protect,type,pad2;} mbi;
__declspec(dllimport) unsigned GetSystemDefaultLCID(void),GetUserDefaultLCID(void),GetThreadLocale(void),GetLastError(void),WaitForSingleObject(void *,unsigned),GetExitCodeThread(void *,unsigned *);
__declspec(dllimport) int GetUserDefaultLocaleName(W *,int),SetThreadLocale(unsigned),EnumSystemLocalesEx(int (*)(W *,unsigned,intptr_t),unsigned,intptr_t,void *),CloseHandle(void *),GetProcessMitigationPolicy(void *,unsigned,void *,uint64_t),VirtualFreeEx(void *,void *,uint64_t,unsigned),FlushViewOfFile(const void *,uint64_t),UnmapViewOfFile(void *);
__declspec(dllimport) void *GetCurrentProcess(void),*OpenProcess(unsigned,int,unsigned),*VirtualAllocEx(void *,void *,uint64_t,unsigned,unsigned),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *),*CreateFileMappingW(void *,void *,unsigned,unsigned,unsigned,const W *),*MapViewOfFile(void *,unsigned,unsigned,unsigned,uint64_t);
__declspec(dllimport) uint64_t VirtualQueryEx(void *,const void *,mbi *,uint64_t);
__declspec(dllimport) unsigned GetCurrentProcessId(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static unsigned seen,enus;static intptr_t cookie=0x1234567812345678ll;
static int visit(W *name,unsigned flags,intptr_t arg){
    check(arg==cookie,"chrome-contract: enum callback parameter FAIL\n");seen++;
    if(name[0]=='e' && name[1]=='n' && name[2]=='-' && name[3]=='U' && name[4]=='S' && !name[5]){check(flags==0x21,"chrome-contract: enum actual locale flags FAIL\n");enus++;}
    return 1;
}
static unsigned locale_thread(void *unused){(void)unused;return GetThreadLocale()==GetUserDefaultLCID() && SetThreadLocale(0x7f) && GetThreadLocale()==0x7f?0:1;}
void mainCRTStartup(void){
    W name[85];check(GetSystemDefaultLCID()!=0 && GetUserDefaultLocaleName(name,85)>1,"chrome-contract: actual configured locale FAIL\n");
    check(!GetUserDefaultLocaleName(name,0),"chrome-contract: locale zero capacity refusal FAIL\n");
    check(EnumSystemLocalesEx(visit,0,cookie,0) && seen && enus==1,"chrome-contract: installed locale enumeration FAIL\n");
    seen=enus=0;check(EnumSystemLocalesEx(visit,0x10,cookie,0) && !enus,"chrome-contract: locale filter FAIL\n");
    check(!EnumSystemLocalesEx(visit,0x40,cookie,0) && GetLastError()==1004,"chrome-contract: invalid locale flags FAIL\n");
    unsigned original=GetThreadLocale();void *thread=CreateThread(0,0,locale_thread,0,0,0);unsigned code=1;
    check(thread && !WaitForSingleObject(thread,5000) && GetExitCodeThread(thread,&code) && !code && CloseHandle(thread) && GetThreadLocale()==original,"chrome-contract: independent per-thread locale FAIL\n");
    struct {unsigned flags;unsigned char permanent,pad[3];} dep;
    void *self=GetCurrentProcess();check(GetProcessMitigationPolicy(self,0,&dep,sizeof(dep)) && (dep.flags&3)==3 && dep.permanent,"chrome-contract: enforced DEP query FAIL\n");
    unsigned char *bytes=VirtualAllocEx(self,0,8192,0x3000,4);check(bytes!=0,"chrome-contract: actual self VM allocation FAIL\n");
    bytes[0]=91;bytes[8191]=37;mbi information;
    check(VirtualQueryEx(self,bytes,&information,sizeof(information))==sizeof(information) && information.state==0x1000 && information.protect==4 && information.allocation==bytes,"chrome-contract: actual VM inventory FAIL\n");
    check(VirtualFreeEx(self,bytes,0,0x8000) && VirtualQueryEx(self,bytes,&information,sizeof(information))==sizeof(information) && information.state==0x10000,"chrome-contract: actual VM release FAIL\n");
#ifdef KYIL_NATIVE_TEST
    void *limited=OpenProcess(0x1000,0,GetCurrentProcessId());check(limited && !VirtualAllocEx(limited,0,4096,0x3000,4) && GetLastError()==50 && CloseHandle(limited),"chrome-contract: query handle cannot grant VM authority FAIL\n");
    check(!VirtualAllocEx((void *)0x1234,0,4096,0x3000,4) && GetLastError()==6,"chrome-contract: invalid VM handle FAIL\n");
    check(!VirtualAllocEx(self,0,4096,0x3000,0x40) && GetLastError()==5,"chrome-contract: generic RWX refused FAIL\n");
#endif
    void *section=CreateFileMappingW((void *)-1,0,4,0,8192,0);check(section!=0,"chrome-contract: real section FAIL\n");
    unsigned char *a=MapViewOfFile(section,6,0,0,8192),*b=MapViewOfFile(section,4,0,0,8192);
    check(a && b && a!=b && CloseHandle(section),"chrome-contract: independent view lifetime FAIL\n");
    a[4017]=183;a[8191]=59;check(FlushViewOfFile(a+4000,100) && b[4017]==183 && FlushViewOfFile(a+4000,0) && b[8191]==59,"chrome-contract: shared volatile view visibility FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!FlushViewOfFile(a+8191,2) && GetLastError()==87,"chrome-contract: flush range bounds FAIL\n");
#endif
    check(UnmapViewOfFile(a) && UnmapViewOfFile(b),"chrome-contract: actual view cleanup FAIL\n");
    OutputDebugStringA("windows-chrome-contract: installed locales / per-thread locale / DEP / self VM / shared view visibility / invalid rights PASS\n");ExitProcess(0);
}
