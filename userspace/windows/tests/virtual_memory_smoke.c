#include <stdint.h>
#ifdef KYIL_NATIVE_TEST
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#endif
typedef struct {void *base,*allocation;unsigned allocation_protect;uint16_t partition,pad;uint64_t length;unsigned state,protect,type,pad2;} info;
__declspec(dllimport) void *VirtualAlloc(void *,uint64_t,unsigned,unsigned),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) int VirtualFree(void *,uint64_t,unsigned),VirtualProtect(void *,uint64_t,unsigned,unsigned *),CloseHandle(void *);
__declspec(dllimport) uint64_t VirtualQuery(const void *,info *,uint64_t);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) int WriteFile(void *,const void *,unsigned,unsigned *,void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static unsigned char *base;
static void check(int ok,const char *why){if(!ok){unsigned n=0,done,error=GetLastError();while(why[n])n++;OutputDebugStringA(why);WriteFile(GetStdHandle((unsigned)-12),why,n,&done,0);
    char text[]="error=0x00000000\n";for(unsigned i=0;i<8;i++)text[8+i]="0123456789abcdef"[(error>>(28-4*i))&15];WriteFile(GetStdHandle((unsigned)-12),text,sizeof(text)-1,&done,0);ExitProcess(1);}}
static unsigned worker(void *argument){unsigned slot=(unsigned)(uintptr_t)argument;unsigned char *p=base+0x10000+slot*4096;
    for(unsigned i=0;i<64;i++){
        check(VirtualAlloc(p,4096,0x1000,4)==p && !*p,"virtual-memory: parallel commit zero FAIL\n");*p=(unsigned char)(slot+1);
        check(VirtualFree(p,4096,0x4000),"virtual-memory: parallel decommit FAIL\n");
    }return 0;
}
void mainCRTStartup(void){info q;unsigned old;
#ifdef KYIL_NATIVE_TEST
    vex_memory_usage_t before,after;vex_memory_view_req_t facts={.size=sizeof(facts),.operation=VEX_MEMORY_VIEW_USAGE,.out=&before};
    check(!vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&facts),"virtual-memory: real RAM snapshot FAIL\n");
#endif
    base=VirtualAlloc(0,32ull<<30,0x2000,1);check(base && !((uintptr_t)base&65535),"virtual-memory: sparse 32GiB reservation FAIL\n");
    check(VirtualQuery(base+8192,&q,sizeof(q))==48 && q.base==base+8192 && q.allocation==base && q.state==0x2000 && !q.protect && q.length==(32ull<<30)-8192,"virtual-memory: reserved metadata FAIL\n");
#ifdef KYIL_NATIVE_TEST
    facts.out=&after;check(!vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&facts) && after.resident_bytes-before.resident_bytes<(2ull<<20) && after.private_charge_bytes-before.private_charge_bytes<(2ull<<20),"virtual-memory: no massive residency or commit charge FAIL\n");
#endif
    check(!VirtualProtect(base,4096,4,&old),"virtual-memory: reserved protection refused FAIL\n");
    unsigned char *p=base+4096;
    check(VirtualAlloc(p+17,20,0x1000,4)==p && !*p,"virtual-memory: unaligned private commit FAIL\n");*p=91;
    check(VirtualAlloc(p,4096,0x1000,4)==p && *p==91,"virtual-memory: repeated commit preserves bytes FAIL\n");
    check(VirtualQuery(p,&q,sizeof(q))==48 && q.base==p && q.allocation==base && q.state==0x1000 && q.protect==4 && q.length==4096,"virtual-memory: committed subrange FAIL\n");
    check(VirtualQuery(base+8192,&q,sizeof(q))==48 && q.state==0x2000,"virtual-memory: reserved neighbor FAIL\n");
    check(VirtualProtect(p,4096,2,&old) && old==4 && VirtualFree(p,4096,0x4000),"virtual-memory: readonly decommit FAIL\n");
    check(VirtualQuery(p,&q,sizeof(q))==48 && q.state==0x2000 && !q.protect && VirtualAlloc(p,4096,0x1000,4)==p && !*p,"virtual-memory: recommit zero FAIL\n");
    check(VirtualAlloc(base+8192,4096,0x1000,1)==base+8192 && VirtualQuery(base+8192,&q,sizeof(q))==48 && q.state==0x1000 && q.protect==1,"virtual-memory: committed noaccess distinct from reserve FAIL\n");
    void *threads[4];for(unsigned i=0;i<4;i++){threads[i]=CreateThread(0,0,worker,(void *)(uintptr_t)i,0,0);check(threads[i]!=0,"virtual-memory: actual SMP worker FAIL\n");}
    for(unsigned i=0;i<4;i++)check(!WaitForSingleObject(threads[i],10000) && CloseHandle(threads[i]),"virtual-memory: real completion FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!VirtualAlloc(p,4096,0x1000,0x40) && GetLastError()==5 && !VirtualAlloc((void *)0xffff800000000000ull,4096,0x1000,4),"virtual-memory: W_X and kernel boundary FAIL\n");
    vex_memory_view_req_t bad={.size=sizeof(bad),.operation=VEX_MEMORY_VIEW_COMMIT,.address=0xffff800000000000ull,.length=4096,.permissions=3};
    check((int64_t)vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&bad)<0,"virtual-memory: native boundary refusal FAIL\n");
#endif
    check(VirtualFree(base,0,0x4000) && VirtualQuery(p,&q,sizeof(q))==48 && q.state==0x2000 && VirtualFree(base,0,0x8000),"virtual-memory: whole decommit and release FAIL\n");
    check(VirtualQuery(base,&q,sizeof(q))==48 && q.state==0x10000,"virtual-memory: release VA ownership FAIL\n");
    OutputDebugStringA("windows-virtual-memory: sparse 32GiB / reserve vs commit noaccess / partial commit / retained bytes / real zero after decommit / readonly / four parallel workers / release PASS\n");ExitProcess(0);
}
