#include "exception_abi.h"
/* Static AMD64 PE function tables, independent of any application. All reads
 * go through the native current-address-space transaction, including stacks. */
static int read_at(void *out,uintptr_t address,size_t size){return wr_memory_copy(out,(const void *)address,size)==0;}
static uint64_t *reg(win_context *c,unsigned n) {
    switch(n){case 0:return &c->rax;case 1:return &c->rcx;case 2:return &c->rdx;case 3:return &c->rbx;
    case 4:return &c->rsp;case 5:return &c->rbp;case 6:return &c->rsi;case 7:return &c->rdi;
    case 8:return &c->r8;case 9:return &c->r9;case 10:return &c->r10;case 11:return &c->r11;
    case 12:return &c->r12;case 13:return &c->r13;case 14:return &c->r14;case 15:return &c->r15;}return 0;
}
static int nonvolatile(unsigned n){return n==3 || n==5 || n==6 || n==7 || n>=12;}
static int add(uint64_t *value,uint64_t n){if(*value>UINT64_MAX-n)return 0;*value+=n;return 1;}
static module *containing(uintptr_t address) {
    for(unsigned i=0;i<wr_modules_count();i++){module *m=wr_module_at(i);if(m->base && address>=(uintptr_t)m->base && pe_span(address-(uintptr_t)m->base,1,m->image.size))return m;}
    return 0;
}
static void *WR_WIN RtlPcToFileHeader(void *pc,void **base) {
    wr_loader_lock();module *m=containing((uintptr_t)pc);void *result=m?m->base:0;wr_loader_unlock();
    if(base && wr_memory_copy(base,&result,sizeof(result)))return 0;return result;
}
static runtime_function *lookup(uintptr_t pc,uint64_t *base) {
    module *m=containing(pc);if(!m)return 0;*base=(uintptr_t)m->base;
    unsigned rva=m->image.dirs[3][0],bytes=m->image.dirs[3][1];
    if(!bytes)return 0;
    if(bytes%12 || !pe_span(rva,bytes,m->image.size))return 0;
    uint32_t offset=(uint32_t)(pc-*base),low=0,high=bytes/12;
    while(low<high) {
        unsigned middle=low+(high-low)/2;runtime_function f;
        if(!read_at(&f,*base+rva+12ull*middle,sizeof(f)) || f.begin>=f.end || f.end>m->image.size)return 0;
        if(offset<f.begin)high=middle;else if(offset>=f.end)low=middle+1;
        else return (void *)(*base+rva+12ull*middle);
    }return 0;
}
static runtime_function *WR_WIN RtlLookupFunctionEntry(uint64_t pc,uint64_t *base,void *history) {
    (void)history;uint64_t image=0;wr_loader_lock();runtime_function *result=lookup(pc,&image);wr_loader_unlock();
    if(base && wr_memory_copy(base,&image,sizeof(image)))return 0;return result;
}
/* Decode only the ABI's legal epilog forms. Validate the entire sequence on a
 * private context before committing; arbitrary body instructions aren't undone. */
