typedef unsigned long long usize;
__declspec(dllimport) void *CreateThread(void *,usize,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned);
__declspec(dllimport) unsigned ResumeThread(void *);
__declspec(dllimport) int CloseHandle(void *);
__declspec(dllimport) int GetExitCodeThread(void *,unsigned *);
__declspec(dllimport) unsigned GetCurrentThreadId(void);
__declspec(dllimport) unsigned GetCurrentProcessId(void);
__declspec(dllimport) unsigned GetCurrentProcessorNumber(void);
__declspec(dllimport) unsigned GetLastError(void);
__declspec(dllimport) void SetLastError(unsigned);
__declspec(dllimport) unsigned TlsAlloc(void);
__declspec(dllimport) int TlsFree(unsigned);
__declspec(dllimport) int TlsSetValue(unsigned,void *);
__declspec(dllimport) void *TlsGetValue(unsigned);
__declspec(dllimport) void Sleep(unsigned);
__declspec(dllimport) void OutputDebugStringA(const char *);
__declspec(dllimport) void ExitProcess(unsigned);
__declspec(dllimport) void AcquireSRWLockExclusive(usize *),ReleaseSRWLockExclusive(usize *),AcquireSRWLockShared(usize *),ReleaseSRWLockShared(usize *);
__declspec(dllimport) unsigned DllOne(unsigned),DllTwo(unsigned),DllOneCounts(void),DllTwoCounts(void);
__declspec(thread) static unsigned value=17;
__declspec(thread) static unsigned stage;
static volatile unsigned failures,cpu_mask,migrations;
static unsigned slot;
static usize main_peb;
static usize lock;
static unsigned shared_counter;
#include "tls_support.h"
static usize gs(unsigned offset) {
    usize r;
    switch(offset) {
    case 0x30:__asm__("movq %%gs:0x30,%0":"=r"(r));break;
    case 0x60:__asm__("movq %%gs:0x60,%0":"=r"(r));break;
    default:r=0;break;
    }
    return r;
}
static void check(int ok,const char *label) {
    if(!ok) {OutputDebugStringA(label);OutputDebugStringA("windows-tls: FAIL\n");ExitProcess(1);}
}
static void tls_callback(void *base,unsigned reason,void *reserved) {
    (void)base;(void)reserved;
    if(reason==1 || reason==2) {if(value!=17 || !gs(0x30) || !gs(0x60))__builtin_trap();stage=reason;}
}
__declspec(noinline) static unsigned grow(unsigned n) {
    volatile unsigned char bytes[2000];bytes[0]=(unsigned char)n;bytes[1999]=(unsigned char)(n+1);
    return n?grow(n-1)+bytes[0]+bytes[1999]:bytes[0]+bytes[1999];
}
static unsigned worker(void *p) {
    unsigned id=(unsigned)(usize)p,failed=0,old_cpu=GetCurrentProcessorNumber();
    failed|=stage!=2 || value!=17 || gs(0x60)!=main_peb;
    failed|=*(usize *)(gs(0x30)+0x48)!=GetCurrentThreadId() ||
        *(usize *)(gs(0x30)+0x40)!=GetCurrentProcessId();
    value=id;failed|=!TlsSetValue(slot,(void *)(usize)id);SetLastError(1000+id);
    for(unsigned k=0;k<2000;k++) {
        AcquireSRWLockExclusive(&lock);shared_counter++;ReleaseSRWLockExclusive(&lock);
        AcquireSRWLockShared(&lock);failed|=shared_counter==0;ReleaseSRWLockShared(&lock);
        failed|=DllOne(1)!=101+k || DllTwo(1)!=201+k || value!=id;
        if((k&31)==0) {
            unsigned cpu=GetCurrentProcessorNumber();if(cpu<32)__atomic_fetch_or(&cpu_mask,1u<<cpu,__ATOMIC_SEQ_CST);
            if(cpu!=old_cpu)__atomic_fetch_add(&migrations,1,__ATOMIC_SEQ_CST);old_cpu=cpu;
            failed|=GetLastError()!=1000+id;failed|=TlsGetValue(slot)!=(void *)(usize)id;
            SetLastError(1000+id);Sleep(0);
        }
    }
    failed|=grow(12)!=169;
    if(failed)__atomic_fetch_add(&failures,1,__ATOMIC_SEQ_CST);
    return id*3;
}
void mainCRTStartup(void) {
    check(stage==1 && value==17,"main TLS/callback");main_peb=gs(0x60);
    check(*(usize *)(main_peb+0x18)!=0 && *(usize *)(main_peb+0x20)!=0,"PEB loader/params");
    slot=TlsAlloc();check(slot!=0xffffffffu,"TlsAlloc");
    void *handles[8];
    for(unsigned i=0;i<8;i++) {unsigned tid=0;handles[i]=CreateThread(0,0,worker,(void *)(usize)(i+1),i==0?4:0,&tid);check(handles[i] && tid,"CreateThread");}
    check(WaitForSingleObject(handles[0],0)==258,"suspended thread really pending");
    check(ResumeThread(handles[0])==1,"ResumeThread");
    for(unsigned i=0;i<8;i++) {
        unsigned result=0;check(WaitForSingleObject(handles[i],0xffffffffu)==0,"join");
        check(GetExitCodeThread(handles[i],&result) && result==(i+1)*3,"exit code");
        check(CloseHandle(handles[i]) && !CloseHandle(handles[i]),"stale handle");
    }
    check(!failures && shared_counter==16000 && value==17 && DllOne(0)==101 && DllTwo(0)==201,"independent TLS/GS/SRW/stack growth");
    check(DllOneCounts()==8*65536+8 && DllTwoCounts()==8*65536+8,"callback lifecycle");
    check(TlsFree(slot) && !TlsFree(slot),"TLS slot lifetime");
    char trace[]="windows-tls: CPU mask=00000000 migrations=00000000\n";
    const char *hex="0123456789abcdef";
    for(unsigned i=0;i<8;i++) {trace[22+i]=hex[(cpu_mask>>(28-i*4))&15];trace[42+i]=hex[(migrations>>(28-i*4))&15];}
    OutputDebugStringA(trace);OutputDebugStringA("windows-tls: 8 threads / EXE + 2 DLL / GS-PEB / callbacks / guard growth PASS\n");ExitProcess(0);
}
