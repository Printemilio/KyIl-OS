#include <stdint.h>
typedef uint16_t W;
typedef struct {unsigned size,pad;void *descriptor;int inherit,pad2;} security;
typedef struct {unsigned size,pad;W *reserved,*desktop,*title;unsigned x,y,w,h,xc,yc,fill,flags;uint16_t show,bytes;unsigned pad2;void *reserved2,*input,*output,*error;} startup;
typedef struct {void *process,*thread;unsigned pid,tid;} info;
__declspec(dllimport) int SetProcessShutdownParameters(unsigned,unsigned),GetProcessShutdownParameters(unsigned *,unsigned *),SetConsoleCtrlHandler(int (*)(unsigned),int),CreatePipe(void **,void **,void *,unsigned),SetHandleInformation(void *,unsigned,unsigned),CreateProcessW(const W *,W *,void *,void *,int,unsigned,void *,const W *,startup *,info *),ReadFile(void *,void *,unsigned,unsigned *,void *),WriteFile(void *,const void *,unsigned,unsigned *,void *),CloseHandle(void *);
__declspec(dllimport) unsigned GetLastError(void);
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned),Sleep(unsigned);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
#ifdef ORDER_LEVEL
static int control(unsigned event){if(event!=5)return 0;
#if ORDER_LEVEL == 0x300
    OutputDebugStringA("windows-shutdown-delivered: high\n");
#elif ORDER_LEVEL == 0x280
    OutputDebugStringA("windows-shutdown-delivered: middle\n");
#else
    OutputDebugStringA("windows-shutdown-delivered: low\n");
#endif
    ExitProcess(0);return 1;
}
#endif
void mainCRTStartup(void){unsigned rank,flags;
#ifdef ORDER_LEVEL
    check(SetProcessShutdownParameters(ORDER_LEVEL,0) && GetProcessShutdownParameters(&rank,&flags) && rank==ORDER_LEVEL && !flags && SetConsoleCtrlHandler(control,1),"shutdown-order: active lifecycle contract FAIL\n");
    unsigned sent;check(WriteFile(GetStdHandle((unsigned)-11),"R",1,&sent,0) && sent==1,"shutdown-order: ready handshake FAIL\n");
    for(;;)Sleep(1000000);
#else
    check(GetProcessShutdownParameters(&rank,&flags) && rank==0x280 && !flags,"shutdown-order: actual default FAIL\n");
    check(!SetProcessShutdownParameters(0x500,0) && GetLastError()==87 && !SetProcessShutdownParameters(0x280,2) && GetLastError()==87,"shutdown-order: invalid inputs FAIL\n");
    check(!SetProcessShutdownParameters(0x280,1) && GetLastError()==50,"shutdown-order: absent retry UI honest FAIL\n");
    const W *executables[3]={L"A:\\windows-order-low.exe",L"A:\\windows-order-high.exe",L"A:\\windows-order-middle.exe"};
    for(unsigned i=0;i<3;i++){
        void *read,*write;security sa={sizeof(sa),0,0,1,0};check(CreatePipe(&read,&write,&sa,0),"shutdown-order: actual pipe FAIL\n");
        startup si={.size=sizeof(si),.flags=0x100,.input=read,.output=write,.error=write};info pi;
        check(CreateProcessW(executables[i],0,0,0,1,0,0,0,&si,&pi) && CloseHandle(write),"shutdown-order: independent process launch FAIL\n");
        char ready=0;unsigned got;check(ReadFile(read,&ready,1,&got,0) && got==1 && ready=='R' && CloseHandle(read) && CloseHandle(pi.thread) && CloseHandle(pi.process),"shutdown-order: initialized child handshake FAIL\n");
    }
    OutputDebugStringA("windows-shutdown-order: independently launched low / high / middle workers armed PASS\n");ExitProcess(0);
#endif
}
