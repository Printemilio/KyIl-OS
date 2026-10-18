/* Initial implemented Windows contracts. Missing imports are fatal at load,
 * rather than redirected to generic zero/success thunks. */
#include "windows.h"
#include "wer.h"
#include "process.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#define last_error (wr_current_teb()->last_error)
static wr_mutex memory_gate;
typedef struct vm_allocation {struct vm_allocation *next;void *base;size_t size;unsigned prot;} vm_allocation;
static vm_allocation *regions;
static int protection(uint32_t p) {
    switch(p) { case 1:return 0; case 2:return PE_R; case 4:return PE_R|PE_W;
        case 0x10:return PE_X; case 0x20:return PE_R|PE_X; default:return -1; }
}
static uint32_t from_protection(unsigned p) {
    switch(p) { case 0:return 1; case PE_R:return 2; case PE_R|PE_W:return 4;
        case PE_X:return 0x10; case PE_R|PE_X:return 0x20; default:return 0; }
}
int wr_memory_query(uintptr_t address,vex_memory_view_info_t *out) {
    vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_QUERY,.address=address,.out=out};
    return (int)(int64_t)vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r);
}
int wr_memory_copy(void *out,const void *source,size_t length) {
    vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_READ,.address=(uintptr_t)source,.length=length,.out=out};
    return (int)(int64_t)vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r);
}
static void WR_WIN ExitProcess(uint32_t code) { wr_finish(code); }
static uint32_t WR_WIN GetLastError(void) {return last_error;}
static void WR_WIN SetLastError(uint32_t e) {last_error=e;}
static uint32_t WR_WIN GetCurrentProcessId(void) {return (uint32_t)wr_pid();}
static uint64_t WR_WIN GetTickCount64(void) {return wr_ticks();}
static uint32_t WR_WIN GetTickCount(void) {return (uint32_t)wr_ticks();}
static void WR_WIN OutputDebugStringA(const char *s) {wr_log(s);}
static vm_allocation *allocation_containing(uintptr_t start,size_t length){
    for(vm_allocation *r=regions;r;r=r->next)if(start>=(uintptr_t)r->base && start-(uintptr_t)r->base<r->size && length<=r->size-(start-(uintptr_t)r->base))return r;return 0;
}
static int private_state(uintptr_t start,size_t length,int prot,int commit){
    vex_memory_view_req_t r={.size=sizeof(r),.operation=commit?VEX_MEMORY_VIEW_COMMIT:VEX_MEMORY_VIEW_DECOMMIT,.address=start,.length=length,.permissions=(unsigned)prot};
    int64_t error=(int64_t)vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r);
    if(error)last_error=error==-12?8:487;return !error;
}
static void *reserve_aligned(void *hint,size_t n){
    uint8_t *b=wr_map(hint,n,0);if(!b)return 0;
    if(hint){if(b!=hint){wr_unmap(b,n);return 0;}return b;}
    if(!((uintptr_t)b&65535))return b;
    if(wr_unmap(b,n) || n>SIZE_MAX-65536)return 0;
    b=wr_map(0,n+65536,0);if(!b)return 0;
    uint8_t *a=(void *)(((uintptr_t)b+65535)&~(uintptr_t)65535);size_t head=a-b,tail=65536-head;
    if(head && wr_unmap(b,head)){wr_unmap(b,n+65536);return 0;}
    if(tail && wr_unmap(a+n,tail)){wr_unmap(a,n+tail);return 0;}return a;
}
static void *WR_WIN alloc_impl(void *at,size_t n,uint32_t kind,uint32_t p) {
    int prot=protection(p);uintptr_t requested=(uintptr_t)at;
    if((kind!=0x1000 && kind!=0x2000 && kind!=0x3000) || !n || requested>UINTPTR_MAX-n || requested+n>UINTPTR_MAX-4095){last_error=87;return 0;}
    if(prot<0){last_error=5;return 0;}
    uintptr_t start=requested&~(uintptr_t)((kind&0x2000)?65535:4095);
    size_t size=((requested+n+4095)&~(uintptr_t)4095)-start;
    if(kind==0x1000 && at){
        if(!allocation_containing(start,size)){last_error=487;return 0;}
        return private_state(start,size,prot,1)?(void *)start:0;
    }
    vm_allocation *r=wr_native_alloc(sizeof(*r));if(!r){last_error=8;return 0;}
    void *base=reserve_aligned((void *)start,size);
    if(!base){wr_native_free(r);last_error=at?487:8;return 0;}
    if((kind&0x1000) && !private_state((uintptr_t)base,size,prot,1)){wr_unmap(base,size);wr_native_free(r);return 0;}
    *r=(vm_allocation){regions,base,size,p};regions=r;return base;
}
static int WR_WIN protect_impl(void *at,size_t n,uint32_t p,uint32_t *old) {
    int prot=protection(p);
    if(!old || !n || (uintptr_t)at>UINTPTR_MAX-n || prot<0) {last_error=prot<0?5:87;return 0;}
    uint64_t start=(uintptr_t)at&~4095ull,end=(uintptr_t)at+n;
    if(end>UINT64_MAX-4095){last_error=87;return 0;}end=(end+4095)&~4095ull;
    vex_memory_view_info_t first,part;if(wr_memory_query(start,&first) || first.kind==VEX_MEMORY_VIEW_FREE){last_error=487;return 0;}
    for(uint64_t cursor=start;cursor<end;) {
        if(wr_memory_query(cursor,&part) || part.kind==VEX_MEMORY_VIEW_FREE || part.allocation_base!=first.allocation_base || !part.length){last_error=487;return 0;}
        if(allocation_containing(start,end-start) && part.kind==VEX_MEMORY_VIEW_RESERVED){last_error=487;return 0;}
        uint64_t next=part.base+part.length;if(next<=cursor){last_error=487;return 0;}cursor=next<end?next:end;
    }
    unsigned previous=from_protection(first.permissions);
    if(wr_memory_copy(old,&previous,sizeof(previous))){last_error=998;return 0;}
    if(wr_protect((void *)(uintptr_t)start,end-start,prot)){last_error=487;return 0;}return 1;
}
static int WR_WIN free_impl(void *at,size_t n,uint32_t kind) {
    if(kind==0x4000){
        uintptr_t start=(uintptr_t)at&~(uintptr_t)4095;
        if((uintptr_t)at>UINTPTR_MAX-n || (uintptr_t)at+n>UINTPTR_MAX-4095){last_error=87;return 0;}
        size_t length=(((uintptr_t)at+n+4095)&~(uintptr_t)4095)-start;vm_allocation *r=allocation_containing(start,length?length:1);
        if(!r || (!n && at!=r->base)){last_error=487;return 0;}if(!n)length=r->size;
        return private_state(start,length,0,0);
    }
    if(n || kind!=0x8000) {last_error=87;return 0;}
    for(vm_allocation **link=&regions;*link;link=&(*link)->next)if((*link)->base==at){
        vm_allocation *r=*link;if(wr_unmap(at,r->size)){last_error=487;return 0;}
        *link=r->next;wr_native_free(r);return 1;
    }
    last_error=487;return 0;
}
static void *WR_WIN VirtualAlloc(void *at,size_t n,uint32_t kind,uint32_t p) {
    wr_lock(&memory_gate);void *r=alloc_impl(at,n,kind,p);wr_unlock(&memory_gate);return r;
}
static int WR_WIN VirtualProtect(void *at,size_t n,uint32_t p,uint32_t *old) {
    wr_lock(&memory_gate);int r=protect_impl(at,n,p,old);wr_unlock(&memory_gate);return r;
}
static int WR_WIN VirtualFree(void *at,size_t n,uint32_t kind) {
    wr_lock(&memory_gate);int r=free_impl(at,n,kind);wr_unlock(&memory_gate);return r;
}
typedef struct {void *base,*allocation;uint32_t allocation_protect;uint16_t partition,pad;
    size_t length;uint32_t state,protect,type,pad2;} memory_basic;
