#include "windows.h"
#include "process.h"
#include "job.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
_Static_assert(sizeof(wr_unicode)==16,"x64 UNICODE_STRING");
static void WR_WIN RtlInitUnicodeString(wr_unicode *out,const uint16_t *source) {
    unsigned n=0;if(source)while(n<32767 && source[n])n++;
    if(n>32766)n=32766;
    *out=(wr_unicode){.length=(uint16_t)(n*2),.maximum=source?(uint16_t)(n*2+2):0,.buffer=(uint16_t *)source};
}
static uint32_t WR_WIN RtlInitUnicodeStringEx(wr_unicode *out,const uint16_t *source) {
    unsigned n=0;if(source)while(n<32767 && source[n])n++;
    if(n>32766){*out=(wr_unicode){0};return 0xc0000106u;}
    RtlInitUnicodeString(out,source);return 0;
}
static void WR_WIN RtlFreeUnicodeString(wr_unicode *string) {
    unsigned saved=wr_current_teb()->last_error;
    if(string->buffer) {
        int (WR_WIN *free_heap)(void *,unsigned,void *)=wr_heap_exports("HeapFree");
        if(!free_heap(wr_process_heap(),0,string->buffer))wr_exit(0xc000000du);
    }
    *string=(wr_unicode){0};wr_current_teb()->last_error=saved;
}
static uint32_t WR_WIN NtClose(void *handle) {
    unsigned saved=wr_current_teb()->last_error;
    int (WR_WIN *close_handle)(void *)=wr_object_exports("CloseHandle");
    int result=close_handle(handle);unsigned error=wr_current_teb()->last_error;
    wr_current_teb()->last_error=saved;
    if(result)return 0;
    switch(error) {
    case 6:return 0xc0000008u;
    case 5:return 0xc0000235u;
    case 8:return 0xc0000017u;
    case 87:return 0xc000000du;
    case 998:return 0xc0000005u;
    case 1117:return 0xc0000185u;
    default:return 0xc00000bbu;
    }
}
static uint32_t WR_WIN RtlGetLastNtStatus(void){return wr_current_teb()->last_status;}
static unsigned WR_WIN RtlNtStatusToDosErrorNoTeb(uint32_t status){
    switch(status){case 0:return 0;case 0x103:return 997;case 0x102:return 1460;case 0x80000005u:return 234;
    case 0xc0000003u:case 0xc000000du:return 87;case 0xc0000004u:return 24;
    case 0xc0000005u:return 998;case 0xc0000008u:case 0xc0000235u:return 6;
    case 0xc0000017u:return 8;case 0xc0000022u:return 5;case 0xc0000034u:return 2;
    case 0xc000003au:return 3;case 0xc0000035u:return 183;case 0xc00000bbu:return 50;
    case 0xc0000106u:return 206;case 0xc0000185u:return 1117;case 0xc0000054u:return 33;
    case 0xc0000023u:return 122;case 0xc000017cu:return 1018;case 0xc000014cu:return 1015;case 0xc0000121u:return 5;
    default:return 317;/* ERROR_MR_MID_NOT_FOUND, never an invented success. */}
}
static unsigned WR_WIN RtlNtStatusToDosError(uint32_t status){
    wr_current_teb()->last_status=status;return RtlNtStatusToDosErrorNoTeb(status);
}
static void WR_WIN RtlSetLastWin32ErrorAndNtStatusFromNtStatus(uint32_t status){
    wr_teb *t=wr_current_teb();t->last_status=status;t->last_error=RtlNtStatusToDosError(status);
}
static uint32_t nt_publish(void *out,unsigned capacity,unsigned *used,const void *value,unsigned length){
    if(used && wr_memory_copy(used,&length,4))return 0xc0000005u;
    if(capacity<length)return 0xc0000004u;
    return wr_memory_copy(out,value,length)?0xc0000005u:0;
}
typedef struct {uint32_t status,pad;void *peb;uint64_t affinity;int32_t priority;uint32_t pad2;uint64_t pid,parent;} basic_process;
_Static_assert(sizeof(basic_process)==48,"AMD64 PROCESS_BASIC_INFORMATION");
static uint32_t WR_WIN NtQueryInformationProcess(void *process,unsigned kind,void *out,unsigned capacity,unsigned *used){
    /* Fixed-size classes require an exact length and leave ReturnLength alone
       on mismatch. Variable image-name records publish their required size. */
    if((kind==0 && capacity!=sizeof(basic_process)) || (kind==26 && capacity!=8))return 0xc0000004u;
    int fd=wr_process_capability(process);if(fd<0)return 0xc0000008u;
    vex_process_resource_info_t target;vex_process_resource_req_t query={.size=sizeof(query),.operation=VEX_PROCESS_QUERY,.fd=fd,.out=&target};
    int error=(int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&query);vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);
    if(error)return error==-13 || error==-1?0xc0000022u:0xc0000008u;
    /* Foreign PEBs/architecture cannot be inferred from native PID possession.
       No foreign memory inspection is silently authorized by this adapter. */
    if(target.pid!=wr_current_teb()->pid)return 0xc00000bbu;
    if(kind==0){
        vex_memory_machine_info_t machine;vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_MACHINE,.out=&machine};
        if(vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r) || !machine.online_cpus || machine.online_cpus>64)return 0xc00000bbu;
        basic_process info={.status=target.exited?(uint32_t)target.status:259,.peb=&wr_process_peb,
            .affinity=machine.online_cpus==64?UINT64_MAX:(1ull<<machine.online_cpus)-1,.priority=8,.pid=target.pid,
            .parent=vex_syscall0(VEX_SYS_GETPPID)};
        return nt_publish(out,capacity,used,&info,sizeof(info));
    }
    if(kind==26){uint64_t wow64=0;/* This loader only admits PE32+ AMD64. */return nt_publish(out,capacity,used,&wow64,8);}
    if(kind==27 || kind==43){
        uint16_t name[1024];int n;
        if(kind==43)n=wr_utf16(wr_guest_image(),name,1024);
        else{extern int wr_native_mapped_name(const char *,uint16_t *,size_t);module *exe=wr_module_at(0);n=exe?wr_native_mapped_name(exe->path,name,1024):-1;}
        if(n<0)return 0xc00000bbu;unsigned length=sizeof(wr_unicode)+((unsigned)n+1)*2;
        uint8_t result[sizeof(wr_unicode)+sizeof(name)];wr_unicode header={(uint16_t)(n*2),(uint16_t)(n*2+2),0,(uint16_t *)((uintptr_t)out+sizeof(wr_unicode))};
        memcpy(result,&header,sizeof(header));memcpy(result+sizeof(header),name,(n+1)*2);
        return nt_publish(out,capacity,used,result,length);
    }
    return 0xc0000003u;
}
static uint32_t object_type(void *h,const char **name){
    unsigned tag=(unsigned)(wr_handle_tag((uintptr_t)h));
    if(h==(void *)-1 || tag==0x54){
        unsigned kind=wr_process_handle_kind(h);if(!kind)return 0xc0000008u;*name=kind==1?"Process":"Thread";return 0;
    }
    if(h==(void *)-2 || wr_handle_tag((uintptr_t)h)==0x5a || tag==0x5e){
        unsigned (WR_WIN *id)(void *)=wr_thread_exports("GetThreadId");if(!id(h))return 0xc0000008u;*name="Thread";return 0;
    }
    if(tag==0x57 || tag==0x55){if(!(tag==0x57?wr_file_valid(h):wr_pipe_valid(h)))return 0xc0000008u;*name="File";return 0;}
    if(tag==0x62){unsigned flags;if(!wr_token_flags(h,0,0,&flags))return 0xc0000008u;*name="Token";return 0;}
    if(tag==0x59){*name=wr_object_type(h);return *name?0:0xc0000008u;}
    if(tag==0x58){if(!wr_mapping_valid(h))return 0xc0000008u;*name="Section";return 0;}
    if(tag==0x5d){unsigned flags;if(!wr_job_flags(h,0,0,&flags))return 0xc0000008u;*name="Job";return 0;}
    return 0xc0000008u;
}
static uint32_t WR_WIN NtQueryObject(void *handle,unsigned kind,void *out,unsigned capacity,unsigned *used){
    unsigned saved=wr_current_teb()->last_error;const char *name=0;uint32_t status=object_type(handle,&name);
    if(!status){
        if(kind==2){
            struct {wr_unicode name;uint32_t reserved[22];uint16_t text[32];} info={0};
            int n=wr_utf16(name,info.text,32);info.name=(wr_unicode){(uint16_t)(n*2),(uint16_t)(n*2+2),0,(uint16_t *)((uintptr_t)out+104)};
            status=nt_publish(out,capacity,used,&info,104+(n+1)*2);
        }else if(kind==4){
            unsigned flags=0;int (WR_WIN *get)(void *,unsigned *)=wr_process_exports("GetHandleInformation");
            if(capacity!=2)status=0xc0000004u;
            else if(get(handle,&flags)){uint8_t info[2]={(flags&1)!=0,(flags&2)!=0};status=nt_publish(out,capacity,used,info,2);}
            else status=0xc00000bbu;
        }else status=kind==0 || kind==1?0xc00000bbu:0xc0000003u;
    }
    wr_current_teb()->last_error=saved;return status;
}
void *wr_nt_core_exports(const char *name) {
    void *registry=wr_registry_exports(name);if(registry)return registry;
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(NtClose);E(RtlInitUnicodeString);E(RtlInitUnicodeStringEx);E(RtlFreeUnicodeString);
    E(RtlGetLastNtStatus);E(RtlNtStatusToDosError);E(RtlNtStatusToDosErrorNoTeb);E(RtlSetLastWin32ErrorAndNtStatusFromNtStatus);
    E(NtQueryInformationProcess);E(NtQueryObject);
#undef E
    return 0;
}
