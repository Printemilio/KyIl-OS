#include <stdint.h>
typedef uint16_t W;
typedef struct {uint16_t length,maximum;unsigned pad;W *buffer;} unicode;
typedef struct {unsigned size,pad;W *reserved,*desktop,*title;unsigned x,y,w,h,xc,yc,fill,flags;uint16_t show,bytes;unsigned pad2;void *reserved2,*input,*output,*error;} startup;
typedef struct {void *process,*thread;unsigned pid,tid;} process_info;
__declspec(dllimport) int SetCurrentDirectoryW(const W *),CreateDirectoryW(const W *,void *),RemoveDirectoryW(const W *),DeleteFileW(const W *),CloseHandle(void *),WriteFile(void *,const void *,unsigned,unsigned *,void *),ReadFile(void *,void *,unsigned,unsigned *,void *),CreateProcessW(const W *,W *,void *,void *,int,unsigned,void *,const W *,startup *,process_info *),GetExitCodeProcess(void *,unsigned *);
__declspec(dllimport) unsigned GetCurrentDirectoryW(unsigned,W *),GetTempPathW(unsigned,W *),GetFullPathNameW(const W *,unsigned,W *,W **),GetModuleFileNameW(void *,W *,unsigned),GetLastError(void),ResumeThread(void *),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) void *CreateFileW(const W *,unsigned,unsigned,void *,unsigned,unsigned,void *),*GetStdHandle(unsigned);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static W original[512],directory[512],child[512],exe[512],text[512],normalized[512];
static unsigned length(const W *s){unsigned n=0;while(s[n])n++;return n;}
static unsigned append(W *out,unsigned n,const W *part){while(*part)out[n++]=*part++;out[n]=0;return n;}
static int equal(const W *a,const W *b){while(*a==*b){if(!*a)return 1;a++;b++;}return 0;}
static void check(int ok,const char *why){if(!ok){unsigned n=0,done;while(why[n])n++;OutputDebugStringA(why);WriteFile(GetStdHandle((unsigned)-12),why,n,&done,0);ExitProcess(1);}}
void mainCRTStartup(void){
    unsigned n=GetTempPathW(470,directory);check(n && n<470,"cwd: actual temp path FAIL\n");append(directory,n,L"kyil-cwd-test");
    for(unsigned i=0;i<=length(directory);i++)child[i]=directory[i];append(child,length(child),L"\\child");
#ifdef CWD_CHILD
    check(GetCurrentDirectoryW(512,text) && equal(text,child),"cwd: transferred child directory identity FAIL\n");
    OutputDebugStringA("windows-cwd-child: inherited canonical directory and native anchor PASS\n");ExitProcess(0);
#else
    check(GetCurrentDirectoryW(512,original),"cwd: original snapshot FAIL\n");
    check(CreateDirectoryW(directory,0) || GetLastError()==183,"cwd: actual parent directory FAIL\n");
    check(CreateDirectoryW(child,0) || GetLastError()==183,"cwd: actual child directory FAIL\n");
    check(SetCurrentDirectoryW(directory) && SetCurrentDirectoryW(L"child\\..") && GetCurrentDirectoryW(512,text) && equal(text,directory),"cwd: canonical relative update FAIL\n");
    void *peb;__asm__ volatile("movq %%gs:0x60,%0":"=r"(peb));void *params=*(void **)((uint8_t *)peb+0x20);unicode *cwd=(void *)((uint8_t *)params+0x38);
    for(unsigned i=0;i<=length(directory);i++)normalized[i]=directory[i];append(normalized,length(normalized),L"\\");
    check(cwd->length==length(normalized)*2 && equal(cwd->buffer,normalized),"cwd: real PEB parameters updated FAIL\n");
    void *file=CreateFileW(L"child\\note.txt",0xc0000000u,7,0,1,0x80,0);
    if(file==(void *)-1 && GetLastError()==80)file=CreateFileW(L"child\\note.txt",0xc0000000u,7,0,5,0x80,0);
    unsigned done;check(file!=(void *)-1 && WriteFile(file,"cwd",3,&done,0) && done==3 && CloseHandle(file),"cwd: relative write resolves current directory FAIL\n");
    check(SetCurrentDirectoryW(L"child") && GetCurrentDirectoryW(512,text) && equal(text,child),"cwd: child navigation FAIL\n");
    file=CreateFileW(L"note.txt",0x80000000u,7,0,3,0x80,0);char bytes[4];
    check(file!=(void *)-1 && ReadFile(file,bytes,4,&done,0) && done==3 && bytes[0]=='c' && bytes[2]=='d' && CloseHandle(file) && DeleteFileW(L"note.txt"),"cwd: real relative read and delete FAIL\n");
    check(GetModuleFileNameW(0,exe,512),"cwd: actual executable directory FAIL\n");n=length(exe);while(n && exe[n-1]!='\\' && exe[n-1]!='/')n--;append(exe,n,L"windows-cwd-child.exe");
    startup si={.size=sizeof(si)};process_info pi;
    check(CreateProcessW(exe,0,0,0,0,4,0,0,&si,&pi) && pi.pid,"cwd: native capability launch FAIL\n");
    check(SetCurrentDirectoryW(original),"cwd: parent directory restore FAIL\n");
#ifdef KYIL_NATIVE_TEST
    /* Native root capabilities deliberately keep the child anchor alive from
       creation onward, including before its primary thread first runs.
       Windows does not promise that stronger pre-start pinning property. */
    check(!RemoveDirectoryW(child),"cwd: child retains anchor after parent leaves FAIL\n");
#endif
    check(ResumeThread(pi.thread)==1 && !WaitForSingleObject(pi.process,10000),"cwd: real child execution FAIL\n");unsigned status=99;
    check(GetExitCodeProcess(pi.process,&status) && !status && CloseHandle(pi.thread) && CloseHandle(pi.process),"cwd: actual child result FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(SetCurrentDirectoryW(L"A:\\") && GetCurrentDirectoryW(512,text) && equal(text,L"A:\\"),"cwd: package drive cwd FAIL\n");
    check(GetFullPathNameW(L"C:marker.txt",512,normalized,0),"cwd: remembered mapped-drive cwd FAIL\n");
    for(unsigned i=0;i<=length(original);i++)text[i]=original[i];append(text,length(text),L"\\marker.txt");check(equal(text,normalized),"cwd: drive-relative coordinates FAIL\n");
    check(CreateFileW(L"not-permitted.txt",0x40000000u,7,0,1,0x80,0)==(void *)-1 && GetLastError()==5,"cwd: readonly package mapping preserved FAIL\n");
#endif
    check(SetCurrentDirectoryW(original) && RemoveDirectoryW(child) && RemoveDirectoryW(directory),"cwd: exact anchor and directory cleanup FAIL\n");
    OutputDebugStringA("windows-cwd: canonical relative paths / PEB / real file IO / mapped drives / child anchor transfer / independent lifetime / cleanup PASS\n");ExitProcess(0);
#endif
}
