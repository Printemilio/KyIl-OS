typedef unsigned long long u64;
typedef struct {unsigned code,flags;void *nested,*address;unsigned count,pad;u64 args[15];} record;
typedef struct {record *exception;unsigned char *context;} pointers;
__declspec(dllimport) void *AddVectoredExceptionHandler(unsigned,int (*)(pointers *));
__declspec(dllimport) unsigned RemoveVectoredExceptionHandler(void *);
__declspec(dllimport) void OutputDebugStringA(const char *);
__declspec(dllimport) void ExitProcess(unsigned);
extern char fault_ip[],resume_ip[];
extern u64 trigger_fault(void);
static unsigned calls;
static int handler(pointers *p) {
    if(p->exception->code!=0xc0000005u || p->exception->count!=2 || p->exception->args[1]!=0 ||
        *(u64 *)(p->context+0xf8)!=(u64)fault_ip)return 0;
    calls++;*(u64 *)(p->context+0xf8)=(u64)resume_ip;*(u64 *)(p->context+0x78)=42;
    return -1;
}
void mainCRTStartup(void) {
    void *h=AddVectoredExceptionHandler(1,handler);
    if(!h || trigger_fault()!=42 || calls!=1 || !RemoveVectoredExceptionHandler(h) || RemoveVectoredExceptionHandler(h)) {
        OutputDebugStringA("windows-veh: FAIL\n");ExitProcess(1);
    }
    OutputDebugStringA("windows-veh: real AV / si-address / modified CONTEXT / resume / stale handler PASS\n");ExitProcess(0);
}
