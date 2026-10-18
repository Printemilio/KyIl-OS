#include <unos/gdt.h>

#include <stdint.h>

#define GDT_ACCESS_KERNEL_CODE 0x9au
#define GDT_ACCESS_KERNEL_DATA 0x92u
#define GDT_ACCESS_USER_CODE   0xfau
#define GDT_ACCESS_USER_DATA   0xf2u

#define GDT_FLAGS_LONG_MODE    0xau
#define GDT_FLAGS_PROTECTED    0xcu

#define MSR_GS_BASE         0xc0000101u
#define MSR_KERNEL_GS_BASE  0xc0000102u

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t limit_high_flags;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_pointer_t;

/* 64-bit Task State Segment. Only RSP0 is used (ring-3 → ring-0 stack switch). */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed)) tss64_t;

typedef struct {
    tss64_t tss;
    gdt_syscall_cpu_t syscall;
    gdt_entry_t gdt[8];
    uint8_t ist[4][4096];
} gdt_cpu_state_t;
static gdt_cpu_state_t boot_cpu __attribute__((aligned(64)));


/* GDT: entries 0-5 (normal) + entries 6-7 (64-bit TSS descriptor, 16 bytes). */


static gdt_entry_t make_entry(uint8_t access, uint8_t flags) {
    gdt_entry_t entry = {0};

    entry.limit_low = 0xffffu;
    entry.access = access;
    entry.limit_high_flags = (uint8_t)(0x0fu | (flags << 4u));
    return entry;
}

static void write_msr(uint32_t msr, uint64_t value) {
    const uint32_t lo = (uint32_t)value;
    const uint32_t hi = (uint32_t)(value >> 32u);

    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

size_t gdt_cpu_storage_size(void) { return sizeof(gdt_cpu_state_t); }
void gdt_init_cpu(void *storage, uint32_t index, uint32_t apic_id, void *local) {
    /* This OS mediates user FS/GS bases through native calls. Do not inherit
       FSGSBASE enabled from firmware/bootloader: arbitrary high user GS would
       violate paranoid SWAPGS entry's kernel/user base invariant. This is not
       an advertised OS feature, and MSR setters work on old x86-64 CPUs. */
    uint64_t cr4;__asm__ volatile("mov %%cr4,%0":"=r"(cr4));
    cr4&=~(1ull<<16u);__asm__ volatile("mov %0,%%cr4"::"r"(cr4):"memory");
    gdt_cpu_state_t *state=storage;
    *state=(gdt_cpu_state_t){0};
    gdt_entry_t *gdt=state->gdt;
    tss64_t *tss=&state->tss;
    state->syscall.cpu_id=apic_id;
    state->syscall.cpu_index=index;
    state->syscall.local=local;
    state->syscall.tss=tss;
    tss->ist1=(uint64_t)&state->ist[0][4096];
    tss->ist2=(uint64_t)&state->ist[1][4096];
    tss->ist3=(uint64_t)&state->ist[2][4096];
    tss->ist4=(uint64_t)&state->ist[3][4096];
    tss->iopb_offset=sizeof(*tss);
    gdt[0] = (gdt_entry_t){0};
    gdt[1] = make_entry(GDT_ACCESS_KERNEL_CODE, GDT_FLAGS_LONG_MODE); /* 0x08 */
    gdt[2] = make_entry(GDT_ACCESS_KERNEL_DATA, GDT_FLAGS_PROTECTED); /* 0x10 */
    gdt[3] = make_entry(GDT_ACCESS_USER_CODE, GDT_FLAGS_PROTECTED);   /* 0x18: SYSRET base selector */
    gdt[4] = make_entry(GDT_ACCESS_USER_DATA, GDT_FLAGS_PROTECTED);   /* 0x20 */
    gdt[5] = make_entry(GDT_ACCESS_USER_CODE, GDT_FLAGS_LONG_MODE);   /* 0x28 */

    /* 64-bit TSS descriptor at selector 0x30 (entries 6+7, 16 bytes total).
       type=9 (available 64-bit TSS), S=0, DPL=0, P=1 → access byte 0x89. */
    const uint64_t tss_base  = (uint64_t)tss;
    const uint64_t tss_limit = sizeof(tss64_t) - 1u;
    const uint64_t tss_lo =
        (tss_limit & 0xffffu)                     |
        ((tss_base  & 0xffffu)           << 16)   |
        (((tss_base  >> 16) & 0xffu)     << 32)   |
        (0x89ull                         << 40)   |
        (((tss_limit >> 16) & 0xfu)      << 48)   |
        (((tss_base  >> 24) & 0xffu)     << 56);
    const uint64_t tss_hi = (tss_base >> 32) & 0xffffffffull;

    uint64_t *p = (uint64_t *)&gdt[6];
    p[0] = tss_lo;
    p[1] = tss_hi;

    const gdt_pointer_t pointer = {
        .limit = sizeof(state->gdt) - 1u,
        .base = (uint64_t)gdt
    };

    __asm__ volatile (
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov $0x30, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(pointer)
        : "rax", "memory"
    );

    /* The BSP starts in kernel mode, so GS_BASE points at its CPU-local
     * state.  Before entering user mode user_enter() swaps this with the
     * zero user base; SYSCALL swaps it back on entry. */
    write_msr(MSR_GS_BASE, (uint64_t)&state->syscall);
    write_msr(MSR_KERNEL_GS_BASE, 0u);
}

void gdt_bind_boot_cpu(uint32_t index, uint32_t apic_id, void *local) {
    boot_cpu.syscall.cpu_index=index;
    boot_cpu.syscall.cpu_id=apic_id;
    boot_cpu.syscall.local=local;
}
void gdt_init(void) {
    uint32_t a=0,b,c,d,max,id;
    __asm__ volatile("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));max=a;
    a=max>=0x1f?0x1f:(max>=0xb?0xb:1);c=0;
    __asm__ volatile("cpuid":"+a"(a),"=b"(b),"+c"(c),"=d"(d));id=d;
    if(max<0xb || !b) {
        a=1;__asm__ volatile("cpuid":"+a"(a),"=b"(b),"=c"(c),"=d"(d));id=b>>24;
    }
    gdt_init_cpu(&boot_cpu,0,id,NULL);
}
void gdt_set_tss_rsp0(uint64_t rsp0) {
    tss64_t *tss;
    __asm__ volatile("movq %%gs:56,%0":"=r"(tss));
    tss->rsp0=rsp0;
    __asm__ volatile("movq %0,%%gs:0"::"r"(rsp0):"memory");
}
