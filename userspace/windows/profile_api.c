#include "windows.h"
#include "process.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
/* Version of the Windows contract selected by this userspace personality.
 * This is NOT a claim to run a Microsoft kernel or to implement every 10.0
 * feature. Build zero deliberately does not impersonate a Windows release. */
typedef struct {uint32_t size,major,minor,build,platform;uint16_t service[128];
    uint16_t service_major,service_minor,suite;uint8_t product,reserved;} os_version;
static const os_version profile={sizeof(os_version),10,0,0,2,{0},0,0,0,1,0};
static int failure(unsigned e){wr_current_teb()->last_error=e;return 0;}
static int WR_WIN SetProcessMitigationPolicy(unsigned kind,const void *buffer,size_t length) {
    /* DEP is mandatory in this x64 personality. No flags are merely recorded
       as security guarantees: unsupported mitigation classes are rejected. */
    if(kind!=0)return failure(50);
    struct {uint32_t flags;uint8_t permanent,pad[3];} policy;
    if(length!=sizeof(policy) || wr_memory_copy(&policy,buffer,sizeof(policy)) || policy.flags&~3u)return failure(87);
    if(!(policy.flags&1u))return failure(5);
    vex_memory_machine_info_t info;vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_MACHINE,.out=&info};
    if(vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r) || !(info.flags&VEX_MEMORY_MACHINE_NX))return failure(50);
    /* ATL executable-data thunks are never enabled; both requested values
       leave that stricter native protection intact. */
    return 1;
}
static int WR_WIN GetProcessMitigationPolicy(void *process,unsigned kind,void *buffer,size_t length){
    int fd=wr_process_capability(process);vex_process_resource_info_t target;
    if(fd<0)return failure(6);
    vex_process_resource_req_t query={.size=sizeof(query),.operation=VEX_PROCESS_QUERY,.fd=fd,.out=&target};
    int error=(int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&query);
    vex_syscall1(VEX_SYS_CLOSE,(uintptr_t)fd);
    if(error)return failure(error==-13?5:6);
    if(kind!=0)return failure(50);
    struct {uint32_t flags;uint8_t permanent,pad[3];} policy={3,1,{0}};
    if(length!=sizeof(policy))return failure(87);
    vex_memory_machine_info_t info;vex_memory_view_req_t r={.size=sizeof(r),.operation=VEX_MEMORY_VIEW_MACHINE,.out=&info};
    if(vex_syscall1(VEX_SYS_MEMORY_VIEW,(uintptr_t)&r) || !(info.flags&VEX_MEMORY_MACHINE_NX))return failure(50);
    return wr_memory_copy(buffer,&policy,sizeof(policy))?failure(998):1;
}
static int WR_WIN GetVersionExW(os_version *out) {
    uint32_t size;if(!out || wr_memory_copy(&size,out,4) || (size!=276 && size!=284))return failure(87);
    os_version value=profile;value.size=size;
    return wr_memory_copy(out,&value,size)?failure(998):1;
}
static int WR_WIN GetProductInfo(unsigned major,unsigned minor,unsigned sp_major,unsigned sp_minor,unsigned *out) {
    (void)minor;(void)sp_major;(void)sp_minor;
    if(!out || major<6)return failure(87);
    /* KyIlOS has no Microsoft product edition/licence. PRODUCT_UNDEFINED is
       the documented no-identifiable-product value, not Professional/Server. */
    unsigned product=0;return wr_memory_copy(out,&product,4)?failure(998):1;
}
static uint64_t WR_WIN VerSetConditionMask(uint64_t mask,unsigned types,uint8_t condition) {
    condition&=7;for(unsigned i=0;i<8;i++)if(types&(1u<<i))mask|=(uint64_t)condition<<(3*i);
    return mask;
}
static int compare(unsigned actual,unsigned wanted,unsigned op) {
    switch(op){case 1:return actual==wanted;case 2:return actual>wanted;case 3:return actual>=wanted;
        case 4:return actual<wanted;case 5:return actual<=wanted;default:return -1;}
}
static int WR_WIN VerifyVersionInfoW(const os_version *in,unsigned types,uint64_t mask) {
    os_version wanted;if(!in || wr_memory_copy(&wanted,in,sizeof(wanted)) || wanted.size!=sizeof(wanted) || !types || types&~255u)return failure(87);
    unsigned actual[8]={profile.minor,profile.major,profile.build,profile.platform,profile.service_minor,profile.service_major,profile.suite,profile.product};
    unsigned query[8]={wanted.minor,wanted.major,wanted.build,wanted.platform,wanted.service_minor,wanted.service_major,wanted.suite,wanted.product};
    /* Version tuple is lexicographic; lower fields are examined only when
       every requested higher component is equal. Build is independent. */
    const unsigned order[8]={1,0,5,4,2,3,6,7};int tuple_decided=0;
    for(unsigned j=0;j<8;j++){unsigned i=order[j];if(!(types&(1u<<i)))continue;
        unsigned op=(unsigned)(mask>>(3*i))&7;
        if(i==6){if(op!=6 && op!=7)return failure(87);if(op==6?(actual[i]&query[i])!=query[i]:(query[i] && !(actual[i]&query[i])))return failure(1150);continue;}
        if(!op || op>5 || ((i==3 || i==7) && op!=1))return failure(87);
        if(j<4 && tuple_decided)continue;
        if(j<4 && actual[i]==query[i] && op!=1) {
            int lower=0;for(unsigned k=j+1;k<4;k++)if(types&(1u<<order[k]))lower=1;
            if(lower)continue;
        }
        int result=compare(actual[i],query[i],op);
        if(!result)return failure(1150);
        if(j<4 && actual[i]!=query[i])tuple_decided=1;
    }return 1;
}
void *wr_profile_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(SetProcessMitigationPolicy);E(GetProcessMitigationPolicy);E(GetVersionExW);E(GetProductInfo);E(VerSetConditionMask);E(VerifyVersionInfoW);
#undef E
    return 0;
}
