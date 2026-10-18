#include <stdint.h>
typedef uint16_t W;
typedef struct {uint16_t length,maximum;unsigned pad;W *buffer;} unicode;
typedef struct {unsigned size,pad;void *root;unicode *name;unsigned flags,pad2;void *security,*qos;} attributes;
typedef struct {unsigned size,pad;W *reserved,*desktop,*title;unsigned x,y,w,h,cx,cy,fill,flags;uint16_t show,extra;void *bytes,*input,*output,*error;} startup;
typedef struct {void *process,*thread;unsigned pid,tid;} process_info;
__declspec(dllimport) uint32_t WerRegisterRuntimeExceptionModule(const W *,void *),WerUnregisterRuntimeExceptionModule(const W *,void *),NtCreateKey(void **,unsigned,attributes *,unsigned,unicode *,unsigned,unsigned *),NtSetValueKey(void *,unicode *,unsigned,unsigned,const void *,unsigned),NtClose(void *);
__declspec(dllimport) void RtlInitUnicodeString(unicode *,const W *),RaiseException(unsigned,unsigned,unsigned,const uintptr_t *),OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) int CreateProcessW(const W *,W *,void *,void *,int,unsigned,void *,const W *,startup *,process_info *),GetExitCodeProcess(void *,unsigned *),CloseHandle(void *),DeleteFileW(const W *),ReadFile(void *,void *,unsigned,unsigned *,void *);
__declspec(dllimport) void *CreateFileW(const W *,unsigned,unsigned,void *,unsigned,unsigned,void *);
__declspec(dllimport) unsigned GetTempPathW(unsigned,W *),WaitForSingleObject(void *,unsigned);
static void check(int yes,const char *why){if(!yes){OutputDebugStringA(why);ExitProcess(1);}}
static const W *provider=(const W *)L"A:\\kyil-wer-provider.dll";
static void copy(W *d,const W *s){while((*d++=*s++));}
void mainCRTStartup(void) {
#ifdef WER_CHILD
    check(!WerRegisterRuntimeExceptionModule(provider,(void *)0x12345678),"wer: deferred provider registration FAIL\n");
    RaiseException(0xe0424242u,0,0,0);ExitProcess(99);
#else
    W missing[]={'C',':','\\','n','o','-','0','.','d','l','l',0};
    for(unsigned i=0;i<16;i++){missing[6]=(W)('A'+i);check(!WerRegisterRuntimeExceptionModule(missing,0),"wer: actual deferred registration FAIL\n");}
    check(WerRegisterRuntimeExceptionModule((const W *)L"C:\\one-too-many.dll",0)==0x8007007au,"wer: actual module bound FAIL\n");
    for(unsigned i=0;i<16;i++){missing[6]=(W)('A'+i);check(!WerUnregisterRuntimeExceptionModule(missing,0) && WerUnregisterRuntimeExceptionModule(missing,0)==0x80070002u,"wer: exact unregister FAIL\n");}
    static W path[512];unsigned n=GetTempPathW(512,path);check(n && n<450,"wer: report directory FAIL\n");copy(path+n,(const W *)L"wer-provider-executed.bin");DeleteFileW(path);
    const W *parts[]={(const W *)L"\\Registry\\Machine",(const W *)L"Software",(const W *)L"Microsoft",(const W *)L"Windows",(const W *)L"Windows Error Reporting",(const W *)L"RuntimeExceptionHelperModules"};
    void *key=0;
    for(unsigned i=0;i<6;i++){unicode name;RtlInitUnicodeString(&name,parts[i]);attributes a={48,0,key,&name,0x40,0,0,0};void *next=0;unsigned disposition;
        check(!NtCreateKey(&next,0xf003fu,&a,0,0,0,&disposition),"wer: prefix-scoped policy creation FAIL\n");if(key)NtClose(key);key=next;}
    unicode value;RtlInitUnicodeString(&value,provider);unsigned ignored_contents=0;
    check(!NtSetValueKey(key,&value,0,4,&ignored_contents,4) && !NtClose(key),"wer: real module authorization FAIL\n");
    startup start={.size=sizeof(start)};process_info child;
    check(CreateProcessW((const W *)L"A:\\windows-wer-child.exe",0,0,0,0,0,0,0,&start,&child) && !WaitForSingleObject(child.process,15000),"wer: actual crashing child / host completion FAIL\n");
    unsigned status;check(GetExitCodeProcess(child.process,&status) && status==0xe0424242u,"wer: original fatal exception retained FAIL\n");
    void *file=CreateFileW(path,0x80000000u,7,0,3,0x80,0);check(file!=(void *)-1,"wer: callback side effect missing FAIL\n");
    struct {unsigned source,helper,tid,code;uint64_t rip;unsigned phase,pad;} record;unsigned bytes;
    check(ReadFile(file,&record,sizeof(record),&bytes,0) && bytes==sizeof(record) && record.source==child.pid && record.helper!=record.source && record.tid==child.tid && record.code==0xe0424242u && record.rip && record.phase==3,"wer: real process / exact context / three callbacks FAIL\n");
    check(CloseHandle(file) && CloseHandle(child.process) && CloseHandle(child.thread) && DeleteFileW(path),"wer: resource retirement FAIL\n");
    OutputDebugStringA("windows-wer: deferred registration / 16 modules / unregister / real prefix policy / separate PE DLL host / source capability / captured thread context / three callbacks PASS\n");ExitProcess(0);
#endif
}
