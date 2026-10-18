#include <unos/driver_dma.h>
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mbedtls/sha256.h>
void mbedtls_free(void *p){free(p);}

/* Hardware is deliberately a test fixture. The ORA implementation, VM and
 * DMA engine under test are the production sources, not reimplementations. */
static _Thread_local int allocation_fail=-1;
static void *metadata(size_t n){if(allocation_fail==0)return NULL;if(allocation_fail>0)allocation_fail--;return malloc(n);}
void *kernel_allocator_alloc(uint32_t owner,size_t n){(void)owner;return metadata(n);}
void kernel_allocator_free(void *p){free(p);}
int kernel_allocator_physical_page(const void *p,size_t off,uint64_t *out){(void)p;(void)off;(void)out;return -1;}
typedef struct fixture {unsigned live,writes;int stop,fail_write;uint64_t bus;void *pages[64];} fixture_t;
static int pages(void *p,size_t n,void **cpu,uint64_t *bus,void **cookie) {
    fixture_t *f=p;if(allocation_fail==0)return DRV_NOMEM;
    if(allocation_fail>0)allocation_fail--;
    void *mem=calloc(1,n);if(!mem)return DRV_NOMEM;assert(f->live<64);
    f->pages[f->live++]=mem;*cpu=mem;*cookie=mem;*bus=f->bus?f->bus:0x100000+f->live*0x10000;return 0;
}
static void free_pages(void *p,void *cookie,size_t n){fixture_t *f=p;(void)n;assert(f->live);f->live--;free(cookie);}
static int reg_read(void *p,uint32_t off,unsigned w,uint64_t *out){(void)p;assert(off==16&&w==4);*out=3;return 0;}
static int reg_write(void *p,uint32_t off,unsigned w,uint64_t v){fixture_t *f=p;assert(off<=20&&off%4==0&&w==4&&v<=UINT32_MAX);f->writes++;return f->fail_write?DRV_IO:0;}
static int stopped(void *p,unsigned endpoint){fixture_t *f=p;assert(endpoint==0);return f->stop?0:DRV_AGAIN;}
static drv_dma_policy_t policy(void) {
    drv_dma_policy_t p={.grant_count=2,.max_bytes=16384,.address_limit=UINT32_MAX,.register_count=5,.endpoint_count=1};
    p.grants[0]=(drv_dma_grant_t){.kind=DRV_DMA_BUFFER,.rights=3,.widths=15,.max_bytes=4096};
    p.grants[1]=(drv_dma_grant_t){.kind=DRV_DMA_RING,.rights=3,.widths=15,.max_bytes=4096,.stride=16,.field_count=4};
    p.grants[1].fields[0]=(drv_dma_field_t){0,8,DRV_DMA_ADDRESS,1,0};
    p.grants[1].fields[1]=(drv_dma_field_t){8,2,DRV_DMA_LENGTH,DRV_ABSENT,0};
    p.grants[1].fields[2]=(drv_dma_field_t){10,1,DRV_DMA_SCALAR,DRV_ABSENT,3};
    p.grants[1].fields[3]=(drv_dma_field_t){11,1,DRV_DMA_STATUS,DRV_ABSENT,0};
    unsigned kinds[]={DRV_REG_ADDRESS_LOW,DRV_REG_ADDRESS_HIGH,DRV_REG_LENGTH,DRV_REG_DOORBELL,DRV_REG_SCALAR};
    for(unsigned i=0;i<5;i++)p.registers[i]=(drv_dma_register_t){i*4,4,kinds[i],i==4?3:DRV_WRITE,i==4?3:0};
    p.endpoints[0]=(drv_dma_endpoint_t){.grant=1,.low=0,.high=1,.length=2,.doorbell=3,.doorbell_value=1};return p;
}
static void init(drv_dma_context_t *c,fixture_t *f) {
    drv_dma_policy_t p=policy();drv_dma_provider_t provider={f,pages,free_pages,metadata,free,reg_read,reg_write,stopped};
    assert(!drv_dma_init(c,&p,&provider));
    /* Policy is copied; changing caller storage cannot widen authority. */
    p.grants[0].rights=0;assert(c->policy.grants[0].rights==3);
}
static int op(drv_dma_context_t *c,unsigned op,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5,uint64_t *out) {
    uint64_t args[8]={a0,a1,a2,a3,a4,a5};return drv_dma_operation(c,op,args,out,NULL,0);
}
static void exercise(void) {
    fixture_t f={0};drv_dma_context_t c={0},foreign={0};init(&c,&f);
    uint64_t buf,ring,out=0;
    assert(!op(&c,DRV_DMA_ALLOC,0,4096,0,0,0,0,&buf));
    assert(!op(&c,DRV_DMA_ALLOC,1,32,0,0,0,0,&ring));
    assert(c.bytes==8192 && f.live==2);
    assert(!op(&c,DRV_DMA_READ,buf,0,8,0,0,0,&out)&&out==0);
    assert(!op(&c,DRV_DMA_WRITE,buf,4088,8,0x123456789abcdef0,0,0,&out));
    assert(!op(&c,DRV_DMA_READ,buf,4088,8,0,0,0,&out)&&out==0x123456789abcdef0);
    assert(op(&c,DRV_DMA_READ,buf,UINT64_MAX,8,0,0,0,&out)==DRV_BOUNDS);
    assert(op(&c,DRV_DMA_WRITE,buf,0,1,256,0,0,&out)==DRV_BOUNDS);
    assert(op(&c,DRV_DMA_ALLOC,0,UINT64_MAX,0,0,0,0,&out)==DRV_BOUNDS);
    assert(op(&c,DRV_DMA_ALLOC,UINT64_MAX,8,0,0,0,0,&out)==DRV_BOUNDS);
    assert(op(&c,DRV_DMA_ALLOC,1,17,0,0,0,0,&out)==DRV_BOUNDS);
    assert(op(&c,DRV_DMA_WRITE,ring,0,8,0xdeadbeef,0,0,&out)==DRV_DENIED);
    assert(op(&c,DRV_DMA_READ,ring,0,8,0,0,0,&out)==DRV_DENIED);
    assert(op(&c,DRV_DMA_WRITE,ring,8,2,65000,0,0,&out)==DRV_DENIED);
    assert(op(&c,DRV_DMA_WRITE,ring,10,1,128,0,0,&out)==DRV_DENIED);
    assert(!op(&c,DRV_DMA_WRITE,ring,10,1,3,0,0,&out));
    assert(op(&c,DRV_DMA_BIND,ring,buf,0,0,4095,2,&out)==DRV_BOUNDS);
    assert(op(&c,DRV_DMA_BIND,ring,ring,0,0,0,1,&out)==DRV_DENIED);
    assert(!op(&c,DRV_DMA_BIND,ring,buf,0,0,128,512,&out));
    assert(!op(&c,DRV_DMA_BIND,ring,buf,1,0,1024,512,&out));
    assert(op(&c,DRV_DMA_BIND,ring,buf,UINT64_MAX,0,0,1,&out)==DRV_DENIED);
    assert(!op(&c,DRV_DMA_READ,ring,8,2,0,0,0,&out)&&out==512);
    uint64_t addr;memcpy(&addr,f.pages[1],8);assert(addr==0x110080);
    assert(drv_dma_register_write(&c,0,4,0,0xdeadbeef)==DRV_DENIED);
    assert(drv_dma_register_read(&c,0,4,0,&out)==DRV_DENIED);
    assert(drv_dma_register_write(&c,0,4,12,1)==DRV_DENIED);
    assert(drv_dma_register_write(&c,0,4,16,4)==DRV_DENIED);
    assert(drv_dma_register_write(&c,0,8,16,1)==DRV_DENIED);
    assert(!drv_dma_register_write(&c,0,4,16,2));
    init(&foreign,&f);assert(op(&foreign,DRV_DMA_READ,buf,0,1,0,0,0,&out)==DRV_NOT_BOUND);assert(!drv_dma_destroy(&foreign));
    /* Register IO may fail AFTER a device sees a pointer. No free on failure. */
    f.fail_write=1;assert(op(&c,DRV_DMA_PUBLISH,ring,0,0,0,0,0,&out)==DRV_IO);
    assert(op(&c,DRV_DMA_RELEASE,buf,0,0,0,0,0,&out)==DRV_BUSY);
    assert(op(&c,DRV_DMA_WRITE,buf,0,1,1,0,0,&out)==DRV_BUSY);
    assert(op(&c,DRV_DMA_READ,ring,8,2,0,0,0,&out)==DRV_BUSY);
    assert(drv_dma_destroy(&c)==DRV_BUSY && f.live==2 && c.bytes==8192);
    assert(op(&c,DRV_DMA_RECLAIM,ring,0,0,0,0,0,&out)==DRV_AGAIN);
    f.stop=1;assert(!op(&c,DRV_DMA_RECLAIM,ring,0,0,0,0,0,&out));
    f.fail_write=0;assert(!op(&c,DRV_DMA_PUBLISH,ring,0,0,0,0,0,&out));
    uint16_t bad_length=600;memcpy((uint8_t *)f.pages[1]+8,&bad_length,2);
    assert(op(&c,DRV_DMA_RECLAIM,ring,0,0,0,0,0,&out)==DRV_IO);
    assert(!op(&c,DRV_DMA_READ,ring,8,2,0,0,0,&out)&&out==0);
    uint8_t scratch[16]={1,2,3},args_buffer[16]={0};uint64_t a[8]={buf,0,0,3};
    assert(!drv_dma_operation(&c,DRV_DMA_COPY_IN,a,&out,scratch,sizeof(scratch)));
    assert(!drv_dma_operation(&c,DRV_DMA_COPY_OUT,a,&out,args_buffer,sizeof(args_buffer)));
    assert(!memcmp(scratch,args_buffer,3));a[0]=ring;
    assert(drv_dma_operation(&c,DRV_DMA_COPY_IN,a,&out,scratch,sizeof(scratch))==DRV_DENIED);
    a[0]=buf;a[2]=UINT64_MAX;assert(drv_dma_operation(&c,DRV_DMA_COPY_OUT,a,&out,scratch,sizeof(scratch))==DRV_BOUNDS);
    assert(!op(&c,DRV_DMA_RELEASE,buf,0,0,0,0,0,&out));
    assert(f.live==2 && c.bytes==8192); /* Ring owns references, not creator token. */
    assert(op(&c,DRV_DMA_READ,buf,0,1,0,0,0,&out)==DRV_NOT_BOUND);
    assert(!op(&c,DRV_DMA_RELEASE,ring,0,0,0,0,0,&out)&&f.live==0&&c.bytes==0);
    assert(!drv_dma_destroy(&c));
}
static void *thread(void *p){(void)p;for(unsigned i=0;i<100;i++)exercise();return NULL;}
static int sha(const void *p,size_t n,uint8_t hash[32]){return mbedtls_sha256(p,n,hash,0);}
static uint64_t clock_ns(void *p){(void)p;return 0;}
static void archive_test(const char *path) {
    FILE *fp=fopen(path,"rb");assert(fp);assert(!fseek(fp,0,SEEK_END));long n=ftell(fp);assert(n>0);rewind(fp);
    uint8_t *bytes=malloc(n);assert(fread(bytes,1,n,fp)==(size_t)n);fclose(fp);
    drv_allocator_t allocator={metadata,free,sha};drv_image_t image={0};assert(!drv_image_load(bytes,n,&allocator,&image));
    fixture_t f={0};drv_dma_context_t c={0};init(&c,&f);
    drv_io_t io;assert(!drv_dma_attach_io(&c,&image,clock_ns,&io));uint64_t out[8]={0};uint8_t scratch[4096]={0};
    assert(!drv_execute(&image,DRV_INIT,NULL,out,scratch,&io,1000,1000000));
    assert(out[0]==0x12345678 && c.bytes==0 && f.live==0);
    drv_image_t irq_image=image;irq_image.manifest.entries[DRV_IRQ_ENTRY]=image.manifest.entries[DRV_INIT];
    assert(drv_execute(&irq_image,DRV_IRQ_ENTRY,NULL,out,scratch,&io,1000,1000000)==DRV_DENIED&&c.bytes==0);
    assert(io.write(io.context,0,4,0,0xdeadbeef)==DRV_DENIED);
    assert(io.read(io.context,0,4,0,&out[0])==DRV_DENIED);
    uint64_t forbidden[8]={1,32};assert(io.dma(io.context,DRV_DMA_ALLOC,forbidden,&out[0],scratch,sizeof(scratch))==DRV_DENIED);
    io.dma=NULL;assert(drv_execute(&image,DRV_INIT,NULL,out,scratch,&io,1000,1000000)==DRV_UNSUPPORTED);
    assert(!drv_dma_destroy(&c));drv_image_release(&image,&allocator);free(bytes);
}
static void bridge_test(void) {
    drv_image_t image={0};image.manifest.engine_min=2;image.manifest.features=DRV_F_DMA|DRV_F_MMIO;
    image.manifest.resource_count=3;
    image.manifest.resources[0]=(drv_requirement_t){.kind=DRV_DMA,.selector=0,.length=4096,.rights=3,.widths=15};
    image.manifest.resources[1]=(drv_requirement_t){.kind=DRV_DMA,.selector=1,.length=32,.rights=3,.widths=15};
    image.manifest.resources[2]=(drv_requirement_t){.kind=DRV_MMIO,.length=20,.rights=3,.widths=4};
    fixture_t f={.stop=1};drv_dma_context_t c={0};init(&c,&f);drv_io_t io;
    image.manifest.resources[0].length=8192;
    assert(drv_dma_attach_io(&c,&image,clock_ns,&io)==DRV_DENIED && !c.attached);
    image.manifest.resources[0].length=4096;
    assert(!drv_dma_attach_io(&c,&image,clock_ns,&io));
    assert(drv_dma_attach_io(&c,&image,clock_ns,&io)==DRV_BUSY);
    uint64_t out=0,buf,ring,args[8]={0,4096};
    assert(!io.dma(io.context,DRV_DMA_ALLOC,args,&buf,NULL,0));args[0]=1;args[1]=32;
    assert(!io.dma(io.context,DRV_DMA_ALLOC,args,&ring,NULL,0));
    assert(op(&c,DRV_DMA_ALLOC,1,64,0,0,0,0,&out)==DRV_BOUNDS); /* Manifest narrowed host grant. */
    args[0]=ring;args[1]=buf;args[2]=0;args[3]=0;args[4]=0;args[5]=128;
    assert(!io.dma(io.context,DRV_DMA_BIND,args,&out,NULL,0));args[2]=1;
    assert(!io.dma(io.context,DRV_DMA_BIND,args,&out,NULL,0));
    assert(io.write(io.context,2,4,0,0xdeadbeef)==DRV_DENIED);
    assert(io.read(io.context,0,4,16,&out)==DRV_DENIED); /* DMA resource is not a BAR. */
    assert(!io.read(io.context,2,4,16,&out)&&out==3);
    args[0]=ring;args[1]=0;assert(!io.dma(io.context,DRV_DMA_PUBLISH,args,&out,NULL,0));
    assert(!io.dma(io.context,DRV_DMA_RECLAIM,args,&out,NULL,0));assert(!drv_dma_destroy(&c)&&!f.live);
    /* Read-only MMIO cannot authorize even engine-generated DMA publication. */
    c=(drv_dma_context_t){0};init(&c,&f);image.manifest.resources[2].rights=DRV_READ;
    assert(!drv_dma_attach_io(&c,&image,clock_ns,&io));args[0]=1;args[1]=32;
    assert(!io.dma(io.context,DRV_DMA_ALLOC,args,&ring,NULL,0));args[0]=ring;args[1]=0;
    assert(io.dma(io.context,DRV_DMA_PUBLISH,args,&out,NULL,0)==DRV_DENIED);
    assert(!drv_dma_destroy(&c)&&!f.live);
}
static void stream_test(void) {
    fixture_t f={.stop=1};drv_dma_context_t c={0};drv_dma_policy_t p=policy();
    p.endpoints[0].streaming=1;p.endpoints[0].initial_owned=1;p.endpoints[0].length_output=1;
    p.endpoints[0].completion_field=3;p.endpoints[0].completion_mask=1;p.endpoints[0].completion_value=1;
    p.endpoints[0].min_span=512;p.endpoints[0].min_slots=2;p.endpoints[0].max_slots=2;p.endpoints[0].doorbell_value=1;
    p.registers[4].guard_mask=1;p.registers[4].require_endpoint=0;p.registers[4].invalidate_mask=2;
    p.registers[5]=(drv_dma_register_t){.offset=20,.width=4,.kind=DRV_REG_SCALAR,.rights=DRV_WRITE,.block_endpoint=1};p.register_count=6;
    drv_dma_provider_t provider={&f,pages,free_pages,metadata,free,reg_read,reg_write,stopped};
    assert(!drv_dma_init(&c,&p,&provider));uint64_t buf,ring,out;
    assert(!drv_dma_register_write(&c,0,4,20,0));
    assert(drv_dma_register_write(&c,0,4,16,1)==DRV_DENIED);
    assert(!drv_dma_register_write(&c,0,4,16,2));
    assert(!op(&c,DRV_DMA_ALLOC,0,4096,0,0,0,0,&buf));assert(!op(&c,DRV_DMA_ALLOC,1,32,0,0,0,0,&ring));
    assert(!op(&c,DRV_DMA_BIND,ring,buf,0,0,128,512,&out));assert(!op(&c,DRV_DMA_BIND,ring,buf,1,0,1024,512,&out));
    assert(!op(&c,DRV_DMA_PUBLISH,ring,0,0,0,0,0,&out));assert(!drv_dma_register_write(&c,0,4,16,1));
    assert(drv_dma_register_write(&c,0,4,20,0)==DRV_DENIED);
    assert(!op(&c,DRV_DMA_SLOT_STATUS,ring,0,0,0,0,0,&out)&&out==0);
    assert(op(&c,DRV_DMA_SLOT_ACQUIRE,ring,0,0,0,0,0,&out)==DRV_AGAIN);
    assert(op(&c,DRV_DMA_WRITE,buf,128,1,1,0,0,&out)==DRV_BUSY);
    ((uint8_t *)f.pages[1])[11]=1;uint16_t size=123;memcpy((uint8_t *)f.pages[1]+8,&size,2);
    assert(!op(&c,DRV_DMA_SLOT_STATUS,ring,0,0,0,0,0,&out)&&out==1);
    assert(op(&c,DRV_DMA_WRITE,buf,128,1,1,0,0,&out)==DRV_BUSY); /* Peek isn't ownership. */
    assert(!op(&c,DRV_DMA_SLOT_ACQUIRE,ring,0,0,0,0,0,&out)&&out==123);
    assert(!op(&c,DRV_DMA_WRITE,buf,128,1,1,0,0,&out));
    assert(op(&c,DRV_DMA_WRITE,buf,1024,1,1,0,0,&out)==DRV_BUSY);
    assert(op(&c,DRV_DMA_SLOT_POST,ring,1,512,0,0,0,&out)==DRV_BUSY);
    assert(!op(&c,DRV_DMA_SLOT_POST,ring,0,512,0,0,0,&out));
    assert(op(&c,DRV_DMA_WRITE,buf,128,1,1,0,0,&out)==DRV_BUSY);
    assert(!drv_dma_register_write(&c,0,4,16,2));
    assert(drv_dma_register_write(&c,0,4,16,1)==DRV_DENIED);
    assert(op(&c,DRV_DMA_SLOT_ACQUIRE,ring,1,0,0,0,0,&out)==DRV_DENIED);
    assert(!drv_dma_quiesce_all(&c));assert(!drv_dma_destroy(&c)&&f.live==0);
}
int main(int argc,char **argv) {
    assert(argc==2);archive_test(argv[1]);bridge_test();stream_test();
    ora_stats_t before,after;ora_get_stats(&before);exercise();
    for(int fail=0;fail<5;fail++) {
        fixture_t f={0};drv_dma_context_t c={0};init(&c,&f);allocation_fail=fail;
        uint64_t out=0xabcdef;int e=op(&c,DRV_DMA_ALLOC,1,32,0,0,0,0,&out);allocation_fail=-1;
        if(e)assert(e==DRV_NOMEM&&out==0xabcdef&&c.bytes==0&&f.live==0);
        assert(!drv_dma_destroy(&c)&&f.live==0);
    }
    fixture_t f={.bus=UINT64_C(0x100000000)};drv_dma_context_t c={0};init(&c,&f);uint64_t out;
    assert(op(&c,DRV_DMA_ALLOC,0,4096,0,0,0,0,&out)==DRV_BOUNDS&&f.live==0&&c.bytes==0);assert(!drv_dma_destroy(&c));
    /* An explicit kernel ORA reference survives its local handle, but the
     * provider/context must not be retired beneath that reference. */
    fixture_t retained={0};c=(drv_dma_context_t){0};init(&c,&retained);
    assert(!op(&c,DRV_DMA_ALLOC,0,4096,0,0,0,0,&out));ora_ref_t ref=ora_retain(c.slots[0].object);
    assert(ref && ora_kind(ref)==ORA_STORAGE);
    assert(!op(&c,DRV_DMA_RELEASE,out,0,0,0,0,0,&out));assert(drv_dma_destroy(&c)==DRV_BUSY);
    ora_release(ref);assert(c.bytes==0&&c.metadata_bytes==0&&!drv_dma_destroy(&c));
    /* Invalid host schemas cannot turn raw writes into descriptor addresses. */
    drv_dma_policy_t p=policy();drv_dma_provider_t provider={&retained,pages,free_pages,metadata,free,reg_read,reg_write,stopped};
    p.grants[1].fields[0].width=4;assert(drv_dma_init(&c,&p,&provider)==DRV_FORMAT);
    p=policy();p.grants[1].fields[2].offset=8;assert(drv_dma_init(&c,&p,&provider)==DRV_FORMAT);
    p=policy();p.grants[1].fields[0].write_mask=1;assert(drv_dma_init(&c,&p,&provider)==DRV_DENIED);
    p=policy();p.registers[0].kind=DRV_REG_SCALAR;assert(drv_dma_init(&c,&p,&provider)==DRV_DENIED);
    p=policy();p.registers[1].offset=0;assert(drv_dma_init(&c,&p,&provider)==DRV_FORMAT);
    p=policy();provider.quiesced=NULL;assert(drv_dma_init(&c,&p,&provider)==DRV_UNSUPPORTED);
    p=policy();provider.quiesced=stopped;p.grants[0].rights=DRV_READ;p.grants[0].widths=1;
    assert(!drv_dma_init(&c,&p,&provider));uint64_t readonly;
    assert(!op(&c,DRV_DMA_ALLOC,0,4096,0,0,0,0,&readonly));
    assert(op(&c,DRV_DMA_WRITE,readonly,0,1,1,0,0,&out)==DRV_DENIED);
    assert(!op(&c,DRV_DMA_READ,readonly,0,1,0,0,0,&out)&&out==0);
    assert(op(&c,DRV_DMA_READ,readonly,0,8,0,0,0,&out)==DRV_DENIED);
    uint64_t copy_args[8]={readonly,0,0,1};uint8_t one=1;
    assert(drv_dma_operation(&c,DRV_DMA_COPY_IN,copy_args,&out,&one,1)==DRV_DENIED);
    assert(!drv_dma_destroy(&c)&&retained.live==0);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,thread,NULL));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    ora_get_stats(&after);assert(before.objects==after.objects&&before.fingerprints==after.fingerprints);
    puts("PASS: real ORA + DMA engine; typed descriptors/MMIO, local/stale/foreign refs, retained buffers, ownership/quiescence, partial IO, corrupt RX length, rollback/OOM, 8 concurrent contexts x100 cycles");
}
