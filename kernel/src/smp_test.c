#include <unos/smp.h>
#include <unos/ora.h>
#include <unos/pmm.h>
#include <unos/vmm.h>
#include <unos/heap.h>
#include <unos/boot_requests.h>
#include <unos/debug_log.h>
#include <unos/sched_clock.h>
#include <unos/scheduler.h>
#include <unos/elf.h>
#include <unos/event_counter.h>
#include <unos/process_memory.h>
#include <string.h>
#include <stdio.h>
#include <unos/fd_borrow.h>
#include <unos/compat.h>
#include <unos/vex_legacy_compat.h>
int ladybird_fd_clone_process(uint32_t parent_pid,uint32_t child_pid);

typedef struct {uint32_t index,iterations;int result;uint64_t generation,checksum,clock_ns,clock_cycles;ora_ref_t content;} cpu_probe_t;
static uint64_t test_address=UINT64_C(0xffffd10000000000);
typedef struct {void *root;uint64_t generation;uint32_t ready,stage;int result;} root_probe_t;
static const uint64_t user_test_address=UINT64_C(0x10000000);
static void probe_active_root(void *argument) {
    root_probe_t *probe=argument;uint64_t previous;
    __asm__ volatile("mov %%cr3,%0":"=r"(previous));
    uint64_t root=vmm_address_space_to_cr3(probe->root);
    if(vmm_activate_cr3(root)!=0){probe->result=-3;__atomic_store_n(&probe->ready,1u,__ATOMIC_RELEASE);return;}
    if(*(volatile uint64_t *)(uintptr_t)user_test_address!=0x1973)probe->result=-1;
    __atomic_store_n(&probe->ready,1u,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&probe->stage,__ATOMIC_ACQUIRE))smp_relax();
    if(*(volatile uint64_t *)(uintptr_t)user_test_address!=0x2026)probe->result=-2;
    if(vmm_activate_cr3(previous)!=0)probe->result=-3;
}
static int test_active_roots(uint32_t count,void *a,void *b) {
    if(count==1)return 0;
    root_probe_t *probes=kmalloc((size_t)count*sizeof(*probes));
    void *roots[2]={vmm_new_address_space(),vmm_new_address_space()};int result=0;
    if(!probes || !roots[0] || !roots[1]){result=-1;goto cleanup;}
    memset(probes,0,(size_t)count*sizeof(*probes));
    for(unsigned r=0;r<2;++r)vmm_map_page(roots[r],user_test_address,(uint64_t)a,VMM_PAGE_PRESENT|VMM_PAGE_USER|VMM_PAGE_SHARED_ACCOUNT|VMM_PAGE_WRITABLE|VMM_PAGE_NX);
    /* Round zero: one address space active on every AP. Round one: multiple
       spaces, all using the same VA. Publication uses real atomic rendezvous. */
    for(unsigned round=0;round<2;++round) {
        for(unsigned r=0;r<2;++r)vmm_map_page(roots[r],user_test_address,(uint64_t)a,VMM_PAGE_PRESENT|VMM_PAGE_USER|VMM_PAGE_SHARED_ACCOUNT|VMM_PAGE_WRITABLE|VMM_PAGE_NX);
        for(uint32_t i=1;i<count;++i) {
            probes[i]=(root_probe_t){.root=roots[round?i%2:0]};
            probes[i].generation=smp_boot_submit(i,probe_active_root,&probes[i]);
        }
        for(uint32_t i=1;i<count;++i)while(!__atomic_load_n(&probes[i].ready,__ATOMIC_ACQUIRE))smp_relax();
        for(unsigned r=0;r<2;++r) {
            vmm_unmap_page(roots[r],user_test_address);
            vmm_map_page(roots[r],user_test_address,(uint64_t)b,VMM_PAGE_PRESENT|VMM_PAGE_USER|VMM_PAGE_SHARED_ACCOUNT|VMM_PAGE_NX);
        }
        for(uint32_t i=1;i<count;++i)__atomic_store_n(&probes[i].stage,1u,__ATOMIC_RELEASE);
        for(uint32_t i=1;i<count;++i){smp_boot_wait(i,probes[i].generation);if(probes[i].result)result=-2;}
    }
cleanup:
    for(unsigned r=0;r<2;++r)if(roots[r]){vmm_unmap_page(roots[r],user_test_address);vmm_destroy_address_space(roots[r]);}
    kfree(probes);return result;
}
typedef struct {void *root;uint32_t index;int result;uint64_t generation;} table_probe_t;
static void concurrent_tables(void *argument) {
    table_probe_t *p=argument;
    void *physical=pmm_alloc_page();
    if(!physical){p->result=-1;return;}
    uint64_t address=user_test_address+(uint64_t)p->index*4096u;
    /* Adjacent leaves share PT/PD/PDPT, so simultaneous publication and last
       table retirement must be serialized by the root, not the calling CPU. */
    for(unsigned i=0;i<32u;++i) {
        vmm_map_page(p->root,address,(uint64_t)physical,VMM_PAGE_PRESENT|VMM_PAGE_USER|VMM_PAGE_SHARED_ACCOUNT|VMM_PAGE_WRITABLE|VMM_PAGE_NX);
        if(vmm_get_physical(p->root,address)!=(uint64_t)physical)p->result=-2;
        if(vmm_set_page_flags(p->root,address,VMM_PAGE_PRESENT|VMM_PAGE_USER|VMM_PAGE_SHARED_ACCOUNT|VMM_PAGE_NX))p->result=-3;
        if(vmm_get_page_flags(p->root,address)&VMM_PAGE_WRITABLE)p->result=-4;
        vmm_unmap_page(p->root,address);
        if(vmm_get_physical_any(p->root,address))p->result=-5;
    }
    pmm_free_page(physical);
}
static int test_concurrent_tables(uint32_t count) {
    int result=0;
    table_probe_t *probes=kmalloc((size_t)count*sizeof(*probes));
    uint64_t before=pmm_used_page_count();
    void *root=vmm_new_address_space();
    if(!root || !probes){if(root)vmm_destroy_address_space(root);kfree(probes);return -1;}
    for(uint32_t i=0;i<count;++i)probes[i]=(table_probe_t){.root=root,.index=i};
    for(uint32_t i=1;i<count;++i)probes[i].generation=smp_boot_submit(i,concurrent_tables,&probes[i]);
    concurrent_tables(&probes[0]);
    for(uint32_t i=1;i<count;++i)smp_boot_wait(i,probes[i].generation);
    for(uint32_t i=0;i<count;++i)if(probes[i].result)result=-2;
    vmm_destroy_address_space(root);
    if(pmm_used_page_count()!=before)result=-3;
    kfree(probes);
    return result;
}
typedef struct {void *root;uint32_t ready,stage;int result;uint64_t generation;} lifetime_probe_t;
static void active_lifetime(void *argument) {
    lifetime_probe_t *p=argument;uint64_t previous;
    __asm__ volatile("mov %%cr3,%0":"=r"(previous));
    if(vmm_activate_cr3(vmm_address_space_to_cr3(p->root)))p->result=-1;
    __atomic_store_n(&p->ready,1u,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&p->stage,__ATOMIC_ACQUIRE))smp_relax();
    if(!p->result && *(volatile uint64_t *)(uintptr_t)user_test_address!=0x881973)p->result=-2;
    if(vmm_activate_cr3(previous))p->result=-3;
}
static int test_root_lifetime(uint32_t count) {
    uint64_t before=pmm_used_page_count();int result=0;
    void *root=vmm_new_address_space(),*physical=pmm_alloc_page();
    if(!root || !physical){if(root)vmm_destroy_address_space(root);if(physical)pmm_free_page(physical);return -1;}
    *(uint64_t *)(hhdm_request.response->offset+(uint64_t)physical)=0x881973;
    vmm_map_page(root,user_test_address,(uint64_t)physical,VMM_PAGE_PRESENT|VMM_PAGE_USER|VMM_PAGE_SHARED_ACCOUNT|VMM_PAGE_NX);
    if(vmm_get_physical(root,user_test_address)!=(uint64_t)physical){vmm_destroy_address_space(root);pmm_free_page(physical);return -1;}
    lifetime_probe_t probe={.root=root};
    if(count>1u) {
        probe.generation=smp_boot_submit(1u,active_lifetime,&probe);
        while(!__atomic_load_n(&probe.ready,__ATOMIC_ACQUIRE))smp_relax();
        uint64_t held=pmm_used_page_count();
        vmm_destroy_address_space(root);
        if(pmm_used_page_count()!=held)result=-2; /* root/pages still CPU-owned */
        if(vmm_activate_cr3(vmm_address_space_to_cr3(root))==0)result=-3;
        __atomic_store_n(&probe.stage,1u,__ATOMIC_RELEASE);
        smp_boot_wait(1u,probe.generation);
        if(probe.result)result=-4;
    } else {
        uint64_t previous;__asm__ volatile("mov %%cr3,%0":"=r"(previous));
        if(vmm_activate_cr3(vmm_address_space_to_cr3(root)))result=-2;
        uint64_t held=pmm_used_page_count();vmm_destroy_address_space(root);
        if(pmm_used_page_count()!=held || *(volatile uint64_t *)(uintptr_t)user_test_address!=0x881973)result=-3;
        if(vmm_activate_cr3(previous))result=-4;
    }
    if(pmm_used_page_count()!=before)result=-5; /* last CPU lease retires exactly once */
    return result;
}
static void probe_cpu(void *argument) {
    cpu_probe_t *probe=argument;vex_cpu_t *cpu=smp_current_cpu();
    probe->clock_cycles=sched_clock_cycles();probe->clock_ns=sched_clock_ns();
    probe->result=cpu && cpu->index==probe->index && gdt_current_cpu_id()==cpu->apic_id ? 0:-1;
    if(!scheduler_current_process() || scheduler_current_process()->pid)probe->result=-1;
    uint32_t a,d;__asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(0xc0000084u));
    if(!(a&(1u<<9u)))probe->result=-1; /* IF masked by hardware at SYSCALL entry */
}
static void probe_mapping(void *argument) {
    uint64_t *result=argument;*result=*(volatile uint64_t *)(uintptr_t)test_address;
}
typedef struct {uint64_t capability,generation;int failed;} counter_probe_t;
static void counter_contention(void *argument) {
    counter_probe_t *p=argument;
    for(unsigned i=0;i<4096u;++i) {
        if(vex_event_counter_retain(p->capability)) {p->failed=1;return;}
        if(vex_event_counter_signal(p->capability,1u))p->failed=1;
        if(vex_event_counter_release(p->capability))p->failed=1;
    }
}
static int test_counter_contention(uint32_t count) {
    uint64_t cap,value;int failed=0;
    counter_probe_t *p=kmalloc((size_t)count*sizeof(*p));
    if(!p)return -1;
    if(vex_event_counter_create(0u,0u,&cap)){kfree(p);return -1;}
    for(uint32_t i=0;i<count;++i)p[i]=(counter_probe_t){.capability=cap};
    for(uint32_t i=1;i<count;++i) {
        p[i].generation=smp_boot_submit(i,counter_contention,&p[i]);
        if(!p[i].generation)failed=1;
    }
    counter_contention(&p[0]);
    for(uint32_t i=1;i<count;++i)if(p[i].generation)smp_boot_wait(i,p[i].generation);
    for(uint32_t i=0;i<count;++i)failed|=p[i].failed;
    if(vex_event_counter_read(cap,&value,VEX_EVENT_COUNTER_NONBLOCK) ||
       value!=(uint64_t)count*4096u)failed=1;
    if(vex_event_counter_release(cap))failed=1;
    if(vex_event_counter_signal(cap,1u)!=-1)failed=1;
    kfree(p);return failed?-2:0;
}
static void storage_stress(void *argument) {
    cpu_probe_t *probe=argument;uint8_t bytes[4096];
    uint8_t saved[SCHEDULER_FPU_STATE_SIZE] __attribute__((aligned(64)))={0};
    uint32_t tag[4]={probe->index,0x1973,0x2026,probe->index+1u},observed[4];
    uint32_t avx_tag[8]={probe->index,1,2,3,4,5,6,probe->index+1u},avx_observed[8];
    uint64_t cr4;__asm__ volatile("mov %%cr4,%0":"=r"(cr4));int avx=0;
    if(cr4&(1ull<<18u)) {
        uint32_t lo,hi;__asm__ volatile("xgetbv":"=a"(lo),"=d"(hi):"c"(0u));
        avx=(lo&7u)==7u;
    }
    uint32_t a,d;__asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(0xc0000100u));
    uint64_t old_fs=((uint64_t)d<<32u)|a;
    uint64_t fs=UINT64_C(0x100000000)+(uint64_t)probe->index*4096u;
    scheduler_capture_fpu_state(saved);
    uint16_t cw=0x037fu|(uint16_t)((probe->index%4u)<<10u);
    uint32_t mxcsr=0x1f80u|((probe->index%4u)<<13u);
    __asm__ volatile("wrmsr"::"c"(0xc0000100u),"a"((uint32_t)fs),"d"((uint32_t)(fs>>32u)):"memory");
    __asm__ volatile("fldcw %0; ldmxcsr %1; movdqu (%2),%%xmm0"::"m"(cw),"m"(mxcsr),"r"(tag):"memory");
    if(avx)__asm__ volatile("vmovdqu (%0),%%ymm1"::"r"(avx_tag):"memory");
    memset(bytes,0x5a,sizeof(bytes));
    probe->content=ora_create(bytes,sizeof(bytes),ORA_CANONICAL);
    if(!probe->content){probe->result=-2;goto restore;}
    for(unsigned cycle=0;cycle<64u;++cycle) {
        void *pages[8]={0};
        for(unsigned i=0;i<8u;++i) {
            pages[i]=pmm_alloc_page();
            if(!pages[i]){probe->result=-3;break;}
            uint64_t *data=(void *)(hhdm_request.response->offset+(uint64_t)pages[i]);
            for(unsigned w=0;w<512u;++w)data[w]=((uint64_t)probe->index<<32u)|cycle;
        }
        for(unsigned i=0;i<8u;++i)if(pages[i]) {
            uint64_t *data=(void *)(hhdm_request.response->offset+(uint64_t)pages[i]);
            for(unsigned w=0;w<512u;++w)if(data[w]!=(((uint64_t)probe->index<<32u)|cycle))probe->result=-4;
            pmm_free_page(pages[i]);
        }
    }
    __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(0xc0000100u));
    uint16_t actual_cw;uint32_t actual_mxcsr;
    __asm__ volatile("fnstcw %0; stmxcsr %1; movdqu %%xmm0,(%2)":"=m"(actual_cw),"=m"(actual_mxcsr):"r"(observed):"memory");
    if((((uint64_t)d<<32u)|a)!=fs || actual_cw!=cw || actual_mxcsr!=mxcsr || memcmp(tag,observed,sizeof(tag)))probe->result=-8;
    if(avx) {
        __asm__ volatile("vmovdqu %%ymm1,(%0)"::"r"(avx_observed):"memory");
        if(memcmp(avx_tag,avx_observed,sizeof(avx_tag)))probe->result=-8;
    }
