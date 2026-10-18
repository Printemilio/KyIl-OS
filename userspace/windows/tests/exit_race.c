__declspec(dllimport) void *CreateThread(void *,unsigned long long,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) void *CreateEventW(void *,int,int,const unsigned short *);
__declspec(dllimport) int SetEvent(void *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),SumSix(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
__declspec(dllimport) void ExitProcess(unsigned),OutputDebugStringA(const char *);
static void *go;
static unsigned worker(void *unused) {(void)unused;if(WaitForSingleObject(go,0xffffffffu))ExitProcess(1);ExitProcess(17);return 0;}
void mainCRTStartup(void) {
    if(SumSix(1,2,3,4,5,6)!=121)ExitProcess(1);
    go=CreateEventW(0,1,0,0);void *park=CreateEventW(0,1,0,0);if(!go || !park)ExitProcess(1);
    for(unsigned i=0;i<8;i++)if(!CreateThread(0,0,worker,0,0,0))ExitProcess(1);
    OutputDebugStringA("windows-exit-race: eight threads requesting process exit\n");
    if(!SetEvent(go))ExitProcess(1);
    /* The first worker stops the main thread too, then executes DLL detach. */
    for(;;)WaitForSingleObject(park,0xffffffffu);
}
