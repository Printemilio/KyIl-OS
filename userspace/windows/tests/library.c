static unsigned initialized;
static unsigned *volatile relocated_pointer=&initialized;
__declspec(dllimport) void OutputDebugStringA(const char *);
__declspec(dllexport) unsigned SumSix(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f) {
    return *relocated_pointer+a+b+c+d+e+f;
}
int DllMain(void *instance,unsigned reason,void *reserved) {
    (void)instance;(void)reserved;
    if(reason==1) {initialized=100;OutputDebugStringA("windows-dll: PROCESS_ATTACH\n");}
    if(reason==0) {initialized=0;OutputDebugStringA("windows-dll: PROCESS_DETACH\n");}
    return 1;
}
