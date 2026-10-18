#ifndef UNOS_SMP_H
#define UNOS_SMP_H
#include <stddef.h>
#include <stdint.h>
#include <unos/gdt.h>
#include <unos/vex_memory_view_abi.h>

/* Private platform/CPU state. Neither CPU count nor APIC IDs are an ABI. */
typedef struct vex_cpu {
    uint64_t stack_top, kernel_cr3; /* assembly AP entry offsets 0, 8 */
    struct vex_cpu *self; /* offset 16: usable after switching to kernel CR3 */
    void *gdt_storage;
    uint32_t index, apic_id;
    void *scheduler;
    void *active_root; /* private VMM execution lease, not a userspace handle */
    uint64_t tlb_request, tlb_ack, tlb_flushes, irq_count, timer_count;
    uint64_t reschedules;
    uint64_t guard_enter_ns, guard_entry_pc, guard_max_ns, guard_max_pc;
    uint32_t nonpreempt_depth;
    uint8_t online, reschedule_pending, scheduler_enabled, tlb_servicing;
    void (*boot_job)(void *);
    void *boot_job_argument;
    uint64_t job_generation, job_completion;
    uint8_t job_lock;
    uint8_t job_scheduled;
    uint8_t stop_requested, stopped;
    /* Bounded diagnostic history; one timer writer per CPU. */
    /* Profiler: set while this CPU spins on a kernel gate (see kernel_guard.h). */
    uint64_t spin_gate;
    uint32_t user_sample_epoch, user_sample_next;
    uint32_t user_sample_tid[64];
    uint64_t user_sample_rip[64];
    uint32_t random_capabilities;
    vex_cpu_topology_info_t topology;
} vex_cpu_t;

int smp_prepare(void);
int smp_start(void);
uint32_t smp_cpu_count(void);
uint32_t smp_online_count(void);
vex_cpu_t *smp_cpu_at(uint32_t index);
static inline vex_cpu_t *smp_current_cpu(void) { return gdt_cpu_local(); }
static inline uint32_t smp_current_index(void) {
    vex_cpu_t *cpu=smp_current_cpu();return cpu?cpu->index:0;
}
void smp_poll_requests(void);
void smp_reschedule(uint32_t index);
void smp_tlb_shootdown(uint64_t cr3, uint64_t start, uint64_t length);
void smp_handle_ipi(void);
void smp_stop_safe_point(void);
int smp_stop_others(uint64_t timeout_ns);
int smp_boot_call(uint32_t index, void (*job)(void *), void *argument);
uint64_t smp_boot_submit(uint32_t index, void (*job)(void *), void *argument);
void smp_boot_wait(uint32_t index, uint64_t generation);
int smp_self_test(void);
/* Runs as an ordinary BSP task; AP rendezvous tests real remote wait/wakeup. */
void smp_wait_race_test(void);
int scheduler_wake_observed(uint32_t tid); /* platform test diagnostic only */
void scheduler_watch_wait(uint32_t tid,const volatile uint8_t *flag);
void scheduler_dump_wait_watch(void);
/* Lock wait loops must acknowledge lock-free shootdowns even with IF=0. */
static inline void smp_relax(void) { smp_poll_requests(); __asm__ volatile("pause"); }
#endif

