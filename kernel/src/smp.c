#include <unos/smp.h>
#include <unos/boot_requests.h>
#include <unos/heap.h>
#include <unos/pmm.h>
#include <unos/vmm.h>
#include <unos/irq.h>
#include <unos/scheduler.h>
#include <unos/sched_clock.h>
#include <unos/random.h>
#include <unos/debug_log.h>
#include <string.h>
#include <stdio.h>

static vex_cpu_t boot_cpu;
static vex_cpu_t **cpus;
static struct limine_mp_info **boot_records;
static uint32_t cpu_count=1u;
static uint8_t shootdown_lock;
static struct {uint64_t generation,cr3,start,length;} shootdown;
extern void smp_ap_entry(struct limine_mp_info *);
extern void syscall_init_current_cpu(void);

uint32_t smp_cpu_count(void) {return cpu_count;}
void smp_stop_safe_point(void) {
    vex_cpu_t *cpu=smp_current_cpu();
    if(!cpu || !__atomic_load_n(&cpu->stop_requested,__ATOMIC_ACQUIRE) || cpu->nonpreempt_depth)return;
    __asm__ volatile("cli;wbinvd":::"memory");
    __atomic_store_n(&cpu->online,0u,__ATOMIC_RELEASE);
    __atomic_store_n(&cpu->stopped,1u,__ATOMIC_RELEASE);
    for(;;)__asm__ volatile("hlt");
}
int smp_stop_others(uint64_t timeout_ns) {
    uint32_t self=smp_current_index();uint64_t start=sched_clock_ns();
    for(uint32_t i=0;i<cpu_count;i++)if(i!=self && smp_cpu_at(i)->online) {
        __atomic_store_n(&smp_cpu_at(i)->stop_requested,1u,__ATOMIC_RELEASE);smp_reschedule(i);
    }
    for(;;) {
        int done=1;
        for(uint32_t i=0;i<cpu_count;i++)if(i!=self && smp_cpu_at(i)->stop_requested &&
            !__atomic_load_n(&smp_cpu_at(i)->stopped,__ATOMIC_ACQUIRE))done=0;
        if(done)return 0;
        if(sched_clock_ns()-start>=timeout_ns)return -1;
        smp_relax();
    }
}
vex_cpu_t *smp_cpu_at(uint32_t index) {return index<cpu_count?(cpus?cpus[index]:&boot_cpu):NULL;}
uint32_t smp_online_count(void) {
    uint32_t n=0;for(uint32_t i=0;i<cpu_count;++i)n+=__atomic_load_n(&smp_cpu_at(i)->online,__ATOMIC_ACQUIRE)!=0;return n;
}
static uint64_t current_cr3(void) {
    uint64_t root;__asm__ volatile("mov %%cr3,%0":"=r"(root));return root&UINT64_C(0x000ffffffffff000);
}
void smp_poll_requests(void) {
    vex_cpu_t *cpu=smp_current_cpu();
    if(!cpu)return;
    if(__atomic_load_n(&cpu->tlb_request,__ATOMIC_ACQUIRE)==__atomic_load_n(&cpu->tlb_ack,__ATOMIC_RELAXED))return;
    uint64_t flags;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    /* NMI can interrupt the invalidation itself. It must not acknowledge the
       mailbox early and let its publisher overwrite the request underneath us. */
    if(__atomic_test_and_set(&cpu->tlb_servicing,__ATOMIC_ACQUIRE))goto restore;
    uint64_t sequence=__atomic_load_n(&cpu->tlb_request,__ATOMIC_ACQUIRE);
    if(sequence==__atomic_load_n(&cpu->tlb_ack,__ATOMIC_RELAXED))goto complete;
    /* No allocator, scheduler or VMM lock here: callers may hold one while
       waiting for another CPU. A shootdown must never depend on that lock. */
    if(!shootdown.cr3 || current_cr3()==shootdown.cr3) {
        uint64_t pages=shootdown.length/4096u+(shootdown.length%4096u!=0);
        if(shootdown.length && pages<=64u) {
            for(uint64_t i=0;i<pages;++i)__asm__ volatile("invlpg (%0)"::"r"(shootdown.start+i*4096u):"memory");
        } else {
            uint64_t cr4,cr3;
            __asm__ volatile("mov %%cr4,%0; mov %%cr3,%1":"=r"(cr4),"=r"(cr3));
            if(cr4&(1ull<<7u)) {
                uint64_t no_global=cr4&~(1ull<<7u);
                __asm__ volatile("mov %0,%%cr4; mov %1,%%cr4"::"r"(no_global),"r"(cr4):"memory");
            } else __asm__ volatile("mov %0,%%cr3"::"r"(cr3):"memory");
        }
        ++cpu->tlb_flushes;
    }
    __atomic_store_n(&cpu->tlb_ack,sequence,__ATOMIC_RELEASE);
complete:
    __atomic_clear(&cpu->tlb_servicing,__ATOMIC_RELEASE);
restore:
    __asm__ volatile("pushq %0; popfq"::"r"(flags):"memory","cc");
}
void smp_tlb_shootdown(uint64_t cr3,uint64_t start,uint64_t length) {
    if(cpu_count<=1u || smp_online_count()<=1u)return;
    uint64_t flags;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    while(__atomic_test_and_set(&shootdown_lock,__ATOMIC_ACQUIRE))smp_relax();
    /* No generation wrap is possible during the lifetime of a running kernel.
       Publication precedes IPIs; acknowledgements precede backing/table frees. */
    shootdown.cr3=cr3;shootdown.start=start;shootdown.length=length;
    uint64_t sequence=++shootdown.generation;
    uint32_t current=smp_current_index();
    for(uint32_t i=0;i<cpu_count;++i) {
        vex_cpu_t *target=cpus[i];
        if(!__atomic_load_n(&target->online,__ATOMIC_ACQUIRE))continue;
        __atomic_store_n(&target->tlb_request,sequence,__ATOMIC_RELEASE);
        if(i!=current)irq_send_ipi(target->apic_id,VEX_IPI_VECTOR);
    }
    smp_poll_requests();
    for(uint32_t i=0;i<cpu_count;++i) {
        vex_cpu_t *target=cpus[i];
        if(!__atomic_load_n(&target->online,__ATOMIC_ACQUIRE))continue;
        while(__atomic_load_n(&target->tlb_ack,__ATOMIC_ACQUIRE)!=sequence)smp_relax();
    }
    __atomic_clear(&shootdown_lock,__ATOMIC_RELEASE);
    __asm__ volatile("pushq %0; popfq"::"r"(flags):"memory","cc");
}
void smp_reschedule(uint32_t index) {
    vex_cpu_t *target=smp_cpu_at(index);
    if(!target || !__atomic_load_n(&target->online,__ATOMIC_ACQUIRE))return;
    __atomic_store_n(&target->reschedule_pending,1u,__ATOMIC_RELEASE);
    if(index!=smp_current_index())irq_send_ipi(target->apic_id,VEX_IPI_VECTOR);
}
void smp_handle_ipi(void) {
    smp_poll_requests();
    vex_cpu_t *cpu=smp_current_cpu();
    if(cpu && __atomic_exchange_n(&cpu->reschedule_pending,0u,__ATOMIC_ACQ_REL)) {
        ++cpu->reschedules;
        if(cpu->index==0u || cpu->scheduler_enabled)scheduler_request_preempt();
    }
}
static void topology_cpuid(uint32_t leaf,uint32_t sub,uint32_t *a,uint32_t *b,uint32_t *c,uint32_t *d) {
    __asm__ volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(leaf),"c"(sub));
}
static unsigned topology_bits(unsigned n){unsigned bits=0;while(n>1){n=(n+1)/2;bits++;}return bits;}
static void topology_init(vex_cpu_t *cpu) {
    vex_cpu_topology_info_t *t=&cpu->topology;memset(t,0,sizeof(*t));t->index=cpu->index;
    uint32_t a,b,c,d,max,extended;topology_cpuid(0,0,&max,&b,&c,&d);
    topology_cpuid(0x80000000,0,&extended,&b,&c,&d);
    unsigned smt_bits=0,package_bits=0,found=0,cache_leaf=0;
    unsigned topology_leaf=max>=0x1f?0x1f:max>=0xb?0xb:0;
    if(topology_leaf)for(unsigned i=0;i<32;i++) {
        topology_cpuid(topology_leaf,i,&a,&b,&c,&d);if(!b)break;
        unsigned type=(c>>8)&255;if(!type)break;
        if(type==1)smt_bits=a&31;package_bits=a&31;found=1;
    }
    if(max>=4){topology_cpuid(4,0,&a,&b,&c,&d);if(a&31)cache_leaf=4;}
    if(!cache_leaf && extended>=0x8000001d){topology_cpuid(0x8000001d,0,&a,&b,&c,&d);if(a&31)cache_leaf=0x8000001d;}
    if(!found) {
        topology_cpuid(1,0,&a,&b,&c,&d);unsigned logical=(d&(1u<<28))?((b>>16)&255):1,cores=0;
        if(cache_leaf){topology_cpuid(cache_leaf,0,&a,&b,&c,&d);cores=((a>>26)&63)+1;}
        else if(extended>=0x80000008){topology_cpuid(0x80000008,0,&a,&b,&c,&d);cores=(c&255)+1;}
        else if(logical==1)cores=1;
        if(!logical || !cores || logical%cores)return;
        smt_bits=topology_bits(logical/cores);package_bits=topology_bits(logical);
    }
    if(package_bits<smt_bits)return;
    t->package=cpu->apic_id>>package_bits;
    t->core=(cpu->apic_id>>smt_bits)&((1u<<(package_bits-smt_bits))-1u);
    t->thread=cpu->apic_id&((1u<<smt_bits)-1u);t->valid=1;
    if(cache_leaf)for(unsigned i=0;i<8;i++) {
        topology_cpuid(cache_leaf,i,&a,&b,&c,&d);unsigned type=a&31;if(!type)break;
        vex_cpu_cache_info_t *cache=&t->caches[t->cache_count++];
        cache->level=(a>>5)&7;cache->type=type;cache->line_bytes=(b&4095)+1;cache->ways=((b>>22)&1023)+1;
        cache->bytes=(uint64_t)cache->line_bytes*cache->ways*(((b>>12)&1023)+1)*((uint64_t)c+1);
        cache->sharing_domain=cpu->apic_id>>topology_bits(((a>>14)&4095)+1);
    }
}

