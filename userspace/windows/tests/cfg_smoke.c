typedef unsigned long long u64;
__declspec(dllimport) void OutputDebugStringA(const char *);
__declspec(dllimport) void ExitProcess(unsigned);
extern unsigned cfg_function_rvas[];
extern void cfg_invoke(void);
u64 __security_cookie=0x2b992ddfa232ull;
void guard_fallback(void) {OutputDebugStringA("windows-cfg: guard slot not installed FAIL\n");ExitProcess(1);}
void (*guard_check)(void)=guard_fallback;
void (*guard_dispatch)(void)=guard_fallback;
volatile unsigned result;
unsigned valid_target(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e,unsigned f) {
    result=a+b+c+d+e+f;return result;
}
const struct {
    unsigned size,time;unsigned short major,minor;
    unsigned clear,set,timeout;
    u64 decommit_free,decommit_total,lock_prefix,max_alloc,vm_threshold,affinity;
    unsigned heap_flags;unsigned short csd,dependent_flags;
    u64 edit,cookie,seh,seh_count,check,dispatch,functions,function_count;
    unsigned guard_flags;
} _load_config_used={.size=0x94,.cookie=(u64)&__security_cookie,.check=(u64)&guard_check,
    .dispatch=(u64)&guard_dispatch,.functions=(u64)cfg_function_rvas,.function_count=1,.guard_flags=0x500};
void mainCRTStartup(void) {
    if(!__security_cookie || __security_cookie==0x2b992ddfa232ull) {
        OutputDebugStringA("windows-cfg: cookie FAIL\n");ExitProcess(1);
    }
    OutputDebugStringA("windows-cfg: real cookie initialized\n");
    cfg_invoke();
    if(result!=21) {OutputDebugStringA("windows-cfg: six-argument dispatch FAIL\n");ExitProcess(1);}
    OutputDebugStringA("windows-cfg: real guarded indirect call PASS\n");ExitProcess(0);
}
