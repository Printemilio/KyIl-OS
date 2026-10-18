#include <stdint.h>
typedef struct {uint32_t size,major,minor,build,platform;uint16_t service[128];uint16_t sp_major,sp_minor,suite;uint8_t product,reserved;} os_version;
__declspec(dllimport) int GetVersionExW(os_version *),VerifyVersionInfoW(const os_version *,unsigned,uint64_t),GetProductInfo(unsigned,unsigned,unsigned,unsigned,unsigned *);
__declspec(dllimport) uint64_t VerSetConditionMask(uint64_t,unsigned,uint8_t);
__declspec(dllimport) unsigned FormatMessageW(unsigned,const void *,unsigned,unsigned,void *,unsigned,void *),FormatMessageA(unsigned,const void *,unsigned,unsigned,void *,unsigned,void *),GetLastError(void);
__declspec(dllimport) int lstrcmpiA(const char *,const char *),lstrcmpiW(const uint16_t *,const uint16_t *);
__declspec(dllimport) void *LocalFree(void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int yes,const char *message){if(!yes){OutputDebugStringA(message);ExitProcess(1);}}
static int same(const uint16_t *a,const uint16_t *b){while(*a==*b){if(!*a)return 1;a++;b++;}return 0;}
void mainCRTStartup(void) {
    static os_version version;version.size=sizeof(version);check(GetVersionExW(&version) && version.major>=6 && version.platform==2,"contract-batch: actual personality version FAIL\n");
    uint64_t mask=VerSetConditionMask(0,2|1|0x20|0x10,3);
    check(VerifyVersionInfoW(&version,3,mask),"contract-batch: version equality FAIL\n");
    version.major--;version.minor=999;
    check(VerifyVersionInfoW(&version,3,mask),"contract-batch: hierarchical version FAIL\n");
    version.major+=2;version.minor=0;check(!VerifyVersionInfoW(&version,3,mask) && GetLastError()==1150,"contract-batch: newer version rejection FAIL\n");
    unsigned product;check(GetProductInfo(10,0,0,0,&product),"contract-batch: product query FAIL\n");
    check(!lstrcmpiA("Alpha73","aLPHa73") && lstrcmpiA("A0","A1")<0 && !lstrcmpiW(L"Beta42",L"bETA42"),"contract-batch: linguistic case comparison FAIL\n");
    uint16_t buffer[128];uintptr_t args[2]={(uintptr_t)L"hello",42};
    check(FormatMessageW(0x400|0x2000,L"%1 %2!04X!%nend%0ignored",0,0,buffer,128,args)==15 && same(buffer,L"hello 002A\r\nend"),"contract-batch: actual message inserts FAIL\n");
    uint16_t *allocated=0;check(FormatMessageW(0x1000|0x200|0x100,0,5,0x409,&allocated,0,0) && allocated && same(allocated,L"Access is denied.\r\n") && !LocalFree(allocated),"contract-batch: real system error allocation FAIL\n");
    char narrow[32];check(FormatMessageA(0x400,"test %% %n%0tail",0,0,narrow,32,0)==9 && narrow[5]=='%' && narrow[7]=='\r' && narrow[8]=='\n',"contract-batch: narrow message FAIL\n");
    check(!FormatMessageW(0x1000|0x200,0,0x7fffffffu,0,buffer,128,0) && GetLastError()==317,"contract-batch: unknown message honest failure FAIL\n");
    check(!FormatMessageW(0x400,L"toolong",0,0,buffer,2,0) && GetLastError()==122,"contract-batch: short message buffer FAIL\n");
    OutputDebugStringA("windows-contract-batch: profile / hierarchical version / actual NLS subset / formatted messages / local allocation / error paths PASS\n");ExitProcess(0);
}
