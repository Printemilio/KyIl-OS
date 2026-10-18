#include <unos/hardware_resources.h>
#ifdef HARDWARE_RESOURCE_TESTING
#include <pthread.h>
typedef pthread_mutex_t kernel_gate_t;
#define KERNEL_GATE_INIT PTHREAD_MUTEX_INITIALIZER
typedef struct {pthread_mutex_t *gate;} host_guard_t;
static host_guard_t host_enter(pthread_mutex_t *p){pthread_mutex_lock(p);return (host_guard_t){p};}
static void host_leave(host_guard_t *g){pthread_mutex_unlock(g->gate);}
#define KERNEL_GUARD(g) host_guard_t guard __attribute__((cleanup(host_leave)))=host_enter(&(g))
#else
#include <unos/kernel_guard.h>
#endif
#include <unos/driver_vm.h>
static kernel_gate_t resource_gate=KERNEL_GATE_INIT,pci_gate=KERNEL_GATE_INIT;
typedef struct {unsigned kind;uint64_t base,size,owner;unsigned shared_irq;} lease_t;
static lease_t leases[256];
int hardware_claim(unsigned kind,uint64_t base,uint64_t size,uint64_t owner) {
    KERNEL_GUARD(resource_gate);
    if(!owner || !size || kind<HW_PIO || kind>HW_PCI || base>UINT64_MAX-size)return DRV_BOUNDS;
    if(kind==HW_PIO && (base>=65536 || size>65536-base))return DRV_BOUNDS;
    int free_slot=-1;
    for(unsigned i=0;i<256;i++) {
        if(!leases[i].owner){if(free_slot<0)free_slot=(int)i;continue;}
        lease_t *p=&leases[i];if(p->kind!=kind || base>=p->base+p->size || p->base>=base+size)continue;
        if(p->owner==owner && p->base==base && p->size==size)return 0;
        return DRV_CONFLICT;
    }
    if(free_slot<0)return DRV_NOMEM;
    leases[free_slot]=(lease_t){kind,base,size,owner,0};return 0;
}
/* Trusted kernel sources may share a level-triggered wire, not the device's
 * PCI/MMIO/DMA ownership. Archive resource claims remain exclusive. */