int smp_prepare(void) {
    struct limine_mp_response *response=mp_request.response;
    uint64_t count=response?response->cpu_count:1u;
    if((response && !response->cpus) || !count || count>UINT32_MAX || count>SIZE_MAX/sizeof(*cpus))return -1;
    cpus=kmalloc((size_t)count*sizeof(*cpus));
    boot_records=kmalloc((size_t)count*sizeof(*boot_records));
    if(!cpus || !boot_records){kfree(cpus);kfree(boot_records);cpus=NULL;boot_records=NULL;return -1;}
    memset(cpus,0,(size_t)count*sizeof(*cpus));
    memset(boot_records,0,(size_t)count*sizeof(*boot_records));
    boot_cpu.index=0u;boot_cpu.apic_id=response?response->bsp_lapic_id:gdt_current_cpu_id();boot_cpu.kernel_cr3=current_cr3();
    cpus[0]=&boot_cpu;
    uint32_t next=1u,bsp_found=response?0u:1u;
    if(response)for(uint64_t i=0;i<count;++i) {
        struct limine_mp_info *record=response->cpus[i];
        if(!record)goto rollback;
        if(record->lapic_id==response->bsp_lapic_id) {
            if(bsp_found++)goto rollback;boot_records[0]=record;continue;
        }
        if(next>=count)goto rollback;
        for(uint32_t j=0;j<next;++j)if(cpus[j]->apic_id==record->lapic_id)goto rollback;
        vex_cpu_t *cpu=kmalloc_aligned(sizeof(*cpu),64u);
        if(!cpu)goto rollback;memset(cpu,0,sizeof(*cpu));cpus[next]=cpu;
        cpu->index=next;cpu->apic_id=record->lapic_id;cpu->kernel_cr3=boot_cpu.kernel_cr3;cpu->self=cpu;
        cpu->gdt_storage=kmalloc_aligned(gdt_cpu_storage_size(),64u);
        void *stack=pmm_alloc_contiguous_pages(16u);
        if(!cpu->gdt_storage || !stack){if(stack)pmm_free_contiguous_pages(stack,16u);goto rollback;}
        cpu->stack_top=hhdm_request.response->offset+(uint64_t)stack+16u*4096u;
        for(unsigned page=0;page<16u;++page)
            vmm_map_kernel_page(cpu->stack_top-16u*4096u+page*4096u,(uint64_t)stack+page*4096u,
                VMM_PAGE_PRESENT|VMM_PAGE_WRITABLE|VMM_PAGE_NX);
        cpus[next]=cpu;boot_records[next]=record;++next;
    }
    if(!bsp_found || next!=count)goto rollback;
    cpu_count=(uint32_t)count;
    gdt_bind_boot_cpu(0u,boot_cpu.apic_id,&boot_cpu);
    topology_init(&boot_cpu);
    if(vmm_activate_cr3(boot_cpu.kernel_cr3)!=0)return -1;
    __atomic_store_n(&boot_cpu.online,1u,__ATOMIC_RELEASE);
    return 0;
rollback:
    for(uint64_t i=1;i<count;++i)if(cpus[i]) {
        if(cpus[i]->stack_top)pmm_free_contiguous_pages((void *)(cpus[i]->stack_top-hhdm_request.response->offset-16u*4096u),16u);
        kfree_aligned(cpus[i]->gdt_storage);kfree_aligned(cpus[i]);
    }
    kfree(cpus);kfree(boot_records);cpus=NULL;boot_records=NULL;return -1;
}
void smp_ap_main(vex_cpu_t *cpu) {
    uint64_t cr0,cr4;
    __asm__ volatile("mov %%cr0,%0; mov %%cr4,%1":"=r"(cr0),"=r"(cr4));
    cr0=(cr0&~((1ull<<2u)|(1ull<<3u)))|(1ull<<1u);
    cr4|=(1ull<<9u)|(1ull<<10u);
    __asm__ volatile("mov %0,%%cr0; mov %1,%%cr4"::"r"(cr0),"r"(cr4):"memory");
    uint32_t a,d;
    __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(0xc0000080u));
    a|=1u<<11u;
    __asm__ volatile("wrmsr"::"a"(a),"d"(d),"c"(0xc0000080u):"memory");
    gdt_init_cpu(cpu->gdt_storage,cpu->index,cpu->apic_id,cpu);
    topology_init(cpu);
    kernel_random_cpu_init();
    gdt_set_tss_rsp0(cpu->stack_top);
    if(vmm_activate_cr3(cpu->kernel_cr3)!=0)for(;;)__asm__ volatile("cli; hlt");
    if(scheduler_init_current_cpu()!=0)for(;;)__asm__ volatile("cli; hlt");
    syscall_init_current_cpu();
    irq_init_cpu();
    __atomic_store_n(&cpu->online,1u,__ATOMIC_RELEASE);
    for(;;) {
        smp_stop_safe_point();
        __asm__ volatile("cli":::"memory");
        smp_poll_requests();
        uint64_t generation=__atomic_load_n(&cpu->job_generation,__ATOMIC_ACQUIRE);
        if(generation!=cpu->job_completion && !cpu->job_scheduled) {
            __asm__ volatile("sti":::"memory");
            cpu->boot_job(cpu->boot_job_argument);
            __atomic_store_n(&cpu->job_completion,generation,__ATOMIC_RELEASE);
        } else if(__atomic_load_n(&cpu->scheduler_enabled,__ATOMIC_ACQUIRE)) {
            __asm__ volatile("sti":::"memory");
            schedule();scheduler_idle_wait();
        } else __asm__ volatile("sti; hlt":::"memory");
    }
}
int smp_start(void) {
    if(smp_online_count()==cpu_count)return 0;
    for(uint32_t i=1u;i<cpu_count;++i) {
        /* AP still runs under Limine's root: only the HHDM view of the first
           cacheline is accessible before loading our CR3. */
        uint64_t physical=vmm_get_physical(vmm_cr3_to_address_space(boot_cpu.kernel_cr3),(uint64_t)cpus[i]);
        if(!physical)return -1;
        boot_records[i]->extra_argument=hhdm_request.response->offset+physical;
        __atomic_store_n(&boot_records[i]->goto_address,smp_ap_entry,__ATOMIC_RELEASE);
    }
    uint64_t deadline=sched_clock_cycles()+UINT64_C(20000000000);
    while(smp_online_count()!=cpu_count) {
        if(sched_clock_cycles()>deadline)return -1;
        smp_relax();
    }
    char line[96];snprintf(line,sizeof(line),"smp: detected=%u online=%u\n",cpu_count,smp_online_count());debug_log_puts(line);
    return 0;
}
/* Bootstrap/self-test rendezvous. Ordinary work belongs to FlowSched, not to
   these private per-CPU platform mailboxes. */
