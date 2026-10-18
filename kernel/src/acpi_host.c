/* AML stays privileged firmware code. No ACPI handle is exposed to applications. */
#include <uacpi/kernel_api.h>
#include <unos/boot_requests.h>
#include <unos/vmm.h>
#include <unos/heap.h>
#include <unos/scheduler.h>
#include <unos/sched_clock.h>
#include <unos/smp.h>
#include <unos/debug_log.h>
#include <unos/event_counter.h>
#include <unos/hardware_resources.h>
#include <unos/irq.h>
#include <string.h>
typedef struct {uint64_t base,length;} io_range_t;
typedef struct {uint64_t cap;uint32_t owner;} acpi_counter_t;
static void *current_root(void){uint64_t root;__asm__ volatile("mov %%cr3,%0":"=r"(root));return vmm_cr3_to_address_space(root);}
uacpi_status uacpi_kernel_get_rsdp(uacpi_phys_addr *out) {
    if(!rsdp_request.response || !rsdp_request.response->address)return UACPI_STATUS_NOT_FOUND;
    *out=vmm_get_physical(current_root(),(uintptr_t)rsdp_request.response->address);
    /* Legacy RSDP may reside in reserved BIOS ROM omitted from our sparse
       bootstrap HHDM. Limine base revision 6 supplies an HHDM virtual address. */
    if(!*out && hhdm_request.response && (uintptr_t)rsdp_request.response->address>=hhdm_request.response->offset)
        *out=(uintptr_t)rsdp_request.response->address-hhdm_request.response->offset;
    return *out?UACPI_STATUS_OK:UACPI_STATUS_NOT_FOUND;
}
/* Private aliases, independent of the HHDM's cache attributes and lifetime. */
static uint64_t map_cursor=0xffffffffc8000000ull;
void *uacpi_kernel_map(uacpi_phys_addr physical,uacpi_size length) {
    if(!length || length>16u*1024u*1024u || physical>0x000fffffffffffffull-length)return UACPI_MAP_FAILED;
    uint64_t offset=physical&4095,bytes=(length+offset+4095)&~4095ull;
    uint64_t base=__atomic_fetch_add(&map_cursor,bytes,__ATOMIC_RELAXED);
    if(base>0xffffffffe0000000ull-bytes)return UACPI_MAP_FAILED;
    uint64_t mapped=0;
    for(;mapped<bytes;mapped+=4096) {
        uint64_t p=(physical&~4095ull)+mapped;
        uint64_t flags=VMM_PAGE_PRESENT|VMM_PAGE_WRITABLE|VMM_PAGE_NX;
        /* Tables/RAM retain WB aliases; device registers use uncached aliases. */
        int ram=0;
        if(memmap_request.response)for(uint64_t i=0;i<memmap_request.response->entry_count;i++) {
            struct limine_memmap_entry *e=memmap_request.response->entries[i];
            if(p>=e->base && p-e->base<e->length && e->type!=LIMINE_MEMMAP_RESERVED &&
               e->type!=LIMINE_MEMMAP_BAD_MEMORY){ram=1;break;}
        }
        if(!ram)flags|=VMM_PAGE_PCD|VMM_PAGE_PWT;
        vmm_map_kernel_page(base+mapped,p,flags);
        if(!(vmm_get_page_flags(current_root(),base+mapped)&VMM_PAGE_PRESENT))break;
    }
    if(mapped!=bytes){if(mapped)vmm_unmap_kernel_range(base,mapped);return UACPI_MAP_FAILED;}
    return (void *)(uintptr_t)(base+offset);
}
void uacpi_kernel_unmap(void *address,uacpi_size length) {
    if(address && address!=UACPI_MAP_FAILED && length)
        (void)vmm_unmap_kernel_range((uintptr_t)address&~4095ull,(length+((uintptr_t)address&4095)+4095)&~4095ull);
}
void uacpi_kernel_log(uacpi_log_level level,const uacpi_char *text) {
    (void)level;debug_log_puts("ACPI: ");debug_log_puts(text);
}
void *uacpi_kernel_alloc(uacpi_size size){return kmalloc(size);}
void uacpi_kernel_free(void *p){kfree(p);}
uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void){return sched_clock_ns();}
void uacpi_kernel_stall(uacpi_u8 us){uint64_t begin=sched_clock_ns();while(sched_clock_ns()-begin<(uint64_t)us*1000)smp_relax();}
void uacpi_kernel_sleep(uacpi_u64 ms){
    uint64_t flags;__asm__ volatile("pushfq;pop %0":"=r"(flags));
    if(flags&512){process_sleep_ticks(ms/10+(ms%10!=0));return;}
    /* ACPI enters S5 with IF=0: scheduler timers cannot service a sleep here. */
    if(ms>60000){debug_log_puts("ACPI: invalid atomic sleep duration\n");for(;;)__asm__ volatile("cli;hlt");}
    uint64_t start=sched_clock_ns();while(sched_clock_ns()-start<ms*1000000ull)smp_relax();
}
uacpi_thread_id uacpi_kernel_get_thread_id(void){return (void *)(uintptr_t)(scheduler_current_tid()+1ull);}
uacpi_interrupt_state uacpi_kernel_disable_interrupts(void){uint64_t f;__asm__ volatile("pushfq;pop %0;cli":"=r"(f)::"memory");return f;}
void uacpi_kernel_restore_interrupts(uacpi_interrupt_state f){__asm__ volatile("push %0;popfq"::"r"(f):"memory","cc");}
static acpi_counter_t *counter_create(uint64_t value){
    acpi_counter_t *e=kmalloc(sizeof(*e));if(!e)return NULL;
    e->owner=scheduler_current_pid();
    if(vex_event_counter_create_owned(e->owner,value,VEX_EVENT_COUNTER_SEMAPHORE,&e->cap)){kfree(e);return NULL;}return e;
}
static void counter_free(acpi_counter_t *e){if(e){vex_event_counter_release_owned(e->owner,e->cap);kfree(e);}}
uacpi_handle uacpi_kernel_create_mutex(void){return counter_create(1);}
void uacpi_kernel_free_mutex(uacpi_handle e){counter_free(e);}
uacpi_handle uacpi_kernel_create_event(void){return counter_create(0);}
void uacpi_kernel_free_event(uacpi_handle e){counter_free(e);}
uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle,uacpi_u16 ms){
    acpi_counter_t *e=handle;uint64_t value;
    return vex_event_counter_read_timed(e->cap,&value,ms?0:VEX_EVENT_COUNTER_NONBLOCK,
        ms==0xffff?0:ms/10+(ms%10!=0))==0;
}
uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle e,uacpi_u16 ms){return uacpi_kernel_wait_for_event(e,ms)?UACPI_STATUS_OK:UACPI_STATUS_TIMEOUT;}
void uacpi_kernel_release_mutex(uacpi_handle handle){acpi_counter_t *e=handle;vex_event_counter_signal(e->cap,1);}
void uacpi_kernel_signal_event(uacpi_handle e){uacpi_kernel_release_mutex(e);}
void uacpi_kernel_reset_event(uacpi_handle handle){acpi_counter_t *e=handle;uint64_t v;while(!vex_event_counter_read(e->cap,&v,VEX_EVENT_COUNTER_NONBLOCK)){} }
uacpi_handle uacpi_kernel_create_spinlock(void){uint8_t *p=kmalloc(1);if(p)*p=0;return p;}
void uacpi_kernel_free_spinlock(uacpi_handle p){kfree(p);}
uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle p){uint64_t f=uacpi_kernel_disable_interrupts();while(__atomic_test_and_set((uint8_t *)p,__ATOMIC_ACQUIRE))smp_relax();return f;}
void uacpi_kernel_unlock_spinlock(uacpi_handle p,uacpi_cpu_flags f){__atomic_clear((uint8_t *)p,__ATOMIC_RELEASE);uacpi_kernel_restore_interrupts(f);}
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base,uacpi_size length,uacpi_handle *out){
    if(!length || base>65535 || length>65536-base)return UACPI_STATUS_INVALID_ARGUMENT;
    io_range_t *r=kmalloc(sizeof(*r));if(!r)return UACPI_STATUS_OUT_OF_MEMORY;
    *r=(io_range_t){base,length};*out=r;return UACPI_STATUS_OK;
}
void uacpi_kernel_io_unmap(uacpi_handle p){kfree(p);}
#define IO_ACCESS(bits,type,instruction,outstruction) \
uacpi_status uacpi_kernel_io_read##bits(uacpi_handle handle,uacpi_size offset,type *out){ \
 io_range_t *r=handle;if(offset>r->length || sizeof(type)>r->length-offset)return UACPI_STATUS_INVALID_ARGUMENT; \
 uint16_t port=r->base+offset;__asm__ volatile(instruction " %1,%0":"=a"(*out):"Nd"(port));return UACPI_STATUS_OK;} \
