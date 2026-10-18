#include <unos/driver_vm.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/sha256.h>
void mbedtls_free(void *p){free(p);}
static int live,fail_allocate;
static void *allocate(size_t n){if(fail_allocate)return NULL;void *p=malloc(n);if(p)live++;return p;}
static void release(void *p){if(p){live--;free(p);}}
static int digest(const void *p,size_t n,uint8_t h[32]){return mbedtls_sha256(p,n,h,0);}
static drv_allocator_t memory={allocate,release,digest};
typedef struct {unsigned reads,writes,emits;uint8_t bytes[16];uint64_t time;int uip;} fixture_t;
static uint64_t clock_fn(void *p){fixture_t *f=p;return f->time+=10;}
static int read_fn(void *p,unsigned resource,unsigned width,uint64_t offset,uint64_t *out){
    fixture_t *f=p;if(resource||width!=1||offset>=16)return DRV_DENIED;
    f->reads++;*out=offset==10&&f->uip?128:f->bytes[offset];return 0;
}
static int write_fn(void *p,unsigned r,unsigned w,uint64_t off,uint64_t value){
    fixture_t *f=p;if(r||w!=1||off>=8||value>255)return DRV_DENIED;f->writes++;f->bytes[off]=(uint8_t)value;return 0;
}
static int emit_fn(void *p,uint64_t value){fixture_t *f=p;if(value>255)return DRV_DENIED;f->emits++;return 0;}
static uint8_t *file(const char *path,size_t *n){FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));*n=ftell(f);rewind(f);uint8_t *p=malloc(*n);assert(p);assert(fread(p,1,*n,f)==*n);fclose(f);return p;}
int main(int argc,char **argv) {
    assert(argc==2);size_t n;uint8_t *bytes=file(argv[1],&n);drv_image_t image={0};
    assert(drv_image_load(bytes,n,&memory,&image)==0);assert(live==1);
    uint8_t scratch[DRV_SCRATCH_MAX]={0};fixture_t f={0};drv_io_t io={&f,read_fn,write_fn,emit_fn,clock_fn};
    uint64_t output[8]={0};
    f.bytes[13]=128;f.bytes[0]=0x59;f.bytes[2]=0x58;f.bytes[4]=0x23;f.bytes[7]=0x31;f.bytes[8]=0x12;f.bytes[9]=0x26;f.bytes[11]=2;
    assert(!drv_execute(&image,DRV_PROBE,NULL,output,scratch,&io,1000,1000000));
    assert(!drv_execute(&image,DRV_READ_ENTRY,NULL,output,scratch,&io,1000,1000000));
    assert(output[0]==2026&&output[1]==12&&output[2]==31&&output[3]==23&&output[4]==58&&output[5]==59);assert(!f.writes);
    f.bytes[4]=0x12;f.bytes[11]=0;
    assert(!drv_execute(&image,DRV_READ_ENTRY,NULL,output,scratch,&io,1000,1000000)&&output[3]==0);
    f.bytes[4]=0x92;assert(!drv_execute(&image,DRV_READ_ENTRY,NULL,output,scratch,&io,1000,1000000)&&output[3]==12);
    f.bytes[0]=17;f.bytes[2]=18;f.bytes[4]=19;f.bytes[7]=20;f.bytes[8]=10;f.bytes[9]=26;f.bytes[11]=6;
    assert(!drv_execute(&image,DRV_READ_ENTRY,NULL,output,scratch,&io,1000,1000000)&&output[3]==19&&output[5]==17);
    f.uip=1;assert(drv_execute(&image,DRV_READ_ENTRY,NULL,output,scratch,&io,100,1000000)==DRV_TIMEOUT);f.uip=0;
    f.time=0;assert(drv_execute(&image,DRV_READ_ENTRY,NULL,output,scratch,&io,1000,5)==DRV_TIMEOUT);
    drv_image_release(&image,&memory);assert(live==0);
    fail_allocate=1;assert(drv_image_load(bytes,n,&memory,&image)==DRV_NOMEM&&live==0);fail_allocate=0;
    for(size_t i=0;i<n;i++) {uint8_t old=bytes[i];bytes[i]^=0x81;
        int e=drv_image_load(bytes,n,&memory,&image);if(!e)drv_image_release(&image,&memory);assert(live==0);bytes[i]=old;}
    for(size_t i=0;i<n;i++){int e=drv_image_load(bytes,i,&memory,&image);assert(e!=0);assert(live==0);}
    /* Validate malicious programs after recomputing a valid integrity digest.
     * Construct literal-coded archive from the genuine immutable payload. */
    assert(!drv_image_load(bytes,n,&memory,&image));
    drv_archive_header_t *head=(drv_archive_header_t *)bytes;size_t raw_size=head->payload_size;
    uint8_t *raw=malloc(raw_size);memcpy(raw,image.payload,raw_size);drv_image_release(&image,&memory);
    size_t maximum=80+raw_size+(raw_size+127)/128;uint8_t *crafted=malloc(maximum);
    drv_member_t *members=(drv_member_t *)(raw+8);drv_manifest_t *m=(drv_manifest_t *)(raw+members[0].offset);
    drv_instruction_t *code=(drv_instruction_t *)(raw+members[1].offset);
    for(unsigned test=0;test<7;test++) {
        uint8_t *modified=malloc(raw_size);memcpy(modified,raw,raw_size);
        drv_manifest_t *m2=(drv_manifest_t *)(modified+members[0].offset);
        drv_instruction_t *c2=(drv_instruction_t *)(modified+members[1].offset);
        if(test==0)c2[0].op=255;
        if(test==1)c2[0].dst=32;
        if(test==2){c2[0].op=DRV_IO_READ;c2[0].aux=4;}
        if(test==3){c2[0].op=DRV_IO_WRITE;c2[0].b=1;c2[0].aux=0;}
        if(test==4){c2[0].op=DRV_JMP;c2[0].immediate=UINT64_MAX;}
        if(test==5)m2->features|=DRV_F_DMA;
        if(test==6)m2->features|=DRV_F_MMIO;
        drv_archive_header_t header=*head;header.payload_size=raw_size;digest(modified,raw_size,header.payload_sha256);
        size_t at=80;for(size_t i=0;i<raw_size;){size_t count=raw_size-i<128?raw_size-i:128;crafted[at++]=count-1;memcpy(crafted+at,modified+i,count);at+=count;i+=count;}
        header.compressed_size=at-80;memcpy(crafted,&header,80);
        assert(drv_image_load(crafted,at,&memory,&image)!=0&&live==0);free(modified);
    }
    (void)m;(void)code;free(crafted);free(raw);free(bytes);
    puts("PASS: actual cmos description, BCD/binary/12h/24h, budget/deadline, OOM, all truncations/mutations, unauthorized ops/features");
}
