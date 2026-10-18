#include <unos/driver_pci.h>
#include <string.h>
/* Approved hardware authority, not a device implementation. The archive owns
 * reset/EEPROM/link/MAC/multicast/queue/IRQ/packet protocol sequences. */
int driver_pci_contract(uint16_t vendor,uint16_t device,drv_pci_contract_t *h,drv_dma_policy_t *p) {
    if(vendor!=0x8086 || device!=0x100e)return DRV_DENIED;
    *h=(drv_pci_contract_t){.vendor=vendor,.device=device,.class_code=0x020000,.bar=0,
        .bar_min=0x20000,.bar_max=0x20000,.reset_offset=0,.reset_mask=0x04000000,
        .quiet_count=3,.reset_settle_ns=1000,.quiet={{0,0x04000000,0},{0x100,2,0},{0x400,2,0}}};
    memset(p,0,sizeof(*p));p->grant_count=3;p->max_bytes=256*1024;p->address_limit=UINT64_MAX;
    p->grants[0]=(drv_dma_grant_t){.kind=DRV_DMA_BUFFER,.rights=3,.widths=15,.max_bytes=65536};
    p->grants[1]=(drv_dma_grant_t){.kind=DRV_DMA_RING,.rights=3,.widths=15,.max_bytes=512,.stride=16,.field_count=6};
    p->grants[1].fields[0]=(drv_dma_field_t){0,8,DRV_DMA_ADDRESS,1,0};
    p->grants[1].fields[1]=(drv_dma_field_t){8,2,DRV_DMA_LENGTH,DRV_ABSENT,0};
    p->grants[1].fields[2]=(drv_dma_field_t){10,2,DRV_DMA_STATUS,DRV_ABSENT,0};
    p->grants[1].fields[3]=(drv_dma_field_t){12,1,DRV_DMA_STATUS,DRV_ABSENT,0};
    p->grants[1].fields[4]=(drv_dma_field_t){13,1,DRV_DMA_STATUS,DRV_ABSENT,0};
    p->grants[1].fields[5]=(drv_dma_field_t){14,2,DRV_DMA_STATUS,DRV_ABSENT,0};
    p->grants[2]=(drv_dma_grant_t){.kind=DRV_DMA_RING,.rights=3,.widths=15,.max_bytes=512,.stride=16,.field_count=7};
    p->grants[2].fields[0]=(drv_dma_field_t){0,8,DRV_DMA_ADDRESS,1,0};
    p->grants[2].fields[1]=(drv_dma_field_t){8,2,DRV_DMA_LENGTH,DRV_ABSENT,0};
    p->grants[2].fields[2]=(drv_dma_field_t){10,1,DRV_DMA_SCALAR,DRV_ABSENT,0};
    p->grants[2].fields[3]=(drv_dma_field_t){11,1,DRV_DMA_SCALAR,DRV_ABSENT,0x0b};
    p->grants[2].fields[4]=(drv_dma_field_t){12,1,DRV_DMA_STATUS,DRV_ABSENT,0};
    p->grants[2].fields[5]=(drv_dma_field_t){13,1,DRV_DMA_SCALAR,DRV_ABSENT,0};
    p->grants[2].fields[6]=(drv_dma_field_t){14,2,DRV_DMA_SCALAR,DRV_ABSENT,0};
    /* Address/length/tail registers are inaccessible to scalar writes. */
    uint32_t queue[]={0x2800,0x2804,0x2808,0x2818,0x3800,0x3804,0x3808,0x3818};
    unsigned kinds[]={DRV_REG_ADDRESS_LOW,DRV_REG_ADDRESS_HIGH,DRV_REG_LENGTH,DRV_REG_DOORBELL};
    for(unsigned i=0;i<8;i++)p->registers[p->register_count++]=(drv_dma_register_t){.offset=queue[i],.width=4,.kind=kinds[i%4],.rights=i%4>=2?3:DRV_WRITE};
    const struct {uint32_t off,rights;uint64_t mask;} scalar[]={
        {0,3,0x04001a61},{8,1,0},{0x14,3,0xff01},{0xc0,1,0},{0xd0,3,0x000000dd},{0xd8,2,UINT32_MAX},
        {0x100,3,0x0400803e},{0x400,3,0x013ff0fau},{0x410,3,0x3fffffff},
        {0x2810,3,0},{0x3810,3,0},{0x5400,3,UINT32_MAX},{0x5404,3,0x8000ffff},
        {0x4074,1,0},{0x4078,1,0},{0x407c,1,0},{0x40a0,1,0},{0x4010,1,0},{0x40d4,1,0}};
    for(unsigned i=0;i<sizeof(scalar)/sizeof(scalar[0]);i++)p->registers[p->register_count++]=(drv_dma_register_t){
        .offset=scalar[i].off,.width=4,.kind=DRV_REG_SCALAR,.rights=scalar[i].rights,.write_mask=scalar[i].mask};
    p->registers[p->register_count++]=(drv_dma_register_t){.offset=0x5200,.width=4,.kind=DRV_REG_SCALAR,.rights=3,.write_mask=UINT32_MAX,.count=128,.stride=4};
    for(unsigned i=0;i<p->register_count;i++) {
        if(p->registers[i].offset==0)p->registers[i].invalidate_mask=0x04000000;
        if(p->registers[i].offset==0x100){p->registers[i].guard_mask=2;p->registers[i].require_endpoint=0;}
        if(p->registers[i].offset==0x400){p->registers[i].guard_mask=2;p->registers[i].require_endpoint=1;}
        if(p->registers[i].offset==0x2810)p->registers[i].block_endpoint=1;
        if(p->registers[i].offset==0x3810)p->registers[i].block_endpoint=2;
    }
    p->endpoint_count=2;
    p->endpoints[0]=(drv_dma_endpoint_t){.grant=1,.low=0,.high=1,.length=2,.doorbell=3,.doorbell_value=31,
        .streaming=1,.initial_owned=1,.length_output=1,.completion_field=3,.completion_mask=1,.completion_value=1,.min_span=2048,.min_slots=32,.max_slots=32};
    p->endpoints[1]=(drv_dma_endpoint_t){.grant=2,.low=4,.high=5,.length=6,.doorbell=7,.doorbell_value=0,
        .streaming=1,.post_next=1,.completion_field=4,.completion_mask=1,.completion_value=1,.min_span=2048,.min_slots=32,.max_slots=32};
    return 0;
}
