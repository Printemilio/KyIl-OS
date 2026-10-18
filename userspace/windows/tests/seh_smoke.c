#include <stdint.h>
typedef struct {unsigned code,flags;void *nested,*address;unsigned count,pad;uint64_t args[15];} record;
typedef struct {record *record;void *context;} pointers;
__declspec(dllimport) void RaiseException(unsigned,unsigned,unsigned,const uint64_t *),OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) void *AddVectoredExceptionHandler(unsigned,int (*)(pointers *)),*SetUnhandledExceptionFilter(int (*)(pointers *));
__declspec(dllimport) unsigned RemoveVectoredExceptionHandler(void *);
unsigned long _exception_code(void);
void *_exception_info(void);
static volatile unsigned veh,cleanups,caught,filters,top;
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static int vehicle(pointers *p){if(p->record->code==0xc0000005u){RaiseException(0x88776655u,0,0,0);return 0;}
    if(p->record->code!=0x88776655u)return 0;veh++;return -1;}
static int last(pointers *p){if(p->record->code!=0x81234567u)return 0;top++;return -1;}
__declspec(noinline) static void inner(void) {
    __try {uint64_t args[2]={12,34};RaiseException(0x88776656u,0,2,args);}
    __finally {cleanups++;}
}
static int select(pointers *p) {filters++;check(p->record->code==0x88776656u && p->record->count==2 && p->record->args[0]==12 && p->record->args[1]==34,"seh: filter record FAIL\n");return 1;}
__declspec(noinline) static void outer(void) {
    volatile unsigned preserved=1234;
    __try {inner();check(0,"seh: returned across exception FAIL\n");}
    __except(select((pointers *)_exception_info())) {caught++;}
    check(preserved==1234,"seh: frame/local restoration FAIL\n");
}
__declspec(noinline) static void hardware(void) {
    __try {*(volatile unsigned *)(uintptr_t)0=1;}
    __except(_exception_code()==0xc0000005u?1:0) {caught++;}
}
void mainCRTStartup(void) {
    void *h=AddVectoredExceptionHandler(1,vehicle);check(h!=0,"seh: VEH allocation FAIL\n");
    RaiseException(0x88776655u,0,0,0);check(veh==1 && RemoveVectoredExceptionHandler(h),"seh: software continue/resume FAIL\n");
    check(!SetUnhandledExceptionFilter(last),"seh: first filter FAIL\n");RaiseException(0x81234567u,0,0,0);
    check(top==1 && SetUnhandledExceptionFilter(0)==(void *)last,"seh: top filter/resume FAIL\n");
    h=AddVectoredExceptionHandler(1,vehicle);check(h!=0,"seh: nested VEH allocation FAIL\n");
    for(unsigned i=0;i<64;i++){outer();hardware();}
    check(veh==65 && RemoveVectoredExceptionHandler(h),"seh: software exception inside native fault handler FAIL\n");
    check(cleanups==64 && filters==64 && caught==128,"seh: unwind/cleanup totals FAIL\n");
    OutputDebugStringA("windows-seh: software VEH / top filter / compiler SEH / finally / hardware AV / repeated event abandonment PASS\n");ExitProcess(0);
}
