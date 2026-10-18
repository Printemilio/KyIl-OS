#include <stdint.h>
typedef uint16_t W;
void *memset(void *p,int c,__SIZE_TYPE__ n){unsigned char *b=p;while(n--)*b++=(unsigned char)c;return p;}
void *memcpy(void *p,const void *q,__SIZE_TYPE__ n){unsigned char *b=p;const unsigned char *s=q;while(n--)*b++=*s++;return p;}
typedef struct {unsigned size,pad;void *descriptor;int inherit,pad2;} attributes;
typedef struct {unsigned size,pad;W *reserved,*desktop,*title;unsigned x,y,w,h,xc,yc,fill,flags;uint16_t show,bytes;unsigned pad2;void *reserved2,*input,*output,*error;} startup;
typedef struct {void *process,*thread;unsigned pid,tid;} process_info;
__declspec(dllimport) void *CreateEventW(void *,int,int,const W *),*CreateThread(void *,uint64_t,void *,void *,unsigned,unsigned *),*GetCurrentProcess(void);
__declspec(dllimport) int CloseHandle(void *),SetEvent(void *),ResetEvent(void *),GetHandleInformation(void *,unsigned *),SetHandleInformation(void *,unsigned,unsigned),DuplicateHandle(void *,void *,void *,void **,unsigned,int,unsigned),CreateProcessW(const W *,W *,void *,void *,int,unsigned,void *,const W *,startup *,process_info *),GetExitCodeProcess(void *,unsigned *),TerminateProcess(void *,unsigned);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void),GetModuleFileNameW(void *,W *,unsigned);
__declspec(dllimport) W *GetCommandLineW(void);
__declspec(dllimport) int RegisterWaitForSingleObject(void **,void *,void (*)(void *,uint8_t),void *,unsigned,unsigned),UnregisterWaitEx(void *,void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) void GetStartupInfoW(startup *);
#ifndef KYIL_NATIVE_TEST
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) int WriteFile(void *,const void *,unsigned,unsigned *,void *);
#endif
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);
#ifndef KYIL_NATIVE_TEST
    unsigned length=0,written;while(why[length])length++;WriteFile(GetStdHandle((unsigned)-12),why,length,&written,0);
#endif
    ExitProcess(1);}}
static unsigned append(W *s,unsigned at,uintptr_t n){W digits[24];unsigned count=0;do{digits[count++]=(W)('0'+n%10);n/=10;}while(n);while(count)s[at++]=digits[--count];s[at++]=' ';s[at]=0;return at;}
static void *argument(const W **s){while(**s==' ')(*s)++;uintptr_t n=0;while(**s>='0' && **s<='9'){n=n*10+*(*s)++-'0';}return (void *)n;}
static void callback(void *arg,uint8_t timed){check(!timed && SetEvent(arg),"shared-event: registered callback signal FAIL\n");}
static void child(const W *command){const W *p=command+6;void *manual=argument(&p),*automatic=argument(&p),*ack=argument(&p);unsigned flags=0;
    startup si;GetStartupInfoW(&si);check(si.flags&0x80,"shared-event: startup feedback suppression preserved FAIL\n");
    check(GetHandleInformation(manual,&flags) && (flags&1),"shared-event: inherited flag FAIL\n");
    check(!WaitForSingleObject(manual,5000) && !WaitForSingleObject(manual,0),"shared-event: manual signal shared and retained FAIL\n");
    check(ResetEvent(manual) && WaitForSingleObject(manual,0)==258,"shared-event: manual reset shared FAIL\n");
    check(SetEvent(automatic) && SetEvent(automatic) && !WaitForSingleObject(automatic,0) && WaitForSingleObject(automatic,0)==258,"shared-event: auto reset/coalescing FAIL\n");
    check(SetEvent(ack) && CloseHandle(manual) && CloseHandle(automatic) && CloseHandle(ack),"shared-event: child release FAIL\n");ExitProcess(31);
}
void mainCRTStartup(void){const W *command=GetCommandLineW();if(command[0]=='-' && command[1]=='-' && command[2]=='c')child(command);
    W image[512];check(GetModuleFileNameW(0,image,512),"shared-event: real image path FAIL\n");attributes a={sizeof(a),0,0,1,0};
    for(unsigned cycle=0;cycle<6;cycle++){
        void *manual=CreateEventW(&a,1,0,0),*automatic=CreateEventW(&a,0,0,0),*ack=CreateEventW(&a,0,0,0);check(manual && automatic && ack,"shared-event: actual shared create FAIL\n");
        void *copy=0;check(DuplicateHandle(GetCurrentProcess(),manual,GetCurrentProcess(),&copy,0,0,2) && SetEvent(copy) && !WaitForSingleObject(manual,0) && ResetEvent(copy) && CloseHandle(copy),"shared-event: same object through duplicate FAIL\n");
#ifdef KYIL_NATIVE_TEST
        check(SetHandleInformation(manual,2,2) && !CloseHandle(manual) && GetLastError()==5 && SetHandleInformation(manual,2,0),"shared-event: protected close flag FAIL\n");
#endif
        W line[160]={'-','-','c','h','d',' ',0};unsigned at=6;at=append(line,at,(uintptr_t)manual);at=append(line,at,(uintptr_t)automatic);append(line,at,(uintptr_t)ack);
        startup si={.size=sizeof(si),.flags=0x80};process_info info={0};check(CreateProcessW(image,line,0,0,1,0,0,0,&si,&info),"shared-event: real child inheritance FAIL\n");
        check(SetEvent(manual),"shared-event: parent signal FAIL\n");
        if(cycle&1){check(CloseHandle(manual) && CloseHandle(automatic),"shared-event: creator close before consumer FAIL\n");}
        check(!WaitForSingleObject(ack,5000) && !WaitForSingleObject(info.process,5000),"shared-event: child wakes across process FAIL\n");
        unsigned code=0;check(GetExitCodeProcess(info.process,&code) && code==31,"shared-event: child verification FAIL\n");
        if(!(cycle&1)){check(WaitForSingleObject(manual,0)==258 && WaitForSingleObject(automatic,0)==258 && CloseHandle(manual) && CloseHandle(automatic),"shared-event: independent owner state FAIL\n");}
        check(CloseHandle(info.thread) && CloseHandle(info.process),"shared-event: child observer close FAIL\n");
        void *target=CreateEventW(&a,0,0,0),*registration=0;check(target && RegisterWaitForSingleObject(&registration,target,callback,ack,UINT32_MAX,8),"shared-event: callback registration FAIL\n");
        check(SetEvent(target) && !WaitForSingleObject(ack,5000) && UnregisterWaitEx(registration,(void *)-1),"shared-event: callback real wait FAIL\n");
        registration=0;check(RegisterWaitForSingleObject(&registration,target,callback,ack,UINT32_MAX,8) && UnregisterWaitEx(registration,(void *)-1),"shared-event: cancel while registering/waiting FAIL\n");
        check(CloseHandle(target) && CloseHandle(ack),"shared-event: final references close FAIL\n");
    }
    OutputDebugStringA("windows-shared-event: six real child lifecycles / creator close / manual-auto reset / coalescing / duplicate / protected close / callback wait and cancel PASS\n");ExitProcess(0);
}
