#include "windows.h"
static uintptr_t native_targets[1024];
static unsigned native_count;
void *wr_cfg_register(void *p) {
    if(!p)return 0;
    for(unsigned i=0;i<native_count;i++)if(native_targets[i]==(uintptr_t)p)return p;
    if(native_count>=1024) {wr_log("windows: native CFG target budget exceeded\n");wr_exit(126);}
    native_targets[native_count++]=(uintptr_t)p;return p;
}
static int image_va(module *m,uintptr_t p,size_t size,unsigned access) {
    uintptr_t b=(uintptr_t)m->base;
    if(p<b || !pe_span(p-b,size,m->image.size))return 0;
    for(uint64_t off=(p-b)&~4095ull;off<p-b+size;off+=4096)
        if((pe_page_access(&m->image,(uint32_t)off)&access)!=access)return 0;
    return 1;
}
extern void wr_cfg_check(void),wr_cfg_dispatch(void);
const char *wr_load_config(module *m) {
    uint32_t r=m->image.dirs[10][0],n=m->image.dirs[10][1];if(!n)return 0;
    if(n<4)return "truncated load-config";
    uint8_t *d=m->base+r;uint32_t size=pe_u32(d);
    if(size>n || size<0x60 || size>0x140)return "unsupported load-config size";
    /* Behavioural policy fields are never silently dropped. Zero/obsolete
       advisory fields need no OS action. AMD64 does not use x86 SafeSEH. */
    if(pe_u32(d+12) || pe_u32(d+16) || pe_u64(d+0x28) || pe_u64(d+0x40) ||
        pe_u32(d+0x48) || pe_u16(d+0x4e) || pe_u64(d+0x50))return "unsupported load-config policy";
    uintptr_t cookie=pe_u64(d+0x58);
    if(cookie) {
        if(!image_va(m,cookie,8,PE_W) || (cookie&7))return "invalid security cookie address";
        uint64_t random;
        do {if(wr_random(&random,8))return "security cookie RNG failed";random&=0xffffffffffffull;}
        while(!random || random==0x2b992ddfa232ull);
        pe_put64((void *)cookie,random);
    }
    if(size>=0x70 && (pe_u64(d+0x60) || pe_u64(d+0x68)))return "AMD64 SafeSEH fields invalid";
    if(size<0x94)return 0;
    uint32_t flags=pe_u32(d+0x90);
    /* CF instrumented, GFIDS present, longjump table present (empty only).
       XFG/EH-continuation/export suppression/RF are explicitly not announced. */
    if(flags&~0xf0010500u)return "unsupported GuardFlags mitigation";
    m->guard_flags=flags;m->guard_stride=4+(flags>>28);
    uintptr_t table=pe_u64(d+0x80);uint64_t count=pe_u64(d+0x88);
    if(count>m->image.size/m->guard_stride || (count && !image_va(m,table,(size_t)count*m->guard_stride,PE_R)))return "invalid CFG function table";
    m->guard_functions=(void *)table;m->guard_count=count;
    uint32_t previous=0;
    for(uint64_t i=0;i<count;i++) {
        uint8_t *v=m->guard_functions+i*m->guard_stride;uint32_t target=pe_u32(v);
        if((i && target<=previous) || !image_va(m,(uintptr_t)m->base+target,1,PE_X))return "invalid CFG function RVA/order";
        for(unsigned j=4;j<m->guard_stride;j++)if(v[j])return "CFG target metadata not implemented";
        previous=target;
    }
    for(unsigned at=0x70;at<=0x78;at+=8) {
        uintptr_t slot=pe_u64(d+at);
        if(slot) {
            if(!image_va(m,slot,8,PE_R) || (slot&7))return "invalid CFG function slot";
            pe_put64((void *)slot,(uintptr_t)(at==0x70?wr_cfg_check:wr_cfg_dispatch));
        } else if(flags&0x100)return "instrumented CFG without guard slots";
    }
    if(size>=0xb0) {
        uintptr_t iat=pe_u64(d+0xa0);uint64_t niat=pe_u64(d+0xa8);
        if(niat>m->image.size/4 || (niat && !image_va(m,iat,(size_t)niat*4,PE_R)))return "invalid address-taken IAT table";
        for(uint64_t i=0;i<niat;i++)if(!image_va(m,(uintptr_t)m->base+pe_u32((uint8_t *)iat+4*i),8,PE_R))return "invalid address-taken IAT RVA";
    }
    /* Nonzero newer security mechanisms require their own implementation.
       XFG fallback slots may exist without XFG_ENABLED; they are untouched
       and no XFG policy is reported. CastGuard mode slot contains zero policy. */
    for(unsigned at=0xb0;at+8<=size;at+=8) {
        if(at==0x118 || at==0x120 || at==0x128 || at==0x130) {
            uintptr_t slot=pe_u64(d+at);
            if(slot && !image_va(m,slot,8,PE_R))return "invalid dormant guard slot";
            continue;
        }
        if(pe_u64(d+at))return "unsupported load-config extension";
    }
    return 0;
}
int wr_cfg_allowed(uintptr_t target) {
    for(unsigned i=0;i<native_count;i++)if(native_targets[i]==target)return 1;
    for(unsigned i=0;i<wr_modules_count();i++) {
        module *m=wr_module_at(i);if(!image_va(m,target,1,PE_X))continue;
        if(!(m->guard_flags&0x100))return !(target&15); /* Windows legacy-image granularity. */
        uint32_t rva=(uint32_t)(target-(uintptr_t)m->base);
        uint64_t lo=0,hi=m->guard_count;
        while(lo<hi) {uint64_t mid=lo+(hi-lo)/2;uint32_t v=pe_u32(m->guard_functions+mid*m->guard_stride);if(v<rva)lo=mid+1;else hi=mid;}
        if(lo<m->guard_count && pe_u32(m->guard_functions+lo*m->guard_stride)==rva)return 1;
    }
    return 0;
}
_Noreturn void wr_cfg_failure(void) {wr_log("windows: CFG invalid indirect call target\n");wr_exit(0xc0000409u);}
