#include "pe_image.h"
int pe_span(uint64_t at, uint64_t size, uint64_t limit) {
    return at <= limit && size <= limit - at;
}
uint16_t pe_u16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
uint32_t pe_u32(const uint8_t *p) { return pe_u16(p) | (uint32_t)pe_u16(p+2) << 16; }
uint64_t pe_u64(const uint8_t *p) { return pe_u32(p) | (uint64_t)pe_u32(p+4) << 32; }
void pe_put64(uint8_t *p, uint64_t v) { for(unsigned i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
unsigned pe_page_access(const pe_image *im, uint32_t page) {
    unsigned f=page < im->headers ? PE_R : 0;
    for(unsigned i=0;i<im->count;i++) {
        const pe_section *s=&im->sections[i];
        if(s->size && page < (uint64_t)s->rva+s->size && (uint64_t)page+PE_PAGE > s->rva)
            f |= s->flags;
    }
    return f;
}
const char *pe_parse(const uint8_t *f, size_t n, pe_image *im) {
    if(n<64 || pe_u16(f)!=0x5a4d) return "invalid DOS header";
    uint32_t p=pe_u32(f+60);
    if(!pe_span(p,24,n) || pe_u32(f+p)!=0x4550) return "invalid PE signature";
    const uint8_t *c=f+p+4;
    unsigned count=pe_u16(c+2), opt=pe_u16(c+16);
    if(pe_u16(c)!=0x8664 || !count || count>PE_MAX_SECTIONS || !(pe_u16(c+18)&2))
        return "not an AMD64 executable image";
    uint64_t o=(uint64_t)p+24, table=o+opt;
    if(opt<112 || !pe_span(o,opt,n) || pe_u16(f+o)!=0x20b || !pe_span(table,count*40,n))
        return "invalid PE32+ optional/section headers";
    const uint8_t *h=f+o;
    im->size=pe_u32(h+56); im->headers=pe_u32(h+60); im->entry=pe_u32(h+16);
    im->preferred=pe_u64(h+24); im->count=count; im->subsystem=pe_u16(h+68);
    im->stack_reserve=pe_u64(h+72);im->stack_commit=pe_u64(h+80);
    im->dll=(pe_u16(c+18)&0x2000)!=0;
    uint32_t sa=pe_u32(h+32), fa=pe_u32(h+36), nd=pe_u32(h+108);
    if(sa<PE_PAGE || (sa&(sa-1)) || fa<512 || fa>65536 || (fa&(fa-1)) || sa<fa ||
       !im->size || im->size%sa || !im->headers || im->headers%fa ||
       im->headers>im->size || im->headers>n || table+count*40>im->headers ||
       im->preferred%65536 || im->preferred>=0x800000000000ull ||
       !pe_span(im->preferred,im->size,0x800000000000ull) ||
       (im->subsystem!=2 && im->subsystem!=3) || (im->entry>=im->size) || (!im->dll && !im->entry))
        return "invalid image alignment/size/base/subsystem";
    if(nd>16 || !pe_span(112,(uint64_t)nd*8,opt)) return "invalid directory table";
    for(unsigned i=0;i<16;i++) {
        im->dirs[i][0]=i<nd?pe_u32(h+112+8*i):0;
        im->dirs[i][1]=i<nd?pe_u32(h+116+8*i):0;
        if((im->dirs[i][0]==0)!=(im->dirs[i][1]==0) ||
           (im->dirs[i][0] && !pe_span(im->dirs[i][0],im->dirs[i][1],i==4?n:im->size)))
            return "directory outside image/file";
    }
    for(unsigned i=0;i<count;i++) {
        const uint8_t *s=f+table+40*i; pe_section *d=&im->sections[i];
        uint32_t vs=pe_u32(s+8), raw=pe_u32(s+16), flags=pe_u32(s+36);
        d->rva=pe_u32(s+12); d->raw=pe_u32(s+20); d->raw_size=raw;
        d->size=vs>raw?vs:raw;
        d->flags=((flags&0x40000000)?PE_R:0)|((flags&0x80000000)?PE_W:0)|((flags&0x20000000)?PE_X:0);
        if(d->rva%sa || d->rva<im->headers || !pe_span(d->rva,d->size,im->size) ||
           (raw && (raw%fa || d->raw%fa || d->raw<im->headers || !pe_span(d->raw,raw,n))))
            return "section outside image/file or misaligned";
        if((d->flags&(PE_W|PE_X))==(PE_W|PE_X)) return "section requires RWX";
        for(unsigned j=0;j<i;j++) {
            pe_section *b=&im->sections[j];
            if(d->size && b->size && d->rva<(uint64_t)b->rva+b->size && b->rva<(uint64_t)d->rva+d->size)
                return "overlapping virtual sections";
        }
    }
    if(im->entry) {
        int executable=0;
        for(unsigned i=0;i<count;i++) {
            const pe_section *s=&im->sections[i];
            if((s->flags&PE_X) && im->entry>=s->rva && im->entry<(uint64_t)s->rva+s->size)executable=1;
        }
        if(!executable)return "entry is not inside an executable section";
    }
    return 0;
}
const char *pe_relocate(uint8_t *b, const pe_image *im, uint64_t base) {
    uint32_t r=im->dirs[5][0], n=im->dirs[5][1];
    if(base!=im->preferred && !n) return "rebasing requires relocation directory";
    uint64_t delta=base-im->preferred;
    for(uint64_t at=r; at<(uint64_t)r+n;) {
        if(!pe_span(at,8,(uint64_t)r+n)) return "truncated relocation block";
        uint32_t page=pe_u32(b+at), size=pe_u32(b+at+4);
        if(size<8 || size%2 || page%PE_PAGE || !pe_span(at,size,(uint64_t)r+n))
            return "invalid relocation block";
        for(unsigned k=8;k<size;k+=2) {
            unsigned entry=pe_u16(b+at+k), type=entry>>12;
            if(!type) continue;
            uint64_t target=(uint64_t)page+(entry&4095);
            if(type!=10) return "unsupported AMD64 relocation";
            if(!pe_span(target,8,im->size) ||
               (target<(uint64_t)r+n && target+8>r)) return "invalid relocation target";
            pe_put64(b+target,pe_u64(b+target)+delta);
        }
        at+=size;
    }
    return 0;
}