uacpi_status uacpi_kernel_io_write##bits(uacpi_handle handle,uacpi_size offset,type value){ \
 io_range_t *r=handle;if(offset>r->length || sizeof(type)>r->length-offset)return UACPI_STATUS_INVALID_ARGUMENT; \
 uint16_t port=r->base+offset;__asm__ volatile(outstruction " %0,%1"::"a"(value),"Nd"(port));return UACPI_STATUS_OK;}
IO_ACCESS(8,uacpi_u8,"inb","outb")
IO_ACCESS(16,uacpi_u16,"inw","outw")
IO_ACCESS(32,uacpi_u32,"inl","outl")
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address a,uacpi_handle *out){
    if(a.segment || a.device>31 || a.function>7)return UACPI_STATUS_UNIMPLEMENTED;
    uacpi_pci_address *p=kmalloc(sizeof(*p));if(!p)return UACPI_STATUS_OUT_OF_MEMORY;*p=a;*out=p;return UACPI_STATUS_OK;
}
void uacpi_kernel_pci_device_close(uacpi_handle p){kfree(p);}
#define PCI_ACCESS(bits,type) \
uacpi_status uacpi_kernel_pci_read##bits(uacpi_handle h,uacpi_size off,type *out){ \
 uacpi_pci_address *a=h;if(off>256-sizeof(type) || (off&3)+sizeof(type)>4)return UACPI_STATUS_INVALID_ARGUMENT; \
 *out=hardware_pci_read32(a->bus,a->device,a->function,off&~3u)>>((off&3)*8);return UACPI_STATUS_OK;} \