restore:
    scheduler_restore_fpu_state(saved);
    __asm__ volatile("wrmsr"::"c"(0xc0000100u),"a"((uint32_t)old_fs),"d"((uint32_t)(old_fs>>32u)):"memory");
}
static void parallel_compute(void *argument) {
    cpu_probe_t *probe=argument;uint64_t value=0x1973;
    for(unsigned i=0;i<probe->iterations;++i) {
        value^=value<<13u;value^=value>>7u;value^=value<<17u;
    }
    probe->checksum=value;
}
typedef struct {
    uint32_t tid,request,completed;
    int wake_observed;
    uint8_t woken;
} wait_race_t;

typedef struct {uint32_t tid;uint8_t acquired,release;process_t *held;} lease_probe_t;
static void remote_hold_task(void *argument) {
    lease_probe_t *probe=argument;
    SCHEDULER_TASK_REF(ref);
    probe->held=scheduler_task_acquire(&ref,probe->tid,0);
    __atomic_store_n(&probe->acquired,1u,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&probe->release,__ATOMIC_ACQUIRE))smp_relax();
}
static uint32_t lease_cycle_ids[2];
static int task_registered(uint32_t tid);
static uint64_t counter_lifetime_cap;
static uint8_t counter_reader_started;
static int counter_reader_result;
static void counter_lifetime_reader(void) {
    __atomic_store_n(&counter_reader_started,1u,__ATOMIC_RELEASE);
    uint64_t value;
    counter_reader_result=vex_event_counter_read(counter_lifetime_cap,&value,0u);
}
static int test_counter_lifetime(void) {
    for(unsigned cycle=0;cycle<32u;++cycle) {
        counter_reader_started=0u;counter_reader_result=1;
        if(vex_event_counter_create(0u,0u,&counter_lifetime_cap))return -1;
        uint32_t tid=process_create_with_stack(counter_lifetime_reader,1u,65536u);
        if(!tid){vex_event_counter_release(counter_lifetime_cap);return -2;}
        for(;;) {
            int waiting=0;
            {SCHEDULER_TASK_REF(ref);process_t *task=scheduler_task_acquire(&ref,tid,0);
             if(!task)return -3;
             waiting=task->state==PROC_WAITING;}
            if(waiting && __atomic_load_n(&counter_reader_started,__ATOMIC_ACQUIRE))break;
            process_yield();
        }
        if(cycle&1u) {
            scheduler_detach_process(tid);
            if(process_kill(tid))return -4;
            process_yield();
            if(task_registered(tid) || vex_event_counter_release(counter_lifetime_cap))return -5;
        } else {
            if(vex_event_counter_release(counter_lifetime_cap))return -6;
            int status;uint32_t reaped;
            if(process_wait_pid((int32_t)tid,&status,0u,&reaped)!=(int)tid ||
               reaped!=tid || status || counter_reader_result!=-1)return -7;
        }
        short ready;
        if(vex_event_counter_poll(counter_lifetime_cap,1,&ready)!=-1)return -8;
    }
    return 0;
}
static int borrowed_fd;
static uint8_t borrowed_read_resumed;
static void fd_blocked_reader(void) {
    uint64_t value=0u;
    (void)read(borrowed_fd,&value,sizeof(value));
    borrowed_read_resumed=1u;
}
static int test_blocked_fd_borrows(void) {
    for(unsigned cycle=0;cycle<48u;++cycle) {
        int peer=-1;
        if(cycle%3u==0u)borrowed_fd=vex_web_eventfd_create(0u,0u);
        else if(cycle%3u==1u)borrowed_fd=vex_web_timerfd_create(1,0u);
        else {int pair[2];if(socketpair(AF_UNIX,SOCK_SEQPACKET,0,pair))return -1;borrowed_fd=pair[0];peer=pair[1];}
        if(borrowed_fd<0)return -2;
        ladybird_fd_object_t *held=ladybird_fd_acquire(borrowed_fd);
        if(!held)return -3;
        borrowed_read_resumed=0u;
        uint32_t tid=process_prepare_in_address_space(fd_blocked_reader,1u,0u);
        if(!tid || ladybird_fd_clone_process(scheduler_current_pid(),tid) ||
           scheduler_bind_prepared_task(tid,cycle%smp_cpu_count()) || scheduler_publish_process(tid))return -4;
        uint64_t watch=sched_clock_ns();
        for(;;) {
            if(sched_clock_ns()-watch>UINT64_C(3000000000)) {
                {SCHEDULER_TASK_REF(ref);process_t *p=scheduler_task_acquire(&ref,tid,0);
                 if(p){char line[180];snprintf(line,sizeof(line),"smp-fd-wait: tid=%u state=%u cpu=%u pin=%u claim=%u mode=%u\n",tid,p->state,p->scheduling.cpu->cpu_id,p->scheduling.pinned_cpu,p->scheduling.running_cpu,p->user_mode);debug_log_puts(line);}}
                scheduler_flow_report();watch=sched_clock_ns();
            }
            int waiting=0;
            {SCHEDULER_TASK_REF(ref);process_t *p=scheduler_task_acquire(&ref,tid,0);
             if(!p)return -5;
             waiting=p->state==PROC_WAITING && p->fd_borrows && p->fd_borrows->object==held;}
            if(waiting)break;process_yield();
        }
        if(close(borrowed_fd) || process_kill(tid))return -6;
        int status=-1;
        if(process_wait_pid((int32_t)tid,&status,0u,NULL)!=(int)tid || status!=137 ||
           borrowed_read_resumed || held->refcount!=1u)return -7;
        ladybird_fd_release(held);
        if(peer>=0 && close(peer))return -8;
    }
    return 0;
}
static uint32_t lease_cycle_started;
static uint8_t lease_never_woken;
static void lease_cycle_task(void) {
    uint32_t self=scheduler_current_tid(),other=lease_cycle_ids[self==lease_cycle_ids[0]?1u:0u];
    SCHEDULER_TASK_REF(ref);
    if(!scheduler_task_acquire(&ref,other,0)) {debug_log_puts("smp-lease: FAILED acquire cycle\n");return;}
    __atomic_add_fetch(&lease_cycle_started,1u,__ATOMIC_RELEASE);
    for(;;)process_wait_event(&lease_never_woken);
}
static void lease_victim(void) {for(;;)process_wait_event(&lease_never_woken);}
static int task_registered(uint32_t tid) {
    SCHEDULER_TASK_REF(ref);return scheduler_task_acquire(&ref,tid,0)!=NULL;
}
typedef struct {uint32_t tid;int result;} publication_probe_t;
static uint32_t publication_started;
static uint64_t publication_value;
static uint8_t unpublished_bytes[4096];
static uint32_t unpublished_tid;
static int unpublished_result;
static void abandon_builder(void) {
    void *root=vmm_new_address_space();
    if(!root){unpublished_result=-1;return;}
    uint64_t cr3=vmm_address_space_to_cr3(root),space=0;
    unpublished_tid=process_prepare_user_abi_caps((void (*)(void))user_test_address,
        1u,cr3,user_test_address+4096u,0u,0u);
    if(!unpublished_tid){vmm_destroy_address_space(root);unpublished_result=-2;return;}
    if(process_memory_namespace_begin(unpublished_tid,user_test_address+8192u,&space) ||
       process_memory_register_region_in_namespace(unpublished_tid,space,user_test_address,
            sizeof(unpublished_bytes),1,0x22u,0u,unpublished_bytes) ||
       process_memory_materialize_namespace(unpublished_tid,space,cr3))unpublished_result=-3;
    /* Deliberately exit without publish/abort or namespace_commit. The stopped
       creator's reaper must retire this draft before freeing its page tables. */
}
static void publication_child(void) {
    publication_value=scheduler_current_fs_base();
    __atomic_add_fetch(&publication_started,1u,__ATOMIC_RELEASE);
}
static void probe_unpublished(void *argument) {
    publication_probe_t *probe=argument;
    for(unsigned i=0;i<32u;++i) {
        process_wake(probe->tid);
        if(scheduler_publish_process(probe->tid)!=-1 ||
           scheduler_abort_process(probe->tid)!=-1 || process_kill(probe->tid)!=-1)
            probe->result=-1;
        SCHEDULER_TASK_REF(ref);
        process_t *p=scheduler_task_acquire(&ref,probe->tid,0);
        if(!p || p->state!=PROC_CONSTRUCTING || p->scheduling.runnable ||
           p->scheduling.running_cpu!=UINT32_MAX)probe->result=-2;
    }
}
static int test_publication(void) {
    for(unsigned cycle=0;cycle<32u;++cycle) {
        publication_started=0u;publication_value=0u;
        publication_probe_t probe={.tid=process_prepare_in_address_space(publication_child,1u,0u)};
        if(!probe.tid)return -1;
        if(smp_cpu_count()>1u)smp_boot_call(1u,probe_unpublished,&probe);
        for(unsigned i=0;i<4u;++i)process_yield();
        if(probe.result || __atomic_load_n(&publication_started,__ATOMIC_ACQUIRE))return -2;
        if(cycle&1u) {
            if(scheduler_abort_process(probe.tid) || task_registered(probe.tid) ||
               scheduler_publish_process(probe.tid)!=-1)return -3;
        } else {
            scheduler_set_process_fs_base(probe.tid,0x12340000u+cycle);
            if(scheduler_publish_process(probe.tid) || scheduler_publish_process(probe.tid)!=-1)return -4;
            if(process_wait_pid((int32_t)probe.tid,NULL,0u,NULL)!=(int)probe.tid ||
               __atomic_load_n(&publication_started,__ATOMIC_ACQUIRE)!=1u ||
               publication_value!=0x12340000u+cycle || task_registered(probe.tid))return -5;
        }
    }
    memset(unpublished_bytes,0xa7,sizeof(unpublished_bytes));
    ora_ref_t held=ora_create(unpublished_bytes,sizeof(unpublished_bytes),ORA_CANONICAL);
    if(!held)return -6;
    uint64_t refs=ora_reference_count(held);
    for(unsigned cycle=0;cycle<16u;++cycle) {
        unpublished_tid=0u;unpublished_result=0;
        uint32_t tid=process_create_with_stack(abandon_builder,1u,65536u);
        int waited=tid?process_wait_pid((int32_t)tid,NULL,0u,NULL):-1;
        /* waitpid publishes stop/status, not completion of an orphan draft's
           destructor on another CPU. Require real retirement quiescence before
           inspecting storage refs; a leak still fails after that boundary. */
        uint64_t deadline=sched_clock_ns()+UINT64_C(1000000000);
        while(unpublished_tid &&
              (task_registered(unpublished_tid) || scheduler_pending_retirements())) {
            if(sched_clock_ns()>=deadline)break;
            process_yield();
        }
        int registered=unpublished_tid?task_registered(unpublished_tid):0;
        uint64_t observed_refs=ora_reference_count(held);
        int corrupted=memcmp(ora_data(held),unpublished_bytes,sizeof(unpublished_bytes))!=0;
        if(!tid || waited!=(int)tid ||
            unpublished_result || !unpublished_tid || registered || scheduler_pending_retirements() ||
           ora_reference_count(held)!=refs ||
           memcmp(ora_data(held),unpublished_bytes,sizeof(unpublished_bytes))) {
            char detail[220];snprintf(detail,sizeof(detail),"smp-publication-draft: cycle=%u creator=%u waited=%d child=%u registered=%d refs=%u expected=%u result=%d corrupted=%d\n",cycle,tid,waited,unpublished_tid,registered,(unsigned)observed_refs,(unsigned)refs,unpublished_result,corrupted);debug_log_puts(detail);
            ora_release(held);return -7;
        }
    }
    ora_release(held);
    return 0;
}
static int wait_task_retired(uint32_t tid) {
    uint64_t deadline=sched_clock_ns()+UINT64_C(3000000000);
    while(task_registered(tid)) {
        if(sched_clock_ns()>=deadline)return -1;
        process_yield();
    }
    return 0;
}
static int test_registry_leases(void) {
    for(unsigned cycle=0;cycle<32u;++cycle) {
        lease_cycle_started=0u;
        lease_cycle_ids[0]=process_create_with_stack(lease_cycle_task,1u,65536u);
        lease_cycle_ids[1]=process_create_with_stack(lease_cycle_task,1u,65536u);
        if(!lease_cycle_ids[0] || !lease_cycle_ids[1])return -1;
        while(__atomic_load_n(&lease_cycle_started,__ATOMIC_ACQUIRE)!=2u)process_yield();
        for(unsigned i=0;i<2u;++i) {scheduler_detach_process(lease_cycle_ids[i]);if(process_kill(lease_cycle_ids[i]))return -2;}
        process_yield();
        if(wait_task_retired(lease_cycle_ids[0]) || wait_task_retired(lease_cycle_ids[1]))return -3;
    }
    if(smp_cpu_count()>1u)for(unsigned cycle=0;cycle<8u;++cycle) {
        lease_probe_t probe={.tid=process_create_with_stack(lease_victim,1u,65536u)};
        if(!probe.tid)return -4;
        uint64_t generation=smp_boot_submit(1u,remote_hold_task,&probe);
        if(!generation)return -5;
        while(!__atomic_load_n(&probe.acquired,__ATOMIC_ACQUIRE))smp_relax();
        int failed=!probe.held;
        scheduler_detach_process(probe.tid);
        if(process_kill(probe.tid))failed=1;
        process_yield();
        { SCHEDULER_TASK_REF(ref);process_t *p=scheduler_task_acquire(&ref,probe.tid,0);
          if(!p || p!=probe.held || p->state!=PROC_ZOMBIE)failed=1; }
        __atomic_store_n(&probe.release,1u,__ATOMIC_RELEASE);
        smp_boot_wait(1u,generation);process_yield();
        if(failed || wait_task_retired(probe.tid))return -6;
    }
    return 0;
}
static void remote_wake_race(void *argument) {
    wait_race_t *race=argument;
    unsigned spins=0;int reported=0;
    for(uint32_t cycle=1;cycle<=2000u;++cycle) {
        uint64_t wait_start=sched_clock_ns();
        while(__atomic_load_n(&race->request,__ATOMIC_ACQUIRE)!=cycle) {
            /* This is a parked platform callback, not a FlowSched task. Its
               request mailbox is signalled by a real IPI. Keep the check and
               HLT adjacent with IF=0 instead of burning an AP while the BSP
               task is legitimately waiting for CPU service under load. */
            __asm__ volatile("cli":::"memory");
            if(__atomic_load_n(&race->request,__ATOMIC_ACQUIRE)!=cycle)
                __asm__ volatile("sti; hlt":::"memory");
            else __asm__ volatile("sti":::"memory");
            if(!(++spins&1023u) && !reported && sched_clock_ns()-wait_start>5000000000ull) {
                reported=1;char line[160];snprintf(line,sizeof(line),"smp-wait: diagnostic tid=%u cycle=%u request=%u completed=%u flag=%u wake=%d\n",race->tid,cycle,__atomic_load_n(&race->request,__ATOMIC_ACQUIRE),__atomic_load_n(&race->completed,__ATOMIC_ACQUIRE),__atomic_load_n(&race->woken,__ATOMIC_ACQUIRE),race->wake_observed);
                debug_log_puts(line);
                for(uint32_t i=0;i<smp_cpu_count();++i) {
                    vex_cpu_t *cpu=smp_cpu_at(i);
                    snprintf(line,sizeof(line),"smp-wait-timer: cpu=%u irqs=%08x%08x timers=%08x%08x\n",i,(unsigned)(cpu->irq_count>>32),(unsigned)cpu->irq_count,(unsigned)(cpu->timer_count>>32),(unsigned)cpu->timer_count);debug_log_puts(line);
                }
                scheduler_dump_wait_watch();
            }
        }
        __atomic_store_n(&race->woken,1u,__ATOMIC_RELEASE);
        race->wake_observed=scheduler_wake_observed(race->tid);
        __atomic_store_n(&race->completed,cycle,__ATOMIC_RELEASE);
    }
}
/* A real remote kill request must not abandon an executing kernel context.
   The AP checks the registry before the BSP reaches its explicit safe point. */
