#include <stdint.h>
__declspec(dllimport) void *CreateNamedPipeW(const uint16_t *,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,void *);
__declspec(dllimport) void *CreateFileW(const uint16_t *,unsigned,unsigned,void *,unsigned,unsigned,void *);
__declspec(dllimport) int ConnectNamedPipe(void *,void *),DisconnectNamedPipe(void *),WaitNamedPipeW(const uint16_t *,unsigned);
__declspec(dllimport) int SetNamedPipeHandleState(void *,const unsigned *,const unsigned *,const unsigned *);
__declspec(dllimport) int ReadFile(void *,void *,unsigned,unsigned *,void *),WriteFile(void *,const void *,unsigned,unsigned *,void *),CloseHandle(void *);
__declspec(dllimport) unsigned GetLastError(void),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) int GetExitCodeThread(void *,unsigned *);
__declspec(dllimport) void *CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) void Sleep(unsigned),OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) int PeekNamedPipe(void *,void *,unsigned,unsigned *,unsigned *,unsigned *);
__declspec(dllimport) int TransactNamedPipe(void *,void *,unsigned,void *,unsigned,unsigned *,void *);
static const uint16_t *name=L"\\\\.\\pipe\\kyil-contract";
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static unsigned reader(void *h) {char data[1];unsigned n;int r=ReadFile(h,data,1,&n,0);return !r && (GetLastError()==109 || GetLastError()==6)?0:1;}
void mainCRTStartup(void) {
#ifdef PIPE_CLIENT
    unsigned waited=0;while(!WaitNamedPipeW(name,5000)) {check(GetLastError()==2 && waited++<5000,"pipe-client: discovery FAIL\n");Sleep(1);}
    for(unsigned cycle=0;cycle<2;cycle++) {
        void *h=(void *)-1;
        while(h==(void *)-1){h=CreateFileW(L"\\\\.\\PIPE\\KYIL-CONTRACT",0xc0000000u,0,0,3,0,0);
            if(h==(void *)-1){check(GetLastError()==231,"pipe-client: real open FAIL\n");check(WaitNamedPipeW(name,5000),"pipe-client: availability FAIL\n");}}
        unsigned mode=2;check(SetNamedPipeHandleState(h,&mode,0,0),"pipe-client: message mode FAIL\n");
        char payload[]="hello-world",bytes[12];unsigned n=0;
        check(TransactNamedPipe(h,payload,11,bytes,sizeof(bytes),&n,0) && n==5 && bytes[0]=='r' && bytes[4]=='y',"pipe-client: real transaction FAIL\n");
        check(CloseHandle(h),"pipe-client: close FAIL\n");
    }
    OutputDebugStringA("windows-pipe-client: separate process / name fold / duplex / reconnect / real replies PASS\n");
#else
    check(CreateNamedPipeW(L"\\\\.\\pipe\\not-supported-async",0x40000003u,6,1,4096,4096,0,0)==(void *)-1 && GetLastError()==50,"pipe-server: honest async refusal FAIL\n");
    void *h=CreateNamedPipeW(name,3|0x80000,6,2,4096,4096,0,0);check(h!=(void *)-1,"pipe-server: create FAIL\n");
    check(CreateNamedPipeW(name,3|0x80000,6,2,4096,4096,0,0)==(void *)-1 && GetLastError()==5,"pipe-server: exclusive refusal FAIL\n");
    for(unsigned cycle=0;cycle<2;cycle++) {
        int result=ConnectNamedPipe(h,0);check(result || GetLastError()==535,"pipe-server: connect FAIL\n");
        char bytes[12];unsigned n=0;
        check(!ReadFile(h,bytes,3,&n,0) && n==3 && GetLastError()==234 && bytes[0]=='h',"pipe-server: partial message FAIL\n");
        unsigned peeked,total,left;char peek[2];
        check(PeekNamedPipe(h,peek,2,&peeked,&total,&left) && peeked==2 && total==8 && left==6 && peek[0]=='l' && peek[1]=='o',"pipe-server: nondestructive pending observation FAIL\n");
        check(ReadFile(h,bytes+3,8,&n,0) && n==8,"pipe-server: retained remainder FAIL\n");
        const char *expected="hello-world";for(unsigned i=0;i<11;i++)check(bytes[i]==expected[i],"pipe-server: exact bytes FAIL\n");
        check(WriteFile(h,"reply",5,&n,0) && n==5,"pipe-server: duplex reply FAIL\n");
        if(!cycle)check(DisconnectNamedPipe(h),"pipe-server: rearm FAIL\n");
    }
    check(CloseHandle(h) && !CloseHandle(h) && GetLastError()==6,"pipe-server: stale FAIL\n");
    /* Pending receiver is canceled by closing its public endpoint. */
    void *server=CreateNamedPipeW(L"\\\\.\\pipe\\close-wake",3,0,1,0,0,0,0);
    void *client=CreateFileW(L"\\\\.\\pipe\\close-wake",0xc0000000u,0,0,3,0,0);
    check(server!=(void *)-1 && client!=(void *)-1,"pipe-server: close pair FAIL\n");
    int connected=ConnectNamedPipe(server,0);check(connected || GetLastError()==535,"pipe-server: close connect FAIL\n");
    void *thread=CreateThread(0,0,reader,server,0,0);check(thread!=0,"pipe-server: close waiter FAIL\n");Sleep(20);
    unsigned exit_code=123;
    check(CloseHandle(server) && !WaitForSingleObject(thread,5000) && GetExitCodeThread(thread,&exit_code) && !exit_code && CloseHandle(thread) && CloseHandle(client),"pipe-server: actual close wake FAIL\n");
    OutputDebugStringA("windows-pipe-server: root-scoped rendezvous / real message remainder / duplex / reconnect / close-wake / stale PASS\n");
#endif
    ExitProcess(0);
}
