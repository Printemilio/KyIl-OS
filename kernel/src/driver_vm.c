#include <unos/driver_vm.h>
#include <unos/driver_dma.h>
#include <string.h>

static int name_valid(const char *s,size_t n) {
    size_t i=0;for(;i<n && s[i];i++)
        if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='A'&&s[i]<='Z')||
             (s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'||s[i]=='.'))return 0;
    if(!i || i==n)return 0;
    for(;i<n;i++)if(s[i])return 0;
    return 1;
}
static int decompress(const uint8_t *src,size_t n,uint8_t *dst,size_t capacity) {
    size_t in=0,out=0;
    while(in<n){unsigned token=src[in++],count=(token&127u)+1u;
        if(count>capacity-out)return DRV_FORMAT;
        if(token&128u){if(in==n)return DRV_FORMAT;memset(dst+out,src[in++],count);}
        else {if(count>n-in)return DRV_FORMAT;memcpy(dst+out,src+in,count);in+=count;}
        out+=count;
    }
    return out==capacity?0:DRV_FORMAT;
}
static int validate_code(const drv_image_t *image) {
    const drv_manifest_t *m=&image->manifest;
    for(unsigned i=0;i<m->code_count;i++) {
        const drv_instruction_t *c=&image->code[i];
        if(c->op>DRV_DMA_OP || c->dst>=DRV_REGS || c->a>=DRV_REGS || c->b>=DRV_REGS)return DRV_FORMAT;
        if(c->op==DRV_DMA_OP && (!(m->features&DRV_F_DMA) || m->engine_min!=2 ||
           c->immediate>DRV_REGS-8 || c->aux<DRV_DMA_ALLOC || c->aux>DRV_DMA_SLOT_STATUS || c->a || c->b))return DRV_DENIED;
        if((c->op==DRV_JMP || c->op==DRV_JZ || c->op==DRV_JNZ) && c->immediate>=m->code_count)return DRV_BOUNDS;
        if(c->op==DRV_IO_READ || c->op==DRV_IO_WRITE) {
            if(c->aux>=m->resource_count || !(c->b==1||c->b==2||c->b==4||c->b==8))return DRV_BOUNDS;
            const drv_requirement_t *r=&m->resources[c->aux];
            if(r->kind==DRV_DMA || !(r->widths&c->b) || !(r->rights&(c->op==DRV_IO_READ?DRV_READ:DRV_WRITE)))return DRV_DENIED;
            if(c->immediate>r->length || c->b>r->length-c->immediate)return DRV_BOUNDS;
        }
        if(c->op==DRV_LOAD || c->op==DRV_STORE) {
            if(!(c->b==1||c->b==2||c->b==4||c->b==8) || !(m->features&DRV_F_BUFFER))return DRV_UNSUPPORTED;
        }
        if(c->op==DRV_EMIT && (m->device_class!=DRV_CLASS_CHARACTER || !(m->features&DRV_F_IRQ)))return DRV_DENIED;
    }
    return 0;
}
int drv_image_load(const void *bytes,size_t length,const drv_allocator_t *a,drv_image_t *out) {
    if(!bytes || !a || !a->allocate || !a->release || !a->sha256 || !out || out->payload ||
       length<sizeof(drv_archive_header_t) || length>DRV_ARCHIVE_MAX)return DRV_FORMAT;
    drv_archive_header_t h;memcpy(&h,bytes,sizeof(h));
    static const uint8_t magic[8]={'K','Y','D','R','I','V','E','R'};
    if(memcmp(h.magic,magic,8)||h.header_size!=sizeof(h)||h.flags)return DRV_FORMAT;
    for(unsigned i=0;i<sizeof(h.reserved);i++)if(h.reserved[i])return DRV_FORMAT;
    if(h.major!=1 || h.minor>1 || h.codec!=1)return DRV_VERSION;
    if(h.compressed_size!=length-sizeof(h) || h.payload_size<8 || h.payload_size>DRV_PAYLOAD_MAX)return DRV_FORMAT;
    void *payload=a->allocate(h.payload_size);if(!payload)return DRV_NOMEM;
    int error=decompress((const uint8_t *)bytes+sizeof(h),h.compressed_size,payload,h.payload_size);
    uint8_t hash[32];
    if(!error && (a->sha256(payload,h.payload_size,hash)||memcmp(hash,h.payload_sha256,32)))error=DRV_INTEGRITY;
    if(error){a->release(payload);return error;}
    uint32_t count,reserved;memcpy(&count,payload,4);memcpy(&reserved,(uint8_t *)payload+4,4);
    if(!count || count>8 || reserved || 8u+count*sizeof(drv_member_t)>h.payload_size){error=DRV_FORMAT;goto fail;}
    const drv_member_t *members=(const drv_member_t *)((const uint8_t *)payload+8);
    const drv_member_t *manifest=NULL,*program=NULL;
    size_t next=8u+count*sizeof(drv_member_t);
    for(unsigned i=0;i<count;i++) {
        const drv_member_t *member=&members[i];
        if(!name_valid(member->name,16)||member->flags || member->offset!=next ||
           member->size>h.payload_size-next){error=DRV_FORMAT;goto fail;}
        for(unsigned j=0;j<i;j++)if(!strcmp(member->name,members[j].name)){error=DRV_FORMAT;goto fail;}
        if(member->type==1 && !strcmp(member->name,"manifest") && !manifest)manifest=member;
        else if(member->type==2 && !strcmp(member->name,"program") && !program)program=member;
        else if(member->type!=3){error=DRV_FORMAT;goto fail;}
        next+=member->size;
    }
    if(next!=h.payload_size || !manifest || !program || manifest->size!=sizeof(drv_manifest_t)){error=DRV_FORMAT;goto fail;}
    drv_image_t image={.payload=payload};memcpy(&image.manifest,(uint8_t *)payload+manifest->offset,sizeof(image.manifest));
    drv_manifest_t *m=&image.manifest;
    if(!name_valid(m->name,32)||!name_valid(m->platform,32)||
       (m->bus!=DRV_BUS_PLATFORM&&m->bus!=DRV_BUS_PCI)||
       (m->device_class!=DRV_CLASS_CLOCK&&m->device_class!=DRV_CLASS_CHARACTER&&m->device_class!=DRV_CLASS_NETWORK)||
       !m->code_count||m->code_count>DRV_CODE_MAX||m->resource_count>DRV_RES_MAX||
       m->scratch_size>DRV_SCRATCH_MAX||program->size!=(size_t)m->code_count*sizeof(drv_instruction_t)) {error=DRV_FORMAT;goto fail;}
    unsigned supported=h.minor==1?DRV_SUPPORTED_V2:DRV_SUPPORTED;
    if((m->engine_min!=1 && m->engine_min!=2) || (m->engine_min==2 && h.minor!=1) ||
       (m->features&~supported) || !(m->features&DRV_F_BASE) ||
       ((m->features&(DRV_F_DMA|DRV_F_MMIO)) && m->engine_min!=2) ||
       ((m->features&DRV_F_MMIO) && !(m->features&DRV_F_DMA))){error=DRV_UNSUPPORTED;goto fail;}
    if(m->irq!=DRV_ABSENT && (m->irq>15 || !(m->features&DRV_F_IRQ))){error=DRV_DENIED;goto fail;}
    for(unsigned i=0;i<DRV_ENTRY_COUNT;i++)if(m->entries[i]!=DRV_ABSENT && m->entries[i]>=m->code_count){error=DRV_BOUNDS;goto fail;}
    if(m->entries[DRV_PROBE]==DRV_ABSENT || m->entries[DRV_INIT]==DRV_ABSENT || m->entries[DRV_STOP]==DRV_ABSENT ||
       (m->device_class==DRV_CLASS_CLOCK&&m->entries[DRV_READ_ENTRY]==DRV_ABSENT)||
       (m->irq!=DRV_ABSENT&&m->entries[DRV_IRQ_ENTRY]==DRV_ABSENT)){error=DRV_FORMAT;goto fail;}
    unsigned dma_resources=0;
    for(unsigned i=0;i<DRV_RES_MAX;i++) {
        const drv_requirement_t *r=&m->resources[i];
        if(i>=m->resource_count){const uint8_t *b=(const uint8_t *)r;for(unsigned j=0;j<sizeof(*r);j++)if(b[j]){error=DRV_FORMAT;goto fail;}continue;}
        unsigned feature=r->kind==DRV_PIO?DRV_F_PIO:r->kind==DRV_MMIO?DRV_F_MMIO:r->kind==DRV_INDEXED_PIO?DRV_F_INDEXED:r->kind==DRV_DMA?DRV_F_DMA:0;
        if(r->kind==DRV_DMA)dma_resources++;
        if(!feature || !(m->features&feature) || !r->length || !r->rights || (r->rights&~3u) ||
           !r->widths || (r->widths&~15u) || r->reserved || r->offset>UINT64_MAX-r->length){error=DRV_FORMAT;goto fail;}
    }
    if((m->features&DRV_F_DMA) && !dma_resources){error=DRV_DENIED;goto fail;}
    image.code=(const drv_instruction_t *)((const uint8_t *)payload+program->offset);
    error=validate_code(&image);if(error)goto fail;
    *out=image;return 0;
fail:a->release(payload);return error;
}
void drv_image_release(drv_image_t *image,const drv_allocator_t *a) {
    if(image){if(image->payload)a->release(image->payload);memset(image,0,sizeof(*image));}
}
int drv_execute(const drv_image_t *image,unsigned entry,const uint64_t input[8],uint64_t output[8],
                uint8_t *scratch,const drv_io_t *io,unsigned fuel,uint64_t duration_ns) {
    if(!image||!image->payload||entry>=DRV_ENTRY_COUNT||!io||!io->clock_ns||!fuel)return DRV_FORMAT;
    uint32_t pc=image->manifest.entries[entry];if(pc==DRV_ABSENT)return DRV_UNSUPPORTED;
    uint64_t regs[DRV_REGS]={0};if(input)memcpy(regs,input,8*sizeof(uint64_t));
    uint64_t start=io->clock_ns(io->context);
    for(unsigned steps=0;steps<fuel;steps++) {
        if(pc>=image->manifest.code_count)return DRV_BOUNDS;
        if(io->clock_ns(io->context)-start>=duration_ns)return DRV_TIMEOUT;
        const drv_instruction_t *c=&image->code[pc++];uint64_t a=regs[c->a],b=regs[c->b];
        switch(c->op) {
        case DRV_RET:if(output)memcpy(output,&regs[8],8*sizeof(uint64_t));return (int32_t)c->immediate;
        case DRV_CONST:regs[c->dst]=c->immediate;break;
        case DRV_MOV:regs[c->dst]=a;break;
        case DRV_ADD:regs[c->dst]=a+b;break;
        case DRV_SUB:regs[c->dst]=a-b;break;
        case DRV_MUL:regs[c->dst]=a*b;break;
        case DRV_DIV:if(!b)return DRV_BOUNDS;regs[c->dst]=a/b;break;
        case DRV_MOD:if(!b)return DRV_BOUNDS;regs[c->dst]=a%b;break;
        case DRV_AND:regs[c->dst]=a&b;break;
        case DRV_OR:regs[c->dst]=a|b;break;
        case DRV_XOR:regs[c->dst]=a^b;break;
        case DRV_SHL:if(b>=64)return DRV_BOUNDS;regs[c->dst]=a<<b;break;
        case DRV_SHR:if(b>=64)return DRV_BOUNDS;regs[c->dst]=a>>b;break;
        case DRV_EQ:regs[c->dst]=a==b;break;
        case DRV_LT:regs[c->dst]=a<b;break;
        case DRV_JMP:pc=(uint32_t)c->immediate;break;
        case DRV_JZ:if(!a)pc=(uint32_t)c->immediate;break;
        case DRV_JNZ:if(a)pc=(uint32_t)c->immediate;break;
        case DRV_IO_READ: {
            int e=io->read?io->read(io->context,c->aux,c->b,c->immediate,&regs[c->dst]):DRV_UNSUPPORTED;
            if(e)return e;break;
        }
        case DRV_IO_WRITE: {
            int e=io->write?io->write(io->context,c->aux,c->b,c->immediate,a):DRV_UNSUPPORTED;
            if(e)return e;break;
        }
        case DRV_LOAD:case DRV_STORE: {
            size_t n=image->manifest.scratch_size;
            if(!scratch || a>n || c->b>n-a)return DRV_BOUNDS;
            if(c->op==DRV_LOAD){uint64_t v=0;memcpy(&v,scratch+a,c->b);regs[c->dst]=v;}
            else memcpy(scratch+a,&regs[c->dst],c->b);
            break;
        }
        case DRV_EMIT: {int e=io->emit?io->emit(io->context,a):DRV_UNSUPPORTED;if(e)return e;break;}
        case DRV_DMA_OP: {
            /* Allocation/zeroing, bulk copies and ring scans cannot fit the
             * hard IRQ budget. Ack via mediated registers, then defer work. */
            if(entry==DRV_IRQ_ENTRY)return DRV_DENIED;
            if(!(image->manifest.features&DRV_F_DMA) || image->manifest.engine_min!=2 ||
               c->immediate>DRV_REGS-8)return DRV_DENIED;
            uint64_t result=0;
            int e=io->dma?io->dma(io->context,c->aux,&regs[c->immediate],&result,scratch,image->manifest.scratch_size):DRV_UNSUPPORTED;
            if(e)return e;
            regs[c->dst]=result;break;
        }
        default:return DRV_UNSUPPORTED;
        }
    }
    return DRV_TIMEOUT;
}
const char *drv_error_name(int e) {
    switch(e){case 0:return "ok";case DRV_FORMAT:return "format";case DRV_VERSION:return "version";
    case DRV_INTEGRITY:return "integrity";case DRV_DENIED:return "denied";case DRV_CONFLICT:return "conflict";
    case DRV_BOUNDS:return "bounds";case DRV_TIMEOUT:return "timeout";case DRV_NOMEM:return "oom";
    case DRV_UNSUPPORTED:return "unsupported";case DRV_NOT_BOUND:return "not-bound";case DRV_BUSY:return "busy";
    case DRV_IO:return "io-error";case DRV_AGAIN:return "again";default:return "protocol-error";}
}
