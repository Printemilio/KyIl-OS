#ifndef UNOS_GDT_H
#define UNOS_GDT_H

#include <stdint.h>
#include <stddef.h>

void gdt_init(void);
void gdt_set_tss_rsp0(uint64_t rsp0);

/* State used by the SYSCALL entry path.  The active CPU's instance is
 * addressed through %gs, so an entry cannot race another CPU's scratch
 * values.  The layout is part of the assembly ABI: keep rsp0 first. */
typedef struct {
    uint64_t rsp0;
    uint64_t scratch_user_rsp;
    uint64_t scratch_number;
    uint64_t reserved_return_mode; /* return mode is on the syscall frame */
    uint64_t scratch_return_rax;
    uint32_t cpu_id; /* detected once when this CPU's kernel GS is installed */
    uint32_t cpu_index; /* dense boot index, independent of APIC identifiers */
    void *local;
    void *tss;
} gdt_syscall_cpu_t;

size_t gdt_cpu_storage_size(void);
void gdt_init_cpu(void *storage, uint32_t index, uint32_t apic_id, void *local);
void gdt_bind_boot_cpu(uint32_t index, uint32_t apic_id, void *local);
static inline void *gdt_cpu_local(void) {
    void *local;
    __asm__ volatile("movq %%gs:48, %0" : "=r"(local));
    return local;
}

/* Kernel entry paths already select this CPU's GS instance. Keep existing
 * assembly offsets intact; identity must not come from a process-global cache. */
static inline uint32_t gdt_current_cpu_id(void) {
    uint32_t cpu;
    __asm__ volatile ("movl %%gs:%c1, %0" : "=r"(cpu)
                      : "i"(__builtin_offsetof(gdt_syscall_cpu_t, cpu_id)));
    return cpu;
}

#endif
