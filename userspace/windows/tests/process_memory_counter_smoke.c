#include <stdint.h>
typedef struct {uint32_t size,faults;uint64_t peak_working,working,peak_paged,paged,peak_nonpaged,nonpaged,charge,peak_charge,private_charge;} counters;
typedef struct {uint32_t size,pad;uint64_t commit,limit,peak,physical,available,cache,kernel,paged,nonpaged,page_size;uint32_t handles,processes,threads,pad2;} performance;
__declspec(dllimport) int K32GetProcessMemoryInfo(void *,counters *,unsigned),K32GetPerformanceInfo(performance *,unsigned),VirtualFree(void *,uint64_t,unsigned),VirtualProtect(void *,uint64_t,unsigned,unsigned *),WriteFile(void *,const void *,unsigned,unsigned *,void *);
__declspec(dllimport) void *VirtualAlloc(void *,uint64_t,unsigned,unsigned),*GetStdHandle(unsigned);
__declspec(dllimport) unsigned GetLastError(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){unsigned n=0,done;while(why[n])n++;OutputDebugStringA(why);WriteFile(GetStdHandle((unsigned)-12),why,n,&done,0);ExitProcess(1);}}
void mainCRTStartup(void){
    counters before={0},allocated={0},after={0};performance info={0};
    check(K32GetProcessMemoryInfo((void *)-1,&before,sizeof(before)) && before.size==sizeof(before) && before.working && before.peak_working>=before.working && before.peak_charge>=before.charge && before.private_charge==before.charge,"memory-counter: actual process counters FAIL\n");
    void *area=VirtualAlloc(0,4ull<<20,0x3000,4);check(area!=0,"memory-counter: actual private allocation FAIL\n");
    check(K32GetProcessMemoryInfo((void *)-1,&allocated,sizeof(allocated)) && allocated.charge>=before.charge+(4ull<<20) && allocated.peak_charge>=allocated.charge,"memory-counter: logical commitment before touch FAIL\n");
    for(unsigned i=0;i<32;i++)((volatile uint8_t *)area)[4096*i]=(uint8_t)i;
    check(K32GetProcessMemoryInfo((void *)-1,&after,sizeof(after)) && after.working && after.faults>=allocated.faults && after.peak_working>=after.working,"memory-counter: actual resident population FAIL\n");
    unsigned old;check(VirtualProtect(area,4ull<<20,1,&old) && old==4,"memory-counter: noaccess protection FAIL\n");
    check(K32GetProcessMemoryInfo((void *)-1,&after,sizeof(after)) && after.charge==allocated.charge,"memory-counter: protection is not decommit FAIL\n");
    check(VirtualFree(area,0,0x8000) && K32GetProcessMemoryInfo((void *)-1,&after,sizeof(after)) && after.charge+(4ull<<20)<=allocated.charge && after.peak_charge>=allocated.charge,"memory-counter: release and historical charge FAIL\n");
    counters base={0};base.private_charge=UINT64_C(0x19732026);
    check(K32GetProcessMemoryInfo((void *)-1,&base,72) && base.size==72 && base.private_charge==UINT64_C(0x19732026),"memory-counter: basic-size bounds FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!K32GetProcessMemoryInfo((void *)0x1234,&base,sizeof(base)),"memory-counter: invalid process refused FAIL\n");
#endif
    check(!K32GetPerformanceInfo(&info,8),"memory-counter: short output refused FAIL\n");
    check(K32GetPerformanceInfo(&info,sizeof(info)) && info.size==104 && info.page_size==4096 && info.physical && info.available<=info.physical && info.peak>=info.commit && info.kernel==info.paged+info.nonpaged && info.threads>=info.processes && info.processes && info.handles,"memory-counter: real system inventory FAIL\n");
    OutputDebugStringA("windows-memory-counter: real process/system facts / logical commit before touch / residency / protections / release / historical peaks / sized records / invalid capabilities PASS\n");ExitProcess(0);
}
