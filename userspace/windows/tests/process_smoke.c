#include <stdint.h>
typedef struct {unsigned size,pad;void *descriptor;int inherit,pad2;} security;
typedef struct {unsigned size,pad;uint16_t *reserved,*desktop,*title;unsigned x,y,w,h,xc,yc,fill,flags;
    uint16_t show,bytes;unsigned pad2;void *reserved2,*input,*output,*error;} startup;
typedef struct {void *process,*thread;unsigned pid,tid;} info;
__declspec(dllimport) int CreateProcessW(const uint16_t *,uint16_t *,void *,void *,int,unsigned,void *,const uint16_t *,startup *,info *);
__declspec(dllimport) int CreateProcessA(const char *,char *,void *,void *,int,unsigned,void *,const char *,startup *,info *);
__declspec(dllimport) void *OpenProcess(unsigned,int,unsigned);
__declspec(dllimport) unsigned GetThreadId(void *);
__declspec(dllimport) void *GetCurrentProcess(void),*GetStdHandle(unsigned);
__declspec(dllimport) unsigned GetProcessId(void *),GetCurrentProcessId(void),GetLastError(void),ResumeThread(void *),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) int GetExitCodeProcess(void *,unsigned *),GetExitCodeThread(void *,unsigned *),TerminateProcess(void *,unsigned),CloseHandle(void *);
__declspec(dllimport) int CreatePipe(void **,void **,void *,unsigned),SetHandleInformation(void *,unsigned,unsigned),GetHandleInformation(void *,unsigned *);
__declspec(dllimport) int DuplicateHandle(void *,void *,void *,void **,unsigned,int,unsigned);
__declspec(dllimport) int ReadFile(void *,void *,unsigned,unsigned *,void *),WriteFile(void *,const void *,unsigned,unsigned *,void *);
__declspec(dllimport) unsigned GetCurrentDirectoryW(unsigned,uint16_t *),GetEnvironmentVariableW(const uint16_t *,uint16_t *,unsigned);
__declspec(dllimport) int SetEnvironmentVariableW(const uint16_t *,const uint16_t *);
__declspec(dllimport) uint16_t *GetCommandLineW(void),*GetEnvironmentStringsW(void);
__declspec(dllimport) int FreeEnvironmentStringsW(uint16_t *);
__declspec(dllimport) void GetStartupInfoW(startup *),Sleep(unsigned),OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) int InitializeProcThreadAttributeList(void *,unsigned,unsigned,uint64_t *),UpdateProcThreadAttribute(void *,unsigned,uintptr_t,void *,uint64_t,void *,void *);
__declspec(dllimport) void DeleteProcThreadAttributeList(void *);
#ifdef HOST_REFERENCE
__declspec(dllimport) unsigned GetModuleFileNameW(void *,uint16_t *,unsigned);
static uint16_t child_path[512],invalid_path[512];
#define CHILD child_path
#define INVALID invalid_path
#else
#define CHILD L"A:\\windows-process-child.exe"
#define INVALID L"A:\\windows-bad-header.exe"
#endif
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);unsigned n=0,done;while(why[n])n++;WriteFile(GetStdHandle((unsigned)-11),why,n,&done,0);ExitProcess(1);}}
static int same(const uint16_t *a,const uint16_t *b){while(*a && *a==*b){a++;b++;}return *a==*b;}
static int contains(const uint16_t *a,const uint16_t *b){for(;*a;a++){unsigned i=0;while(b[i] && a[i]==b[i])i++;if(!b[i])return 1;}return 0;}
void mainCRTStartup(void) {
#ifdef PROCESS_CHILD
    const uint16_t *cmd=GetCommandLineW();
    if(contains(cmd,L"--waiting")){for(;;)Sleep(1000);}
    check(contains(cmd,L"--test \"two words\""),"process-child: raw command line FAIL\n");
    uint16_t value[64];check(GetEnvironmentVariableW(L"KYIL_CHILD_VALUE",value,64)==4 && same(value,L"real"),"process-child: explicit Unicode environment FAIL\n");
    startup si;GetStartupInfoW(&si);check(si.size==sizeof(si) && si.output==GetStdHandle((unsigned)-11),"process-child: startup handles FAIL\n");
    char bytes[16];unsigned n=0;check(ReadFile(GetStdHandle((unsigned)-10),bytes,5,&n,0) && n==5 && bytes[0]=='i' && bytes[4]=='t',"process-child: real stdin FAIL\n");
    check(WriteFile(GetStdHandle((unsigned)-11),"child-out",9,&n,0) && n==9,"process-child: real stdout FAIL\n");ExitProcess(42);
#else
#ifdef HOST_REFERENCE
    unsigned path_size=GetModuleFileNameW(0,child_path,512);check(path_size && path_size<450,"process-host: actual module path FAIL\n");
    while(path_size && child_path[path_size-1]!='\\')path_size--;
    const uint16_t *name=L"windows-process-host-child.exe";unsigned j=0;while((child_path[path_size+j]=name[j]))j++;
    for(unsigned i=0;i<path_size;i++)invalid_path[i]=child_path[i];name=L"windows-bad-header-host.exe";j=0;while((invalid_path[path_size+j]=name[j]))j++;
#endif
    check(GetProcessId(GetCurrentProcess())==GetCurrentProcessId(),"process: self identity FAIL\n");
    void *self=0;check(DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&self,0,0,2) &&
        GetProcessId(self)==GetCurrentProcessId() && CloseHandle(self),"process: native self capability duplicate FAIL\n");
    check(SetEnvironmentVariableW(L"KYIL_PARENT",L"actual"),"process: set environment FAIL\n");
    uint16_t value[32];check(GetEnvironmentVariableW(L"kyil_parent",value,32)==6 && same(value,L"actual"),"process: casefold environment FAIL\n");
    uint16_t *snapshot=GetEnvironmentStringsW();check(snapshot && FreeEnvironmentStringsW(snapshot),"process: environment snapshot lifetime FAIL\n");
