#include <stdint.h>
typedef uint16_t W;
__declspec(dllimport) void *LoadLibraryExW(const W *,void *,unsigned),*GetProcAddress(void *,const char *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *),*GetStdHandle(unsigned);
__declspec(dllimport) int FreeLibrary(void *),CloseHandle(void *),WriteFile(void *,const void *,unsigned,unsigned *,void *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetCurrentProcessId(void),GetLastError(void);
__declspec(dllimport) void SetLastError(unsigned),OutputDebugStringA(const char *),ExitProcess(unsigned);
#define WINAPI __attribute__((ms_abi))
static unsigned (WINAPI *open_key)(void *,const W *,unsigned,unsigned,void **), (WINAPI *create_key)(void *,const W *,unsigned,W *,unsigned,unsigned,void *,void **,unsigned *);
static unsigned (WINAPI *set_value)(void *,const W *,unsigned,unsigned,const void *,unsigned), (WINAPI *query_value)(void *,const W *,unsigned *,unsigned *,void *,unsigned *);
static unsigned (WINAPI *delete_value)(void *,const W *), (WINAPI *delete_key)(void *,const W *), (WINAPI *close_key)(void *);
static void *key;
static void check(int ok,const char *why){if(!ok){unsigned n=0,done;while(why[n])n++;OutputDebugStringA(why);WriteFile(GetStdHandle((unsigned)-12),why,n,&done,0);ExitProcess(1);}}
static void value_name(W *name,unsigned id){const W *prefix=L"producer-";unsigned n=0;while(prefix[n]){name[n]=prefix[n];n++;}name[n++]=(W)('0'+id/100);name[n++]=(W)('0'+id/10%10);name[n++]=(W)('0'+id%10);name[n]=0;}
static unsigned worker(void *argument){unsigned slot=(unsigned)(uintptr_t)argument;
    for(unsigned i=0;i<8;i++){unsigned value=slot*8+i;W name[32];value_name(name,value);check(!set_value(key,name,0,4,&value,4),"registry-win32: concurrent value publication FAIL\n");}return 0;
}
void mainCRTStartup(void){
    void *library=LoadLibraryExW(L"advapi32",0,0x800);check(library!=0,"registry-win32: actual provider FAIL\n");
#define RESOLVE(target,symbol) check((target=GetProcAddress(library,symbol))!=0,"registry-win32: export " symbol " FAIL\n")
    RESOLVE(open_key,"RegOpenKeyExW");RESOLVE(create_key,"RegCreateKeyExW");RESOLVE(set_value,"RegSetValueExW");RESOLVE(query_value,"RegQueryValueExW");
    RESOLVE(delete_value,"RegDeleteValueW");RESOLVE(delete_key,"RegDeleteKeyW");RESOLVE(close_key,"RegCloseKey");
#undef RESOLVE
    W path[128];unsigned n=0;const W *prefix=L"Software\\KyIlOS-CompatReference-";while(prefix[n]){path[n]=prefix[n];n++;}
    unsigned pid=GetCurrentProcessId();W digits[12];unsigned count=0;do{digits[count++]=(W)('0'+pid%10);pid/=10;}while(pid);while(count)path[n++]=digits[--count];path[n]=0;
    void *self=(void *)(uintptr_t)0xffffffff80000001ull;unsigned disposition=0;
    check(!create_key(self,path,0,0,0,0xf003f,0,&key,&disposition) && key && (disposition==1 || disposition==2),"registry-win32: recursive actual creation FAIL\n");
    unsigned value=0x1234abcd,bytes=4,type=0,got=0;check(!set_value(key,L"payload",0,4,&value,4) && !query_value(key,L"payload",0,&type,&got,&bytes) && type==4 && bytes==4 && got==value,"registry-win32: DWORD roundtrip FAIL\n");
    bytes=0;check(!query_value(key,L"payload",0,&type,0,&bytes) && bytes==4 && type==4,"registry-win32: size query FAIL\n");
    bytes=1;check(query_value(key,L"payload",0,&type,&got,&bytes)==234 && bytes==4,"registry-win32: partial buffer required size FAIL\n");
    const W text[]=L"KyIlOS \u00e9";W output[32];check(!set_value(key,0,0,1,text,sizeof(text)),"registry-win32: unnamed UTF16 value FAIL\n");
    bytes=sizeof(output);check(!query_value(key,0,0,&type,output,&bytes) && type==1 && bytes==sizeof(text),"registry-win32: UTF16 sized value FAIL\n");for(unsigned i=0;i<sizeof(text)/2;i++)check(output[i]==text[i],"registry-win32: exact UTF16 bytes FAIL\n");
    void *read=0;check(!open_key(key,L"",0,1,&read) && read && set_value(read,L"denied",0,4,&value,4)==5 && !close_key(read),"registry-win32: rights attenuation FAIL\n");
    SetLastError(0x12345678);bytes=4;check(query_value(key,L"actually-missing",0,&type,&got,&bytes)==2 && GetLastError()==0x12345678,"registry-win32: LSTATUS and LastError independence FAIL\n");
    void *threads[4];for(unsigned i=0;i<4;i++){threads[i]=CreateThread(0,0,worker,(void *)(uintptr_t)i,0,0);check(threads[i]!=0,"registry-win32: real writers FAIL\n");}
    for(unsigned i=0;i<4;i++)check(!WaitForSingleObject(threads[i],10000) && CloseHandle(threads[i]),"registry-win32: writer completion FAIL\n");
    for(unsigned i=0;i<32;i++){W name[32];value_name(name,i);bytes=4;check(!query_value(key,name,0,&type,&got,&bytes) && got==i && bytes==4 && !delete_value(key,name),"registry-win32: no lost update and actual deletion FAIL\n");}
    check(!delete_value(key,L"payload") && !delete_value(key,0),"registry-win32: real value removal FAIL\n");bytes=4;check(query_value(key,L"payload",0,&type,&got,&bytes)==2,"registry-win32: deletion observable FAIL\n");
    void *retired=0;check(!create_key(key,L"retired",0,0,0,0xf003f,0,&retired,0) && !set_value(retired,L"value",0,4,&value,4) && !delete_key(key,L"retired"),"registry-win32: pending key deletion FAIL\n");bytes=4;
    check(query_value(retired,L"value",0,&type,&got,&bytes)==1018 && !close_key(retired),"registry-win32: deleted identity remains distinct FAIL\n");
#ifdef KYIL_NATIVE_TEST
    void *view=0;check(!open_key(self,path,0,1|0x100,&view) && !close_key(view) && open_key(self,path,0,1|0x200,&view)==50,"registry-win32: actual AMD64 view and absent WOW64 honest FAIL\n");
    bytes=4;check(query_value((void *)0x1234,L"v",0,0,&got,&bytes)==6 && close_key((void *)0x1234)==6,"registry-win32: invalid capabilities FAIL\n");
    check(!set_value(key,L"boundary",0,4,&value,4),"registry-win32: boundary fixture FAIL\n");bytes=4;
    check(query_value(key,L"boundary",0,0,(void *)0xffff800000000000ull,&bytes)==998 && !delete_value(key,L"boundary"),"registry-win32: kernel output refused FAIL\n");
#endif
    check(!close_key(key) && !delete_key(self,path) && open_key(self,path,0,1,&key)==2 && FreeLibrary(library),"registry-win32: final object cleanup FAIL\n");
    OutputDebugStringA("windows-registry-win32: actual per-user hive / recursive keys / DWORD and UTF16 / sizes / rights / LastError / four writers / delete / lifetime PASS\n");ExitProcess(0);
}
