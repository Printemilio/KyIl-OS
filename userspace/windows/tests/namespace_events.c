typedef unsigned long long u64;
typedef unsigned short W;
__declspec(dllimport) unsigned GetSystemDirectoryW(W *,unsigned);
__declspec(dllimport) unsigned GetWindowsDirectoryW(W *,unsigned);
__declspec(dllimport) unsigned GetTempPathW(unsigned,W *);
__declspec(dllimport) unsigned GetCurrentDirectoryW(unsigned,W *);
__declspec(dllimport) W *GetCommandLineW(void);
__declspec(dllimport) unsigned GetCurrentProcessId(void);
__declspec(dllimport) int CreateDirectoryW(const W *,void *);
__declspec(dllimport) int CompareStringW(unsigned,unsigned,const W *,int,const W *,int);
__declspec(dllimport) unsigned GetLastError(void);
__declspec(dllimport) void *CreateEventW(void *,int,int,const W *);
__declspec(dllimport) int SetEvent(void *),ResetEvent(void *),CloseHandle(void *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned);
__declspec(dllimport) void *CreateThread(void *,u64,unsigned (*)(void *),void *,unsigned,unsigned *);
__declspec(dllimport) void Sleep(unsigned),ExitProcess(unsigned),OutputDebugStringA(const char *);
__declspec(dllimport) void *CreateFileMappingW(void *,void *,unsigned,unsigned,unsigned,const W *);
__declspec(dllimport) void *MapViewOfFile(void *,unsigned,unsigned,unsigned,u64);
__declspec(dllimport) int UnmapViewOfFile(void *);
static void *manual,*automatic;static unsigned entered,completed,failed;
static void require(int ok,const char *why) {if(!ok) {OutputDebugStringA(why);ExitProcess(1);}}
static unsigned worker(void *unused) {
    (void)unused;__atomic_fetch_add(&entered,1,__ATOMIC_RELEASE);
    if(WaitForSingleObject(manual,5000))__atomic_fetch_add(&failed,1,__ATOMIC_RELAXED);
    __atomic_fetch_add(&completed,1,__ATOMIC_RELEASE);
    return 0;
}
static unsigned consumer(void *unused) {
    (void)unused;
    if(WaitForSingleObject(automatic,5000))__atomic_fetch_add(&failed,1,__ATOMIC_RELAXED);
    __atomic_fetch_add(&completed,1,__ATOMIC_RELEASE);return 0;
}
void mainCRTStartup(void) {
    W dir[512],small[2]={0x77,0x88},temp[512];
    require(GetSystemDirectoryW(dir,512)==19 && dir[0]=='C' && dir[18]=='2',"namespace: System32 FAIL\n");
    require(GetSystemDirectoryW(small,2)==20 && small[0]==0x77,"namespace: short buffer FAIL\n");
    require(GetWindowsDirectoryW(dir,512)==10,"namespace: Windows path FAIL\n");
    require(GetCurrentDirectoryW(512,dir)>9 && dir[0]=='C',"namespace: cwd FAIL\n");
    require(GetCommandLineW()[0]=='\"' && GetCommandLineW()[1]=='A',"namespace: command line mapping FAIL\n");
    unsigned n=GetTempPathW(512,temp);require(n && n<450,"namespace: temp FAIL\n");
    const W suffix[]=L"contract-test-";unsigned i=0;while(suffix[i])temp[n++]=suffix[i++];
    unsigned pid=GetCurrentProcessId();for(i=0;i<8;i++)temp[n++]=L"0123456789abcdef"[(pid>>(28-i*4))&15];temp[n]=0;
    int created=CreateDirectoryW(temp,0);require(created || GetLastError()==183,"namespace: real mkdir FAIL\n");
    require(!CreateDirectoryW(temp,0) && GetLastError()==183,"namespace: EEXIST translation FAIL\n");
    require(!CreateDirectoryW(L"C:\\..\\escape",0) && GetLastError()==5,"namespace: escape FAIL\n");
    require(!CreateDirectoryW(L"A:\\write-refused",0) && GetLastError()==5,"namespace: package rights FAIL\n");
    require(CompareStringW(0x7f,0,L"a",0,L"b",0)==2 && CompareStringW(0x409,0,L"a",-1,L"A",-1)==1 &&
        CompareStringW(0x7f,1,L"ab",-1,L"aB",-1)==2,"nls: reference comparisons FAIL\n");
    require(!CompareStringW(0x7f,0,L"é",-1,L"a",-1) && GetLastError()==50,"nls: unsupported weights FAIL\n");
    void *section=CreateFileMappingW((void *)-1,0,4,0,131072,0);
    require(section!=0,"section: native SHM create FAIL\n");
    unsigned char *rw=MapViewOfFile(section,2,0,0,131072),*ro=MapViewOfFile(section,4,0,65536,65536);
    require(rw && ro && !((u64)rw&65535) && !((u64)ro&65535),"section: views/alignment FAIL\n");
    rw[65536]=0x39;rw[131071]=0x57;
    require(ro[0]==0x39 && ro[65535]==0x57,"section: shared backing/offset FAIL\n");
    require(!MapViewOfFile(section,4,0,1,4096) && GetLastError()==87,"section: offset validation FAIL\n");
    require(!MapViewOfFile(section,32,0,0,0) && GetLastError()==5,"section: execute refusal FAIL\n");
    require(CloseHandle(section) && !MapViewOfFile(section,4,0,0,0) && GetLastError()==6,"section: stale handle FAIL\n");
    rw[65536]=0x66;require(ro[0]==0x66 && !UnmapViewOfFile(ro+1),"section: view lifetime FAIL\n");
    require(UnmapViewOfFile(rw) && ro[0]==0x66 && UnmapViewOfFile(ro) && !UnmapViewOfFile(ro),"section: close order FAIL\n");
    section=CreateFileMappingW((void *)-1,0,2,0,4096,0);
    require(section && !MapViewOfFile(section,2,0,0,0) && GetLastError()==5 && CloseHandle(section),"section: rights attenuation FAIL\n");
    require(!CreateFileMappingW((void *)-1,0,0x40,0,4096,0) && GetLastError()==5,"section: RWX refusal FAIL\n");
    manual=CreateEventW(0,1,0,0);automatic=CreateEventW(0,0,0,0);
    require(manual && automatic && WaitForSingleObject(manual,0)==258,"events: initial state FAIL\n");
    void *threads[4];for(i=0;i<4;i++) {threads[i]=CreateThread(0,0,worker,0,0,0);require(threads[i]!=0,"events: thread FAIL\n");}
    while(__atomic_load_n(&entered,__ATOMIC_ACQUIRE)!=4)Sleep(0);
    /* A real scheduling wait allows registration; functional stress follows
       with Set/Reset races and no timing dependence on a fixed sleep. */
    require(SetEvent(manual),"events: manual Set FAIL\n");
    for(i=0;i<4;i++)require(WaitForSingleObject(threads[i],5000)==0 && CloseHandle(threads[i]),"events: manual wake FAIL\n");
    require(completed==4 && !failed && WaitForSingleObject(manual,0)==0 && ResetEvent(manual) && WaitForSingleObject(manual,0)==258,"events: manual reset FAIL\n");
    require(SetEvent(automatic) && SetEvent(automatic) && WaitForSingleObject(automatic,0)==0 && WaitForSingleObject(automatic,0)==258,"events: auto consume FAIL\n");
    for(i=0;i<100;i++) {
        void *t=CreateThread(0,0,consumer,0,0,0);require(t!=0 && SetEvent(automatic),"events: registration race FAIL\n");
        require(WaitForSingleObject(t,5000)==0 && CloseHandle(t),"events: signal/wait race FAIL\n");
    }
    require(completed==104 && !failed,"events: results FAIL\n");
    void *old=automatic;require(CloseHandle(automatic) && !CloseHandle(old),"events: stale close FAIL\n");
    automatic=CreateEventW(0,0,1,0);
    require(automatic && automatic!=old && !SetEvent(old) && WaitForSingleObject(old,0)==0xffffffffu,"events: generation FAIL\n");
    require(!CreateEventW(0,0,0,L"named-not-implemented") && GetLastError()==50,"events: named refusal FAIL\n");
    require(CloseHandle(automatic) && CloseHandle(manual),"events: cleanup FAIL\n");
    OutputDebugStringA("windows-namespace-events: prefix / mkdir / NLS / SHM section-views / rights-lifetime / manual-auto / 100 races PASS\n");ExitProcess(0);
}