typedef struct stop_probe {uint32_t tid;uint8_t ready,checked;int result;uint64_t generation;} stop_probe_t;
static stop_probe_t stop_probe;
static uint8_t stop_resumed;
static void request_remote_stop(void *argument) {
    stop_probe_t *probe=argument;
    while(!__atomic_load_n(&probe->ready,__ATOMIC_ACQUIRE))smp_relax();
    if(process_kill(probe->tid))probe->result=-1;
    SCHEDULER_TASK_REF(ref);
    process_t *p=scheduler_task_acquire(&ref,probe->tid,0);
    if(!p || !p->terminate_requested || p->state!=PROC_RUNNING ||
       p->stopped_cleanup_done || p->scheduling.running_cpu!=0u)probe->result=-2;
    __atomic_store_n(&probe->checked,1u,__ATOMIC_RELEASE);
}
static void remote_stop_victim(void) {
    stop_probe.tid=scheduler_current_tid();
    stop_probe.generation=smp_boot_submit(1u,request_remote_stop,&stop_probe);
    __atomic_store_n(&stop_probe.ready,1u,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&stop_probe.checked,__ATOMIC_ACQUIRE))smp_relax();
    /* The kernel is still executing here. Only this boundary may stop us. */
    scheduler_safe_point();
    stop_resumed=1u;
    process_exit_status(99);
}
static int test_spawn_memory_borrow(void) {
    const uint64_t control=UINT64_C(0x600000000);
    for(unsigned cycle=0;cycle<12u;++cycle) {
        uint32_t parent=0,child=0;uint64_t root=0,physical=0;
        if(elf_spawn_pid("/usr/linux-tests/linux_spawn_borrow_smoke",&parent) || !parent)return -1;
        SCHEDULER_TASK_REF(child_ref);
        process_t *borrower=NULL;
        for(;;) {
            {SCHEDULER_TASK_REF(parent_ref);
             process_t *p=scheduler_task_acquire(&parent_ref,parent,0);
             if(!p || p->state==PROC_ZOMBIE)return -2;
             root=p->cr3;physical=vmm_get_physical(vmm_cr3_to_address_space(root),control);
             if(physical)child=__atomic_load_n((uint32_t *)(uintptr_t)(hhdm_request.response->offset+physical),__ATOMIC_ACQUIRE);}
            if(child)break;process_yield();
        }
        borrower=scheduler_task_acquire(&child_ref,child,0);
        if(!borrower || borrower->cr3!=root || !borrower->spawn_shared)return -3;
        if(process_kill(parent))return -4;
        int status=-1;
        if(process_wait_pid((int32_t)parent,&status,0u,NULL)!=(int)parent || status!=137 || task_registered(parent))return -5;
        /* Do not hold a reference to the dead parent task. Only the child must
           keep its memory inventory, sparse backing and old root alive. */
        if(vmm_get_physical(vmm_cr3_to_address_space(root),control)!=physical ||
           !(vmm_get_page_flags(vmm_cr3_to_address_space(root),UINT64_C(0x610000000))&VMM_PAGE_PRESENT))return -6;
        uint32_t *words=(uint32_t *)(uintptr_t)(hhdm_request.response->offset+physical);
        words[1]=cycle&1u;__atomic_store_n(&words[2],1u,__ATOMIC_RELEASE);
        while(borrower->state!=PROC_ZOMBIE || __atomic_load_n(&borrower->stopped_cleanup_done,__ATOMIC_ACQUIRE)!=2u)process_yield();
        if(borrower->exit_status)return -7;
        scheduler_task_release(&child_ref);
        process_yield();
        if(task_registered(child))return -8;
    }
    return 0;
}
static int test_remote_stop(void) {
    if(smp_cpu_count()<2u)return 0;
    for(unsigned cycle=0;cycle<32u;++cycle) {
        memset(&stop_probe,0,sizeof(stop_probe));stop_resumed=0u;
        uint32_t tid=process_create_with_stack(remote_stop_victim,1u,65536u);
        int status=-1;
        if(!tid || process_wait_pid((int32_t)tid,&status,0u,NULL)!=(int)tid)return -1;
        smp_boot_wait(1u,stop_probe.generation);
        if(status!=137 || stop_probe.result || stop_resumed || task_registered(tid))return -2;
    }
    return 0;
}
typedef struct dispatch_probe {
    uint32_t tid,index,iterations;int result;uint64_t checksum;
} dispatch_probe_t;
static dispatch_probe_t *dispatch_probes;
static uint32_t dispatch_count;
static uint8_t dispatch_start,dispatch_complete,dispatch_run_users;
static uint32_t dispatch_waiter;
static uint32_t dispatch_done;
static void dispatch_worker(void) {
    dispatch_probe_t *probe=NULL;
    for(uint32_t i=0;i<dispatch_count;++i)
        if(dispatch_probes[i].tid==scheduler_current_tid()){probe=&dispatch_probes[i];break;}
    if(!probe)process_exit_status(98);
    while(!__atomic_load_n(&dispatch_start,__ATOMIC_ACQUIRE))process_wait_event(&dispatch_start);
    uint64_t expected[2]={0x1973000000000000ull+probe->tid,0x2026000000000000ull+probe->index};
    __asm__ volatile("movdqu %0,%%xmm15"::"m"(expected):"memory");
    uint64_t value=0x1973;
    for(uint32_t i=0;i<probe->iterations;++i) {
        value^=value<<13u;value^=value>>7u;value^=value<<17u;
        if(!(i&65535u)) {
            process_yield();
            uint64_t observed[2];uint32_t a,d;
            __asm__ volatile("movdqu %%xmm15,%0":"=m"(observed)::"memory");
            __asm__ volatile("rdmsr":"=a"(a),"=d"(d):"c"(0xc0000100u));
            if(smp_current_index()!=probe->index || memcmp(expected,observed,sizeof(expected)) ||
               (((uint64_t)d<<32u)|a)!=0x12340000u+probe->tid)probe->result=-1;
        }
    }
    probe->checksum=value;
    if(__atomic_add_fetch(&dispatch_done,1u,__ATOMIC_ACQ_REL)==dispatch_count) {
        __atomic_store_n(&dispatch_complete,1u,__ATOMIC_RELEASE);process_wake(dispatch_waiter);
    }
    while(!__atomic_load_n(&dispatch_run_users,__ATOMIC_ACQUIRE))process_wait_event(&dispatch_run_users);
    uint32_t target=(probe->index+1u)%smp_cpu_count();
    if(scheduler_relocate_current(target))probe->result=-3;
    uint64_t after[2];uint32_t lo,hi;
    __asm__ volatile("movdqu %%xmm15,%0":"=m"(after)::"memory");
    __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0xc0000100u));
    if(memcmp(after,expected,sizeof(after)) || (((uint64_t)hi<<32u)|lo)!=0x12340000u+probe->tid)probe->result=-4;
    /* Exercise actual AP ring3, syscall stack, TLS/SSE/AVX and thread teardown.
       Separate processes run this concurrently on the selected CPUs. */
    uint32_t child=0u;int status=-1;
    if(elf_spawn_pid("/usr/linux-tests/linux_thread_context_smoke",&child) || !child ||
       process_wait_pid((int32_t)child,&status,0u,NULL)!=(int)child || status)
        probe->result=-2;
    char detail[128];snprintf(detail,sizeof(detail),"smp-dispatch-worker: cpu=%u tid=%u child=%u status=%d result=%d\n",probe->index,probe->tid,child,status,probe->result);debug_log_puts(detail);
    process_exit_status(probe->result?97:0);
}
static int test_context_dispatch(void) {
    if(smp_cpu_count()>UINT32_MAX/2u)return -1;
    dispatch_count=2u*smp_cpu_count();dispatch_start=dispatch_complete=dispatch_run_users=0u;dispatch_done=0u;
    dispatch_waiter=scheduler_current_tid();
    dispatch_probes=kmalloc((size_t)dispatch_count*sizeof(*dispatch_probes));
    if(!dispatch_probes)return -1;
    memset(dispatch_probes,0,(size_t)dispatch_count*sizeof(*dispatch_probes));
    int result=0;
    /* Publish all metadata before any worker can access the probe array. */
    for(uint32_t i=0;i<dispatch_count;++i) {
        dispatch_probe_t *p=&dispatch_probes[i];p->index=i%smp_cpu_count();
        p->iterations=48000000u/dispatch_count+(i<48000000u%dispatch_count);
        p->tid=process_prepare_in_address_space(dispatch_worker,1u,0u);
        if(!p->tid || scheduler_bind_prepared_task(p->tid,p->index)){result=-2;break;}
        scheduler_set_process_fs_base(p->tid,0x12340000u+p->tid);
    }
    if(result) {
        for(uint32_t i=0;i<dispatch_count;++i)if(dispatch_probes[i].tid)scheduler_abort_process(dispatch_probes[i].tid);
        kfree(dispatch_probes);dispatch_probes=NULL;return result;
    }
    for(uint32_t i=1;i<smp_cpu_count();++i) {
        __atomic_store_n(&smp_cpu_at(i)->scheduler_enabled,1u,__ATOMIC_RELEASE);
        smp_reschedule(i);
    }
    for(uint32_t i=0;i<dispatch_count;++i)
        if(scheduler_publish_process(dispatch_probes[i].tid))result=-3;
    uint64_t start=sched_clock_ns();
    __atomic_store_n(&dispatch_start,1u,__ATOMIC_RELEASE);
    for(uint32_t i=0;i<dispatch_count;++i)process_wake(dispatch_probes[i].tid);
    while(!__atomic_load_n(&dispatch_complete,__ATOMIC_ACQUIRE))process_wait_event(&dispatch_complete);
    uint64_t duration=sched_clock_ns()-start;
    __atomic_store_n(&dispatch_run_users,1u,__ATOMIC_RELEASE);
    for(uint32_t i=0;i<dispatch_count;++i)process_wake(dispatch_probes[i].tid);
    for(uint32_t i=0;i<dispatch_count;++i) {
        int status=-1;
        if(process_wait_pid((int32_t)dispatch_probes[i].tid,&status,0u,NULL)!=(int)dispatch_probes[i].tid ||
           status || dispatch_probes[i].result)result=-4;
        cpu_probe_t reference={.iterations=dispatch_probes[i].iterations};parallel_compute(&reference);
        if(reference.checksum!=dispatch_probes[i].checksum)result=-5;
    }
    char line[160];snprintf(line,sizeof(line),"smp-dispatch: CPUs=%u FlowSched contexts/TLS/SSE/migration/ring3 compute_ns=%u result=%d\n",smp_cpu_count(),(unsigned)duration,result);debug_log_puts(line);
    kfree(dispatch_probes);dispatch_probes=NULL;return result;
}
static int offset_test_fd;
static uint32_t *offset_test_seen;
static uint8_t offset_test_start;
static uint32_t offset_test_done,offset_test_errors;
static void shared_offset_reader(void) {
    while(!__atomic_load_n(&offset_test_start,__ATOMIC_ACQUIRE))process_wait_event(&offset_test_start);
    uint32_t value,iterations=0;
    for(;;) {
        ssize_t n=read(offset_test_fd,&value,sizeof(value));
        if(!n)break;
        if(n!=sizeof(value) || value>=16384u) {__atomic_add_fetch(&offset_test_errors,1u,__ATOMIC_RELAXED);break;}
        if(__atomic_fetch_add(&offset_test_seen[value],1u,__ATOMIC_RELAXED))
            __atomic_add_fetch(&offset_test_errors,1u,__ATOMIC_RELAXED);
        if(!(++iterations&63u))process_yield();
    }
    __atomic_add_fetch(&offset_test_done,1u,__ATOMIC_RELEASE);
}
static int test_shared_fd_offset(void) {
    const char *path="/home/root/smp-shared-offset.bin";
    uint32_t count=smp_cpu_count(),*tids=kmalloc((size_t)count*sizeof(*tids));
    offset_test_seen=kmalloc(16384u*sizeof(*offset_test_seen));
    uint32_t *contents=kmalloc(16384u*sizeof(*contents));
    if(!tids || !offset_test_seen || !contents)return -1;
    for(unsigned i=0;i<16384u;++i)contents[i]=i;
    memset(offset_test_seen,0,16384u*sizeof(*offset_test_seen));
    offset_test_fd=open(path,O_CREAT|O_TRUNC|O_RDWR,0600);
    if(offset_test_fd<0 || write(offset_test_fd,contents,16384u*sizeof(*contents))!=16384u*sizeof(*contents) ||
       lseek(offset_test_fd,0,SEEK_SET))return -2;
    offset_test_start=0;offset_test_done=offset_test_errors=0;
    for(uint32_t i=0;i<count;++i) {
        tids[i]=process_prepare_in_address_space(shared_offset_reader,1u,0u);
        if(!tids[i] || ladybird_fd_clone_process(scheduler_current_process_id(),tids[i]) ||
           scheduler_bind_prepared_task(tids[i],i) || scheduler_publish_process(tids[i]))return -3;
    }
    __atomic_store_n(&offset_test_start,1u,__ATOMIC_RELEASE);
    for(uint32_t i=0;i<count;++i)process_wake(tids[i]);
    for(uint32_t i=0;i<count;++i) {
        int status;
        if(process_wait_pid((int32_t)tids[i],&status,0u,NULL)!=(int)tids[i] || status)return -4;
    }
    int result=offset_test_errors || offset_test_done!=count ? -5:0;
    for(unsigned i=0;i<16384u;++i)if(offset_test_seen[i]!=1u)result=-6;
    if(close(offset_test_fd) || unlink(path))result=-7;
    kfree(tids);kfree(contents);kfree(offset_test_seen);offset_test_seen=NULL;return result;
}
static int test_usercopy_unmap(void) {
    for(unsigned cycle=0;cycle<12u;++cycle) {
        uint32_t pid=0;
        if(elf_spawn_pid("/usr/linux-tests/linux_smp_usercopy_smoke",&pid) || !pid)return -1;
        SCHEDULER_TASK_REF(leader_ref);process_t *p=scheduler_task_acquire(&leader_ref,pid,0);
        uint64_t physical=0;
        while(p && !(physical=vmm_get_physical(vmm_cr3_to_address_space(p->cr3),UINT64_C(0x630000000)))) {
            if(p->state==PROC_ZOMBIE)return -2;process_yield();
        }
        if(!p)return -3;
        uint32_t *words=(void *)(uintptr_t)(hhdm_request.response->offset+physical);
        words[4]=cycle&1u;
        uint64_t check=sched_clock_ns();
        for(;;) {
            uint32_t tid=__atomic_load_n(&words[2],__ATOMIC_ACQUIRE);int waiting=0;
            {SCHEDULER_TASK_REF(ref);process_t *t=tid?scheduler_task_acquire(&ref,tid,0):NULL;
             if(t)waiting=(t->state==PROC_WAITING || t->state==PROC_SLEEPING) && t->fd_borrows!=NULL;}
            if(waiting)break;
            if(sched_clock_ns()-check>UINT64_C(2000000000)) {
                SCHEDULER_TASK_REF(ref);process_t *child=tid?scheduler_task_acquire(&ref,tid,0):NULL;
                char line[160];snprintf(line,sizeof(line),"usercopy-wait: parent=%u state=%u child=%u state=%u borrows=%u ready=%u go=%u\n",pid,p->state,tid,child?child->state:99u,(unsigned)(child && child->fd_borrows),words[0],words[3]);debug_log_puts(line);
                check=sched_clock_ns();
            }
            if(p->state==PROC_ZOMBIE)return -4;process_yield();
        }
        __atomic_store_n(&words[3],1u,__ATOMIC_RELEASE);
        int status=-1;
        if(process_wait_pid((int32_t)pid,&status,0u,NULL)!=(int)pid || status)return -5;
    }
    return 0;
}
typedef struct test_task_refs {scheduler_task_ref_t *refs;uint32_t count;} test_task_refs_t;
static void test_task_refs_release(test_task_refs_t *owner) {
    if(!owner->refs)return;
    for(uint32_t i=0;i<owner->count;++i)scheduler_task_release(&owner->refs[i]);
    kfree(owner->refs);
}
static int test_shared_userspace(void) {
    const uint64_t control=UINT64_C(0x600000000);
    uint32_t count=smp_cpu_count();
    /* This one-page test fixture holds 128 simultaneous contexts; the kernel
       CPU inventory and placement have no corresponding fixed maximum. */
    if(count>128u){debug_log_puts("smp-shared-space: fixture capacity exceeded\n");return -1;}
    uint32_t pid=0;
    if(elf_spawn_pid("/usr/linux-tests/linux_smp_shared_space",&pid) || !pid)return -2;
    SCHEDULER_TASK_REF(leader_ref);
    process_t *leader=scheduler_task_acquire(&leader_ref,pid,0);
    if(!leader)return -3;
    uint64_t physical=0;
    while(!(physical=vmm_get_physical(vmm_cr3_to_address_space(leader->cr3),control))) {
        if(leader->state==PROC_ZOMBIE)return -4;process_yield();
    }
    uint32_t *words=(uint32_t *)(uintptr_t)(hhdm_request.response->offset+physical);
    words[6]=48000000u/count;words[7]=48000000u%count;
    __atomic_store_n(&words[0],count,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&words[1],__ATOMIC_ACQUIRE)) {
        if(leader->state==PROC_ZOMBIE)return -5;process_yield();
    }
    test_task_refs_t retained __attribute__((cleanup(test_task_refs_release)))={.count=count};
    retained.refs=kmalloc((size_t)count*sizeof(*retained.refs));
    if(!retained.refs)return -14;
    memset(retained.refs,0,(size_t)count*sizeof(*retained.refs));
    for(uint32_t i=0;i<count;++i) {
        uint32_t tid=__atomic_load_n(&words[16u+i],__ATOMIC_ACQUIRE);
        process_t *thread=scheduler_task_acquire(&retained.refs[i],tid,0);
        if(!thread || thread->group!=leader->group || thread->cr3!=leader->cr3)return -6;
        /* syscall code may still own its context after marking WAITING. Wait
           for the incoming-stack switch to release its exclusive claim. */
        while(thread->state!=PROC_WAITING || thread->scheduling.running_cpu!=UINT32_MAX) {
            if(thread->state==PROC_ZOMBIE)return -7;process_yield();
        }
        /* These are ordinary unpinned threads. Do not force the placement that
           this test is meant to validate; collect actual executed CPU time. */
        words[272u+i]=UINT32_MAX;
    }
    __atomic_store_n(&words[3],1u,__ATOMIC_RELEASE);
    while(__atomic_load_n(&words[10],__ATOMIC_ACQUIRE)!=count) {
        if(leader->state==PROC_ZOMBIE || __atomic_load_n(&words[8],__ATOMIC_ACQUIRE)) {
            (void)process_kill_group(pid,92);return -10;
        }
        process_yield();
    }
    uint64_t start=sched_clock_ns(),start_cycles=sched_clock_cycles();
    __atomic_store_n(&words[11],1u,__ATOMIC_RELEASE);
    int status=-1;
    if(process_wait_pid((int32_t)pid,&status,0u,NULL)!=(int)pid || status)return -8;
    uint64_t duration=sched_clock_ns()-start;
    if(__atomic_load_n(&words[8],__ATOMIC_ACQUIRE) || words[5]!=count)return -9;
    uint32_t used_cpus=0;
    for(uint32_t cpu=0;cpu<count;++cpu) {
        uint64_t total=0;
        for(uint32_t i=0;i<count;++i) {
            process_t *thread=retained.refs[i].target;
            total+=thread->execution_per_cpu?thread->execution_per_cpu[cpu]:(cpu?0:thread->scheduling.cpu_ns);
        }
        if(total)++used_cpus;
    }
    if(used_cpus!=count)return -15;
    char placement[96];snprintf(placement,sizeof(placement),"smp-automatic: %u ordinary shared-CR3 threads executed across %u CPUs PASS\n",count,used_cpus);debug_log_puts(placement);
    uint64_t shared_fault_page=vmm_get_physical(vmm_cr3_to_address_space(leader->cr3),control+4096u);
    if(!shared_fault_page || *(uint32_t *)(uintptr_t)(hhdm_request.response->offset+shared_fault_page)!=count)return -13;
    const uint64_t *checksums=(const uint64_t *)((const uint8_t *)words+2624u);
    const uint64_t *finish_cycles=(const uint64_t *)((const uint8_t *)words+1600u);
    uint64_t last_cycle=start_cycles;
    for(uint32_t i=0;i<count;++i) {
        cpu_probe_t reference={.iterations=48000000u/count+(i<48000000u%count)};
        parallel_compute(&reference);
        if(checksums[i]!=reference.checksum)return -11;
        if(finish_cycles[i]<start_cycles)return -12;
        if(finish_cycles[i]>last_cycle)last_cycle=finish_cycles[i];
    }
    uint64_t cycles=last_cycle-start_cycles,hz=sched_clock_frequency();
    uint64_t compute_ns=cycles/hz*UINT64_C(1000000000)+(cycles%hz)*UINT64_C(1000000000)/hz;
    char line[200];snprintf(line,sizeof(line),"smp-userspace-compute: CPUs=%u shared Process 48000000 iterations execution_ns=%u with_join_cleanup_ns=%u\n",count,(unsigned)compute_ns,(unsigned)duration);debug_log_puts(line);
    return 0;
}
void smp_wait_race_test(void) {
    /* A protected memory transaction may take time, but cannot suppress the
       CPU timer/input IRQs or allow its executing context to migrate. */
    uint32_t irq_cpu=smp_current_index(),irq_tid=scheduler_current_tid();
    uint64_t before_irq=smp_current_cpu()->timer_count,start_irq=sched_clock_ns();
    kernel_guard_t irq_memory=process_memory_begin_update();
    uint64_t held_flags;__asm__ volatile("pushfq; popq %0":"=r"(held_flags));
    while(sched_clock_ns()-start_irq<25000000ull)__asm__ volatile("pause");
    uint64_t after_irq=smp_current_cpu()->timer_count;
    int irq_ok=(held_flags&512u) && after_irq>before_irq &&
               irq_cpu==smp_current_index() && irq_tid==scheduler_current_tid();
    process_memory_end_update(&irq_memory);
    if(!irq_ok){debug_log_puts("smp-irq-memory: FAILED\n");return;}
    debug_log_puts("smp-irq-memory: IRQ delivery with pinned memory/context PASS\n");
    if(test_context_dispatch()){debug_log_puts("smp-dispatch: FAILED\n");return;}
    debug_log_puts("smp-dispatch: PASS\n");
    int stop_result=test_remote_stop();
    if(stop_result){char line[80];snprintf(line,sizeof(line),"smp-stop: FAILED result=%d\n",stop_result);debug_log_puts(line);return;}
    debug_log_puts("smp-stop: remote request/safe boundary/retirement PASS\n");
    int fd_result=test_blocked_fd_borrows();
    if(fd_result){char line[100];snprintf(line,sizeof(line),"smp-fd-borrow: FAILED result=%d\n",fd_result);debug_log_puts(line);return;}
    debug_log_puts("smp-fd-borrow: event/timer/message blocked read close/kill/refcount PASS\n");
    int borrow_result=test_spawn_memory_borrow();
    if(borrow_result){char line[100];snprintf(line,sizeof(line),"smp-memory-borrow: FAILED result=%d\n",borrow_result);debug_log_puts(line);return;}
    debug_log_puts("smp-memory-borrow: parent retirement/lazy fault/child exit/exec PASS\n");
    int shared_result=test_shared_userspace();
    if(shared_result){char line[100];snprintf(line,sizeof(line),"smp-shared-space: FAILED result=%d\n",shared_result);debug_log_puts(line);return;}
    debug_log_puts("smp-shared-space: simultaneous ring3/TLS/SSE/mmap/futex/RW-RX/unmap PASS\n");
    int copy_result=test_usercopy_unmap();
    if(copy_result){char line[80];snprintf(line,sizeof(line),"smp-usercopy: FAILED result=%d\n",copy_result);debug_log_puts(line);return;}
    debug_log_puts("smp-usercopy: blocked read versus sibling unmap/readonly returns EFAULT PASS\n");
    int offset_result=test_shared_fd_offset();
    if(offset_result){char line[100];snprintf(line,sizeof(line),"smp-fd-offset: FAILED result=%d\n",offset_result);debug_log_puts(line);return;}
    debug_log_puts("smp-fd-offset: parallel shared descriptor exact records/no duplicates PASS\n");
    int publication_result=test_publication();
    if(publication_result){char line[80];snprintf(line,sizeof(line),"smp-publication: FAILED result=%d\n",publication_result);debug_log_puts(line);return;}
    debug_log_puts("smp-publication: prepare/publish/abort/remote rejection PASS\n");
    int lease_result=test_registry_leases();
    if(lease_result) {char line[80];snprintf(line,sizeof(line),"smp-lease: FAILED result=%d\n",lease_result);debug_log_puts(line);return;}
    debug_log_puts("smp-lease: cancelled borrower cycles/retirement PASS\n");
    int counter_result=test_counter_lifetime();
    if(counter_result){char line[80];snprintf(line,sizeof(line),"smp-counter: FAILED lifetime result=%d\n",counter_result);debug_log_puts(line);return;}
    debug_log_puts("smp-counter: blocked reader close/kill/retirement PASS\n");
    if(elf_load("/usr/linux-tests/linux_spawn_thread_smoke")) {
        debug_log_puts("smp-spawn: FAILED non-leader exec handshake\n");return;
    }
    debug_log_puts("smp-spawn: non-leader exec/exit/wait PASS\n");
    if(smp_cpu_count()==1u) {
        debug_log_puts("smp-wait: remote race not applicable with one CPU\n");
        return;
    }
    wait_race_t race={.tid=scheduler_current_tid()};
    scheduler_watch_wait(race.tid,&race.woken);
    uint64_t generation=smp_boot_submit(1u,remote_wake_race,&race);
    if(!generation){debug_log_puts("smp-wait: FAILED to start producer\n");return;}
    for(uint32_t cycle=1;cycle<=2000u;++cycle) {
        __atomic_store_n(&race.woken,0u,__ATOMIC_RELEASE);
        __atomic_store_n(&race.request,cycle,__ATOMIC_RELEASE);
        smp_reschedule(1u);
        while(!__atomic_load_n(&race.woken,__ATOMIC_ACQUIRE))process_wait_event(&race.woken);
        while(__atomic_load_n(&race.completed,__ATOMIC_ACQUIRE)!=cycle)smp_relax();
    }
    smp_boot_wait(1u,generation);
    scheduler_watch_wait(0u,NULL);
    debug_log_puts("smp-wait: 2000 remote predicate/register/wake races PASS\n");
}
int smp_self_test(void) {
    uint32_t count=smp_cpu_count();
    int counter_result=test_counter_contention(count);
    if(counter_result)return counter_result;
    debug_log_puts("smp-counter: simultaneous retain/signal/release/consume PASS\n");
    cpu_probe_t *probes=kmalloc((size_t)count*sizeof(*probes));
    if(!probes)return -1;memset(probes,0,(size_t)count*sizeof(*probes));
    int result=0;
    for(uint32_t i=0;i<count;++i) {
        probes[i].index=i;
        uint64_t start=sched_clock_cycles();
        smp_boot_call(i,probe_cpu,&probes[i]);
        uint64_t end=sched_clock_cycles();char line[180];
        snprintf(line,sizeof(line),"smp-clock: cpu=%u BSP=[%08x%08x,%08x%08x] cycles=%08x%08x ns=%08x%08x\n",i,(unsigned)(start>>32),(unsigned)start,(unsigned)(end>>32),(unsigned)end,(unsigned)(probes[i].clock_cycles>>32),(unsigned)probes[i].clock_cycles,(unsigned)(probes[i].clock_ns>>32),(unsigned)probes[i].clock_ns);debug_log_puts(line);
        if(probes[i].result)result=-1;
    }
    for(uint32_t i=1;i<count;++i)probes[i].generation=smp_boot_submit(i,storage_stress,&probes[i]);
    storage_stress(&probes[0]);
    for(uint32_t i=1;i<count;++i)smp_boot_wait(i,probes[i].generation);
    for(uint32_t i=0;i<count;++i) {
        if(probes[i].result || !probes[i].content || probes[i].content!=probes[0].content)result=-2;
    }
    for(uint32_t i=0;i<count;++i)ora_release(probes[i].content);
    void *a=pmm_alloc_page(),*b=pmm_alloc_page();
    if(!a || !b){if(a)pmm_free_page(a);if(b)pmm_free_page(b);kfree(probes);return -3;}
    *(uint64_t *)(hhdm_request.response->offset+(uint64_t)a)=0x1973;
    *(uint64_t *)(hhdm_request.response->offset+(uint64_t)b)=0x2026;
    /* Cache, remap, reread on each CPU while it keeps the same CR3. This fails
       if INVLPG is local-only. No arbitrary sleep is used for acknowledgement. */
    for(unsigned cycle=0;cycle<16;++cycle) {
        void *physical=cycle&1u?b:a;uint64_t expected=cycle&1u?0x2026:0x1973;
        vmm_map_kernel_page(test_address,(uint64_t)physical,VMM_PAGE_PRESENT|VMM_PAGE_WRITABLE|VMM_PAGE_NX);
        for(uint32_t i=0;i<count;++i) {
            uint64_t observed=0;smp_boot_call(i,probe_mapping,&observed);
            if(observed!=expected)result=-4;
        }
    }
    vmm_unmap_kernel_page(test_address);
    if(test_active_roots(count,a,b))result=-7;
    if(test_concurrent_tables(count))result=-9;
    if(test_root_lifetime(count))result=-10;
    pmm_free_page(a);pmm_free_page(b);
    for(uint32_t i=1;i<count;++i) {
        uint64_t before=smp_cpu_at(i)->reschedules;
        smp_reschedule(i);smp_boot_call(i,probe_cpu,&probes[i]);
        if(smp_cpu_at(i)->reschedules<=before || !smp_cpu_at(i)->timer_count)result=-5;
    }
    uint64_t start=sched_clock_ns();
    for(uint32_t i=0;i<count;++i)probes[i].iterations=48000000u/count+(i<48000000u%count);
    for(uint32_t i=1;i<count;++i)probes[i].generation=smp_boot_submit(i,parallel_compute,&probes[i]);
    parallel_compute(&probes[0]);
    for(uint32_t i=1;i<count;++i)smp_boot_wait(i,probes[i].generation);
    uint64_t duration=sched_clock_ns()-start;
    cpu_probe_t reference={.iterations=48000000u/count};parallel_compute(&reference);
    cpu_probe_t extra={.iterations=reference.iterations+1u};
    if(48000000u%count)parallel_compute(&extra);
    for(uint32_t i=0;i<count;++i)if(probes[i].checksum!=(i<48000000u%count?extra.checksum:reference.checksum))result=-6;
    char line[192];snprintf(line,sizeof(line),"smp-platform: CPUs=%u perCPU/PMM/ORA/TLB/IPI/VMM-lifetime result=%d compute_ns=%u checksum=%08x%08x\n",
        count,result,(unsigned)duration,(unsigned)(probes[0].checksum>>32),(unsigned)probes[0].checksum);
    debug_log_puts(line);kfree(probes);return result;
}