int hardware_claim_shared_irq(uint8_t irq,uint64_t owner) {
    KERNEL_GUARD(resource_gate);
    if(!owner || irq>=16 || irq==0 || irq==1 || irq==2 || irq==8 || irq==13 || irq==7 || irq==15)return DRV_BOUNDS;
    int free_slot=-1;
    for(unsigned i=0;i<256;i++) {
        lease_t *p=&leases[i];
        if(!p->owner){if(free_slot<0)free_slot=(int)i;continue;}
        if(p->kind!=HW_IRQ || irq<p->base || irq-p->base>=p->size)continue;
        if(!p->shared_irq)return DRV_CONFLICT;
        if(p->owner==owner)return 0;
    }
    if(free_slot<0)return DRV_NOMEM;
    leases[free_slot]=(lease_t){HW_IRQ,irq,1,owner,1};return 0;
}
void hardware_release_owner(uint64_t owner) {
    KERNEL_GUARD(resource_gate);if(!owner)return;
    for(unsigned i=0;i<256;i++)if(leases[i].owner==owner)leases[i]=(lease_t){0};
}
int hardware_release(unsigned kind,uint64_t base,uint64_t size,uint64_t owner) {
    KERNEL_GUARD(resource_gate);
    for(unsigned i=0;i<256;i++)if(leases[i].owner==owner && leases[i].kind==kind && leases[i].base==base && leases[i].size==size){leases[i]=(lease_t){0};return 0;}
    return DRV_NOT_BOUND;
}
int hardware_native_pci(uint8_t bus,uint8_t slot,uint8_t fn) {
    return hardware_claim(HW_PCI,((uint64_t)bus<<8)|(slot<<3)|fn,1,UINT64_MAX);
}
int hardware_native_mmio(uint64_t base,uint64_t size) {return hardware_claim(HW_MMIO,base,size,UINT64_MAX);}
static inline void out32(uint16_t p,uint32_t v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static inline uint32_t in32(uint16_t p){uint32_t v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}
static uint32_t pci_read_locked(uint8_t bus,uint8_t slot,uint8_t fn,uint8_t off) {
    out32(0xcf8,0x80000000u|((uint32_t)bus<<16)|((uint32_t)slot<<11)|((uint32_t)fn<<8)|(off&0xfcu));return in32(0xcfc);
}
static void pci_write_locked(uint8_t bus,uint8_t slot,uint8_t fn,uint8_t off,uint32_t value) {
    out32(0xcf8,0x80000000u|((uint32_t)bus<<16)|((uint32_t)slot<<11)|((uint32_t)fn<<8)|(off&0xfcu));out32(0xcfc,value);
}
int hardware_pci_bar(uint8_t bus,uint8_t slot,uint8_t fn,unsigned bar,uint64_t owner,uint64_t *base,uint64_t *size) {
    if(!base || !size || bar>=6 || slot>=32 || fn>=8 || !owner)return DRV_BOUNDS;
    KERNEL_GUARD(pci_gate);
    /* Claim is idempotent for its owner and cannot steal an active native BDF. */
    int e=hardware_claim(HW_PCI,((uint64_t)bus<<8)|(slot<<3)|fn,1,owner);if(e)return e;
    uint8_t off=0x10+bar*4;
    uint32_t command=pci_read_locked(bus,slot,fn,4);
    if(command&7)return DRV_BUSY;
    uint32_t low=pci_read_locked(bus,slot,fn,off),high=0,maskhigh=0;
    if(low&1 || ((low&6)!=0 && (low&6)!=4) || ((low&6)==4 && bar==5))return DRV_UNSUPPORTED;
    int wide=(low&6)==4;if(wide)high=pci_read_locked(bus,slot,fn,off+4);
    pci_write_locked(bus,slot,fn,off,UINT32_MAX);
    if(wide)pci_write_locked(bus,slot,fn,off+4,UINT32_MAX);
    uint32_t masklow=pci_read_locked(bus,slot,fn,off);
    if(wide)maskhigh=pci_read_locked(bus,slot,fn,off+4);
    pci_write_locked(bus,slot,fn,off,low);
    if(wide)pci_write_locked(bus,slot,fn,off+4,high);
    uint64_t mask=((uint64_t)maskhigh<<32)|(masklow&~15u);
    if(!wide)mask|=UINT64_C(0xffffffff00000000);
    uint64_t length=~mask+1,physical=((uint64_t)high<<32)|(low&~15u);
    if(!physical || !length || (length&(length-1)) || physical%length || physical>UINT64_MAX-length)return DRV_BOUNDS;
    *base=physical;*size=length;return 0;
}
uint32_t hardware_pci_read32(uint8_t bus,uint8_t slot,uint8_t fn,uint8_t offset) {
    KERNEL_GUARD(pci_gate);return pci_read_locked(bus,slot,fn,offset);
}
void hardware_pci_write32(uint8_t bus,uint8_t slot,uint8_t fn,uint8_t offset,uint32_t value) {
    KERNEL_GUARD(pci_gate);pci_write_locked(bus,slot,fn,offset,value);
}
void hardware_pci_write_width(uint8_t bus,uint8_t slot,uint8_t fn,uint8_t off,unsigned width,uint32_t value) {
    KERNEL_GUARD(pci_gate);
    out32(0xcf8,0x80000000u|((uint32_t)bus<<16)|((uint32_t)slot<<11)|((uint32_t)fn<<8)|(off&0xfc));
    uint16_t port=0xcfcu+(off&3u);
    if(width==1)__asm__ volatile("outb %0,%1"::"a"((uint8_t)value),"Nd"(port));
    else if(width==2)__asm__ volatile("outw %0,%1"::"a"((uint16_t)value),"Nd"(port));
    else if(width==4)out32(port,value);
}
void hardware_pci_enumerate(void (*visit)(const hardware_pci_info_t *,void *),void *context) {
    uint8_t seen[256]={1},todo[256]={0};unsigned head=0,tail=1;
    while(head<tail) {
        uint8_t bus=todo[head++];
        for(uint8_t slot=0;slot<32;slot++) {
            uint32_t first=hardware_pci_read32(bus,slot,0,0);if((first&65535)==65535)continue;
            unsigned functions=(hardware_pci_read32(bus,slot,0,12)&0x800000u)?8:1;
            for(uint8_t fn=0;fn<functions;fn++) {
                uint32_t id=hardware_pci_read32(bus,slot,fn,0);if((id&65535)==65535)continue;
                hardware_pci_info_t info={bus,slot,fn,id&65535,id>>16,hardware_pci_read32(bus,slot,fn,8)>>8};
                if(visit)visit(&info,context);
                if((info.class_code>>8)==0x0604) {
                    unsigned secondary=(hardware_pci_read32(bus,slot,fn,24)>>8)&255;
                    if(secondary&&!seen[secondary]){seen[secondary]=1;todo[tail++]=(uint8_t)secondary;}
                }
            }
        }
    }
}
