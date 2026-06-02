#include <unos/gdt.h>

#include <stdint.h>

#define GDT_ACCESS_KERNEL_CODE 0x9au
#define GDT_ACCESS_KERNEL_DATA 0x92u
#define GDT_ACCESS_USER_CODE   0xfau
#define GDT_ACCESS_USER_DATA   0xf2u

#define GDT_FLAGS_LONG_MODE    0xau
#define GDT_FLAGS_PROTECTED    0xcu

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

static tss64_t tss;

/* GDT: entries 0-5 (normal) + entries 6-7 (64-bit TSS descriptor, 16 bytes). */
static gdt_entry_t gdt[8];

static gdt_entry_t make_entry(uint8_t access, uint8_t flags) {
    gdt_entry_t entry = {0};

    entry.limit_low = 0xffffu;
    entry.access = access;
    entry.limit_high_flags = (uint8_t)(0x0fu | (flags << 4u));
    return entry;
}

void gdt_init(void) {
    gdt[0] = (gdt_entry_t){0};
    gdt[1] = make_entry(GDT_ACCESS_KERNEL_CODE, GDT_FLAGS_LONG_MODE); /* 0x08 */
    gdt[2] = make_entry(GDT_ACCESS_KERNEL_DATA, GDT_FLAGS_PROTECTED); /* 0x10 */
    gdt[3] = make_entry(GDT_ACCESS_USER_CODE, GDT_FLAGS_PROTECTED);   /* 0x18: SYSRET base selector */
    gdt[4] = make_entry(GDT_ACCESS_USER_DATA, GDT_FLAGS_PROTECTED);   /* 0x20 */
    gdt[5] = make_entry(GDT_ACCESS_USER_CODE, GDT_FLAGS_LONG_MODE);   /* 0x28 */

    /* 64-bit TSS descriptor at selector 0x30 (entries 6+7, 16 bytes total).
       type=9 (available 64-bit TSS), S=0, DPL=0, P=1 → access byte 0x89. */
    const uint64_t tss_base  = (uint64_t)&tss;
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
        .limit = sizeof(gdt) - 1u,
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
}

void gdt_set_tss_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
