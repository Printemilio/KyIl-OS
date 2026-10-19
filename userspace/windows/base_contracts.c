#include "process.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
static int error(unsigned e){wr_current_teb()->last_error=e;return 0;}
static int ansi_wide(const char *s,uint16_t *out,unsigned capacity){
    if(!s)return error(87);size_t limit=(size_t)capacity*4;char *bytes=wr_native_alloc(limit);
    if(!bytes)return error(8);unsigned at=0;int result=0;
    while(at<limit){unsigned n=4096-(((uintptr_t)s+at)&4095);if(n>limit-at)n=(unsigned)(limit-at);
        if(wr_memory_copy(bytes+at,s+at,n)){error(998);goto done;}
        for(unsigned i=0;i<n;i++)if(!bytes[at+i]){if(wr_utf16(bytes,out,capacity)<0)error(1113);else result=1;goto done;}at+=n;
    }error(206);
done:wr_native_free(bytes);return result;
}
static unsigned narrow_result(const uint16_t *text,char *out,unsigned capacity){
    size_t units=0;while(text[units])units++;size_t space=(units+1)*4;
    char *bytes=wr_native_alloc(space);if(!bytes){error(8);return 0;}
    unsigned n=0;if(wr_utf8(text,bytes,space))error(1113);
    else {n=(unsigned)wr_len(bytes);if(n>=capacity)n++;else if(!out || wr_memory_copy(out,bytes,n+1)){n=0;error(998);}}
    wr_native_free(bytes);return n;
}
static unsigned WR_WIN GetEnvironmentVariableA(const char *name,char *out,unsigned capacity){
    uint16_t *storage=wr_native_alloc(131072);if(!storage){error(8);return 0;}
    uint16_t *wide=storage,*value=storage+32768;unsigned n=0;
    if(!ansi_wide(name,wide,32768))goto done;
    unsigned (WR_WIN *get)(const uint16_t *,uint16_t *,unsigned)=wr_environment_exports("GetEnvironmentVariableW");
    unsigned saved=wr_current_teb()->last_error;wr_current_teb()->last_error=0;
    n=get(wide,value,32768);unsigned why=wr_current_teb()->last_error;
    if(n>=32768){error(8);n=0;}else if(!n && why){n=0;}else{wr_current_teb()->last_error=saved;n=narrow_result(value,out,capacity);}
done:wr_native_free(storage);return n;
}
static int WR_WIN SetEnvironmentVariableA(const char *name,const char *value){
    uint16_t *storage=wr_native_alloc(131072);if(!storage)return error(8);
    uint16_t *key=storage,*text=storage+32768;int result=0;
    if(ansi_wide(name,key,32768) && (!value || ansi_wide(value,text,32768))){
        int (WR_WIN *set)(const uint16_t *,const uint16_t *)=wr_environment_exports("SetEnvironmentVariableW");result=set(key,value?text:0);}
    wr_native_free(storage);return result;
}
static unsigned narrow_directory(const char *name,char *out,unsigned n,int temp){
    uint16_t text[1024];unsigned got;
    if(temp){unsigned (WR_WIN *get)(unsigned,uint16_t *)=wr_path_exports(name);got=get(1024,text);}
    else {unsigned (WR_WIN *get)(uint16_t *,unsigned)=wr_path_exports(name);got=get(text,1024);}
    return got && got<1024?narrow_result(text,out,n):0;
}
static unsigned WR_WIN GetSystemDirectoryA(char *out,unsigned n){return narrow_directory("GetSystemDirectoryW",out,n,0);}
static unsigned WR_WIN GetWindowsDirectoryA(char *out,unsigned n){return narrow_directory("GetWindowsDirectoryW",out,n,0);}
static unsigned WR_WIN GetTempPathA(unsigned n,char *out){return narrow_directory("GetTempPathW",out,n,1);}
static int WR_WIN AreFileApisANSI(void){return 1;} /* configured Windows ACP is UTF-8 */
static int WR_WIN GetModuleHandleExA(unsigned flags,const char *name,void **out){
    int (WR_WIN *get)(unsigned,const uint16_t *,void **)=wr_module_exports("GetModuleHandleExW");
    if((flags&4) || !name)return get(flags,(const uint16_t *)name,out);
    uint16_t *text=wr_native_alloc(65536);if(!text)return error(8);
    int result=ansi_wide(name,text,32768)?get(flags,text,out):0;wr_native_free(text);return result;
}
static int WR_WIN GetComputerNameW(uint16_t *out,unsigned *size){
    int (WR_WIN *get)(unsigned,uint16_t *,unsigned *)=wr_system_exports("GetComputerNameExW");
    int ok=get(0,out,size);if(!ok && wr_current_teb()->last_error==234)error(111);return ok;
}
static unsigned WR_WIN GetActiveProcessorCount(unsigned group){
    if(group && group!=0xffffu){error(87);return 0;}
    vex_memory_machine_info_t m;vex_memory_view_req_t q={.size=sizeof(q),.operation=VEX_MEMORY_VIEW_MACHINE,.out=&m};
    if(vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&q)){error(1117);return 0;}
    if(m.online_cpus>64){error(50);return 0;}return m.online_cpus;
}
static unsigned WR_WIN GetMaximumProcessorCount(unsigned group){return GetActiveProcessorCount(group);}
static unsigned short WR_WIN GetMaximumProcessorGroupCount(void){return GetActiveProcessorCount(0)?1:0;}
static int WR_WIN IsThreadAFiber(void){return wr_current_teb()->fiber!=0;}
static int WR_WIN LockFile(void *h,unsigned lo,unsigned hi,unsigned bytes_lo,unsigned bytes_hi){
    wr_overlapped o={.offset=lo,.offset_high=hi};
    int (WR_WIN *lock)(void *,unsigned,unsigned,unsigned,unsigned,wr_overlapped *)=wr_file_exports("LockFileEx");return lock(h,3,0,bytes_lo,bytes_hi,&o);
}
static int WR_WIN UnlockFile(void *h,unsigned lo,unsigned hi,unsigned bytes_lo,unsigned bytes_hi){
    wr_overlapped o={.offset=lo,.offset_high=hi};
    int (WR_WIN *unlock)(void *,unsigned,unsigned,unsigned,wr_overlapped *)=wr_file_exports("UnlockFileEx");return unlock(h,0,bytes_lo,bytes_hi,&o);
}
static unsigned WR_WIN SetFilePointer(void *h,int low,int *high,unsigned method){
    int upper=low<0?-1:0;if(high && wr_memory_copy(&upper,high,4)){error(998);return UINT32_MAX;}
    int64_t distance=(int64_t)(((uint64_t)(uint32_t)upper<<32)|(uint32_t)low),position;
    int (WR_WIN *seek)(void *,int64_t,int64_t *,unsigned)=wr_file_exports("SetFilePointerEx");
    if(!seek(h,distance,&position,method))return UINT32_MAX;
    upper=(int)(position>>32);if(high && wr_memory_copy(high,&upper,4)){error(998);return UINT32_MAX;}
    wr_current_teb()->last_error=0;return (unsigned)position;
}
static unsigned WR_WIN WaitForSingleObjectEx(void *h,unsigned timeout,int alertable){
    /* APC execution has no represented contract. Nonalertable waits use the
       existing capability-backed engine without manufacturing APC success. */
    if(alertable){error(50);return UINT32_MAX;}return wr_wait_one(h,timeout);
}
static int WR_WIN lstrlenW(const uint16_t *s){
    if(!s)return 0;unsigned n=0;uint16_t value;
    while(n<INT32_MAX){if(wr_memory_copy(&value,s+n,2)){error(998);return 0;}if(!value)return (int)n;n++;}error(8);return 0;
}
void *wr_base_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(GetEnvironmentVariableA);E(SetEnvironmentVariableA);E(GetSystemDirectoryA);E(GetWindowsDirectoryA);E(GetTempPathA);E(AreFileApisANSI);
    E(GetModuleHandleExA);E(GetComputerNameW);
    E(GetActiveProcessorCount);E(GetMaximumProcessorCount);E(GetMaximumProcessorGroupCount);E(IsThreadAFiber);
    E(LockFile);E(UnlockFile);E(SetFilePointer);E(WaitForSingleObjectEx);E(lstrlenW);
#undef E
    return 0;
}
