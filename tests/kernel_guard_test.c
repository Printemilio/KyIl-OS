#include <unos/kernel_guard.h>

static uint64_t flags_now(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return flags;
}

/* Executed in the VM, including from a scheduled kernel thread. */
int kernel_guard_test(void) {
    _Static_assert(__builtin_offsetof(gdt_syscall_cpu_t, cpu_id) == 40, "GS layout");
    uint32_t eax=0, ebx, ecx, edx, max, expected;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    max=eax; eax=max>=0x1f?0x1f:(max>=0xb?0xb:1); ecx=0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    expected=edx;
    if(max<0xb || !ebx) {
        eax=1; __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
        expected=ebx>>24;
    }
    if(gdt_current_cpu_id()!=expected)return -1;
    kernel_gate_t gate = KERNEL_GATE_INIT;
    uint64_t before = flags_now();
    kernel_guard_t outer = kernel_guard_enter(&gate);
    uint64_t owner = ((uint64_t)scheduler_current_tid() << 32) | gdt_current_cpu_id();
    int ok = gate.locked && gate.depth == 1 && gate.owner == owner && !(flags_now() & 0x200);
    kernel_guard_t inner = kernel_guard_enter(&gate);
    ok &= gate.depth == 2 && gate.owner == owner;
    kernel_guard_leave(&inner);
    ok &= gate.depth == 1 && gate.locked && !(flags_now() & 0x200);
    kernel_guard_leave(&outer);
    ok &= !gate.depth && !gate.locked && gate.owner == UINT64_MAX;
    ok &= (flags_now() & 0x200) == (before & 0x200);
    /* Enter with IF already cleared: leave must not enable it. */
    __asm__ volatile("cli" ::: "memory");
    outer = kernel_guard_enter(&gate);
    kernel_guard_leave(&outer);
    ok &= !(flags_now() & 0x200);
    __asm__ volatile("pushq %0; popfq" :: "r"(before) : "memory", "cc");
    return ok ? 0 : -1;
}
