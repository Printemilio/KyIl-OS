#include <stdint.h>
typedef uint16_t W;
typedef struct {unsigned size,pad;W *reserved,*desktop,*title;unsigned x,y,w,h,xc,yc,fill,flags;uint16_t show,bytes;unsigned pad2;void *reserved2,*input,*output,*error;} startup;
typedef struct {void *process,*thread;unsigned pid,tid;} process_info;
__declspec(dllimport) int GetProcessTimes(void *,uint64_t *,uint64_t *,uint64_t *,uint64_t *),CloseHandle(void *),CreateProcessW(const W *,W *,void *,void *,int,unsigned,void *,const W *,startup *,process_info *),WriteFile(void *,const void *,unsigned,unsigned *,void *);
__declspec(dllimport) unsigned GetCurrentProcessId(void),GetModuleFileNameW(void *,W *,unsigned),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) void Sleep(unsigned),OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){unsigned n=0,done;while(why[n])n++;OutputDebugStringA(why);WriteFile(GetStdHandle((unsigned)-12),why,n,&done,0);ExitProcess(1);}}
static void work(void){volatile uint64_t checksum=0;for(unsigned j=0;j<64;j++){for(unsigned i=0;i<150000;i++)checksum=(checksum<<1)^i;GetCurrentProcessId();}if(!checksum)Sleep(0);}
void mainCRTStartup(void){
#ifdef TIMES_CHILD
    work();Sleep(15);ExitProcess(37);
#else
    uint64_t birth,end,k,u,b2,e2,k2,u2;
    check(GetProcessTimes((void *)-1,&birth,&end,&k,&u) && birth>116444736000000000ull && !end,"process-times: real birth / live state FAIL\n");
    work();check(GetProcessTimes((void *)-1,&b2,&e2,&k2,&u2) && b2==birth && !e2 && k2>=k && u2>u && k2>0,"process-times: actual user/kernel accounting FAIL\n");
    check(!GetProcessTimes((void *)0x1234,&b2,&e2,&k2,&u2),"process-times: invalid capability FAIL\n");
    W exe[512];unsigned n=GetModuleFileNameW(0,exe,480);check(n && n<480,"process-times: real image path FAIL\n");while(n && exe[n-1]!='\\' && exe[n-1]!='/')n--;const W *suffix=L"windows-times-child.exe";while(*suffix)exe[n++]=*suffix++;exe[n]=0;
    startup si={.size=sizeof(si)};process_info pi;
    check(CreateProcessW(exe,0,0,0,0,0,0,0,&si,&pi) && !WaitForSingleObject(pi.process,20000),"process-times: real child completion FAIL\n");
    check(GetProcessTimes(pi.process,&birth,&end,&k,&u) && end>=birth && end && u && k,"process-times: retained final CPU and completion FAIL\n");
    Sleep(20);check(GetProcessTimes(pi.process,&b2,&e2,&k2,&u2) && b2==birth && e2==end && k2==k && u2==u && CloseHandle(pi.thread) && CloseHandle(pi.process),"process-times: completed snapshot lifetime FAIL\n");
    OutputDebugStringA("windows-process-times: birth / live state / real user and kernel CPU / final completion / capability lifetime PASS\n");ExitProcess(0);
#endif
}
