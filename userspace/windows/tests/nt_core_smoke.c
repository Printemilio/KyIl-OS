#include <stdint.h>
typedef struct {uint16_t length,maximum;uint32_t pad;uint16_t *buffer;} unicode;
__declspec(dllimport) void RtlInitUnicodeString(unicode *,const uint16_t *),RtlFreeUnicodeString(unicode *);
__declspec(dllimport) uint32_t RtlInitUnicodeStringEx(unicode *,const uint16_t *),NtClose(void *);
__declspec(dllimport) void *CreateEventW(void *,int,int,const uint16_t *),*GetProcessHeap(void),*HeapAlloc(void *,unsigned,uint64_t);
__declspec(dllimport) uint64_t HeapSize(void *,unsigned,void *);
__declspec(dllimport) unsigned GetLastError(void),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) void SetLastError(unsigned),OutputDebugStringA(const char *),ExitProcess(unsigned);
static uint16_t large[32768];
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
void mainCRTStartup(void) {
    unicode string;const uint16_t text[]=L"hello";
    RtlInitUnicodeString(&string,text);check(string.length==10 && string.maximum==12 && string.buffer==text,"nt-core: borrowed Unicode byte lengths FAIL\n");
    RtlInitUnicodeString(&string,0);check(!string.length && !string.maximum && !string.buffer,"nt-core: NULL Unicode FAIL\n");
    for(unsigned i=0;i<32767;i++)large[i]='x';
    RtlInitUnicodeString(&string,large);check(string.length==65532 && string.maximum==65534 && string.buffer==large,"nt-core: actual Windows length cap FAIL\n");
    check(RtlInitUnicodeStringEx(&string,large)==0xc0000106u && !string.buffer && !string.length,"nt-core: long Unicode Ex error FAIL\n");
    uint16_t *owned=HeapAlloc(GetProcessHeap(),0,6);check(owned!=0,"nt-core: real allocation FAIL\n");
    owned[0]='o';owned[1]='k';owned[2]=0;
    check(!RtlInitUnicodeStringEx(&string,owned) && string.length==4 && string.maximum==6,"nt-core: Unicode Ex success FAIL\n");
    SetLastError(99);RtlFreeUnicodeString(&string);
    check(GetLastError()==99 && !string.buffer && !string.length && HeapSize(GetProcessHeap(),0,owned)==UINT64_MAX,"nt-core: owned string actually freed FAIL\n");
    void *event=CreateEventW(0,1,1,0);check(event!=0,"nt-core: real handle FAIL\n");
    SetLastError(99);check(!NtClose(event) && GetLastError()==99,"nt-core: NT close preserves Win32 error FAIL\n");
    check(NtClose(event)==0xc0000008u && GetLastError()==99 && WaitForSingleObject(event,0)==0xffffffffu,"nt-core: stale NTSTATUS and handle FAIL\n");
    OutputDebugStringA("windows-nt-core: UNICODE_STRING byte lengths / actual Windows bound / Ex errors / real owned free / typed NtClose / NTSTATUS / stale PASS\n");ExitProcess(0);
}
