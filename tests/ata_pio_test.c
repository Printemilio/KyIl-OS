#include <unos/ata_pio.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct fixture {
    uint16_t identify[256],data[512];
    unsigned words,reads,writes,commands,flushes;
    uint8_t status,command,registers[8];
    uint64_t clock;
    int fail,stuck,absent;
} fixture_t;
static uint8_t rd8(void *opaque,uint16_t port) {
    fixture_t *f=opaque;
    if(port==0x3f6)return f->status;
    if((port&7)==7)return f->absent?255:f->stuck?128:f->fail?1:f->status;
    return f->registers[port&7];
}
static void wr8(void *opaque,uint16_t port,uint8_t value) {
    fixture_t *f=opaque;unsigned reg=port&7;
    if(port==0x3f6)return;
    f->registers[reg]=value;
    if(reg==7) {
        f->commands++;f->command=value;f->words=0;
        if(value==0xe7||value==0xea){f->flushes++;f->status=0x40;}
        else f->status=0x48;
    }
}
static uint16_t rd16(void *opaque,uint16_t port) {
    (void)port;fixture_t *f=opaque;f->reads++;
    unsigned n=f->words++;
    uint16_t value=f->command==0xec?f->identify[n]:f->data[n];
    unsigned limit=f->command==0xec?256u:(unsigned)f->registers[2]*256u;
    if(f->words==limit)f->status=0x40;
    return value;
}
static void wr16(void *opaque,uint16_t port,uint16_t value) {
    (void)port;fixture_t *f=opaque;f->writes++;f->data[f->words++]=value;
    if(f->words==(unsigned)f->registers[2]*256u)f->status=0x40;
}
static uint64_t now(void *opaque) {fixture_t *f=opaque;return f->clock+=100000000u;}
int main(void) {
    fixture_t f={0};f.identify[49]=1<<9;f.identify[60]=8192;
    ata_pio_t d={.io={&f,rd8,wr8,rd16,wr16,now},.base=0x1f0,.control=0x3f6};
    assert(ata_pio_identify(&d)==0 && d.sectors==8192 && !d.lba48);
    unsigned char storage[1025];unsigned char *buffer=storage+1;
    for(unsigned i=0;i<1024;++i)buffer[i]=(unsigned char)(i*37u);
    assert(!ata_pio_transfer(&d,2048,2,buffer,1));assert(f.flushes==1 && f.writes==512);
    assert(!ata_pio_flush(&d) && f.flushes==2 && f.command==0xe7);
    memset(buffer,0,1024);assert(!ata_pio_transfer(&d,2048,2,buffer,0));
    for(unsigned i=0;i<1024;++i)assert(buffer[i]==(unsigned char)(i*37u));
    unsigned before=f.commands;
    assert(ata_pio_transfer(&d,8191,2,buffer,0)<0);
    assert(ata_pio_transfer(&d,UINT64_MAX,2,buffer,0)<0);
    assert(ata_pio_transfer(&d,0,0,buffer,0)<0 && f.commands==before);
    f.fail=1;assert(ata_pio_transfer(&d,0,1,buffer,0)<0);f.fail=0;
    f.stuck=1;assert(ata_pio_transfer(&d,0,1,buffer,0)<0);f.stuck=0;
    f.identify[83]=0x4400;f.identify[100]=10;f.identify[103]=1;
    assert(ata_pio_identify(&d)<0); /* 48-bit capacity overflow */
    f.identify[103]=0;f.identify[102]=1;
    assert(!ata_pio_identify(&d) && d.lba48 && d.sectors==UINT64_C(0x10000000a));
    assert(!ata_pio_flush(&d) && f.command==0xea);
    f.fail=1;assert(ata_pio_flush(&d)<0);f.fail=0;
    f.stuck=1;assert(ata_pio_flush(&d)<0);f.stuck=0;
    f.absent=1;assert(ata_pio_identify(&d)<0);
    puts("ata-pio: PASS identify/LBA28/LBA48/unaligned/read/write/flush/bounds/errors/timeout");
}