static int epilog(uint64_t base,runtime_function f,win_context *context,context_pointers *pointers) {
    win_context c=*context;context_pointers locations={0};uintptr_t pc=c.rip;
    for(unsigned step=0;step<32;step++) {
        unsigned char b[8]={0};unsigned available=(unsigned)((base+f.end-pc)<8?(base+f.end-pc):8);
        if(pc<base+f.begin || pc>=base+f.end || !available || !read_at(b,pc,available))return 0;
        unsigned n=0;
        if(available>=4 && b[0]==0x48 && b[1]==0x83 && b[2]==0xc4){if((int8_t)b[3]<0 || !add(&c.rsp,b[3]))return 0;n=4;}
        else if(available>=7 && b[0]==0x48 && b[1]==0x81 && b[2]==0xc4){uint32_t amount=pe_u32(b+3);if(amount>>31 || !add(&c.rsp,amount))return 0;n=7;}
        else if(available>=4 && (b[0]&0xfe)==0x48 && b[1]==0x8d && ((b[2]>>3)&7)==4 && (b[2]&7)!=4 && (b[2]>>6)==1) {
            unsigned source=(b[2]&7)|((b[0]&1)?8:0);if(!nonvolatile(source))return 0;
            int64_t displacement=(int8_t)b[3];uint64_t value=*reg(&c,source);
            if((displacement<0 && value<(uint64_t)-displacement) || (displacement>0 && value>UINT64_MAX-(uint64_t)displacement))return 0;
            c.rsp=value+displacement;n=4;
        }else if(available>=7 && (b[0]&0xfe)==0x48 && b[1]==0x8d && ((b[2]>>3)&7)==4 && (b[2]&7)!=4 && (b[2]>>6)==2) {
            unsigned source=(b[2]&7)|((b[0]&1)?8:0);if(!nonvolatile(source))return 0;
            int64_t displacement=(int32_t)pe_u32(b+3);uint64_t value=*reg(&c,source);
            if((displacement<0 && value<(uint64_t)-displacement) || (displacement>0 && value>UINT64_MAX-(uint64_t)displacement))return 0;
            c.rsp=value+displacement;n=7;
        }else if((b[0]>=0x58 && b[0]<=0x5f) || (available>=2 && b[0]==0x41 && b[1]>=0x58 && b[1]<=0x5f)) {
            unsigned extended=b[0]==0x41,number=(b[extended]-0x58)+(extended?8:0);
            if(!nonvolatile(number) || !read_at(reg(&c,number),c.rsp,8))return 0;
            locations.integer[number]=(void *)(uintptr_t)c.rsp;if(!add(&c.rsp,8))return 0;n=extended?2:1;
        }else {
            unsigned extra=0;int finish=b[0]==0xc3;
            if(available>=3 && b[0]==0xc2){extra=pe_u16(b+1);finish=1;}
            if(available>=2 && b[0]==0xf3 && b[1]==0xc3)finish=1;
            if(available>=5 && b[0]==0xe9){int64_t target=(int64_t)pc+5+(int32_t)pe_u32(b+1);finish=target<(int64_t)(base+f.begin) || target>=(int64_t)(base+f.end);}
            if(available>=2 && b[0]==0xff && (b[1]&0x38)==0x20 && (b[1]>>6)==0)finish=1;
            if(!finish || !read_at(&c.rip,c.rsp,8) || !add(&c.rsp,8+extra))return 0;
            *context=c;if(pointers)*pointers=locations;return 1;
        }
        pc+=n;
    }return 0;
}
static int step_unwind(unsigned handler_type,uint64_t base,uint64_t pc,runtime_function f,win_context *c,
    void **handler_data,uint64_t *establisher,context_pointers *locations,void **handler) {
    *handler=0;*handler_data=0;*establisher=c->rsp;if(locations)memset(locations,0,sizeof(*locations));
    if(f.begin>=f.end || base>UINT64_MAX-f.end || pc<base+f.begin || pc>=base+f.end)return 0;
    for(unsigned chain=0;chain<64;chain++) {
        uint8_t h[4],codes[512];if(base>UINT64_MAX-f.unwind || (f.unwind&3) || !read_at(h,base+f.unwind,4))return 0;
        unsigned version=h[0]&7,flags=h[0]>>3,count=h[2],frame=h[3]&15,frame_offset=(h[3]>>4)*16;
        if((version!=1 && version!=2) || (flags&~7u) || ((flags&4) && (flags&3)) || (frame && !nonvolatile(frame)))return 0;
        if(count && !read_at(codes,base+f.unwind+4,count*2))return 0;
        unsigned first=0,epi_size=0,inside_epilog=0;
        if(version==2 && count && (codes[1]&15)==6) {
            epi_size=codes[0];if(!epi_size || epi_size>f.end-f.begin || (codes[1]>>4)>1)return 0;
            if(codes[1]>>4)inside_epilog=pc>=base+f.end-epi_size;
            first=1;
            while(first<count && (codes[2*first+1]&15)==6) {
                unsigned offset=codes[2*first]+((unsigned)(codes[2*first+1]>>4)<<8);
                if(offset && (offset<epi_size || offset>f.end-f.begin))return 0;
                if(offset && pc>=base+f.end-offset && pc<base+f.end-offset+epi_size)inside_epilog=1;
                first++;
            }
        }
        uint64_t prolog_offset=pc-(base+f.begin);
        int in_prolog=prolog_offset<h[1];
        if(!chain && !in_prolog && (version==1 || inside_epilog)) {
            if(epilog(base,f,c,locations)){*establisher=c->rsp-8;return 1;}
            if(inside_epilog)return 0;
        }
        uint64_t frame_base=c->rsp;
        if(frame) {
            int established=!in_prolog;
            for(unsigned i=first;i<count;i++)if((codes[2*i+1]&15)==3 && codes[2*i]<=prolog_offset)established=1;
            if(established){if(*reg(c,frame)<frame_offset)return 0;frame_base=*reg(c,frame)-frame_offset;}
        }
        if(!chain)*establisher=frame_base;
        unsigned machine_frame=0;
        for(unsigned i=first;i<count;) {
            unsigned offset=codes[2*i],op=codes[2*i+1]&15,info=codes[2*i+1]>>4,slots=1;
            uint64_t amount=0;
            if(op==1)slots=info==0?2:(info==1?3:0);
            else if(op==4 || op==8)slots=2;else if(op==5 || op==9)slots=3;
            if(!slots || i+slots>count)return 0;
            if(slots==2)amount=pe_u16(codes+2*(i+1));else if(slots==3)amount=pe_u32(codes+2*(i+1));
            if(chain || !in_prolog || offset<=prolog_offset) {
                uint64_t address;
                switch(op) {
                case 0:
                    if(!nonvolatile(info) || !read_at(reg(c,info),c->rsp,8))return 0;
                    if(locations)locations->integer[info]=(void *)(uintptr_t)c->rsp;
                    if(!add(&c->rsp,8))return 0;break;
                case 1:if(info==0)amount*=8;if(!add(&c->rsp,amount))return 0;break;
                case 2:if(!add(&c->rsp,info*8+8))return 0;break;
                case 3:if(info || !frame || *reg(c,frame)<frame_offset)return 0;c->rsp=*reg(c,frame)-frame_offset;break;
                case 4:case 5:
                    if(!nonvolatile(info))return 0;if(op==4)amount*=8;address=frame_base;if(!add(&address,amount) || !read_at(reg(c,info),address,8))return 0;
                    if(locations)locations->integer[info]=(void *)(uintptr_t)address;break;
                case 8:case 9:
                    if(info<6)return 0;if(op==8)amount*=16;address=frame_base;if(!add(&address,amount) || !read_at(c->fp+160+16*info,address,16))return 0;
                    if(locations)locations->floating[info]=(void *)(uintptr_t)address;break;
                case 10: {
                    if(info>1)return 0;uint64_t hardware[5];address=c->rsp;
                    if(!add(&address,info*8) || !read_at(hardware,address,sizeof(hardware)))return 0;
                    c->rip=hardware[0];c->cs=(uint16_t)hardware[1];c->eflags=(uint32_t)hardware[2];c->rsp=hardware[3];c->ss=(uint16_t)hardware[4];machine_frame=1;break;
                }
                default:return 0;
                }
            }
            i+=slots;
        }
        uint64_t tail=base+f.unwind+4+2*((count+1)&~1u);
        if(flags&4){if(!read_at(&f,tail,sizeof(f)))return 0;continue;}
        if((flags&handler_type) && !in_prolog) {
            uint32_t rva;if(!read_at(&rva,tail,4) || !rva || base>UINT64_MAX-rva)return 0;
            vex_memory_view_info_t target;if(wr_memory_query(base+rva,&target) || !(target.permissions&4))return 0;
            *handler=(void *)(uintptr_t)(base+rva);*handler_data=(void *)(uintptr_t)(tail+4);
        }
        if(!machine_frame){if(!read_at(&c->rip,c->rsp,8) || !add(&c->rsp,8))return 0;}
        return 1;
    }return 0;
}
static void *WR_WIN RtlVirtualUnwind(unsigned type,uint64_t base,uint64_t pc,runtime_function *entry,win_context *context,
    void **data,uint64_t *frame,context_pointers *pointers) {
    win_context c;runtime_function f;context_pointers places;void *handler=0,*payload=0;uint64_t established=0;
    if(type&~3u || !read_at(&c,(uintptr_t)context,sizeof(c)))wr_exit(0xc00000ffu);
    if(!entry) {
        established=c.rsp;memset(&places,0,sizeof(places));
        if(!read_at(&c.rip,c.rsp,8) || !add(&c.rsp,8))wr_exit(0xc00000ffu);
    }else if(!read_at(&f,(uintptr_t)entry,sizeof(f)) || !step_unwind(type,base,pc,f,&c,&payload,&established,&places,&handler)) {
        wr_log("windows-unwind: invalid/unsupported function metadata\n");wr_exit(0xc00000ffu);
    }
    if((data && wr_memory_copy(data,&payload,sizeof(payload))) || (frame && wr_memory_copy(frame,&established,8)) ||
       (pointers && wr_memory_copy(pointers,&places,sizeof(places))) || wr_memory_copy(context,&c,sizeof(c)))wr_exit(0xc0000005u);
    return handler;
}
unsigned short wr_walk_backtrace(win_context *c,unsigned skip,unsigned maximum,void **out,unsigned *hash) {
    unsigned count=0,value=0;wr_loader_lock();
    for(uint64_t walked=0;walked<(uint64_t)skip+maximum && count<65535;walked++) {
        if(!c->rip || !containing(c->rip))break;
        if(walked>=skip) {
            void *ip=(void *)(uintptr_t)c->rip;if(wr_memory_copy(out+count,&ip,8))break;
            value+=(unsigned)c->rip;count++;
        }
        uint64_t image=0;runtime_function *entry=lookup(c->rip,&image);uint64_t previous=c->rsp;
        if(entry){runtime_function f;void *data,*handler;uint64_t frame;
            if(!read_at(&f,(uintptr_t)entry,sizeof(f)) || !step_unwind(0,image,c->rip,f,c,&data,&frame,0,&handler))break;
        }else {if(!read_at(&c->rip,c->rsp,8) || !add(&c->rsp,8))break;}
        if(c->rsp<=previous)break;
    }
    wr_loader_unlock();if(hash)wr_memory_copy(hash,&value,4);return (unsigned short)count;
}
void *wr_unwind_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(RtlPcToFileHeader);E(RtlLookupFunctionEntry);E(RtlVirtualUnwind);
#undef E
    if(wr_equal(name,"RtlCaptureContext",0))return (void *)&wr_capture_context;
    if(wr_equal(name,"RtlCaptureStackBackTrace",0))return (void *)&wr_capture_backtrace;
    return 0;
}
