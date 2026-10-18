#include <stdint.h>
typedef struct {uintptr_t status,bytes;uint32_t offset,high;void *event;} overlap;
__declspec(dllimport) void *CreateFileW(const uint16_t *,unsigned,unsigned,void *,unsigned,unsigned,void *);
__declspec(dllimport) void *CreateIoCompletionPort(void *,void *,uintptr_t,unsigned);
__declspec(dllimport) int GetQueuedCompletionStatus(void *,unsigned *,uintptr_t *,void **,unsigned);
__declspec(dllimport) int ReadFile(void *,void *,unsigned,unsigned *,overlap *),WriteFile(void *,const void *,unsigned,unsigned *,overlap *);
__declspec(dllimport) int GetOverlappedResult(void *,overlap *,unsigned *,int),CancelIoEx(void *,overlap *),CloseHandle(void *),FlushFileBuffers(void *);
__declspec(dllimport) void *CreateEventW(void *,int,int,const uint16_t *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void),GetTempPathW(unsigned,uint16_t *),GetCurrentProcessId(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) void *GetStdHandle(unsigned);
static uint8_t data[65536],copy[65536];static overlap writes[16],reads[16];
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);unsigned n=0,written;while(why[n])n++;WriteFile(GetStdHandle((unsigned)-11),why,n,&written,0);ExitProcess(1);}}
static void pending(int result){check(result || GetLastError()==997,"overlap: real pending FAIL\n");}
void mainCRTStartup(void) {
    uint16_t path[512];unsigned n=GetTempPathW(512,path),pid=GetCurrentProcessId();check(n && n<440,"overlap: path FAIL\n");
    const uint16_t suffix[]=L"overlap-";unsigned a=0;while(suffix[a])path[n++]=suffix[a++];
    for(a=0;a<8;a++)path[n++]=L"0123456789abcdef"[(pid>>(28-a*4))&15];path[n]=0;
    void *file=CreateFileW(path,0xc0000000u,7,0,1,0x40000080u,0);
    if(file==(void *)-1 && GetLastError()==80)file=CreateFileW(path,0xc0000000u,7,0,5,0x40000080u,0);
    check(file!=(void *)-1,"overlap: create FAIL\n");
    void *port=CreateIoCompletionPort(file,0,0x1234567887654321ull,1);check(port!=0,"overlap: real association FAIL\n");
    check(!CreateIoCompletionPort(file,port,7,0) && GetLastError()==87,"overlap: immutable association FAIL\n");
    for(a=0;a<sizeof(data);a++)data[a]=(uint8_t)(a*37u+19u);
    void *events[16];for(a=0;a<16;a++) {
        events[a]=CreateEventW(0,1,0,0);check(events[a] && !((uintptr_t)events[a]&3),"overlap: aligned event handles FAIL\n");
        writes[a].offset=a*4096;writes[a].event=(void *)((uintptr_t)events[a]|(a==15));
        pending(WriteFile(file,data+a*4096,4096,0,&writes[a]));
    }
    uint32_t seen=0;unsigned bytes;uintptr_t key;void *o;
    for(a=0;a<15;a++) {
        check(GetQueuedCompletionStatus(port,&bytes,&key,&o,10000) && bytes==4096 && key==0x1234567887654321ull,"overlap: actual write completion FAIL\n");
        overlap *op=o;check(op>=writes && op<writes+15 && !op->status,"overlap: pointer/status FAIL\n");
        unsigned i=(unsigned)(op-writes);check(!(seen&(1u<<i)),"overlap: duplicate completion FAIL\n");seen|=1u<<i;
    }
    check(seen==0x7fff && GetOverlappedResult(file,&writes[15],&bytes,1) && bytes==4096,"overlap: suppressed IOCP event completion FAIL\n");
    check(!GetQueuedCompletionStatus(port,&bytes,&key,&o,0) && !o && GetLastError()==258,"overlap: low event bit suppression FAIL\n");
    check(FlushFileBuffers(file),"overlap: disk flush FAIL\n");
    for(a=0;a<16;a++){check(!WaitForSingleObject(events[a],10000) && CloseHandle(events[a]),"overlap: actual event signaling FAIL\n");reads[a].offset=a*4096;pending(ReadFile(file,copy+a*4096,4096,0,&reads[a]));}
    for(a=0;a<16;a++)check(GetQueuedCompletionStatus(port,&bytes,&key,&o,10000) && bytes==4096 && key==0x1234567887654321ull,"overlap: actual read completion FAIL\n");
    for(a=0;a<sizeof(data);a++)check(copy[a]==data[a],"overlap: persisted data FAIL\n");
    overlap eof={.offset=65536};pending(ReadFile(file,copy,1,0,&eof));
    check(!GetQueuedCompletionStatus(port,&bytes,&key,&o,10000) && o==&eof && !bytes && GetLastError()==38,"overlap: failed EOF packet FAIL\n");
    check(!GetOverlappedResult(file,&eof,&bytes,1) && GetLastError()==38,"overlap: EOF result FAIL\n");
    overlap bad={0};int accepted=ReadFile(file,(void *)0xffff800000000000ull,1,0,&bad);unsigned bad_error=GetLastError();
    check(!accepted && (bad_error==997 || bad_error==998),"overlap: kernel-buffer refusal FAIL\n");
    if(bad_error==997)
        check(!GetQueuedCompletionStatus(port,&bytes,&key,&o,10000) && o==&bad && GetLastError()==998,"overlap: kernel-buffer completion refusal FAIL\n");
    else
        check(!GetQueuedCompletionStatus(port,&bytes,&key,&o,0) && !o && GetLastError()==258,"overlap: rejected request has no packet FAIL\n");
    check(!CancelIoEx(file,&bad) && GetLastError()==1168,"overlap: already complete cancel FAIL\n");
    overlap reused={0};
    for(unsigned cycle=0;cycle<128;cycle++) {
        pending(ReadFile(file,copy,1,0,&reused));
        check(GetQueuedCompletionStatus(port,&bytes,&key,&o,10000) && o==&reused && bytes==1 && copy[0]==data[0],"overlap: immediate reuse after completion FAIL\n");
        check(!CancelIoEx(file,&reused) && GetLastError()==1168,"overlap: completed request still cancelable FAIL\n");
    }
    check(CloseHandle(file) && CloseHandle(port),"overlap: close FAIL\n");
    OutputDebugStringA("windows-overlapped: 65536 real bytes / 32 asynchronous IOs / wide IOCP keys / events / low-bit suppression / EOF-error packets / kernel buffer refused / flush / cleanup PASS\n");ExitProcess(0);
}