#ifndef HOST_REFERENCE
    check(!FreeEnvironmentStringsW(snapshot),"process: invalid environment snapshot refused FAIL\n");
#endif
    startup si={.size=sizeof(si)};info child={0};uint16_t bad[]=L"A:\\missing-child.exe";
#ifdef HOST_REFERENCE
    uint16_t actual_missing[512];unsigned base=0;while(child_path[base])base++;while(base && child_path[base-1]!='\\')base--;
    for(unsigned i=0;i<base;i++)actual_missing[i]=child_path[i];const uint16_t *missing=L"missing-child.exe";unsigned append=0;while((actual_missing[base+append]=missing[append]))append++;
#define MISSING actual_missing
#else
#define MISSING bad
#endif
    check(!CreateProcessW(MISSING,0,0,0,0,0,0,0,&si,&child) && !child.pid && !child.process && GetLastError()==2,"process: missing image rollback FAIL\n");
    int invalid=CreateProcessW(INVALID,0,0,0,0,0,0,0,&si,&child);unsigned invalid_error=GetLastError();
#ifdef HOST_REFERENCE
    check(!invalid && invalid_error==216 && !child.pid,"process-host: unsupported DOS/image architecture FAIL\n");
#else
    check(!invalid && invalid_error==193 && !child.pid,"process: invalid executable rollback FAIL\n");
