#include <stdint.h>
#ifdef KYIL_NATIVE_TEST
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#endif
typedef struct {uint32_t size,load;uint64_t total_phys,free_phys,total_page,free_page,total_va,free_va,extended;} memory_info;
typedef struct {uint64_t mask;uint32_t relation,pad;uint8_t data[16];} cpu_info;
__declspec(dllimport) int GlobalMemoryStatusEx(memory_info *),GetLogicalProcessorInformation(cpu_info *,unsigned *),GetConsoleMode(void *,unsigned *),WriteConsoleW(void *,const uint16_t *,unsigned,unsigned *,void *);
__declspec(dllimport) unsigned GetConsoleOutputCP(void),GetCurrentThreadId(void),GetLastError(void),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) void *GetStdHandle(unsigned),*GetCurrentThread(void),*CreateEventW(void *,int,int,const uint16_t *);
__declspec(dllimport) int SetEvent(void *),ResetEvent(void *),CloseHandle(void *),SetThreadPriority(void *,int),GetThreadPriority(void *),SetThreadPriorityBoost(void *,int),GetThreadPriorityBoost(void *,int *),SetThreadInformation(void *,unsigned,const void *,unsigned),SetProcessMitigationPolicy(unsigned,const void *,uint64_t),SetConsoleCtrlHandler(int (*)(unsigned),int);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int yes,const char *why){if(!yes){OutputDebugStringA(why);ExitProcess(1);}}
static void *control_event;static unsigned controls,control_tid;
static int control(unsigned code){if(code)return 0;controls++;control_tid=GetCurrentThreadId();SetEvent(control_event);return 1;}
void mainCRTStartup(void) {
    memory_info memory;memory.size=sizeof(memory);
    check(GlobalMemoryStatusEx(&memory) && memory.total_phys && memory.free_phys<=memory.total_phys && memory.load<=100 && memory.free_va<=memory.total_va,"system-contract: actual memory capacity FAIL\n");
    unsigned size=0;check(!GetLogicalProcessorInformation(0,&size) && GetLastError()==122 && size && !(size%32),"system-contract: CPU inventory sizing FAIL\n");
    static cpu_info cpus[1024];unsigned cap=sizeof(cpus);check(GetLogicalProcessorInformation(cpus,&cap) && cap<=sizeof(cpus),"system-contract: actual CPU inventory FAIL\n");
    uint64_t cores=0,packages=0;unsigned caches=0;
    for(unsigned i=0;i<cap/32;i++){check(cpus[i].mask!=0,"system-contract: empty CPU relation FAIL\n");if(cpus[i].relation==0){check(!(cores&cpus[i].mask),"system-contract: overlapping cores FAIL\n");cores|=cpus[i].mask;}
        if(cpus[i].relation==3)packages|=cpus[i].mask;if(cpus[i].relation==2){check(*(uint32_t *)(cpus[i].data+4) && *(uint16_t *)(cpus[i].data+2),"system-contract: actual cache geometry FAIL\n");caches++;}}
    check(cores && packages==cores,"system-contract: package/core consistency FAIL\n");
    void *thread=GetCurrentThread();int disabled=0;
    check(GetThreadPriority(thread)==0 && SetThreadPriority(thread,2) && GetThreadPriority(thread)==2 && SetThreadPriorityBoost(thread,1) && GetThreadPriorityBoost(thread,&disabled) && disabled,"system-contract: real QoS changes FAIL\n");
    const uint32_t policy[3]={1,1,1};check(SetThreadInformation(thread,3,policy,sizeof(policy)),"system-contract: execution QoS preference FAIL\n");
#ifdef KYIL_NATIVE_TEST
    static vex_thread_control_req_t q;q.size=sizeof(q);q.operation=VEX_THREAD_QOS_QUERY;q.tid=GetCurrentThreadId();
    check(!vex_syscall1(VEX_SYS_THREAD_CONTROL,(uintptr_t)&q) && (q.value&0xff)==2 && (q.value&0x300)==0x300,"system-contract: actual native QoS state FAIL\n");
    const uint32_t dep[2]={3,1};check(SetProcessMitigationPolicy(0,dep,sizeof(dep)),"system-contract: mandatory native NX policy FAIL\n");
#endif
    const uint32_t normal[3]={1,1,0};check(SetThreadInformation(thread,3,normal,sizeof(normal)) && SetThreadPriority(thread,0) && SetThreadPriorityBoost(thread,0),"system-contract: QoS restore FAIL\n");
    check(!SetThreadPriority(thread,9) && GetLastError()==87,"system-contract: invalid QoS refusal FAIL\n");
    unsigned mode,written;void *output=GetStdHandle((unsigned)-11);
    if(GetConsoleMode(output,&mode))check(GetConsoleOutputCP()==65001 && WriteConsoleW(output,L"console-contract: UTF-16 -> native UTF-8\r\n",41,&written,0) && written==41,"system-contract: actual console output FAIL\n");
    control_event=CreateEventW(0,1,0,0);check(control_event && SetConsoleCtrlHandler(control,1),"system-contract: real console handler registration FAIL\n");
#ifdef KYIL_NATIVE_TEST
    vex_event_post_req_t post={.process_id=(uint32_t)vex_syscall0(VEX_SYS_GETPID),.thread_id=GetCurrentThreadId(),.event_id=VEX_EVENT_USER_INTERRUPT};
    check(!vex_syscall1(VEX_SYS_EVENT_POST,(uintptr_t)&post) && !WaitForSingleObject(control_event,5000) && controls==1 && control_tid!=GetCurrentThreadId(),"system-contract: native interrupt -> separate Windows control thread FAIL\n");
    check(ResetEvent(control_event) && SetConsoleCtrlHandler(0,1) && !vex_syscall1(VEX_SYS_EVENT_POST,(uintptr_t)&post) && WaitForSingleObject(control_event,30)==258 && controls==1,"system-contract: ignore console interrupt FAIL\n");
    check(SetConsoleCtrlHandler(0,0),"system-contract: restore console interrupt FAIL\n");
#endif
    check(SetConsoleCtrlHandler(control,0) && CloseHandle(control_event),"system-contract: handler removal/cleanup FAIL\n");
    OutputDebugStringA("windows-system-contract: real memory / CPU topology / QoS / NX / console / native control dispatch PASS\n");ExitProcess(0);
}