_Static_assert(sizeof(memory_basic)==48,"AMD64 MEMORY_BASIC_INFORMATION");
static size_t WR_WIN VirtualQuery(const void *address,memory_basic *out,size_t capacity) {
    if(!out || capacity<sizeof(*out)){last_error=87;return 0;}
    wr_loader_lock();wr_lock(&memory_gate);vex_memory_view_info_t view;
    if(wr_memory_query((uintptr_t)address,&view)){wr_unlock(&memory_gate);wr_loader_unlock();last_error=87;return 0;}
    memory_basic result={.base=(void *)(uintptr_t)view.base,.allocation=(void *)(uintptr_t)view.allocation_base,.length=view.length};
    if(view.kind==VEX_MEMORY_VIEW_FREE){result.state=0x10000;}
    else {
        result.state=view.kind==VEX_MEMORY_VIEW_RESERVED?0x2000:0x1000;
        result.protect=from_protection(view.permissions);result.allocation_protect=result.protect;
        result.type=(view.mapping_flags&MAP_SHARED)?0x40000:0x20000;
        vm_allocation *a=allocation_containing((uintptr_t)address,1);
        if(a){
            /* Windows scans forward from the queried page, rather than
               returning the start of a region preceding that address. */
            uintptr_t end=(uintptr_t)a->base+a->size,cursor=view.base+view.length;
            if(cursor>end)cursor=end;
            while(cursor<end){vex_memory_view_info_t part;
                if(wr_memory_query(cursor,&part) || !part.length || part.kind!=view.kind || part.permissions!=view.permissions)break;
                if(part.length>end-cursor){cursor=end;break;}cursor+=part.length;
            }
            result.length=cursor-view.base;result.allocation=a->base;result.allocation_protect=a->prot;
            result.state=view.kind==VEX_MEMORY_VIEW_RESERVED?0x2000:0x1000;result.protect=result.state==0x2000?0:from_protection(view.permissions);
        }
        for(unsigned i=0;i<wr_modules_count();i++) {
            module *m=wr_module_at(i);
            if(m->base && view.base>=(uintptr_t)m->base && pe_span(view.base-(uintptr_t)m->base,1,m->image.size)) {
                result.allocation=m->base;result.type=0x1000000;result.state=0x1000;
                uint64_t remain=m->image.size-(view.base-(uintptr_t)m->base);if(result.length>remain)result.length=remain;break;
            }
        }
    }
    wr_unlock(&memory_gate);wr_loader_unlock();
    if(wr_memory_copy(out,&result,sizeof(result))){last_error=998;return 0;}return sizeof(*out);
}
static void *WR_WIN VirtualAllocEx(void *process,void *at,size_t n,unsigned kind,unsigned protect){
    return wr_process_self_access(process,8)?VirtualAlloc(at,n,kind,protect):0;
}
static int WR_WIN VirtualFreeEx(void *process,void *at,size_t n,unsigned kind){
    return wr_process_self_access(process,8)?VirtualFree(at,n,kind):0;
}
static size_t WR_WIN VirtualQueryEx(void *process,const void *at,memory_basic *out,size_t n){
    return wr_process_self_access(process,0x400)?VirtualQuery(at,out,n):0;
}
static int transfer_memory(void *process,const void *address,void *buffer,size_t length,size_t *copied,int writing) {
    if(copied)*copied=0;
    if(!wr_process_self_access(process,writing?0x28:0x10))return 0;
    if((uintptr_t)address>UINTPTR_MAX-length || (uintptr_t)buffer>UINTPTR_MAX-length){last_error=299;return 0;}
    size_t done=0;
    while(done<length) {
        size_t count=length-done;if(count>VEX_MEMORY_COPY_MAX)count=VEX_MEMORY_COPY_MAX;
        int error=writing?wr_memory_copy((char *)(uintptr_t)address+done,(char *)buffer+done,count):
            wr_memory_copy((char *)buffer+done,(const char *)address+done,count);
        if(error){if(copied)*copied=done;last_error=299;return 0;}done+=count;
    }
    if(copied)*copied=done;return 1;
}
static int WR_WIN ReadProcessMemory(void *process,const void *address,void *out,size_t length,size_t *read) {return transfer_memory(process,address,out,length,read,0);}
static int WR_WIN WriteProcessMemory(void *process,void *address,const void *input,size_t length,size_t *written) {return transfer_memory(process,address,(void *)input,length,written,1);}
static int WR_WIN VirtualProtectEx(void *process,void *address,size_t length,unsigned protect,unsigned *old) {
    if(!wr_process_self_access(process,8))return 0;return VirtualProtect(address,length,protect,old);
}
void *wr_builtin(const char *dll,const char *name) {
    const char *host=wr_api_set_host(dll);if(host)dll=host;
    if(wr_equal(dll,"kyilwin-provider.dll",1)) {
        char module_name[32];unsigned i=0;while(name[i] && name[i]!='$' && i<sizeof(module_name)-1){module_name[i]=name[i];i++;}
        if(name[i]!='$')return 0;module_name[i]=0;
        if(!wr_native_module_name(module_name))return 0;
        return wr_builtin(module_name,name+i+1);
    }
    if(wr_equal(dll,"bcryptprimitives.dll",1))return wr_cfg_register(wr_rng_exports(name));
    if(wr_equal(dll,"winmm.dll",1))return wr_cfg_register(wr_multimedia_exports(name));
    if(wr_equal(dll,"shell32.dll",1))return wr_cfg_register(wr_shell_exports(name));
    if(wr_equal(dll,"advapi32.dll",1))return wr_cfg_register(wr_advapi_exports(name));
    if(wr_equal(dll,"ntdll.dll",1)) {
        void *p=wr_heap_exports(name);if(!p)p=wr_exception_exports(name);if(!p)p=wr_nt_core_exports(name);return wr_cfg_register(p);
    }
    if(wr_equal(dll,"version.dll",1))return wr_cfg_register(wr_version_exports(name));
    if(!wr_equal(dll,"kernel32.dll",1) && !wr_equal(dll,"kernelbase.dll",1)) return 0;
    void *proc=wr_wer_exports(name);if(proc)return wr_cfg_register(proc);
    proc=wr_process_exports(name);if(proc)return wr_cfg_register(proc);
    proc=wr_environment_exports(name);if(proc)return wr_cfg_register(proc);
    proc=wr_module_exports(name);if(proc)return wr_cfg_register(proc);
    void *extra=wr_file_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_object_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_completion_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_pipe_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_thread_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_heap_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_sync_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_exception_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_path_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_nls_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_encoding_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_system_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_mapping_exports(name);if(extra)return wr_cfg_register(extra);
    extra=wr_memory_exports(name);if(extra)return wr_cfg_register(extra);
#define EXPORT(f) if(wr_equal(name,#f,0)) return wr_cfg_register((void *)&f)
    EXPORT(ExitProcess);EXPORT(GetLastError);
    EXPORT(SetLastError);EXPORT(GetCurrentProcessId);EXPORT(GetTickCount64);
    EXPORT(GetTickCount);EXPORT(OutputDebugStringA);EXPORT(VirtualAlloc);
    EXPORT(VirtualProtect);EXPORT(VirtualFree);
    EXPORT(VirtualQuery);EXPORT(VirtualAllocEx);EXPORT(VirtualFreeEx);EXPORT(VirtualQueryEx);
    EXPORT(ReadProcessMemory);EXPORT(WriteProcessMemory);EXPORT(VirtualProtectEx);
#undef EXPORT
    return 0;
}
