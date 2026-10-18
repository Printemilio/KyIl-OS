typedef unsigned long long usize;
__declspec(dllimport) void *GetStdHandle(unsigned);
__declspec(dllimport) int WriteFile(void *,const void *,unsigned,unsigned *,void *);
__declspec(dllimport) void ExitProcess(unsigned);
__declspec(dllimport) unsigned GetLastError(void);
__declspec(dllimport) void SetLastError(unsigned);
__declspec(dllimport) void OutputDebugStringA(const char *);
__declspec(dllimport) void *VirtualAlloc(void *,usize,unsigned,unsigned);
__declspec(dllimport) int VirtualProtect(void *,usize,unsigned,unsigned *);
__declspec(dllimport) int VirtualFree(void *,usize,unsigned);
__declspec(dllimport) unsigned SumSix(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
static void check(int ok,const char *s) {
    OutputDebugStringA(s);
    if(!ok) {OutputDebugStringA("windows-smoke: FAIL\n");ExitProcess(1);}
}
__attribute__((naked)) static long long raw_call(void) {
    /* Untagged 20 would be native GETPID if the PE wire domain were lost. */
    __asm__("mov $20, %eax; syscall; ret");
}
void mainCRTStartup(void) {
    unsigned written=0,old=0;
    const char line[]="PE -> userspace Windows runtime -> native KyIlOS\n";
    check(WriteFile(GetStdHandle((unsigned)-11),line,sizeof(line)-1,&written,0) && written==sizeof(line)-1,
        "windows-smoke: console checked\n");
    SetLastError(1234);check(GetLastError()==1234,"windows-smoke: error state checked\n");
    check(raw_call()==-1,"windows-smoke: untagged calls isolated checked\n");
    check(!WriteFile(GetStdHandle((unsigned)-11),(void *)0xffff800000000000ull,1,&written,0),
        "windows-smoke: kernel pointer rejected checked\n");
    check(SumSix(1,2,3,4,5,6)==121,"windows-smoke: DLL init / DIR64 / six arguments checked\n");
    unsigned char *p=VirtualAlloc(0,4096,0x3000,4);
    check(p!=0,"windows-smoke: native allocation checked\n");
    p[0]=0xb8;p[1]=42;p[2]=0;p[3]=0;p[4]=0;p[5]=0xc3;
    check(VirtualProtect(p,4096,0x20,&old) && old==4,"windows-smoke: RW -> RX checked\n");
    check(((unsigned (*)(void))p)()==42,"windows-smoke: real RX execution checked\n");
    check(!VirtualProtect(p,4096,0x40,&old) && GetLastError()==5,"windows-smoke: RWX denied checked\n");
    check(VirtualFree(p,0,0x8000),"windows-smoke: release checked\n");
    check(!VirtualFree(p,0,0x8000),"windows-smoke: stale release rejected checked\n");
    OutputDebugStringA("windows-smoke: PASS\n");ExitProcess(0);
}