static void smp_scheduled_probe(void) {
    vex_cpu_t *cpu=smp_current_cpu();
    uint64_t generation=__atomic_load_n(&cpu->job_generation,__ATOMIC_ACQUIRE);
    cpu->boot_job(cpu->boot_job_argument);
    /* Publish before completion; the caller may reuse the mailbox as soon as
       it observes completion. This execution context no longer uses its data. */
    __atomic_store_n(&cpu->job_scheduled,0u,__ATOMIC_RELEASE);
    __atomic_store_n(&cpu->job_completion,generation,__ATOMIC_RELEASE);
}
uint64_t smp_boot_submit(uint32_t index,void (*job)(void *),void *argument) {
    vex_cpu_t *cpu=smp_cpu_at(index);
    if(!cpu || !job || !cpu->online || index==smp_current_index())return 0;
    while(__atomic_test_and_set(&cpu->job_lock,__ATOMIC_ACQUIRE))smp_relax();
    uint32_t task=0u;
    if(__atomic_load_n(&cpu->scheduler_enabled,__ATOMIC_ACQUIRE)) {
        /* Once ordinary applications run here, the idle mailbox cannot be
           expected to run promptly. Use the SAME FlowSched execution engine. */
        task=process_prepare_in_address_space(smp_scheduled_probe,1u,0u);
        if(!task || scheduler_bind_prepared_task(task,index)) {
            if(task)scheduler_abort_process(task);
            __atomic_clear(&cpu->job_lock,__ATOMIC_RELEASE);return 0u;
        }
        scheduler_detach_process(task);
        __atomic_store_n(&cpu->job_scheduled,1u,__ATOMIC_RELEASE);
    }
    cpu->boot_job=job;cpu->boot_job_argument=argument;
    uint64_t generation=__atomic_add_fetch(&cpu->job_generation,1u,__ATOMIC_RELEASE);
    if(task && scheduler_publish_process(task)) {
        scheduler_abort_process(task);__atomic_store_n(&cpu->job_scheduled,0u,__ATOMIC_RELEASE);
        __atomic_store_n(&cpu->job_completion,generation,__ATOMIC_RELEASE);
        __atomic_clear(&cpu->job_lock,__ATOMIC_RELEASE);return 0u;
    }
    irq_send_ipi(cpu->apic_id,VEX_IPI_VECTOR);
    return generation;
}
void smp_boot_wait(uint32_t index,uint64_t generation) {
    vex_cpu_t *cpu=smp_cpu_at(index);
    if(!cpu || !generation)return;
    while(__atomic_load_n(&cpu->job_completion,__ATOMIC_ACQUIRE)!=generation)smp_relax();
    __atomic_clear(&cpu->job_lock,__ATOMIC_RELEASE);
}
int smp_boot_call(uint32_t index,void (*job)(void *),void *argument) {
    if(!job || index>=cpu_count)return -1;
    if(index==smp_current_index()){job(argument);return 0;}
    uint64_t generation=smp_boot_submit(index,job,argument);
    if(!generation)return -1;smp_boot_wait(index,generation);return 0;
}

