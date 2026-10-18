#include <unos/driver_manager.h>
#include <unos/driver_dma.h>
#include <unos/driver_pci.h>
#include <unos/netdev.h>
#include <unos/hardware_resources.h>
#include <unos/kernel_guard.h>
#include <unos/event_counter.h>
#include <unos/fs.h>
#include <unos/heap.h>
#include <unos/irq.h>
#include <unos/rtc.h>
#include <unos/security.h>
#include <unos/users.h>
#include <unos/tty.h>
#include <unos/debug_log.h>
#include <mbedtls/sha256.h>
#include <string.h>
#include <stdio.h>

#define DRIVER_SLOTS 8u
#define TRUST_MAX 24u
#define TRUST_BOOT "/drivers/boot.trust"
#define TRUST_LOCAL "/home/root/packages/driver-trust"
#define TRUST_AUTO 1u
enum {DEV_FREE,DEV_LOADING,DEV_ACTIVE,DEV_STOPPING,DEV_FAULTED};
typedef struct __attribute__((packed)) trust_entry {
    char path[192];uint8_t digest[32];uint32_t flags;uint8_t reserved[28];
} trust_entry_t;
_Static_assert(sizeof(trust_entry_t)==256,"driver trust ABI");
typedef struct platform_grant {
    const char *name;unsigned device_class,kind;uint16_t base,length;uint8_t irq;
    uint64_t read_mask;uint8_t write_masks[8];
} platform_grant_t;
/* Authority is a host policy, not a manifest claim. NMI selection bit cannot be
 * changed by a package. CMOS writes and UART DMA mode are deliberately denied. */