uacpi_status uacpi_kernel_pci_write##bits(uacpi_handle h,uacpi_size off,type value){ \
 uacpi_pci_address *a=h;if(off>256-sizeof(type) || (off&3)+sizeof(type)>4)return UACPI_STATUS_INVALID_ARGUMENT; \
 hardware_pci_write_width(a->bus,a->device,a->function,off,sizeof(type),value);return UACPI_STATUS_OK;}
PCI_ACCESS(8,uacpi_u8)
PCI_ACCESS(16,uacpi_u16)
PCI_ACCESS(32,uacpi_u32)
/* Firmware debug requests cannot be silently acknowledged. */
uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *r){(void)r;debug_log_puts("ACPI: firmware Breakpoint/Fatal rejected\n");return UACPI_STATUS_UNIMPLEMENTED;}
static struct {uacpi_interrupt_handler handler;uacpi_handle context;uint32_t irq,inflight;uint8_t old_elcr;} sci;
static struct {uacpi_work_handler handler;uacpi_handle context;} jobs[64];
static unsigned job_head,job_tail,job_count,job_active;
static uint8_t jobs_lock;
static volatile uint8_t jobs_pending,jobs_idle=1;
static uint32_t work_tid,wait_tid;
static uint64_t work_lock(void){uint64_t f=uacpi_kernel_disable_interrupts();while(__atomic_test_and_set(&jobs_lock,__ATOMIC_ACQUIRE))smp_relax();return f;}
static void work_unlock(uint64_t f){__atomic_clear(&jobs_lock,__ATOMIC_RELEASE);uacpi_kernel_restore_interrupts(f);}
static void deferred_worker(void) {
    for(;;) {
        process_wait_event(&jobs_pending);
        uint64_t flags=work_lock();
        if(!job_count){jobs_pending=0;work_unlock(flags);continue;}
        uacpi_work_handler handler=jobs[job_head].handler;uacpi_handle context=jobs[job_head].context;
        job_head=(job_head+1)%64;--job_count;job_active=1;jobs_idle=0;work_unlock(flags);
        handler(context);
        flags=work_lock();job_active=0;jobs_pending=job_count!=0;jobs_idle=job_count==0;
        uint32_t waiter=wait_tid;work_unlock(flags);if(waiter)process_wake(waiter);
    }
}
int acpi_host_start(void) {
    work_tid=process_create_with_stack(deferred_worker,1,128u*1024u);
    if(work_tid)scheduler_set_process_name(work_tid,"acpi-events");
    return work_tid?0:-1;
}
static void sci_interrupt(interrupt_frame_t *frame) {
    (void)frame;__atomic_add_fetch(&sci.inflight,1,__ATOMIC_ACQ_REL);
    if(sci.handler)sci.handler(sci.context);
    __atomic_sub_fetch(&sci.inflight,1,__ATOMIC_RELEASE);
}
uacpi_status uacpi_kernel_install_interrupt_handler(uacpi_u32 irq,uacpi_interrupt_handler f,uacpi_handle c,uacpi_handle *out){
    if(irq>=16 || irq==0 || irq==1 || irq==2 || irq==8 || irq==13 || irq==7 || irq==15 || !f || sci.handler)return UACPI_STATUS_UNIMPLEMENTED;
    uint64_t owner=UINT64_C(0xa00000000000ac01);
    if(hardware_claim_shared_irq(irq,owner))return UACPI_STATUS_DENIED;
    sci.handler=f;sci.context=c;sci.irq=irq;
    if(irq_claim_pic(irq,sci_interrupt)){sci.handler=NULL;hardware_release_owner(owner);return UACPI_STATUS_DENIED;}
    uint16_t port=irq<8?0x4d0:0x4d1;uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));sci.old_elcr=value;
    value|=1u<<(irq&7);__asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
    irq_mask_pic(irq,0);*out=&sci;return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_uninstall_interrupt_handler(uacpi_interrupt_handler f,uacpi_handle h){
    if(h!=&sci || f!=sci.handler)return UACPI_STATUS_INVALID_ARGUMENT;
    irq_mask_pic(sci.irq,1);
    uint64_t start=sched_clock_ns();
    while(__atomic_load_n(&sci.inflight,__ATOMIC_ACQUIRE))if(sched_clock_ns()-start>=1000000000ull)return UACPI_STATUS_TIMEOUT;
    if(irq_release_pic(sci.irq,sci_interrupt))return UACPI_STATUS_INTERNAL_ERROR;
    uint16_t port=sci.irq<8?0x4d0:0x4d1;uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    uint8_t bit=1u<<(sci.irq&7);value=(value&~bit)|(sci.old_elcr&bit);
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
    sci.handler=NULL;hardware_release_owner(UINT64_C(0xa00000000000ac01));irq_refresh_pic(sci.irq);return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_schedule_work(uacpi_work_type t,uacpi_work_handler f,uacpi_handle c){
    if(!work_tid || !f || (t!=UACPI_WORK_GPE_EXECUTION && t!=UACPI_WORK_NOTIFICATION))return UACPI_STATUS_INVALID_ARGUMENT;
    uint64_t flags=work_lock();
    if(job_count==64){work_unlock(flags);return UACPI_STATUS_OUT_OF_MEMORY;}
    jobs[job_tail].handler=f;jobs[job_tail].context=c;job_tail=(job_tail+1)%64;++job_count;
    jobs_pending=1;jobs_idle=0;work_unlock(flags);process_wake(work_tid);return UACPI_STATUS_OK;
}
uacpi_status uacpi_kernel_wait_for_work_completion(void){
    if(scheduler_current_tid()==work_tid)return UACPI_STATUS_INTERNAL_ERROR;
    uint64_t start=sched_clock_ns();
    while(__atomic_load_n(&sci.inflight,__ATOMIC_ACQUIRE))if(sched_clock_ns()-start>=1000000000ull)return UACPI_STATUS_TIMEOUT;
    for(;;) {
        uint64_t flags=work_lock();
        if(!job_count && !job_active){wait_tid=0;work_unlock(flags);return UACPI_STATUS_OK;}
        wait_tid=scheduler_current_tid();work_unlock(flags);
        if(sched_clock_ns()-start>=5000000000ull){flags=work_lock();wait_tid=0;work_unlock(flags);return UACPI_STATUS_TIMEOUT;}
        process_wait_event_timeout(&jobs_idle,10);
    }
}
