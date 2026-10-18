#include "windows.h"
#include "process.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
extern int wr_native_mapped_name(const char *,uint16_t *,size_t);
static int self(void *h){if(h==(void *)-1)return 1;unsigned (WR_WIN *id)(void *)=wr_process_exports("GetProcessId");if(!id(h))return 0;wr_current_teb()->last_error=50;return 0;}
static unsigned protection(unsigned p){switch(p){case 0:return 1;case 1:return 2;case 3:return 4;case 4:return 0x10;case 5:return 0x20;default:return 0;}}
static unsigned WR_WIN DiscardVirtualMemory(void *address,size_t length) {
    vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_DISCARD,.address=(uintptr_t)address,.length=length};
    int e=(int)(int64_t)vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r);return e?e==-12?8:87:0;
}
static int WR_WIN PrefetchVirtualMemory(void *process,size_t count,const void *entries,unsigned flags){
    if(!self(process))return 0;
    if(flags || !count || !entries || count>SIZE_MAX/16 || (uintptr_t)entries>UINTPTR_MAX-count*16){wr_current_teb()->last_error=87;return 0;}
    for(size_t i=0;i<count;i++){
        struct {uintptr_t address;size_t length;} range;
        if(wr_memory_copy(&range,(const uint8_t *)entries+16*i,16)){wr_current_teb()->last_error=998;return 0;}
        if(!range.length || range.address>UINTPTR_MAX-range.length){wr_current_teb()->last_error=87;return 0;}
        /* Native usercopy materializes a readable sparse page under the memory
           transaction. Read one byte per page, not a full copy of the range;
           concurrent unmap/protect returns an error rather than a kernel fault.
           No writable/executable permissions are added, no file is flushed. */
        uintptr_t end=range.address+range.length,cursor=range.address;
        while(cursor<end){uint8_t byte;int e=wr_memory_copy(&byte,(const void *)cursor,1);
            if(e){wr_current_teb()->last_error=e==-12?8:998;return 0;}
            uintptr_t next=(cursor|4095ull)+1;if(next<=cursor)break;cursor=next;
        }
    }return 1;
}
static unsigned WR_WIN K32GetMappedFileNameW(void *process,void *address,uint16_t *out,unsigned capacity) {
    if(!self(process))return 0;if(!capacity || !out){wr_current_teb()->last_error=87;return 0;}
    char path[512]={0};uint16_t text[1024];
    vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_PATH,.address=(uintptr_t)address,.length=sizeof(path),.out=path};
    int e=(int)(int64_t)vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r);
    if(e) {
        /* PE images are anonymous protected copies of the file, owned by
           the loader's module inventory rather than the file mmap cache. */
        wr_loader_lock();int found=0;
        for(unsigned i=0;i<wr_modules_count();i++){module *m=wr_module_at(i);uintptr_t a=(uintptr_t)address,b=(uintptr_t)m->base;
            if(a>=b && a-b<m->image.size){memcpy(path,m->path,wr_len(m->path)+1);found=1;break;}}
        wr_loader_unlock();if(!found){wr_current_teb()->last_error=487;return 0;}
    }
    int n=wr_native_mapped_name(path,text,1024);if(n<0){wr_current_teb()->last_error=5;return 0;}
    unsigned count=(unsigned)n;if(count>=capacity){count=capacity-1;text[count]=0;wr_current_teb()->last_error=122;}
    if(wr_memory_copy(out,text,(count+1)*2)){wr_current_teb()->last_error=998;return 0;}
    return (unsigned)n>=capacity?capacity:count;
}
typedef struct {void *address;uint64_t attributes;} working_set;
typedef struct {uint32_t size,load;uint64_t total_phys,free_phys,total_page,free_page,total_virtual,free_virtual,extended;} memory_status;
typedef struct {uint32_t size,faults;uint64_t peak_working,working,peak_paged,paged,peak_nonpaged,nonpaged,charge,peak_charge,private_charge;} process_counters;
typedef struct {uint32_t size,pad;uint64_t commit,limit,peak,physical,available,cache,kernel,paged,nonpaged,page_size;uint32_t handles,processes,threads,pad2;} performance_info;
_Static_assert(sizeof(process_counters)==80,"PROCESS_MEMORY_COUNTERS_EX");
_Static_assert(sizeof(performance_info)==104,"PERFORMANCE_INFORMATION");
static int memory_usage(vex_memory_usage_t *out){
    vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_USAGE,.out=out};
    if(vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r)){wr_current_teb()->last_error=1117;return 0;}return 1;
}
static int WR_WIN K32GetProcessMemoryInfo(void *process,process_counters *out,unsigned size){
    if(!out || size<72){wr_current_teb()->last_error=87;return 0;}
    int fd=wr_process_capability(process);if(fd<0){wr_current_teb()->last_error=6;return 0;}
    vex_process_resource_info_t identity;vex_process_resource_req_t q={.size=sizeof(q),.operation=VEX_PROCESS_QUERY,.fd=fd,.out=&identity};
    int error=(int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&q);vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);
    if(error || identity.pid!=wr_current_teb()->pid){wr_current_teb()->last_error=error?(error==-13?5:6):50;return 0;}
    vex_memory_usage_t native;if(!memory_usage(&native))return 0;
    process_counters value={.size=size>=80?80:72,.faults=(uint32_t)native.materializations,
        .peak_working=native.peak_resident_bytes,.working=native.resident_bytes,
        .peak_nonpaged=native.peak_owned_allocator_bytes,.nonpaged=native.owned_allocator_bytes,
        .peak_charge=native.peak_private_charge_bytes,.charge=native.private_charge_bytes,.private_charge=native.private_charge_bytes};
    /* No pageable kernel pool or swap exists. PagefileUsage is the private
       logical backing charge, NOT resident pages or an invented swap file.
       Native materialization events include eager loader population. */
    if(wr_memory_copy(out,&value,value.size)){wr_current_teb()->last_error=998;return 0;}return 1;
}
static int WR_WIN K32GetPerformanceInfo(performance_info *out,unsigned size){
    if(!out || size<sizeof(*out)){wr_current_teb()->last_error=87;return 0;}
    vex_memory_usage_t native;if(!memory_usage(&native))return 0;
    performance_info value={.size=sizeof(value),.commit=(native.committed_bytes+4095)/4096,
        .limit=native.physical_bytes/4096,.peak=(native.peak_committed_bytes+4095)/4096,
        .physical=native.physical_bytes/4096,.available=native.available_bytes/4096,.cache=(native.cache_bytes+4095)/4096,
        .kernel=native.allocator_bytes/4096,.nonpaged=native.allocator_bytes/4096,.page_size=4096,
        .handles=native.resource_handles,.processes=native.processes,.threads=native.threads};
    /* System inventory is Vex's resource/process inventory, including native
       applications; it does not impersonate a separate Microsoft kernel.
       Overcommit is observable: charge may exceed actual RAM capacity. */
    if(wr_memory_copy(out,&value,sizeof(value))){wr_current_teb()->last_error=998;return 0;}return 1;
}
static int WR_WIN GlobalMemoryStatusEx(memory_status *out) {
    uint32_t size;if(!out || wr_memory_copy(&size,out,4) || size!=sizeof(*out)){wr_current_teb()->last_error=87;return 0;}
    vex_memory_capacity_t info;vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_CAPACITY,.out=&info};
    if(vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r) || !info.physical_bytes){wr_current_teb()->last_error=1117;return 0;}
    /* No swap backend exists: backing capacity is physical RAM. These are
       instantaneous capacity/headroom observations, never a swapfile size
       or a promise that a future allocation cannot race another process. */
    memory_status value={sizeof(value),(uint32_t)(100-(info.available_physical_bytes*100/info.physical_bytes)),
        info.physical_bytes,info.available_physical_bytes,info.physical_bytes,info.available_physical_bytes,
        info.virtual_end-info.virtual_begin,info.available_virtual_bytes,0};
    if(wr_memory_copy(out,&value,sizeof(value))){wr_current_teb()->last_error=998;return 0;}return 1;
}
static int WR_WIN K32QueryWorkingSetEx(void *process,working_set *out,unsigned bytes) {
    if(!self(process))return 0;if(bytes%sizeof(*out) || (!out && bytes)){wr_current_teb()->last_error=87;return 0;}
    for(unsigned i=0;i<bytes/sizeof(*out);i++) {
        working_set item;if(wr_memory_copy(&item,out+i,sizeof(item))){wr_current_teb()->last_error=998;return 0;}
        vex_memory_page_info_t info;vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_PAGE,.address=(uintptr_t)item.address,.out=&info};
        if(vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r)){wr_current_teb()->last_error=87;return 0;}
        item.attributes=0;
        if(info.present)item.attributes=1ull|((uint64_t)(info.mapping_refs>7?7:info.mapping_refs)<<1)|
            ((uint64_t)protection(info.permissions)<<4)|((uint64_t)info.shared<<15);
        if(wr_memory_copy(out+i,&item,sizeof(item))){wr_current_teb()->last_error=998;return 0;}
    }return 1;
}
void *wr_memory_exports(const char *name){
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(GlobalMemoryStatusEx);E(DiscardVirtualMemory);E(PrefetchVirtualMemory);E(K32GetMappedFileNameW);E(K32QueryWorkingSetEx);
    E(K32GetProcessMemoryInfo);E(K32GetPerformanceInfo);
#undef E
    return 0;
}
