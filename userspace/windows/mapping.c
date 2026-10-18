#include "windows.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
/* Pagefile-backed sections reuse native SHM capabilities. Section handles and
 * views have independent lifetime: native process_memory retains each view.
 * File-backed sections, named/inherited sections and executable sections are
 * refused, not substituted with private anonymous memory. */
static struct {uint64_t cap,size;uint32_t generation,prot;} sections[256];
static struct {void *base;size_t length;} views[256];
static wr_mutex gate;
unsigned wr_mapping_handle_count(void){wr_lock(&gate);unsigned count=0;for(unsigned i=0;i<256;i++)if(sections[i].cap)count++;wr_unlock(&gate);return count;}
static int section(void *h) {
    uintptr_t v=(uintptr_t)h;unsigned i=(unsigned)(wr_handle_slot(v));
    if((wr_handle_tag(v))!=0x58 || wr_handle_generation(v)!=sections[i].generation || !sections[i].cap)return -1;
    return (int)i;
}
int wr_mapping_close(void *h) {
    if((wr_handle_tag((uintptr_t)h))!=0x58)return -1;
    wr_lock(&gate);int i=section(h),ok=0;
    if(i>=0 && !vex_syscall1(VEX_SYS_SHM_CLOSE,sections[i].cap)) {sections[i].cap=0;ok=1;}
    wr_unlock(&gate);if(!ok)wr_current_teb()->last_error=i<0?6:5;return ok;
}
int wr_mapping_valid(void *h){wr_lock(&gate);int valid=section(h)>=0;wr_unlock(&gate);return valid;}
static void *WR_WIN CreateFileMappingW(void *file,void *security,unsigned protection,unsigned high,unsigned low,const uint16_t *name) {
    if(file!=(void *)-1 || security || name) {wr_current_teb()->last_error=50;return 0;}
    unsigned p=protection&~0x08000000u;
    if(p!=2 && p!=4) {wr_current_teb()->last_error=(p==0x40 || p==0x20)?5:87;return 0;}
    uint64_t size=((uint64_t)high<<32)|low;
    if(!size || size>0x00007ffffffff000ull) {wr_current_teb()->last_error=87;return 0;}
    uint8_t random[16];
    if(wr_random(random,sizeof(random))) {wr_current_teb()->last_error=8;return 0;}
    const char *hex="0123456789abcdef";
    /* 12 + 32 hex characters plus NUL. */
    char unique[45];memcpy(unique,"win-section-",12);
    for(unsigned k=0;k<16;k++) {unique[12+k*2]=hex[random[k]>>4];unique[13+k*2]=hex[random[k]&15];}unique[44]=0;
    wr_lock(&gate);unsigned i=0;while(i<256 && (sections[i].cap || sections[i].generation>=WR_HANDLE_GENERATION_MAX))i++;
    if(i==256) {wr_unlock(&gate);wr_current_teb()->last_error=8;return 0;}
    vex_shm_create_req_t r={.name=unique,.size=(size_t)size,.prot=PE_R|(p==4?PE_W:0)};
    if(vex_syscall1(VEX_SYS_SHM_CREATE,(uintptr_t)&r)) {wr_unlock(&gate);wr_current_teb()->last_error=8;return 0;}
    sections[i].cap=r.handle;sections[i].size=size;sections[i].prot=(unsigned)r.prot;
    if(!++sections[i].generation)++sections[i].generation;
    void *h=(void *)(wr_handle_pack(0x58,sections[i].generation,i));
    wr_unlock(&gate);return h;
}
static void *WR_WIN MapViewOfFile(void *h,unsigned access,unsigned high,unsigned low,size_t count) {
    wr_lock(&gate);int i=section(h);unsigned slot=0;while(slot<256 && views[slot].base)slot++;
    unsigned error=0;void *base=0;
    if(i<0) {error=6;goto out;}if(slot==256) {error=8;goto out;}
    unsigned prot;
    if(access==4)prot=PE_R;
    else if(access==2 || access==6 || access==0xf001f)prot=PE_R|PE_W;
    else {error=(access&32)?5:50;goto out;}
    uint64_t offset=((uint64_t)high<<32)|low;
    if((offset&65535) || offset>=sections[i].size || (prot&~sections[i].prot)) {error=(prot&~sections[i].prot)?5:87;goto out;}
    uint64_t n=count?count:sections[i].size-offset;
    if(!n || n>sections[i].size-offset || n>SIZE_MAX-4095) {error=87;goto out;}
    size_t length=((size_t)n+4095)&~(size_t)4095;
    void *hint=wr_map_image(0,length);
    if(!hint) {error=8;goto out;}if(wr_unmap(hint,length)) {error=487;goto out;}
    vex_shm_map_req_t r={.handle=sections[i].cap,.preferred_addr=hint,.length=length,.offset=(size_t)offset,.prot=(int)prot};
    if(vex_syscall1(VEX_SYS_SHM_MAP,(uintptr_t)&r)) {error=8;goto out;}
    base=r.mapped_addr;views[slot].base=base;views[slot].length=length;
out:
    wr_unlock(&gate);if(error)wr_current_teb()->last_error=error;return base;
}
static int WR_WIN UnmapViewOfFile(void *base) {
    wr_lock(&gate);unsigned i=0;while(i<256 && views[i].base!=base)i++;
    int ok=i<256 && base && !wr_unmap(base,views[i].length);
    if(ok)views[i].base=0;
    wr_unlock(&gate);if(!ok)wr_current_teb()->last_error=487;return ok;
}
static int WR_WIN FlushViewOfFile(const void *address,size_t bytes){
    /* Only pagefile-backed sections are currently supported. Their content is
       already the shared volatile native backing: there is no file to write.
       Serialize against unmap, validate the entire requested view range, and
       publish CPU writes. This never substitutes a fence for a disk flush. */
    uintptr_t at=(uintptr_t)address;unsigned error=487;
    wr_lock(&gate);
    for(unsigned i=0;i<256;i++)if(views[i].base && at>=(uintptr_t)views[i].base &&
        at-(uintptr_t)views[i].base<views[i].length){
        size_t remain=views[i].length-(at-(uintptr_t)views[i].base);
        if(bytes>remain)error=87;
        else {__atomic_thread_fence(__ATOMIC_SEQ_CST);error=0;}
        break;
    }
    wr_unlock(&gate);if(error)wr_current_teb()->last_error=error;return !error;
}
void *wr_mapping_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(CreateFileMappingW);E(MapViewOfFile);E(UnmapViewOfFile);E(FlushViewOfFile);
#undef E
    return 0;
}