#endif
    for(unsigned cycle=0;cycle<4;cycle++) {
        void *in_read,*in_write,*out_read,*out_write;security attributes={sizeof(attributes),0,0,1,0};
        check(CreatePipe(&in_read,&in_write,&attributes,0) && CreatePipe(&out_read,&out_write,&attributes,0),"process: native pipes FAIL\n");
        check(SetHandleInformation(in_write,1,0) && SetHandleInformation(out_read,1,0),"process: inheritance attenuation FAIL\n");
        unsigned handle_flags;check(GetHandleInformation(in_read,&handle_flags) && handle_flags==1,"process: actual inherit flag FAIL\n");
        startup io={.size=sizeof(io),.flags=0x100,.input=in_read,.output=out_write,.error=out_write};
        uint16_t cmd[]=L"\"A:\\windows-process-child.exe\" --test \"two words\"";
        uint16_t env[]=L"KYIL_CHILD_VALUE=real\0";
        uint64_t attribute_storage[8]={0};uint64_t attribute_bytes=0;
        struct {startup basic;void *attributes;} extended={io,attribute_storage};
        void *selection[2]={in_read,out_write};startup *selected_startup=&io;unsigned creation=4|0x40000;
        if(cycle==2) {
            check(!InitializeProcThreadAttributeList(0,1,0,&attribute_bytes) && attribute_bytes && attribute_bytes<=sizeof(attribute_storage) && GetLastError()==122,"process: attribute sizing FAIL\n");
            attribute_bytes=sizeof(attribute_storage);check(InitializeProcThreadAttributeList(attribute_storage,1,0,&attribute_bytes),"process: attribute list init FAIL\n");
            check(UpdateProcThreadAttribute(attribute_storage,0,0x20002,selection,sizeof(selection),0,0),"process: actual handle list FAIL\n");
            extended.basic.size=sizeof(extended);selected_startup=&extended.basic;creation|=0x80000;
            selection[1]=in_read;
            check(!CreateProcessW(CHILD,cmd,0,0,1,creation,env,0,selected_startup,&child) && GetLastError()==50 && !child.pid && !child.process,
                "process: explicit list never widened for stdio FAIL\n");
            selection[1]=out_write;
        }
        int created;
#ifndef HOST_REFERENCE
        if(cycle==3) {
            char ansi_command[]="\"A:\\windows-process-child.exe\" --test \"two words\"";
            char ansi_env[]="KYIL_CHILD_VALUE=real\0";
            created=CreateProcessA("A:\\windows-process-child.exe",ansi_command,0,0,1,creation&~0x40000u,ansi_env,0,selected_startup,&child);
        }else
#endif
        created=CreateProcessW(CHILD,cmd,0,0,1,creation,env,0,selected_startup,&child);
        if(cycle==2)DeleteProcThreadAttributeList(attribute_storage);
        if(!created){unsigned code=GetLastError(),written;char diagnostic[]="process-create errno=0000000000\n";for(unsigned i=0;i<10;i++){diagnostic[29-i]=(char)('0'+code%10);code/=10;}WriteFile(GetStdHandle((unsigned)-11),diagnostic,sizeof(diagnostic)-1,&written,0);}
        check(created,"process: real confined launch FAIL\n");
        unsigned code=0;check(child.pid && child.tid && GetProcessId(child.process)==child.pid &&
            GetExitCodeProcess(child.process,&code) && code==259 && WaitForSingleObject(child.process,0)==258,"process: suspended state FAIL\n");
        check(GetThreadId(child.thread)==child.tid,"process: actual primary thread identity FAIL\n");
        void *limited=OpenProcess(0x1000,0,child.pid);
        check(limited && GetProcessId(limited)==child.pid && !TerminateProcess(limited,999) && GetLastError()==5 &&
            WaitForSingleObject(limited,0)==0xffffffffu && GetLastError()==5 && CloseHandle(limited),"process: query capability never becomes terminate/wait FAIL\n");
        limited=OpenProcess(0x100000,0,child.pid);
        check(limited && !GetProcessId(limited) && GetLastError()==5 && WaitForSingleObject(limited,0)==258 && CloseHandle(limited),
            "process: wait capability independent of query FAIL\n");
        check(ResumeThread(child.thread)==1 && ResumeThread(child.thread)==0,"process: actual resume count FAIL\n");
        unsigned n;check(WriteFile(in_write,"input",5,&n,0) && n==5 && CloseHandle(in_write),"process: stdin transfer FAIL\n");
        check(CloseHandle(in_read) && CloseHandle(out_write),"process: parent endpoints close FAIL\n");
        check(!WaitForSingleObject(child.thread,5000) && !WaitForSingleObject(child.process,5000) &&
            GetExitCodeThread(child.thread,&code) && code==42 && GetExitCodeProcess(child.process,&code) && code==42,"process: real completion status FAIL\n");
        char data[16];check(ReadFile(out_read,data,16,&n,0) && n==9 && data[0]=='c' && data[8]=='t',"process: actual output bytes FAIL\n");
        check(ReadFile(out_read,data,16,&n,0) && !n && CloseHandle(out_read),"process: EOF after child exit FAIL\n");
        check(CloseHandle(child.thread) && GetExitCodeProcess(child.process,&code) && code==42 && CloseHandle(child.process),"process: independent handles lifetime FAIL\n");
    }
    uint16_t waiting[]=L"A:\\windows-process-child.exe --waiting";
    check(CreateProcessW(CHILD,waiting,0,0,0,0,0,0,&si,&child),"process: terminate target launch FAIL\n");
    check(TerminateProcess(child.process,77) && !WaitForSingleObject(child.process,5000),"process: actual whole-process terminate FAIL\n");
    unsigned code;check(GetExitCodeProcess(child.process,&code) && code==77 && CloseHandle(child.thread) && CloseHandle(child.process),"process: terminate status/lifetime FAIL\n");
    OutputDebugStringA("windows-process: native sealed launch / argv-env / real stdin-stdout / suspended-resume / exit-status / EOF / handles / invalid image / repeated children / terminate PASS\n");ExitProcess(0);
#endif
}
