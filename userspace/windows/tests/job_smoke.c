#include <stdint.h>
typedef uint16_t W;
typedef struct {uint32_t size,pad;W *reserved,*desktop,*title;uint32_t x,y,width,height,xc,yc,fill,flags;uint16_t show,reserved_bytes;uint32_t pad2;void *reserved2,*input,*output,*error;} startup;
typedef struct {void *process,*thread;unsigned pid,tid;} process_info;
typedef struct {uint64_t user_time,job_time;uint32_t flags,pad;uint64_t minimum,maximum;uint32_t active,pad2;uint64_t affinity;uint32_t priority,scheduling;} basic;
typedef struct {basic basic;uint64_t rest[10];} limits;
__declspec(dllimport) void *CreateJobObjectW(void *,const W *),*GetCurrentProcess(void),*CreateNamedPipeW(const W *,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,void *),*CreateFileW(const W *,unsigned,unsigned,void *,unsigned,unsigned,void *);
__declspec(dllimport) int SetInformationJobObject(void *,unsigned,const void *,unsigned),QueryInformationJobObject(void *,unsigned,void *,unsigned,unsigned *),AssignProcessToJobObject(void *,void *),IsProcessInJob(void *,void *,int *),CloseHandle(void *),DuplicateHandle(void *,void *,void *,void **,unsigned,int,unsigned),SetHandleInformation(void *,unsigned,unsigned),GetExitCodeProcess(void *,unsigned *),ReadFile(void *,void *,unsigned,unsigned *,void *),WriteFile(void *,const void *,unsigned,unsigned *,void *),ConnectNamedPipe(void *,void *),CreateProcessW(const W *,W *,void *,void *,int,unsigned,void *,const W *,startup *,process_info *);
__declspec(dllimport) unsigned GetLastError(void),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int yes,const char *why){if(!yes){OutputDebugStringA(why);ExitProcess(1);}}
static const W *worker_pipe=(const W *)L"\\\\.\\pipe\\kyil-job-owner",*leaf_pipe=(const W *)L"\\\\.\\pipe\\kyil-job-leaf";
static void *connect(const W *name){void *h=CreateFileW(name,0xc0000000u,0,0,3,0,0);check(h!=(void *)-1,"job: real pipe connection FAIL\n");return h;}
static void send(void *h,char byte){unsigned count;check(WriteFile(h,&byte,1,&count,0) && count==1,"job: real control bytes FAIL\n");}
static char receive(void *h){char byte;unsigned count;check(ReadFile(h,&byte,1,&count,0) && count==1,"job: receive control bytes FAIL\n");return byte;}
void mainCRTStartup(void) {
#ifdef JOB_LEAF
    int in=0;check(IsProcessInJob(GetCurrentProcess(),0,&in) && in,"job: child membership before entry FAIL\n");
    void *pipe=connect(leaf_pipe);send(pipe,'L');receive(pipe);ExitProcess(99);
#elif defined(JOB_WORKER)
    void *job=CreateJobObjectW(0,0),*duplicate=0;check(job!=0,"job: service creation FAIL\n");
    static limits policy;policy.basic.flags=0x2008;policy.basic.active=2;
    check(SetInformationJobObject(job,9,&policy,sizeof(policy)) && AssignProcessToJobObject(job,GetCurrentProcess()),"job: actual limits / self membership FAIL\n");
    int in=0;check(IsProcessInJob(GetCurrentProcess(),job,&in) && in,"job: actual membership query FAIL\n");
    check(DuplicateHandle(GetCurrentProcess(),job,GetCurrentProcess(),&duplicate,0,0,2),"job: duplicate capability FAIL\n");
    startup start={.size=sizeof(start)};process_info child;
    int created=CreateProcessW((const W *)L"A:\\windows-job-leaf.exe",0,0,0,0,0,0,0,&start,&child);
    if(!created){unsigned error=GetLastError();char text[]="job: descendant admission error=00000000\n";unsigned at=0;while(text[at]!='=')at++;at++;for(unsigned i=0;i<8;i++)text[at+i]="0123456789abcdef"[(error>>(28-4*i))&15];OutputDebugStringA(text);}
    check(created,"job: descendant admission FAIL\n");
    CloseHandle(child.thread);CloseHandle(child.process);
    process_info refused;check(!CreateProcessW((const W *)L"A:\\windows-job-leaf.exe",0,0,0,0,0,0,0,&start,&refused) && GetLastError()==1816 && !refused.process && !refused.thread,"job: real active process limit FAIL\n");
    void *control=connect(worker_pipe);send(control,'W');check(receive(control)=='a',"job: owner first close command FAIL\n");
    check(CloseHandle(job),"job: independent public close FAIL\n");send(control,'A');check(receive(control)=='b',"job: owner last close command FAIL\n");
    CloseHandle(duplicate);
    /* No voluntary ExitProcess: parent must observe kernel termination caused
       by the independent service's last-public-handle lifecycle. */
    receive(control);ExitProcess(98);
#else
    for(unsigned i=0;i<4;i++) {
        void *job=CreateJobObjectW(0,0);static limits policy,result;
        check(job!=0,"job: empty object creation FAIL\n");
        check(WaitForSingleObject(job,0)==0,"job: empty object signal FAIL\n");
        policy.basic.flags=8;policy.basic.active=3;unsigned used=0;
        check(SetInformationJobObject(job,9,&policy,sizeof(policy)) && QueryInformationJobObject(job,9,&result,sizeof(result),&used) && used==sizeof(result) && result.basic.flags==8 && result.basic.active==3,"job: real limit roundtrip FAIL\n");
        policy.basic.flags=0x100;check(!SetInformationJobObject(job,9,&policy,sizeof(policy)) && GetLastError()==50,"job: unsupported limits explicit FAIL\n");
        check(!SetHandleInformation(job,1,1) && GetLastError()==50 && CloseHandle(job),"job: unsupported public inheritance / cleanup FAIL\n");
#ifdef KYIL_NATIVE_TEST
        check(!CloseHandle(job) && GetLastError()==6,"job: stale handle FAIL\n");
#endif
    }
    void *owner=CreateNamedPipeW(worker_pipe,3,6,1,4096,4096,0,0),*leaf=CreateNamedPipeW(leaf_pipe,3,6,1,4096,4096,0,0);
    check(owner!=(void *)-1 && leaf!=(void *)-1,"job: independent control endpoints FAIL\n");
    startup start={.size=sizeof(start)};process_info child;
    check(CreateProcessW((const W *)L"A:\\windows-job-worker.exe",0,0,0,0,0,0,0,&start,&child),"job: owner process launch FAIL\n");
    check(ConnectNamedPipe(owner,0) || GetLastError()==535,"job: owner connection FAIL\n");check(receive(owner)=='W',"job: owner ready FAIL\n");
    check(ConnectNamedPipe(leaf,0) || GetLastError()==535,"job: leaf connection FAIL\n");check(receive(leaf)=='L',"job: leaf ready FAIL\n");
    send(owner,'a');check(receive(owner)=='A' && WaitForSingleObject(child.process,0)==258,"job: duplicate keeps members alive FAIL\n");
    send(owner,'b');check(!WaitForSingleObject(child.process,10000),"job: real kill-on-last-close FAIL\n");unsigned status;
    check(GetExitCodeProcess(child.process,&status) && status==0,"job: kernel termination status FAIL\n");
    char byte;unsigned count;check(!ReadFile(leaf,&byte,1,&count,0) && GetLastError()==109,"job: descendant terminated / IPC EOF FAIL\n");
    check(CloseHandle(child.process) && CloseHandle(child.thread) && CloseHandle(owner) && CloseHandle(leaf),"job: owner/observer cleanup FAIL\n");
    OutputDebugStringA("windows-job: userspace service / real policy / active process limit / inherited membership / independent duplicates / last-close terminates owner and descendant PASS\n");ExitProcess(0);
#endif
}
