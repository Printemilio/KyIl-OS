#include <unos/ata_pio.h>
#include <string.h>
enum { ATA_ERR=1,ATA_DRQ=8,ATA_DF=32,ATA_BSY=128 };
static uint8_t read8(ata_pio_t *d,unsigned reg) {return d->io.read8(d->io.context,d->base+reg);}
static void write8(ata_pio_t *d,unsigned reg,uint8_t v) {d->io.write8(d->io.context,d->base+reg,v);}
static void settle(ata_pio_t *d) {
    for(unsigned i=0;i<4;++i)(void)d->io.read8(d->io.context,d->control);
}
static int wait_status(ata_pio_t *d,int data) {
    uint64_t start=d->io.now_ns(d->io.context);
    for(;;) {
        uint8_t s=read8(d,7);
        if(!s||s==255)return -1;
        if(!(s&ATA_BSY)) {
            if(s&(ATA_ERR|ATA_DF))return -1;
            if(data ? (s&ATA_DRQ) : !(s&ATA_DRQ))return 0;
        }
        if(d->io.now_ns(d->io.context)-start>=UINT64_C(2000000000))return -1;
    }
}
static void select(ata_pio_t *d,uint8_t high) {
    d->io.write8(d->io.context,d->control,2u); /* polled backend, nIEN */
    write8(d,6,(uint8_t)(0xe0u|(d->slave?16u:0u)|high));settle(d);
}
int ata_pio_identify(ata_pio_t *d) {
    if(!d || !d->io.read8 || !d->io.write8 || !d->io.read16 ||
       !d->io.write16 || !d->io.now_ns || !d->base || !d->control)return -1;
    d->sectors=0;d->lba48=0;select(d,0);
    /* Do not reset the channel or disturb an ATAPI sibling. */
    write8(d,2,0);write8(d,3,0);write8(d,4,0);write8(d,5,0);write8(d,7,0xec);
    settle(d);
    if(wait_status(d,1))return -1;
    if(read8(d,4)||read8(d,5))return -1;
    uint16_t id[256];
    for(unsigned i=0;i<256;++i)id[i]=d->io.read16(d->io.context,d->base);
    if(wait_status(d,0) || (id[0]&0x8000u) || !(id[49]&(1u<<9u)))return -1;
    /* Unsupported logical sector geometry is an error, never a 512-byte lie. */
    if((id[106]&0xc000u)==0x4000u && (id[106]&0x1000u) &&
       ((uint32_t)id[117]|((uint32_t)id[118]<<16u))!=256u)return -1;
    uint64_t sectors=(uint32_t)id[60]|((uint32_t)id[61]<<16u);
    if((id[83]&0xc400u)==0x4400u) {
        sectors=(uint64_t)id[100]|((uint64_t)id[101]<<16u)|
                ((uint64_t)id[102]<<32u)|((uint64_t)id[103]<<48u);
        d->lba48=1;
    }
    if(!sectors || sectors>(d->lba48?(UINT64_C(1)<<48u):(UINT64_C(1)<<28u)))return -1;
    d->sectors=sectors;return 0;
}
int ata_pio_transfer(ata_pio_t *d,uint64_t lba,uint32_t count,void *buffer,int write) {
    if(!d || !buffer || !count || lba>=d->sectors || count>d->sectors-lba ||
       (write!=0 && write!=1))return -1;
    uint8_t *bytes=buffer;
    while(count) {
        /* 1..255 sectors avoids both count-register zero conventions. */
        uint8_t n=(uint8_t)(count>255u?255u:count);
        select(d,d->lba48?0u:(uint8_t)((lba>>24u)&15u));
        if(wait_status(d,0)) {
            /* A preceding IDENTIFY rejection may leave ERR set; normal
             * transfers on an identified device must not ignore it. */
            return -1;
        }
        if(d->lba48) {
            write8(d,2,0);write8(d,3,(uint8_t)(lba>>24u));
            write8(d,4,(uint8_t)(lba>>32u));write8(d,5,(uint8_t)(lba>>40u));
        }
        write8(d,2,n);write8(d,3,(uint8_t)lba);
        write8(d,4,(uint8_t)(lba>>8u));write8(d,5,(uint8_t)(lba>>16u));
        write8(d,7,d->lba48?(write?0x34u:0x24u):(write?0x30u:0x20u));
        settle(d);
        for(unsigned sector=0;sector<n;++sector) {
            if(wait_status(d,1))return -1;
            for(unsigned word=0;word<256;++word) {
                uint16_t value;
                if(write) {
                    memcpy(&value,bytes+word*2u,2u);d->io.write16(d->io.context,d->base,value);
                } else {
                    value=d->io.read16(d->io.context,d->base);memcpy(bytes+word*2u,&value,2u);
                }
            }
            bytes+=512u;settle(d);
        }
        if(wait_status(d,0))return -1;
        lba+=n;count-=n;
    }
    if(write) {write8(d,7,d->lba48?0xeau:0xe7u);settle(d);if(wait_status(d,0))return -1;}
    return 0;
}
int ata_pio_flush(ata_pio_t *d) {
    if(!d || !d->sectors)return -1;
    select(d,0);if(wait_status(d,0))return -1;
    write8(d,7,d->lba48?0xea:0xe7);settle(d);return wait_status(d,0);
}
