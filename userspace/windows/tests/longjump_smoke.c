#include <stdint.h>
typedef struct {uint64_t home[6];unsigned flags,mxcsr;uint16_t segments[6];unsigned eflags;uint64_t debug[6];
 uint64_t rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15,rip;unsigned char fp[512],tail[0x1d0];} __declspec(align(16)) context;
typedef struct {uint64_t frame,rbx,rsp,rbp,rsi,rdi,r12,r13,r14,r15,rip;unsigned mxcsr;unsigned short fpcsr,spare;unsigned char xmm[10][16];} jump_buffer;
typedef struct {unsigned code,flags;void *nested,*address;unsigned count,pad;uint64_t args[15];} record;
__declspec(dllimport) void RtlCaptureContext(context *),RtlRestoreContext(context *,record *),RtlUnwindEx(void *,void *,record *,void *,context *,void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
__declspec(dllimport) void *GetModuleHandleW(const uint16_t *),*GetProcAddress(void *,const char *);
extern unsigned longjump_fixture(unsigned);
extern void longjump_landing(void),cfg_dummy(void);
extern unsigned char __guard_fids_table[],__guard_fids_count[],__guard_longjmp_table[],__guard_longjmp_count[];
jump_buffer longjump_buffer;
static context captured;
static volatile unsigned cleanups;
uint64_t cookie=0x2b992ddfa232ull;
static void fallback(void){ExitProcess(1);}
void (*check_slot)(void)=fallback,(*dispatch_slot)(void)=fallback;
const struct {
 unsigned size,time;unsigned short major,minor;unsigned clear,set,timeout;
 uint64_t decommit_free,decommit_total,lock_prefix,max_alloc,vm_threshold,affinity;
 unsigned heap_flags;unsigned short csd,dependent_flags;uint64_t edit,cookie,seh,seh_count,check,dispatch,functions,function_count;
 unsigned flags,integrity[3];uint64_t iat,iat_count,jumps,jump_count;
} _load_config_used={.size=0xc0,.cookie=(uint64_t)&cookie,.check=(uint64_t)&check_slot,.dispatch=(uint64_t)&dispatch_slot,
 .functions=(uint64_t)__guard_fids_table,.function_count=(uint64_t)__guard_fids_count,.flags=0x10500,
 .jumps=(uint64_t)__guard_longjmp_table,.jump_count=(uint64_t)__guard_longjmp_count};
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
__declspec(noinline) static void inner(record *r) {
 __try {RtlUnwindEx((void *)longjump_buffer.frame,(void *)longjump_buffer.rip,r,(void *)42,&captured,0);ExitProcess(1);}
 __finally {cleanups++;}
}
void do_longjump(unsigned mode){
 RtlCaptureContext(&captured);captured.rax=42;
 longjump_buffer.rbx=0x1234567887654321ull;longjump_buffer.r12=0x1122334455667788ull;
 *(uint64_t *)longjump_buffer.xmm[0]=0x9988776655443322ull;
 if(mode>=2)longjump_buffer.rip++;
 record r={.code=0x80000026u,.count=1,.args={(uint64_t)&longjump_buffer}};
 if(mode&1)inner(&r);else RtlRestoreContext(&captured,&r);
 ExitProcess(1);
}
void mainCRTStartup(void){
 void *module=GetModuleHandleW((const uint16_t *)L"kernel32.dll");
#ifdef HOST_REFERENCE
 unsigned (*set_error_mode)(unsigned)=GetProcAddress(module,"SetErrorMode");
 check(set_error_mode!=0,"longjump: host error-mode export FAIL\n");set_error_mode(2);
#endif
 int (*guard)(void *,unsigned)=GetProcAddress(module,"GuardCheckLongJumpTarget");
#ifdef HOST_REFERENCE
 if(!guard)guard=GetProcAddress(GetModuleHandleW((const uint16_t *)L"kernelbase.dll"),"GuardCheckLongJumpTarget");
#endif
 check(guard!=0,"longjump: guard export FAIL\n");
 check(guard(longjump_landing,1),"longjump: declared target FAIL\n");
 check(!guard((char *)longjump_landing+1,1),"longjump: adjacent target FAIL\n");
 check(!guard(cfg_dummy,1),"longjump: ordinary CFG target must not become a longjump target FAIL\n");
#ifdef LONGJUMP_BAD
 longjump_fixture(LONGJUMP_BAD+1);ExitProcess(1);
#else
 for(unsigned i=0;i<32;i++){check(longjump_fixture(0)==1,"longjump: restore GPR/XMM/RAX FAIL\n");check(longjump_fixture(1)==1,"longjump: unwind GPR/XMM/RAX FAIL\n");}
 check(cleanups==32,"longjump: finally cleanup count FAIL\n");
 OutputDebugStringA("windows-longjump: exact independent target catalog / 32 restore + 32 SEH unwind / cleanup / nonvolatile state PASS\n");ExitProcess(0);
#endif
}
