#include <stdint.h>
#include <unos/vex_abi.h>
#include "vex_syscall.h"
typedef struct {void *base,*allocation;unsigned allocation_protect;uint16_t partition,pad;uint64_t length;unsigned state,protect,type,pad2;} info;
__declspec(dllimport) void *VirtualAlloc(void *,uint64_t,unsigned,unsigned),*GetCurrentProcess(void),*GetModuleHandleW(const uint16_t *);
__declspec(dllimport) int VirtualProtect(void *,uint64_t,unsigned,unsigned *),VirtualFree(void *,uint64_t,unsigned);
__declspec(dllimport) uint64_t VirtualQuery(const void *,info *,uint64_t);
__declspec(dllimport) int ReadProcessMemory(void *,const void *,void *,uint64_t,uint64_t *),WriteProcessMemory(void *,void *,const void *,uint64_t,uint64_t *);
__declspec(dllimport) void *CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) int CloseHandle(void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) unsigned DiscardVirtualMemory(void *,uint64_t),K32GetMappedFileNameW(void *,void *,uint16_t *,unsigned);
typedef struct {void *address;uint64_t attributes;} ws;
__declspec(dllimport) int K32QueryWorkingSetEx(void *,ws *,unsigned);
static unsigned stop,failed;static unsigned char *racing;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static unsigned protector(void *unused) {
    (void)unused;
    while(!__atomic_load_n(&stop,__ATOMIC_ACQUIRE)) {
        unsigned old;if(!VirtualProtect(racing,4096,2,&old) || !VirtualProtect(racing,4096,4,&old))__atomic_store_n(&failed,1,__ATOMIC_RELEASE);
    }return 0;
}
void mainCRTStartup(void) {
    unsigned char *pages=VirtualAlloc(0,12288,0x3000,4);check(pages!=0,"memory-query: real allocation FAIL\n");
    info view;check(VirtualQuery(pages,&view,sizeof(view))==48 && view.allocation==pages && view.length==12288 && view.protect==4 && view.type==0x20000,
        "memory-query: actual private inventory FAIL\n");
    ws resident[2]={{pages,0},{pages+4096,0}};
    check(K32QueryWorkingSetEx(GetCurrentProcess(),resident,sizeof(resident)) && !resident[0].attributes && !resident[1].attributes,"memory-query: observation must not materialize FAIL\n");
    pages[0]=17;pages[4096]=39;pages[8192]=81;unsigned old;
    check(K32QueryWorkingSetEx(GetCurrentProcess(),resident,sizeof(resident)) && (resident[0].attributes&1) && (resident[1].attributes&1),"memory-query: actual resident PTE FAIL\n");
    check(!DiscardVirtualMemory(pages+4096,4096) && pages[4096]==0 && pages[0]==17 && pages[8192]==81,"memory-query: discard private zero and neighbors FAIL\n");
    pages[4096]=39;uint16_t mapped[1024];unsigned mapped_length=K32GetMappedFileNameW(GetCurrentProcess(),GetModuleHandleW(0),mapped,1024);
    check(mapped_length>20 && mapped[0]=='\\' && mapped[1]=='D',"memory-query: actual module mapped path FAIL\n");
    check(DiscardVirtualMemory((void *)0xffff800000000000ull,4096)==87,"memory-query: kernel discard refusal FAIL\n");
    check(VirtualProtect(pages+4113,100,2,&old) && old==4,"memory-query: partial unaligned protect FAIL\n");
    check(VirtualQuery(pages+4097,&view,sizeof(view))==48 && view.base==pages+4096 && view.allocation==pages && view.length==4096 && view.protect==2,
        "memory-query: split permissions and origin FAIL\n");
    uint64_t count=9;unsigned char value=7;
    check(!WriteProcessMemory(GetCurrentProcess(),pages+4096,&value,1,&count) && !count && GetLastError()==299 && pages[4096]==39,
        "memory-query: readonly write refused FAIL\n");
    check(WriteProcessMemory(GetCurrentProcess(),pages,&value,1,&count) && count==1 && pages[0]==7,"memory-query: real controlled write FAIL\n");
    value=0;check(ReadProcessMemory(GetCurrentProcess(),pages+4096,&value,1,&count) && count==1 && value==39,"memory-query: real controlled read FAIL\n");
    check(!ReadProcessMemory(GetCurrentProcess(),(void *)0xffff800000000000ull,&value,1,&count) && !count && GetLastError()==299,"memory-query: kernel pointer refused FAIL\n");
    check(VirtualQuery(GetModuleHandleW(0),&view,sizeof(view))==48 && view.type==0x1000000,"memory-query: actual PE image classification FAIL\n");
    check(!VirtualQuery((void *)0xffff800000000000ull,&view,sizeof(view)) && GetLastError()==87,"memory-query: kernel inventory refused FAIL\n");
    check(VirtualFree(pages,0,0x8000) && VirtualQuery(pages,&view,sizeof(view))==48 && view.state==0x10000,"memory-query: split cleanup/free FAIL\n");
    unsigned char *code=VirtualAlloc(0,4096,0x3000,4);check(code!=0,"memory-query: code allocation FAIL\n");
    const unsigned char machine[]={0xb8,42,0,0,0,0xc3};for(unsigned i=0;i<6;i++)code[i]=machine[i];
    check(!VirtualProtect(code,4096,0x40,&old) && GetLastError()==5 && VirtualProtect(code,4096,0x20,&old),"memory-query: strict W_X transition FAIL\n");
    int (*execute)(void)=(void *)code;check(execute()==42 && VirtualFree(code,0,0x8000),"memory-query: actual RX execution FAIL\n");
    racing=VirtualAlloc(0,4096,0x3000,4);check(racing!=0,"memory-query: race allocation FAIL\n");
    void *thread=CreateThread(0,0,protector,0,0,0);check(thread!=0,"memory-query: concurrent protector FAIL\n");
    for(unsigned i=0;i<400;i++) {
        value=11;int ok=WriteProcessMemory(GetCurrentProcess(),racing,&value,1,&count);
        check((ok && count==1) || (!ok && !count && GetLastError()==299),"memory-query: atomic copy/protect race FAIL\n");
    }
    __atomic_store_n(&stop,1,__ATOMIC_RELEASE);check(!WaitForSingleObject(thread,5000) && CloseHandle(thread) && !failed && VirtualFree(racing,0,0x8000),"memory-query: race cleanup FAIL\n");
    vex_mmap_req_t reservation={0,128ull<<30,3,MAP_PRIVATE|MAP_ANONYMOUS,-1,0};
    uint64_t address=vex_syscall1(VEX_SYS_MMAP,(uintptr_t)&reservation);check(address!=(uint64_t)-1,"memory-query: native large lazy reservation FAIL\n");
    vex_memory_view_info_t native;
    vex_memory_view_req_t request={sizeof(request),VEX_MEMORY_VIEW_QUERY,address,0,&native,0,0};
    check(!vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&request) && native.length==(128ull<<30) && !native.present,"memory-query: metadata does not materialize 128GiB FAIL\n");
    request.out=(void *)0xffff800000001000ull;check((int64_t)vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&request)==-14,"memory-query: native output boundary FAIL\n");
    vex_memory_req_t unmap={(void *)(uintptr_t)address,128ull<<30,0};check(!vex_syscall1(VEX_SYS_MUNMAP,(uintptr_t)&unmap),"memory-query: sparse cleanup FAIL\n");
    OutputDebugStringA("windows-memory-query: native inventory / partial protections / real copy / readonly-kernel rejection / W_X execution / 400 SMP races / sparse 128GiB PASS\n");ExitProcess(0);
}
