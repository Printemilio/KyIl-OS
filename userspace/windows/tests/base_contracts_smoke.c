#include <stdint.h>
__declspec(dllimport) void *GetModuleHandleW(const uint16_t *),*GetProcAddress(void *,const char *),*LoadLibraryExW(const uint16_t *,void *,unsigned);
__declspec(dllimport) unsigned GetLastError(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
#define GET(result,name,args) result (*name) args=GetProcAddress(k,#name);check(name!=0,"base-contracts: missing " #name " FAIL\n")
void mainCRTStartup(void){void *k=GetModuleHandleW(L"kernel32.dll");
#ifndef HOST_REFERENCE
    check(!GetProcAddress(k,(void *)1) && GetLastError()==127,"base-contracts: unmodeled provider ordinal must not select an arbitrary function FAIL\n");
#endif
    GET(const uint16_t *,GetCommandLineW,(void));
    const uint16_t *line=GetCommandLineW();const uint16_t marker[]=L" --probe-child";
    for(unsigned i=0;line[i];i++){unsigned n=0;while(marker[n] && line[i+n]==marker[n])n++;if(!marker[n] && !line[i+n])ExitProcess(17);}
    GET(int,SetEnvironmentVariableA,(const char *,const char *));GET(unsigned,GetEnvironmentVariableA,(const char *,char *,unsigned));
    GET(int,CompareStringOrdinal,(const uint16_t *,int,const uint16_t *,int,int));
    GET(unsigned,LocaleNameToLCID,(const uint16_t *,unsigned));GET(int,LCIDToLocaleName,(unsigned,uint16_t *,int,unsigned));
    GET(unsigned,GetSystemDirectoryA,(char *,unsigned));GET(unsigned,GetTempPathA,(unsigned,char *));
    GET(int,GetModuleHandleExA,(unsigned,const char *,void **));GET(unsigned,GetActiveProcessorCount,(unsigned));
    GET(int,WaitForSingleObjectEx,(void *,unsigned,int));GET(int,lstrlenW,(const uint16_t *));
    GET(unsigned,GetModuleFileNameA,(void *,char *,unsigned));
    GET(int,CreateProcessA,(const char *,char *,void *,void *,int,unsigned,void *,const char *,void *,void *));
    GET(int,GetExitCodeProcess,(void *,unsigned *));GET(int,CloseHandle,(void *));
    char text[1024];check(SetEnvironmentVariableA("KSW_BATCH","caf\xc3\xa9"),"base-contracts: environment set FAIL\n");
    check(GetEnvironmentVariableA("KSW_BATCH",0,0)==6 && GetEnvironmentVariableA("KSW_BATCH",text,2)==6 &&
        GetEnvironmentVariableA("KSW_BATCH",text,sizeof(text))==5 && text[3]==(char)0xc3 && text[4]==(char)0xa9 && !text[5],"base-contracts: ACP byte counts FAIL\n");
    check(SetEnvironmentVariableA("KSW_BATCH",0) && !GetEnvironmentVariableA("KSW_BATCH",text,sizeof(text)) && GetLastError()==203,"base-contracts: environment missing FAIL\n");
    check(CompareStringOrdinal(L"\u00e9",1,L"\u00c9",1,1)==2 && CompareStringOrdinal(L"a",1,L"A",1,0)==3 &&
        CompareStringOrdinal(L"abc",0,L"def",0,0)==2,"base-contracts: ordinal comparison FAIL\n");
    check(LocaleNameToLCID(L"en-US",0)==0x409 && LCIDToLocaleName(0x409,0,0,0)==6 &&
        LCIDToLocaleName(0x409,(uint16_t *)text,2,0)==0 && GetLastError()==122,"base-contracts: locale names and sizing FAIL\n");
    check(GetSystemDirectoryA(0,0)>1 && GetSystemDirectoryA(text,sizeof(text))>1 && GetTempPathA(sizeof(text),text)>1,"base-contracts: real namespace directories FAIL\n");
    void *module=0;check(GetModuleHandleExA(2,"kernel32.dll",&module) && module==k,"base-contracts: module identity FAIL\n");
    check(GetActiveProcessorCount(0)>0 && GetActiveProcessorCount(0xffff)>0 && !GetActiveProcessorCount(42) && GetLastError()==87,"base-contracts: processor facts FAIL\n");
    check(lstrlenW(L"abc")==3 && lstrlenW(0)==0,"base-contracts: string lengths FAIL\n");
    check((unsigned)WaitForSingleObjectEx((void *)0x1234,0,0)==0xffffffffu,"base-contracts: invalid wait FAIL\n");
    /* CreateProcessA has a native frame larger than a guard page. Exercise
       its real stack probes and bootstrap, not a larger precommitted stack. */
    char image[1024],command[1100];unsigned size=GetModuleFileNameA(0,image,sizeof(image));
    check(size && size<sizeof(image),"base-contracts: own image FAIL\n");command[0]='"';
    for(unsigned i=0;i<size;i++)command[i+1]=image[i];command[size+1]='"';
    const char suffix[]=" --probe-child";for(unsigned i=0;i<sizeof(suffix);i++)command[size+2+i]=suffix[i];
    struct {unsigned size,pad;char *reserved,*desktop,*title;unsigned x,y,w,h,cx,cy,fill,flags;uint16_t show,extra;void *bytes,*input,*output,*error;} startup={0};
    struct {void *process,*thread;unsigned pid,tid;} child;startup.size=sizeof(startup);
    check(CreateProcessA(image,command,0,0,0,0,0,0,&startup,&child),"base-contracts: actual ANSI spawn with guarded native stack FAIL\n");
    unsigned status=0;check(!WaitForSingleObjectEx(child.process,30000,0) && GetExitCodeProcess(child.process,&status) && status==17 && CloseHandle(child.thread) && CloseHandle(child.process),"base-contracts: actual ANSI child result FAIL\n");
    void *m=LoadLibraryExW(L"winmm.dll",0,0x800);check(m!=0,"base-contracts: WinMM provider FAIL\n");
    unsigned (*count)(void)=GetProcAddress(m,"midiInGetNumDevs");
    unsigned (*open)(void **,unsigned,uintptr_t,uintptr_t,unsigned)=GetProcAddress(m,"midiInOpen");
    unsigned (*close)(void *)=GetProcAddress(m,"midiInClose");void *h=(void *)1;
    check(count && open && close && open(&h,count()+1,0,0,0)==2 && !h && close((void *)0x1234)==5,"base-contracts: absent MIDI device and invalid handle FAIL\n");
    OutputDebugStringA("windows-base-contracts: ACP bytes / locale / module / machine facts / invalid wait / absent device PASS\n");ExitProcess(0);
}
