#include <unos/driver_pci.h>
#include <unos/vmm.h>
#include <unos/irq.h>
#include <unos/sched_clock.h>
#include <unos/smp.h>
#include <string.h>
static unsigned window_lock;
static uint8_t windows[8];
static void release_window(drv_pci_t *d) {
    if(!d->window_slot)return;
    while(__atomic_exchange_n(&window_lock,1,__ATOMIC_ACQUIRE))smp_relax();
    windows[d->window_slot-1]=0;d->window_slot=0;
    __atomic_store_n(&window_lock,0,__ATOMIC_RELEASE);
}
static void find(const hardware_pci_info_t *p,void *context) {
    drv_pci_t *d=context;
    if(!d->info.vendor && p->vendor==d->contract.vendor && p->device==d->contract.device && p->class_code==d->contract.class_code)d->info=*p;
}
static uint32_t read_reg(drv_pci_t *d,uint32_t off){return *(volatile uint32_t *)(d->mmio+off);}
static int reg_read(void *context,uint32_t off,unsigned width,uint64_t *out) {
    drv_pci_t *d=context;if(width!=4 || off%4 || off>d->length || width>d->length-off)return DRV_DENIED;
    *out=read_reg(d,off);d->last_offset=off;d->last_value=*out;d->reads++;return 0;
}
static int reg_write(void *context,uint32_t off,unsigned width,uint64_t value) {
    drv_pci_t *d=context;if(width!=4 || off%4 || off>d->length || width>d->length-off || value>UINT32_MAX)return DRV_DENIED;
    *(volatile uint32_t *)(d->mmio+off)=(uint32_t)value;
    d->last_offset=off;d->last_value=value;d->writes++;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if(off==d->contract.reset_offset && (value&d->contract.reset_mask)) {
        uint64_t start=sched_clock_ns();
        while(sched_clock_ns()-start<d->contract.reset_settle_ns)smp_relax();
    }
    /* A posted write is drained by a non-destructive, approved reset register. */
    (void)read_reg(d,d->contract.reset_offset);
    if(off==d->contract.reset_offset && (value&d->contract.reset_mask))d->reset_generation++;
    return 0;
}
static int quiesced(void *context,unsigned endpoint) {
    drv_pci_t *d=context;(void)endpoint;
    if(!d->dma_started && !d->reset_required)return (hardware_pci_read32(d->info.bus,d->info.slot,d->info.function,4)&4)?DRV_BUSY:0;
    if(!d->reset_required || d->reset_generation<d->reset_required ||
       (hardware_pci_read32(d->info.bus,d->info.slot,d->info.function,4)&4))return DRV_BUSY;
    for(unsigned i=0;i<d->contract.quiet_count;i++) {
        const drv_pci_condition_t *p=&d->contract.quiet[i];
        if((read_reg(d,p->offset)&p->mask)!=p->value)return DRV_BUSY;
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);return 0;
}
drv_dma_provider_t driver_pci_provider(drv_pci_t *d) {
    drv_dma_provider_t p=driver_dma_memory_provider();p.context=d;p.reg_read=reg_read;p.reg_write=reg_write;p.quiesced=quiesced;return p;
}
int driver_pci_open(drv_pci_t *d,uint64_t owner,const drv_manifest_t *m) {
    if(!d || !owner || m->vendor>65535 || m->device>65535)return DRV_DENIED;
    memset(d,0,sizeof(*d));d->owner=owner;
    int e=driver_pci_contract(m->vendor,m->device,&d->contract,&d->policy);if(e)return e;
    if(m->pci_class!=d->contract.class_code)return DRV_DENIED;
    hardware_pci_enumerate(find,d);if(!d->info.vendor)return DRV_NOT_BOUND;
    uint64_t bdf=((uint64_t)d->info.bus<<8)|(d->info.slot<<3)|d->info.function;
    e=hardware_claim(HW_PCI,bdf,1,owner);if(e)return e;d->claimed=1;
    uint32_t cmd=hardware_pci_read32(d->info.bus,d->info.slot,d->info.function,4)&65535;
    hardware_pci_write32(d->info.bus,d->info.slot,d->info.function,4,cmd&~7u);
    e=hardware_pci_bar(d->info.bus,d->info.slot,d->info.function,d->contract.bar,owner,&d->physical,&d->length);if(e)goto fail;
    if(d->length<d->contract.bar_min || d->length>d->contract.bar_max || d->physical%4096){e=DRV_DENIED;goto fail;}
    d->irq=hardware_pci_read32(d->info.bus,d->info.slot,d->info.function,0x3c)&255;
    if(d->irq!=m->irq || d->irq!=11){e=DRV_DENIED;goto fail;}
    e=hardware_claim(HW_MMIO,d->physical,d->length,owner);if(e)goto fail;
    e=hardware_claim(HW_IRQ,d->irq,1,owner);if(e)goto fail;
    irq_mask_pic(d->irq,1);
    while(__atomic_exchange_n(&window_lock,1,__ATOMIC_ACQUIRE))smp_relax();
    for(unsigned i=0;i<8;i++)if(!windows[i]){windows[i]=1;d->window_slot=i+1;break;}
    __atomic_store_n(&window_lock,0,__ATOMIC_RELEASE);
    if(!d->window_slot || d->length>0x200000){e=DRV_NOMEM;goto fail;}
    d->virtual_base=UINT64_C(0xffffffffb1000000)+(d->window_slot-1)*UINT64_C(0x200000);
    for(uint64_t off=0;off<d->length;off+=4096) {
        vmm_map_kernel_page(d->virtual_base+off,d->physical+off,VMM_PAGE_PRESENT|VMM_PAGE_WRITABLE|VMM_PAGE_PWT|VMM_PAGE_PCD|VMM_PAGE_NX);
        d->mapped+=4096;
        uint64_t cr3;__asm__ volatile("mov %%cr3,%0":"=r"(cr3));
        if(vmm_get_physical(vmm_cr3_to_address_space(cr3),d->virtual_base+off)!=d->physical+off){e=DRV_NOMEM;goto fail;}
    }
    d->mmio=(volatile uint8_t *)d->virtual_base;
    hardware_pci_write32(d->info.bus,d->info.slot,d->info.function,4,(cmd|2u)&~5u);
    return 0;
fail:
    hardware_pci_write32(d->info.bus,d->info.slot,d->info.function,4,cmd&~7u);
    if(d->mapped && vmm_unmap_kernel_range(d->virtual_base,d->mapped))return DRV_BUSY;
    release_window(d);
    hardware_release_owner(owner);d->claimed=d->mapped=0;return e;
}
void driver_pci_prepare_stop(drv_pci_t *d){d->reset_required=d->reset_generation+1;irq_mask_pic(d->irq,1);}
int driver_pci_disable(drv_pci_t *d) {
    uint32_t cmd=hardware_pci_read32(d->info.bus,d->info.slot,d->info.function,4)&65535;
    hardware_pci_write32(d->info.bus,d->info.slot,d->info.function,4,cmd&~4u);
    return quiesced(d,0);
}
int driver_pci_enable_dma(drv_pci_t *d) {
    int e=quiesced(d,0);if(e)return e;
    uint32_t cmd=hardware_pci_read32(d->info.bus,d->info.slot,d->info.function,4)&65535;
    hardware_pci_write32(d->info.bus,d->info.slot,d->info.function,4,(cmd|6u)&~1u);
    d->reset_required=0;d->dma_started=1;return 0;
}
int driver_pci_close(drv_pci_t *d) {
    int e=quiesced(d,0);if(e)return e;
    uint32_t cmd=hardware_pci_read32(d->info.bus,d->info.slot,d->info.function,4)&65535;
    hardware_pci_write32(d->info.bus,d->info.slot,d->info.function,4,cmd&~7u);
    if(d->mapped && vmm_unmap_kernel_range(d->virtual_base,d->mapped))return DRV_BUSY;
    release_window(d);
    hardware_release_owner(d->owner);d->mapped=d->claimed=0;d->mmio=NULL;return 0;
}