static const platform_grant_t platforms[]={
    {"rtc.cmos",DRV_CLASS_CLOCK,DRV_INDEXED_PIO,0x70,16,255,
        (1ull<<0)|(1ull<<2)|(1ull<<4)|(1ull<<7)|(1ull<<8)|(1ull<<9)|(1ull<<10)|(1ull<<11)|(1ull<<13),{0}},
    {"uart.com2",DRV_CLASS_CHARACTER,DRV_PIO,0x2f8,8,3,0xff,
        {0xff,0xff,0xc7,0xff,0x1f,0,0,0xff}}
};
static const platform_grant_t network_grant={.name="pci.network",.device_class=DRV_CLASS_NETWORK,.kind=DRV_MMIO,.irq=11};
typedef struct driver_instance {
    uint32_t id,state,refs;const platform_grant_t *grant;
    drv_image_t image;kernel_gate_t gate;
    uint8_t scratch[DRV_SCRATCH_MAX],rx[256];unsigned rx_head,rx_count;
    uint64_t counter,executions,io_reads,io_writes,interrupts,timeouts,emitted;
    uint64_t irq_window_ns,irq_work_ns;unsigned irq_burst;
    int last_error,irq_claimed;
    char path[192];
    drv_pci_t *pci;drv_dma_context_t *dma;drv_io_t dma_io;
    uint64_t net_tx,net_rx,net_irq,net_broadcast,net_multicast,net_no_buffer,net_missed,native_start[3];
    int packet_error;unsigned packet_entry;uint64_t packet_ns;
} driver_instance_t;
static driver_instance_t instances[DRIVER_SLOTS];
static kernel_gate_t manager_gate=KERNEL_GATE_INIT,install_gate=KERNEL_GATE_INIT;
static trust_entry_t trust[TRUST_MAX];static unsigned trust_count;
static uint32_t next_id=1;static int manager_ready;
static int digest(const void *p,size_t n,uint8_t out[32]){return mbedtls_sha256(p,n,out,0);}
static const drv_allocator_t allocator={kmalloc,kfree,digest};
static inline void port_write(uint16_t p,unsigned n,uint64_t v) {
    if(n==1)__asm__ volatile("outb %0,%1"::"a"((uint8_t)v),"Nd"(p));
    else if(n==2)__asm__ volatile("outw %0,%1"::"a"((uint16_t)v),"Nd"(p));
    else __asm__ volatile("outl %0,%1"::"a"((uint32_t)v),"Nd"(p));
}
static inline uint64_t port_read(uint16_t p,unsigned n) {
    if(n==1){uint8_t v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
    if(n==2){uint16_t v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
    uint32_t v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;
}
static uint64_t vm_clock(void *c){(void)c;return sched_clock_ns();}
static int io_access(driver_instance_t *d,unsigned resource,unsigned width,uint64_t off,int write,uint64_t *value) {
    if(resource>=d->image.manifest.resource_count)return DRV_DENIED;
    const drv_requirement_t *r=&d->image.manifest.resources[resource];const platform_grant_t *g=d->grant;
    if(!g || width!=1 || !(r->rights&(write?DRV_WRITE:DRV_READ)) || !(r->widths&width) ||
       off>r->length || width>r->length-off || r->offset>g->length || off>g->length-r->offset ||
       width>g->length-r->offset-off)return DRV_DENIED;
    off+=r->offset;
    if(write) {
        if(g->kind!=DRV_PIO || off>=8 || !g->write_masks[off] || (*value&~(uint64_t)g->write_masks[off]))return DRV_DENIED;
        port_write(g->base+(uint16_t)off,1,*value);d->io_writes++;
    } else {
        if(!(g->read_mask&(1ull<<off)))return DRV_DENIED;
        if(g->kind==DRV_INDEXED_PIO) {port_write(g->base,1,0x80u|off);*value=port_read(g->base+1,1);}
        else *value=port_read(g->base+(uint16_t)off,1);
        d->io_reads++;
    }
    return 0;
}
static int vm_read(void *c,unsigned r,unsigned w,uint64_t off,uint64_t *v){return io_access(c,r,w,off,0,v);}
static int vm_write(void *c,unsigned r,unsigned w,uint64_t off,uint64_t v){return io_access(c,r,w,off,1,&v);}
static int vm_emit(void *c,uint64_t value) {
    driver_instance_t *d=c;
    if(d->image.manifest.device_class!=DRV_CLASS_CHARACTER || value>255)return DRV_DENIED;
    if(d->rx_count==sizeof(d->rx))return DRV_BUSY;
    d->rx[(d->rx_head+d->rx_count)%sizeof(d->rx)]=(uint8_t)value;d->rx_count++;d->emitted++;
    return vex_event_counter_signal(d->counter,1)==0?0:DRV_IO;
}
static int execute(driver_instance_t *d,unsigned entry,const uint64_t in[8],uint64_t out[8]) {
    kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter(&d->gate);
    drv_io_t io=d->dma?d->dma_io:(drv_io_t){.context=d,.read=vm_read,.write=vm_write,.emit=vm_emit,.clock_ns=vm_clock};d->executions++;
    int result=drv_execute(&d->image,entry,in,out,d->scratch,&io,
        entry==DRV_IRQ_ENTRY?256:20000,entry==DRV_IRQ_ENTRY?200000:
        d->dma && (entry==DRV_INIT || entry==DRV_STOP)?100000000:
        d->dma && (entry==DRV_READ_ENTRY || entry==DRV_WRITE_ENTRY)?10000000:1000000);
    d->last_error=result;if(result==DRV_TIMEOUT)d->timeouts++;return result;
}
static driver_instance_t *pin(uint32_t id) {
    KERNEL_GUARD(manager_gate);
    for(unsigned i=0;i<DRIVER_SLOTS;i++)if(instances[i].id==id && instances[i].state==DEV_ACTIVE){instances[i].refs++;return &instances[i];}
    return NULL;
}
static void unpin(driver_instance_t *d){KERNEL_GUARD(manager_gate);if(d->refs)d->refs--;}
static void retire(driver_instance_t *d) {
    drv_image_t image;
    {KERNEL_GUARD(manager_gate);
        image=d->image;memset(&d->image,0,sizeof(d->image));d->state=DEV_FREE;}
    /* No metadata reader sees a half-cleared manifest. A newly claimed slot
     * cannot inherit the retired payload; freeing happens outside registry. */
    drv_image_release(&image,&allocator);
}
static void described_irq(interrupt_frame_t *frame) {
    driver_instance_t *d=NULL;
    {KERNEL_GUARD(manager_gate);
        for(unsigned i=0;i<DRIVER_SLOTS;i++)if(instances[i].state==DEV_ACTIVE && instances[i].image.manifest.irq+0x20==frame->vector){d=&instances[i];d->refs++;break;}}
    if(!d)return;
    uint64_t start=sched_clock_ns();int result=0;
    {kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter(&d->gate);
        d->interrupts++;
        if(start-d->irq_window_ns>=100000000){d->irq_window_ns=start;d->irq_work_ns=0;d->irq_burst=0;}
        if(++d->irq_burst>4096 || d->irq_work_ns>=20000000)result=DRV_TIMEOUT;}
    if(!result)result=execute(d,DRV_IRQ_ENTRY,NULL,NULL);
    {kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter(&d->gate);
        d->irq_work_ns+=sched_clock_ns()-start;if(result)d->last_error=result;}
    if(result){irq_mask_pic((uint8_t)d->image.manifest.irq,1);
        {KERNEL_GUARD(manager_gate);if(d->state==DEV_ACTIVE)d->state=DEV_FAULTED;}
        if(d->counter)(void)vex_event_counter_signal(d->counter,1);}
    unpin(d);
}
int driver_clock_read(uint64_t date[8]) {
    driver_instance_t *d=NULL;
    {KERNEL_GUARD(manager_gate);
        for(unsigned i=0;i<DRIVER_SLOTS;i++)if(instances[i].state!=DEV_FREE && instances[i].image.manifest.device_class==DRV_CLASS_CLOCK){
            if(instances[i].state!=DEV_ACTIVE)return DRV_BUSY;
            d=&instances[i];d->refs++;break;}}
    if(!d)return DRV_NOT_BOUND;
    int result=execute(d,DRV_READ_ENTRY,NULL,date);unpin(d);return result;
}
int driver_character_write(uint32_t id,uint8_t byte) {
    driver_instance_t *d=pin(id);if(!d)return DRV_NOT_BOUND;
    uint64_t in[8]={byte};int e=d->image.manifest.device_class==DRV_CLASS_CHARACTER?execute(d,DRV_WRITE_ENTRY,in,NULL):DRV_DENIED;
    unpin(d);return e;
}
int driver_character_read(uint32_t id,uint8_t *byte,uint64_t ticks) {
    if(!byte)return DRV_BOUNDS;uint64_t start=scheduler_ticks_now();
    for(;;) {
        driver_instance_t *d=pin(id);if(!d)return DRV_NOT_BOUND;
        if(d->image.manifest.device_class!=DRV_CLASS_CHARACTER){unpin(d);return DRV_DENIED;}
        uint64_t counter=d->counter;int found=0;
        {kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter(&d->gate);
            if(d->rx_count){*byte=d->rx[d->rx_head];d->rx_head=(d->rx_head+1)%sizeof(d->rx);d->rx_count--;found=1;}}
        /* No driver reference crosses sleep/kill. The existing counter owns its
         * waiter lifetime; closing it wakes consumers and stale IDs cannot pin. */
        unpin(d);if(found)return 0;if(!ticks)return DRV_AGAIN;
        uint64_t elapsed=scheduler_ticks_now()-start;if(elapsed>=ticks)return DRV_TIMEOUT;
        uint64_t value;
        if(vex_event_counter_read_timed(counter,&value,0,ticks-elapsed)!=0) {
            d=pin(id);if(!d)return DRV_NOT_BOUND;unpin(d);return DRV_TIMEOUT;
        }
    }
}
static int trusted_digest(const char *path,uint8_t hash[32]) {
    KERNEL_GUARD(manager_gate);
    for(unsigned i=0;i<trust_count;i++)if(!strcmp(path,trust[i].path)){memcpy(hash,trust[i].digest,32);return 0;}
    return DRV_DENIED;
}
static int read_image(const char *path,const uint8_t expected[32],drv_image_t *image) {
    ora_view_t view={0};void *data=NULL;size_t size=0;
    int read=fs_read_view_bounded(path,&data,&size,&view,DRV_ARCHIVE_MAX);
    if(read)return read==-3?DRV_BOUNDS:DRV_IO;
    uint8_t hash[32];int e;
    if(size>DRV_ARCHIVE_MAX)e=DRV_BOUNDS;
    else if(digest(data,size,hash)||memcmp(hash,expected,32))e=DRV_INTEGRITY;
    else e=drv_image_load(data,size,&allocator,image);
    ora_view_release(&view);return e;
}
static int policy(const drv_manifest_t *m,const platform_grant_t **grant) {
    if(m->device_class==DRV_CLASS_NETWORK) {
        if(m->bus!=DRV_BUS_PCI || strcmp(m->platform,network_grant.name) || m->irq!=11 || m->engine_min!=2 ||
           m->features!=(DRV_F_BASE|DRV_F_BUFFER|DRV_F_DMA|DRV_F_MMIO|DRV_F_IRQ) || m->scratch_size<3584 ||
           m->entries[DRV_READ_ENTRY]==DRV_ABSENT || m->entries[DRV_WRITE_ENTRY]==DRV_ABSENT ||
           m->entries[DRV_CONTROL]==DRV_ABSENT || m->entries[DRV_DIAGNOSTIC]==DRV_ABSENT)return DRV_DENIED;
        drv_pci_contract_t h;drv_dma_policy_t authority;
        int e=driver_pci_contract(m->vendor,m->device,&h,&authority);if(e)return e;
        if(m->pci_class!=h.class_code)return DRV_DENIED;
        *grant=&network_grant;return 0;
    }
    /* No guessed BAR lengths or USB facade. No granted PCI BAR policy exists
     * yet; discovery and ownership still track those devices for native drivers. */
    /* No archive can widen UART/RTC authority by adding DMA resources/features.
     * A future PCI adapter must install a validated DMA schema and register
     * mediation together, before granting ANY hardware access. */
    if(m->bus!=DRV_BUS_PLATFORM || (m->features&DRV_F_DMA))return DRV_DENIED;
    for(unsigned i=0;i<sizeof(platforms)/sizeof(platforms[0]);i++)if(!strcmp(m->platform,platforms[i].name)){
        const platform_grant_t *g=&platforms[i];
        if(m->device_class!=g->device_class || m->resource_count!=1 || m->vendor || m->device || m->pci_class)return DRV_DENIED;
        const drv_requirement_t *r=&m->resources[0];
        if(r->kind!=g->kind || r->selector || r->offset>g->length || r->length>g->length-r->offset ||
           r->widths!=1 || (g->kind==DRV_INDEXED_PIO && r->rights!=DRV_READ) ||
           (m->irq!=DRV_ABSENT && m->irq!=g->irq))return DRV_DENIED;
        if(g->device_class==DRV_CLASS_CHARACTER && (m->irq!=g->irq || m->entries[DRV_WRITE_ENTRY]==DRV_ABSENT))return DRV_DENIED;
        *grant=g;return 0;
    }
    return DRV_DENIED;
}
typedef struct activation {const char *path;drv_image_t *image;const platform_grant_t *grant;uint32_t *id;} activation_t;
static void network_fault(driver_instance_t *d,int e) {
    if(e==DRV_AGAIN || e==DRV_BUSY || !e)return;
    irq_mask_pic(d->pci->irq,1);
    KERNEL_GUARD(manager_gate);
    if(d->state==DEV_ACTIVE){d->state=DEV_FAULTED;d->last_error=e;debug_log_puts("driver-net: datapath fault, quarantined until proven stop\n");}
}
static int net_send(void *context,const void *bytes,size_t size) {
    driver_instance_t *d=context;if(!bytes || !size || size>1518)return DRV_BOUNDS;
    if(!pin(d->id))return DRV_NOT_BOUND;
    int e;
    {KERNEL_GUARD(d->gate);memcpy(d->scratch,bytes,size);uint64_t input[8]={size};
        uint64_t started=sched_clock_ns();e=execute(d,DRV_WRITE_ENTRY,input,NULL);if(!e)d->net_tx++;
        else if(e!=DRV_AGAIN && e!=DRV_BUSY){d->packet_error=e;d->packet_entry=DRV_WRITE_ENTRY;d->packet_ns=sched_clock_ns()-started;}}
    network_fault(d,e);unpin(d);return e;
}
static int net_receive(void *context,void *bytes,size_t *size) {
    driver_instance_t *d=context;if(!bytes || !size)return DRV_BOUNDS;
    if(!pin(d->id))return DRV_NOT_BOUND;
    int e;
    {KERNEL_GUARD(d->gate);uint64_t out[8]={0};uint64_t started=sched_clock_ns();e=execute(d,DRV_READ_ENTRY,NULL,out);
        if(e && e!=DRV_AGAIN && e!=DRV_BUSY){d->packet_error=e;d->packet_entry=DRV_READ_ENTRY;d->packet_ns=sched_clock_ns()-started;}
        if(!e){if(!out[0] || out[0]>1518 || out[0]>*size)e=DRV_BOUNDS;
            else {memcpy(bytes,d->scratch,out[0]);*size=out[0];d->net_rx++;
                unsigned all=0;for(unsigned i=0;i<6;i++)all+=d->scratch[i]==255;
                if(all==6)d->net_broadcast++;else if(d->scratch[0]&1)d->net_multicast++;}}}
    network_fault(d,e);unpin(d);return e;
}
static int net_link(void *context) {
    driver_instance_t *d=context;if(!pin(d->id))return 0;
    uint64_t out[8]={0};int e=execute(d,DRV_CONTROL,NULL,out);unpin(d);return !e && out[0]==1;
}
static void net_irq(void *context) {
    driver_instance_t *d=context;if(!pin(d->id))return;
    int e=0;uint64_t start=sched_clock_ns();
    {KERNEL_GUARD(d->gate);d->net_irq++;
        if(start-d->irq_window_ns>=100000000){d->irq_window_ns=start;d->irq_work_ns=0;d->irq_burst=0;}
        if(++d->irq_burst>4096 || d->irq_work_ns>=20000000)e=DRV_TIMEOUT;
        if(!e)e=execute(d,DRV_IRQ_ENTRY,NULL,NULL);
        d->irq_work_ns+=sched_clock_ns()-start;}
    if(e){irq_mask_pic(d->pci->irq,1);KERNEL_GUARD(manager_gate);d->state=DEV_FAULTED;d->last_error=e;}
    unpin(d);
}
static void net_stats(void *context,netdev_stats_t *s) {
    driver_instance_t *d=context;if(!pin(d->id))return;
    {KERNEL_GUARD(d->gate);uint64_t out[8]={0};
        if(!execute(d,DRV_DIAGNOSTIC,NULL,out)){s->link_up=out[0];s->rx_head=out[1];s->rx_tail=out[2];s->rx_control=out[3];d->net_no_buffer+=out[4];d->net_missed+=out[5];
            for(unsigned i=0;i<4;i++)s->rx_descriptor_status[i]=out[6]>>(i*8);}
        s->tx_packets=d->net_tx;s->rx_packets=d->net_rx;s->rx_broadcast=d->net_broadcast;s->rx_multicast=d->net_multicast;
        s->rx_no_buffer=d->net_no_buffer;s->missed_packets=d->net_missed;}
    unpin(d);
}
static const netdev_driver_ops_t network_ops={net_send,net_receive,net_link,net_irq,net_stats};
static int network_cleanup(driver_instance_t *d) {
    int e=0;
    if(d->pci && d->pci->claimed) {
        driver_pci_prepare_stop(d->pci);
        if(d->dma && d->dma->attached)e=execute(d,DRV_STOP,NULL,NULL);
        int proof=driver_pci_disable(d->pci);if(!e)e=proof;
        if(e)return e; /* Quarantine rather than freeing on a claimed stop. */
    }
    if(d->dma && d->dma->epoch) {
        e=drv_dma_quiesce_all(d->dma);if(e)return e;
        e=drv_dma_destroy(d->dma);if(e)return e;
        char line[110];snprintf(line,sizeof(line),"driver-net: DMA retired id=%u bytes=%u metadata=%u\n",d->id,(unsigned)d->dma->bytes,(unsigned)d->dma->metadata_bytes);debug_log_puts(line);
    }
    if(d->pci && d->pci->claimed){e=driver_pci_close(d->pci);if(e)return e;}
    kfree(d->dma);d->dma=NULL;kfree(d->pci);d->pci=NULL;return 0;
}
static int network_start(void *context) {
    driver_instance_t *d=context;int was_native=strcmp(netdev_type(),"none")!=0;
    int e=netdev_native_suspend();if(e)return e;
    d->pci=kmalloc(sizeof(*d->pci));d->dma=kmalloc(sizeof(*d->dma));
    if(d->pci)memset(d->pci,0,sizeof(*d->pci));if(d->dma)memset(d->dma,0,sizeof(*d->dma));
    if(!d->pci || !d->dma){e=DRV_NOMEM;goto fail;}
    e=driver_pci_open(d->pci,UINT64_C(0x4452000000000000)|d->id,&d->image.manifest);if(e)goto fail;
    drv_dma_provider_t provider=driver_pci_provider(d->pci);
    e=drv_dma_init(d->dma,&d->pci->policy,&provider);if(e)goto fail;
    e=drv_dma_attach_io(d->dma,&d->image,vm_clock,&d->dma_io);if(e)goto fail;
    /* Prove firmware/old backend stopped before allocating or exposing pages. */
    driver_pci_prepare_stop(d->pci);e=execute(d,DRV_STOP,NULL,NULL);if(e)goto fail;
    e=driver_pci_disable(d->pci);if(e)goto fail;
    e=execute(d,DRV_PROBE,NULL,NULL);if(e)goto fail;
    e=driver_pci_enable_dma(d->pci);if(e)goto fail;
    uint64_t output[8]={0};e=execute(d,DRV_INIT,NULL,output);if(e)goto fail;
    uint8_t mac[6];unsigned zero=0,ones=0;
    for(unsigned i=0;i<6;i++){mac[i]=(uint8_t)(output[0]>>(i*8));zero+=mac[i]==0;ones+=mac[i]==255;}
    if(zero==6 || ones==6 || (mac[0]&1)){e=DRV_IO;goto fail;}
    uint64_t link[8]={0};e=execute(d,DRV_CONTROL,NULL,link);if(!e && link[0]>1)e=DRV_IO;if(e)goto fail;
    netdev_native_counters(d->native_start);
    e=netdev_driver_install(&network_ops,d,mac);if(e)goto fail;
    {KERNEL_GUARD(manager_gate);d->state=DEV_ACTIVE;}
    irq_mask_pic(d->pci->irq,0);
    char line[220];snprintf(line,sizeof(line),"driver-net: active id=%u name=%s pci=%u:%u.%u irq=%u ORA DMA=%u bytes (interpreted protocol)\n",
        d->id,d->image.manifest.name,d->pci->info.bus,d->pci->info.slot,d->pci->info.function,d->pci->irq,(unsigned)d->dma->bytes);
    debug_log_puts(line);return 0;
fail: {
    char line[220];snprintf(line,sizeof(line),"driver-net: activation error=%s executions=%u DMA=%u last_reg=%x value=%x reads=%u writes=%u\n",drv_error_name(e),(unsigned)d->executions,
        d->dma?(unsigned)d->dma->bytes:0,d->pci?d->pci->last_offset:0,d->pci?d->pci->last_value:0,d->pci?(unsigned)d->pci->reads:0,d->pci?(unsigned)d->pci->writes:0);debug_log_puts(line);
    int stop=network_cleanup(d);
    if(stop){KERNEL_GUARD(manager_gate);d->state=DEV_FAULTED;d->last_error=stop;debug_log_puts("driver-net: QUARANTINED, fallback refused until stop proof\n");}
    else if(was_native)(void)netdev_native_resume();return e;
    }
}
static int network_stop(void *context) {
    driver_instance_t *d=context;irq_mask_pic(11,1);
    (void)netdev_driver_detach(d);
    uint64_t start=sched_clock_ns();
    for(;;){unsigned refs;{KERNEL_GUARD(manager_gate);refs=d->refs;}if(!refs)break;
        if(sched_clock_ns()-start>10000000){KERNEL_GUARD(manager_gate);d->state=DEV_FAULTED;return DRV_BUSY;}smp_relax();}
    int e=network_cleanup(d);
    if(e){KERNEL_GUARD(manager_gate);d->state=DEV_FAULTED;d->last_error=e;return e;}
    retire(d);return netdev_native_resume();
}
static int network_stop(void *);
static int network_start(void *);
static int activate(void *arg) {
    activation_t *a=arg;driver_instance_t *d=NULL;
    {KERNEL_GUARD(manager_gate);
        for(unsigned i=0;i<DRIVER_SLOTS;i++)if(instances[i].state && instances[i].grant==a->grant)return DRV_CONFLICT;
        for(unsigned i=0;i<DRIVER_SLOTS;i++)if(!instances[i].state){d=&instances[i];break;}
        if(!d || !next_id)return DRV_NOMEM;
        memset(d,0,sizeof(*d));d->id=next_id++;d->state=DEV_LOADING;d->grant=a->grant;
        d->gate=(kernel_gate_t)KERNEL_GATE_INIT;d->image=*a->image;memset(a->image,0,sizeof(*a->image));
        memcpy(d->path,a->path,strlen(a->path)+1);}
    if(a->grant==&network_grant) {
        int e=netdev_control_transaction(network_start,d);
        if(!e)*a->id=d->id;
        else {KERNEL_GUARD(manager_gate);if(d->state!=DEV_FAULTED){drv_image_t image=d->image;memset(&d->image,0,sizeof(d->image));d->state=DEV_FREE;drv_image_release(&image,&allocator);}}
        return e;
    }
    int e=hardware_claim(HW_PIO,a->grant->base,a->grant->kind==DRV_INDEXED_PIO?2:a->grant->length,d->id);
    if(!e && d->image.manifest.irq!=DRV_ABSENT) {
        e=hardware_claim(HW_IRQ,d->image.manifest.irq,1,d->id);
        if(!e && irq_claim_pic((uint8_t)d->image.manifest.irq,described_irq)!=0)e=DRV_CONFLICT;
        else if(!e)d->irq_claimed=1;
    }
    if(!e && d->image.manifest.device_class==DRV_CLASS_CHARACTER && vex_event_counter_create(0,0,&d->counter)!=0)e=DRV_NOMEM;
    int hardware_started=0;
    if(!e){hardware_started=1;e=execute(d,DRV_PROBE,NULL,NULL);}
    if(!e)e=execute(d,DRV_INIT,NULL,NULL);
    if(e) {
        if(hardware_started && execute(d,DRV_STOP,NULL,NULL)!=0) {
            KERNEL_GUARD(manager_gate);d->state=DEV_FAULTED;d->last_error=e;return e;
        }
        if(d->irq_claimed)irq_release_pic((uint8_t)d->image.manifest.irq,described_irq);
        if(d->counter)vex_event_counter_release(d->counter);
        hardware_release_owner(d->id);retire(d);return e;
    }
    {KERNEL_GUARD(manager_gate);d->state=DEV_ACTIVE;}
    if(d->irq_claimed)irq_mask_pic((uint8_t)d->image.manifest.irq,0);
    *a->id=d->id;return 0;
}
static int load(const char *path,uint32_t *id,int administrative) {
    if(!manager_ready)return DRV_NOT_BOUND;
    if(administrative && (!user_has_capability(VEX_CAP_SYSTEM_ADMIN) || !scheduler_current_has_capability(VEX_CAP_SYSTEM_ADMIN)))return DRV_DENIED;
    if(!path||!id||strlen(path)>=192)return DRV_BOUNDS;
    uint8_t expected[32];int e=trusted_digest(path,expected);if(e)return e;
    drv_image_t image={0};e=read_image(path,expected,&image);if(e)return e;
    const platform_grant_t *grant=NULL;e=policy(&image.manifest,&grant);
    if(!e){activation_t a={path,&image,grant,id};e=grant->device_class==DRV_CLASS_CLOCK?rtc_driver_transaction(activate,&a):activate(&a);}
    drv_image_release(&image,&allocator);return e;
}
int driver_manager_load(const char *path,uint32_t *id){return load(path,id,1);}
static int deactivate(void *arg) {
    uint32_t id=*(uint32_t *)arg;driver_instance_t *d=NULL;
    {KERNEL_GUARD(manager_gate);
        for(unsigned i=0;i<DRIVER_SLOTS;i++)if(instances[i].state && instances[i].id==id){d=&instances[i];break;}
        if(!d)return DRV_NOT_BOUND;
        if(d->state!=DEV_ACTIVE && d->state!=DEV_FAULTED)return DRV_BUSY;
        d->state=DEV_STOPPING;}
    if(d->irq_claimed)irq_mask_pic((uint8_t)d->image.manifest.irq,1);
    if(d->grant==&network_grant)return netdev_control_transaction(network_stop,d);
    /* Stop new pins before synchronizing old executions. Do not hold any
     * registry/device transaction across this bounded scheduler wait. */
    uint64_t start=sched_clock_ns();
    for(;;){uint32_t refs;{KERNEL_GUARD(manager_gate);refs=d->refs;}if(!refs)break;
        if(sched_clock_ns()-start>10000000){KERNEL_GUARD(manager_gate);d->state=DEV_FAULTED;return DRV_BUSY;}
        /* Pins only cover bounded synchronous executions, never waits. The
         * CLOCK handoff holds rtc_gate; it must not sleep in that transaction. */
        smp_relax();}
    int e=execute(d,DRV_STOP,NULL,NULL);
    if(e){KERNEL_GUARD(manager_gate);d->state=DEV_FAULTED;return e;}
    if(d->irq_claimed)irq_release_pic((uint8_t)d->image.manifest.irq,described_irq);
    if(d->counter)vex_event_counter_release(d->counter);
    hardware_release_owner(d->id);retire(d);return 0;
}
int driver_manager_unload(uint32_t id) {
    if(!user_has_capability(VEX_CAP_SYSTEM_ADMIN)||!scheduler_current_has_capability(VEX_CAP_SYSTEM_ADMIN))return DRV_DENIED;
    int clock=0;{KERNEL_GUARD(manager_gate);for(unsigned i=0;i<DRIVER_SLOTS;i++)if(instances[i].id==id && instances[i].state)clock=instances[i].image.manifest.device_class==DRV_CLASS_CLOCK;}
    return clock?rtc_driver_transaction(deactivate,&id):deactivate(&id);
}
int driver_manager_stop_all(void) {
    uint32_t ids[DRIVER_SLOTS];unsigned count=0;
    {KERNEL_GUARD(manager_gate);for(unsigned i=0;i<DRIVER_SLOTS;i++)if(instances[i].state && instances[i].id)ids[count++]=instances[i].id;}
    int result=0;for(unsigned i=0;i<count;i++)if(driver_manager_unload(ids[i]))result=-1;
    return result;
}
static int parse_trust(const void *bytes,size_t size,int merge) {
    if(size<16 || size>16+TRUST_MAX*sizeof(trust_entry_t)||memcmp(bytes,"KYTRUST1",8))return DRV_FORMAT;
    uint32_t count,reserved;memcpy(&count,(const uint8_t *)bytes+8,4);memcpy(&reserved,(const uint8_t *)bytes+12,4);
    if(count>TRUST_MAX || reserved || size!=16+count*sizeof(trust_entry_t))return DRV_FORMAT;
    const trust_entry_t *entries=(const trust_entry_t *)((const uint8_t *)bytes+16);
    for(unsigned i=0;i<count;i++) {
        const trust_entry_t *e=&entries[i];const char *zero=NULL;
        for(unsigned k=0;k<sizeof(e->path);k++)if(!e->path[k]){zero=e->path+k;break;}
        if(!zero || e->path[0]!='/' || e->flags&~TRUST_AUTO)return DRV_FORMAT;
        for(const char *p=zero;p<e->path+sizeof(e->path);p++)if(*p)return DRV_FORMAT;
        for(unsigned j=0;j<sizeof(e->reserved);j++)if(e->reserved[j])return DRV_FORMAT;
        for(unsigned j=0;j<i;j++)if(!strcmp(e->path,entries[j].path))return DRV_FORMAT;
    }
    KERNEL_GUARD(manager_gate);
    trust_entry_t staged[TRUST_MAX];unsigned staged_count=merge?trust_count:0;
    if(staged_count)memcpy(staged,trust,staged_count*sizeof(*staged));
    for(unsigned i=0;i<count;i++) {
        unsigned at;for(at=0;at<staged_count;at++)if(!strcmp(entries[i].path,staged[at].path))break;
        if(at==staged_count){if(staged_count==TRUST_MAX)return DRV_NOMEM;staged_count++;}
        staged[at]=entries[i];
    }
    memcpy(trust,staged,staged_count*sizeof(*staged));trust_count=staged_count;
    return 0;
}
static void pci_report(const hardware_pci_info_t *p,void *unused) {
    (void)unused;char line[120];snprintf(line,sizeof(line),"driver-pci: %u:%u.%u vendor=%04x device=%04x class=%06x\n",p->bus,p->slot,p->function,p->vendor,p->device,p->class_code);
    debug_log_puts(line);
}
void driver_manager_init(void) {
    if(manager_ready)return;
    /* Existing platform controllers/debug console can never be claimed by a
     * package. PCI native drivers register their BDF and MMIO before this init. */
    const struct {uint16_t p,n;} native[]={{0x20,2},{0xa0,2},{0x40,4},{0x60,1},{0x64,1},{0x3f8,8},{0xcf8,8},{0x1f0,8},{0x3f6,1}};
    for(unsigned i=0;i<sizeof(native)/sizeof(native[0]);i++)(void)hardware_claim(HW_PIO,native[i].p,native[i].n,UINT64_MAX);
    const void *bytes;size_t size;ora_view_t view={0};
    if(fs_read_boot_module_view(TRUST_BOOT,&bytes,&size,&view)==0){
        int e=parse_trust(bytes,size,0);debug_log_puts(e?"driver: boot trust rejected\n":"driver: boot trust imported\n");ora_view_release(&view);}
    fs_metadata_t metadata;void *mutable_bytes;
    if(fs_get_metadata(TRUST_LOCAL,&metadata,1)==0 && metadata.uid==0 && !(metadata.mode&0022) &&
       fs_read_view_bounded(TRUST_LOCAL,&mutable_bytes,&size,&view,16+TRUST_MAX*sizeof(trust_entry_t))==0){
        int e=parse_trust(mutable_bytes,size,1);if(e)debug_log_puts("driver: installed trust rejected\n");ora_view_release(&view);}
    manager_ready=1;hardware_pci_enumerate(pci_report,NULL);
    /* Highest-priority compatible authorized package wins. Conflicts are real
     * refusals; failed/unmatched packages do not replace native devices. */
    unsigned order[TRUST_MAX],n=0;uint32_t priority[TRUST_MAX]={0};
    for(unsigned i=0;i<trust_count;i++)if(trust[i].flags&TRUST_AUTO){
        drv_image_t image={0};const platform_grant_t *g;
        if(!read_image(trust[i].path,trust[i].digest,&image) && !policy(&image.manifest,&g)){
            priority[i]=image.manifest.priority;order[n++]=i;}drv_image_release(&image,&allocator);}
    for(unsigned i=0;i<n;i++)for(unsigned j=i+1;j<n;j++)if(priority[order[j]]>priority[order[i]]){unsigned t=order[i];order[i]=order[j];order[j]=t;}
    for(unsigned i=0;i<n;i++) {uint32_t id;int e=load(trust[order[i]].path,&id,0);char line[240];
        snprintf(line,sizeof(line),"driver: autoload %s %s\n",trust[order[i]].path,drv_error_name(e));debug_log_puts(line);}
}
int driver_manager_install(const char *path,const uint8_t expected[32]) {
    if(!user_has_capability(VEX_CAP_SYSTEM_ADMIN|VEX_CAP_FS_ADMIN)||!scheduler_current_has_capability(VEX_CAP_SYSTEM_ADMIN|VEX_CAP_FS_ADMIN))return DRV_DENIED;
    drv_image_t image={0};int e=read_image(path,expected,&image);if(e)return e;
    const platform_grant_t *g; e=policy(&image.manifest,&g);if(e){drv_image_release(&image,&allocator);return e;}
    char suffix[17];const char *hex="0123456789abcdef";
    for(unsigned i=0;i<8;i++){suffix[2*i]=hex[expected[i]>>4];suffix[2*i+1]=hex[expected[i]&15];}suffix[16]=0;
    char destination[192];snprintf(destination,sizeof(destination),"/home/root/packages/%s-%s.driver",image.manifest.name,suffix);
    drv_image_release(&image,&allocator);
    kernel_guard_t transaction __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter_irq_safe(&install_gate);
    /* Content-addressed filename: replacing a package cannot overwrite the
     * bytes still referenced by an older trust record. Prefix collisions are
     * checked using the complete SHA-256, never merged by the short name. */
    int existed=fs_exists(destination);
    if(existed){drv_image_t existing={0};int same=read_image(destination,expected,&existing);
        drv_image_release(&existing,&allocator);if(same)return DRV_CONFLICT;}
    uint8_t *catalog=kmalloc(16+TRUST_MAX*sizeof(trust_entry_t));if(!catalog)return DRV_NOMEM;
    ora_view_t view={0};void *data;size_t size;
    if(fs_read_view_bounded(path,&data,&size,&view,DRV_ARCHIVE_MAX)!=0){kfree(catalog);return DRV_IO;}
    uint8_t hash[32];if(digest(data,size,hash)||memcmp(hash,expected,32)){ora_view_release(&view);kfree(catalog);return DRV_INTEGRITY;}
    e=existed?0:fs_write(destination,data,size);ora_view_release(&view);if(e){kfree(catalog);return DRV_IO;}
    if(!existed && fs_set_mode(destination,0600)!=0){fs_delete(destination);kfree(catalog);return DRV_IO;}
    trust_entry_t entries[TRUST_MAX];unsigned count;
    {KERNEL_GUARD(manager_gate);count=trust_count;memcpy(entries,trust,count*sizeof(*entries));}
    unsigned at;for(at=0;at<count;at++)if(!strcmp(destination,entries[at].path))break;
    if(at==count){if(count==TRUST_MAX){if(!existed)fs_delete(destination);kfree(catalog);return DRV_NOMEM;}count++;}
    memset(&entries[at],0,sizeof(entries[at]));memcpy(entries[at].path,destination,strlen(destination)+1);memcpy(entries[at].digest,expected,32);
    memcpy(catalog,"KYTRUST1",8);memcpy(catalog+8,&count,4);memset(catalog+12,0,4);memcpy(catalog+16,entries,count*sizeof(*entries));
    /* Installed content is durable. Enrollment does not silently execute it;
     * hot activation is a distinct administrative load command. */
    if(!fs_exists(TRUST_LOCAL)) {
        uint8_t empty[16]={'K','Y','T','R','U','S','T','1',0};
        e=fs_write(TRUST_LOCAL,empty,sizeof(empty));if(!e)e=fs_set_mode(TRUST_LOCAL,0600);
        if(e){if(!existed)fs_delete(destination);kfree(catalog);return DRV_IO;}
    }
    fs_metadata_t tm;
    if(fs_get_metadata(TRUST_LOCAL,&tm,1)!=0 || tm.uid || (tm.mode&0022)) {
        if(!existed)fs_delete(destination);kfree(catalog);return DRV_DENIED;
    }
    e=fs_write(TRUST_LOCAL,catalog,16+count*sizeof(*entries));
    if(!e)e=parse_trust(catalog,16+count*sizeof(*entries),0);else e=DRV_IO;
    if(e && !existed)fs_delete(destination);
    kfree(catalog);return e;
}

static void report(const char *text){tty_puts(text);debug_log_puts(text);}
/* Kernel snprintf currently supports no length modifiers. Never let an
 * unsupported %llu/%zu shift a later %s onto an integer argument. */
static void decimal64(uint64_t value,char out[24]) {
    char reverse[24];unsigned n=0;do{reverse[n++]=(char)('0'+value%10);value/=10;}while(value);
    unsigned at=0;while(n)out[at++]=reverse[--n];out[at]=0;
}
static int network_report(void *unused) {
    (void)unused;uint64_t native[3];netdev_native_counters(native);unsigned found=0;
    for(unsigned i=0;i<DRIVER_SLOTS;i++) {
        uint32_t id;{KERNEL_GUARD(manager_gate);id=instances[i].grant==&network_grant?instances[i].id:0;}
        driver_instance_t *d=id?pin(id):NULL;if(!d)continue;found++;
        {KERNEL_GUARD(d->gate);char v[8][24],line[340];
            uint64_t values[]={d->net_tx,d->net_rx,d->net_irq,native[0]-d->native_start[0],native[1]-d->native_start[1],native[2]-d->native_start[2],d->pci->reads,d->pci->writes};
            for(unsigned j=0;j<8;j++)decimal64(values[j],v[j]);
            uint64_t link[8]={0};int e=execute(d,DRV_CONTROL,NULL,link);
            snprintf(line,sizeof(line),"driver-net-stats: id=%u link=%u TX=%s RX=%s IRQ=%s native_TX_delta=%s native_RX_delta=%s native_IRQ_delta=%s MMIO_reads=%s MMIO_writes=%s ORA_bytes=%u error=%s\n",
                id,(unsigned)link[0],v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],(unsigned)d->dma->bytes,drv_error_name(e));report(line);}
        {KERNEL_GUARD(d->gate);char line[150],elapsed[24];decimal64(d->packet_ns,elapsed);
            snprintf(line,sizeof(line),"driver-net-last-error: entry=%u error=%s ns=%s\n",d->packet_entry,drv_error_name(d->packet_error),elapsed);report(line);}
        unpin(d);
    }
    if(!found){report("driver-net-stats: no active archive backend\n");return DRV_NOT_BOUND;}return 0;
}
static int read_hex_digest(const char *text,uint8_t out[32]) {
    if(strlen(text)!=64)return -1;
    for(unsigned i=0;i<32;i++){unsigned value=0;for(unsigned j=0;j<2;j++){
        char c=text[2*i+j];unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;
        if(d>15)return -1;value=value*16+d;}out[i]=value;}return 0;
}
static uint32_t number_id(const char *text) {
    uint64_t n=0;if(!*text)return 0;
    while(*text){if(*text<'0'||*text>'9')return 0;n=n*10+(*text++-'0');if(n>UINT32_MAX)return 0;}return (uint32_t)n;
}
static int file_visitor(const char *name,size_t size,uint8_t type,void *context) {
    (void)context;size_t n=strlen(name);if(type!=FS_ENTRY_REGULAR || n<7 || strcmp(name+n-7,".driver"))return 0;
    char line[240],bytes[24];decimal64(size,bytes);
    snprintf(line,sizeof(line),"driver-file: %s bytes=%s (not authority)\n",name,bytes);report(line);return 0;
}
static void describe_instances(void) {
    for(unsigned i=0;i<DRIVER_SLOTS;i++) {
        uint32_t id,state;
        {KERNEL_GUARD(manager_gate);id=instances[i].id;state=instances[i].state;}
        if(!state)continue;
        driver_instance_t *d=pin(id);char line[384];
        if(d) {
            {kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter(&d->gate);
                char values[6][24];uint64_t counters[]={d->executions,d->io_reads,d->io_writes,d->interrupts,d->emitted,d->timeouts};
                for(unsigned k=0;k<6;k++)decimal64(counters[k],values[k]);
                snprintf(line,sizeof(line),"driver: id=%u active name=%s class=%u calls=%s reads=%s writes=%s irq=%s rx=%s timeouts=%s last=%s\n",
                    id,d->image.manifest.name,d->image.manifest.device_class,values[0],values[1],values[2],values[3],values[4],values[5],drv_error_name(d->last_error));}
            unpin(d);
        } else snprintf(line,sizeof(line),"driver: id=%u state=%u (loading/stopping/faulted)\n",id,state);
        report(line);
    }
}
static uint32_t selftest_uart;
static int dma_memory_archive_test(void) {
    const char *path="/drivers/dma-memory-test.driver";uint8_t expected[32];
    int e=trusted_digest(path,expected);if(e)return e;
    drv_image_t image={0};e=read_image(path,expected,&image);if(e)return e;
    drv_manifest_t *m=&image.manifest;
    if(m->features!=(DRV_F_BASE|DRV_F_BUFFER|DRV_F_DMA) || m->resource_count!=1 ||
       m->resources[0].kind!=DRV_DMA || m->resources[0].offset || m->resources[0].selector ||
       m->resources[0].length!=4096 || m->resources[0].rights!=3 || m->resources[0].widths!=15 || m->irq!=DRV_ABSENT){e=DRV_DENIED;goto done;}
    drv_dma_context_t *c=kmalloc(sizeof(*c));if(!c){e=DRV_NOMEM;goto done;}
    memset(c,0,sizeof(*c));
    drv_dma_policy_t approved={.grant_count=1,.max_bytes=16384,.address_limit=UINT64_MAX};
    approved.grants[0]=(drv_dma_grant_t){.kind=DRV_DMA_BUFFER,.rights=3,.widths=15,.max_bytes=4096};
    drv_dma_provider_t provider=driver_dma_memory_provider();
    e=drv_dma_init(c,&approved,&provider);
    if(!e) {
        uint8_t scratch[DRV_SCRATCH_MAX]={0};uint64_t output[8]={0};
        /* Memory-only fixture: hardware callbacks deliberately absent. This
         * does not install a device or delegate to any native NIC. */
        drv_io_t io;e=drv_dma_attach_io(c,&image,vm_clock,&io);
        if(!e)e=drv_execute(&image,DRV_INIT,NULL,output,scratch,&io,20000,1000000);
        if(!e && (output[0]!=0x12345678 || c->bytes))e=DRV_IO;
        int closed=drv_dma_destroy(c);if(!e)e=closed;
    }
    kfree(c);
done:drv_image_release(&image,&allocator);return e;
}
static void selftest_sender(void) {
    process_sleep_ticks(2);
    int e=driver_character_write(selftest_uart,'K');
    process_exit_status(e?1:0);
}
static void selftest_reader(void) {
    uint8_t byte;int e=driver_character_read(selftest_uart,&byte,100);
    process_exit_status(e==DRV_NOT_BOUND?0:1);
}
/* A real UART external-peer/IRQ/wait exercise. The control entry is package code;
 * the engine has no UART register sequence or native legacy serial delegate. */
int driver_manager_self_test(void) {
    uint64_t date[8];int e=driver_clock_read(date);if(e)return e;
    uint64_t epoch;if(rtc_epoch_seconds(&epoch)!=0)return DRV_IO;
    char line[160],epoch_text[24];decimal64(epoch,epoch_text);
    snprintf(line,sizeof(line),"driver-test: real clock %u-%02u-%02u %02u:%02u:%02u epoch=%s\n",
        (unsigned)date[0],(unsigned)date[1],(unsigned)date[2],(unsigned)date[3],(unsigned)date[4],(unsigned)date[5],epoch_text);report(line);
    uint32_t id;e=driver_manager_load("/drivers/uart.driver",&id);if(e)return e;
    uint32_t duplicate;int conflict=driver_manager_load("/drivers/uart.driver",&duplicate);
    if(conflict!=DRV_CONFLICT)e=DRV_IO;
    driver_instance_t *d=pin(id);if(!d)return DRV_IO;
    uint64_t control[8]={0};if(!e)e=execute(d,DRV_CONTROL,control,NULL);unpin(d);
    uint32_t sender=0;
    if(!e){selftest_uart=id;sender=process_create_with_stack(selftest_sender,1,32768);if(!sender)e=DRV_NOMEM;}
    uint8_t byte=0;if(!e)e=driver_character_read(id,&byte,100);
    if(sender){int status=0;uint32_t reaped=0;
        if(process_wait_pid((int32_t)sender,&status,0,&reaped)!=(int)sender || reaped!=sender || status)e=DRV_IO;}
    if(!e && byte!='K')e=DRV_IO;
    d=pin(id);if(d){
        kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter(&d->gate);
        if(!d->interrupts||!d->emitted)e=DRV_IO;
        char interrupts[24],emitted[24];decimal64(d->interrupts,interrupts);decimal64(d->emitted,emitted);
        snprintf(line,sizeof(line),"driver-test: UART external echo byte=%u IRQ=%s RX=%s result=%s\n",byte,interrupts,emitted,drv_error_name(e));
    }
    if(d)unpin(d);report(line);
    if(!e){int wait=driver_character_read(id,&byte,2);
        d=pin(id);unsigned rx_count=0,rx_total=0,interrupts=0;
        if(d){kernel_guard_t guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter(&d->gate);rx_count=d->rx_count;rx_total=(unsigned)d->emitted;interrupts=(unsigned)d->interrupts;}
        if(d)unpin(d);
        snprintf(line,sizeof(line),"driver-test: empty receive=%s byte=%u queue=%u total=%u IRQ=%u\n",drv_error_name(wait),byte,rx_count,rx_total,interrupts);report(line);
        if(wait!=DRV_TIMEOUT)e=DRV_IO;}
    int stop=driver_manager_unload(id);if(!e)e=stop;
    snprintf(line,sizeof(line),"driver-test: stop=%s\n",drv_error_name(stop));report(line);
    if(!e && driver_character_write(id,'X')!=DRV_NOT_BOUND)e=DRV_IO;
    /* Closing and killing a blocked consumer cannot retain a driver pin or
     * leave a stack waiter. Counter retirement is the existing tested path. */
    for(unsigned killed=0;!e && killed<2;killed++) {
        e=driver_manager_load("/drivers/uart.driver",&id);if(e)break;
        selftest_uart=id;uint32_t reader=process_create_with_stack(selftest_reader,1,32768);
        if(!reader){e=DRV_NOMEM;(void)driver_manager_unload(id);break;}
        process_sleep_ticks(2);
        if(killed && process_kill(reader)!=0)e=DRV_IO;
        int close=driver_manager_unload(id);if(!e)e=close;
        int status=0;uint32_t reaped=0;
        if(process_wait_pid((int32_t)reader,&status,0,&reaped)!=(int)reader || reaped!=reader || (!killed && status))e=DRV_IO;
        snprintf(line,sizeof(line),"driver-test: blocked reader %s result=%s\n",killed?"kill":"unload",drv_error_name(e));report(line);
    }
    /* Repeated ownership/IRQ installation/removal, unchanged kernel. */
    for(unsigned i=0;!e && i<8;i++) {
        e=driver_manager_load("/drivers/uart.driver",&id);
        if(!e)e=driver_manager_unload(id);
        if(e){snprintf(line,sizeof(line),"driver-test: reload cycle=%u result=%s\n",i,drv_error_name(e));report(line);}
    }
    /* A changed archive programs a different real baud divisor without any
     * different engine/native device logic. Query is also described bytecode. */
    if(!e)e=driver_manager_load("/drivers/uart-slow.driver",&id);
    if(!e){d=pin(id);uint64_t observed[8]={0};
        e=d?execute(d,DRV_DIAGNOSTIC,NULL,observed):DRV_IO;
        if(d)unpin(d);if(!e && observed[0]!=2)e=DRV_IO;
        report(e?"driver-test: baud variant FAILED\n":"driver-test: archive variant actual divisor=2\n");
        stop=driver_manager_unload(id);if(!e)e=stop;}
    report(e?"driver-test: FAILED\n":"driver-test: PASS clock/UART/IRQ/timeout/conflict/stale/unload/reload\n");return e;
}
static int dma_memory_tests(void) {
    int e=driver_dma_self_test();
    if(!e)e=dma_memory_archive_test();
    report(e?"driver-dma-test: FAILED\n":"driver-dma-test: PASS real PMM/VMM/ORA storage + external bytecode; no hardware DMA claimed\n");return e;
}
static int network_cycle_tests(void) {
    uint32_t id=0;
    {KERNEL_GUARD(manager_gate);for(unsigned i=0;i<DRIVER_SLOTS;i++)if(instances[i].state==DEV_ACTIVE && instances[i].grant==&network_grant)id=instances[i].id;}
    if(!id)return DRV_NOT_BOUND;
    uint32_t duplicate=0;
    int e=driver_manager_load("/drivers/e1000.driver",&duplicate);
    if(e!=DRV_CONFLICT)return DRV_IO;
    e=driver_manager_unload(id);if(e)return e;
    if(strcmp(netdev_type(),"E1000"))return DRV_IO;
    uint32_t refused=0;e=driver_manager_load("/drivers/e1000-denied-test.driver",&refused);
    if(e!=DRV_DENIED || refused || strcmp(netdev_type(),"E1000"))return DRV_IO;
    report("driver-net-test: unsafe RX enable refused, native fallback restored\n");
    e=driver_manager_load("/drivers/e1000.driver",&id);if(e)return e;
    for(unsigned i=0;i<3;i++) {
        e=driver_manager_unload(id);if(e)return e;
        e=driver_manager_load("/drivers/e1000.driver",&id);if(e)return e;
        driver_instance_t *d=pin(id);if(!d)return DRV_IO;
        {KERNEL_GUARD(d->gate);if(d->dma->bytes!=139264)e=DRV_IO;}
        unpin(d);if(e)return e;
    }
    report("driver-net-test: PASS conflict/stop-proof/negative-protocol/native-fallback/3-reload-cycles\n");return 0;
}
void driver_manager_command(const char *arguments) {
    char words[4][192]={{0}};unsigned argc=0;
    while(*arguments && argc<4){while(*arguments==' ')arguments++;if(!*arguments)break;unsigned at=0;
        while(*arguments && *arguments!=' '){if(at+1>=sizeof(words[0])){report("driver: argument too long\n");return;}words[argc][at++]=*arguments++;}argc++;}
    if(!argc || !strcmp(words[0],"help")){report("driver list | devices | time | selftest | dmatest | install PATH SHA256 | load PATH | unload ID | send ID TEXT | read ID\n");return;}
    if(!strcmp(words[0],"time")) {
        uint64_t date[8]={0};int e=driver_clock_read(date);char line[180];
        snprintf(line,sizeof(line),"driver-clock: %s %u-%02u-%02u %02u:%02u:%02u\n",drv_error_name(e),
            (unsigned)date[0],(unsigned)date[1],(unsigned)date[2],(unsigned)date[3],(unsigned)date[4],(unsigned)date[5]);report(line);return;
    }
    if(!strcmp(words[0],"list")){describe_instances();(void)fs_list_directory("/drivers",file_visitor,NULL);
        (void)fs_list_directory("/home/root/packages",file_visitor,NULL);return;}
    if(!strcmp(words[0],"devices")){hardware_pci_enumerate(pci_report,NULL);report("driver-platform: rtc.cmos (native clock handoff), uart.com2 (requires successful archive probe); USB host absent\n");return;}
    if(!strcmp(words[0],"netstats")){(void)netdev_control_transaction(network_report,NULL);return;}
    if(!user_has_capability(VEX_CAP_SYSTEM_ADMIN)||!scheduler_current_has_capability(VEX_CAP_SYSTEM_ADMIN)){report("driver: denied\n");return;}
    int e=DRV_FORMAT;uint32_t id=0;
    if(!strcmp(words[0],"load")&&argc==2)e=driver_manager_load(words[1],&id);
    else if(!strcmp(words[0],"unload")&&argc==2)e=driver_manager_unload(number_id(words[1]));
    else if(!strcmp(words[0],"install")&&argc==3){uint8_t hash[32];e=read_hex_digest(words[2],hash)?DRV_FORMAT:driver_manager_install(words[1],hash);}
    else if(!strcmp(words[0],"selftest")&&argc==1)e=driver_manager_self_test();
    else if(!strcmp(words[0],"dmatest")&&argc==1)e=dma_memory_tests();
    else if(!strcmp(words[0],"netcycle")&&argc==1)e=network_cycle_tests();
    else if(!strcmp(words[0],"send")&&argc==3){e=0;id=number_id(words[1]);for(const char *s=words[2];!e&&*s;s++)e=driver_character_write(id,(uint8_t)*s);}
    else if(!strcmp(words[0],"read")&&argc==2){uint8_t byte;e=driver_character_read(number_id(words[1]),&byte,100);if(!e){char line[64];snprintf(line,sizeof(line),"driver-character: byte=%u\n",byte);report(line);}}
    char line[80];snprintf(line,sizeof(line),"driver-result: %s id=%u\n",drv_error_name(e),id);report(line);
}
