#include <unos/irq.h>
#include <unos/profiler.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unos/boot_requests.h>
#include <unos/compat.h>
#include <unos/debug_log.h>
#include <unos/events.h>
#include <unos/keyboard.h>
#include <unos/mouse.h>
#include <unos/netdev.h>
#include <unos/boot_console.h>
#include <unos/process_memory.h>
#include <unos/scheduler.h>
#include <unos/sched_clock.h>
#include <unos/timer_calibration.h>
#include <unos/smp.h>
#include <unos/vex_syscall.h>

#include <unos/tty.h>
#include <unos/vmm.h>
#include <unos/kernel_guard.h>

#include <lvgl.h>

#define IDT_ENTRIES 256u
#define KERNEL_CODE_SELECTOR 0x08u
#define IDT_INTERRUPT_GATE 0x8eu
#define IDT_USER_BREAKPOINT_GATE 0xefu

#define PIC1_COMMAND 0x20u
#define PIC1_DATA 0x21u
#define PIC2_COMMAND 0xa0u
#define PIC2_DATA 0xa1u
#define PIC_EOI 0x20u
#define PIC_OCW3_READ_ISR 0x0bu

#define ICW1_INIT 0x10u
#define ICW1_ICW4 0x01u
#define ICW4_8086 0x01u

#define PIT_COMMAND 0x43u
#define PIT_CHANNEL0 0x40u
#define PIT_BASE_HZ 1193182u
#define PIT_TARGET_HZ 100u

static const char *const exception_names[22] = {
    "Division by Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    NULL,
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Float Exception",
    "Virtualization Exception",
    "Control Protection Exception",
};

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

extern void *interrupt_stub_table[IDT_ENTRIES];

static idt_entry_t idt[IDT_ENTRIES];
static void (*handlers[IDT_ENTRIES])(interrupt_frame_t *frame);
static kernel_gate_t pic_route_gate=KERNEL_GATE_INIT;
int irq_claim_pic(uint8_t irq,void (*handler)(interrupt_frame_t *)) {
    KERNEL_GUARD(pic_route_gate);
    if(irq>=16 || irq==0 || irq==2 || irq==7 || irq==15 || !handler)return -1;
    void (*expected)(interrupt_frame_t *)=NULL;
    if(!__atomic_compare_exchange_n(&handlers[0x20u+irq],&expected,handler,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED))return -1;
    irq_mask_pic(irq,1);return 0;
}
/* Implemented below the port accessors. */

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
void irq_mask_pic(uint8_t irq,int masked) {
    KERNEL_GUARD(pic_route_gate);if(irq>=16)return;
    uint16_t port=irq<8?PIC1_DATA:PIC2_DATA;uint8_t bit=1u<<(irq&7u),value=inb(port);
    outb(port,masked?(value|bit):(value&~bit));
}
int irq_release_pic(uint8_t irq,void (*handler)(interrupt_frame_t *)) {
    KERNEL_GUARD(pic_route_gate);if(irq>=16)return -1;
    if(__atomic_load_n(&handlers[0x20u+irq],__ATOMIC_ACQUIRE)!=handler)return -1;
    irq_mask_pic(irq,1);
    int removed=__atomic_compare_exchange_n(&handlers[0x20u+irq],&handler,NULL,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
    if(removed)irq_refresh_pic(irq);
    return removed?0:-1;
}
void irq_refresh_pic(uint8_t irq) {
    KERNEL_GUARD(pic_route_gate);
    if(irq>=16)return;
    if(netdev_pic_irq()==irq) {
        uint16_t port=irq<8?0x4d0:0x4d1;
        outb(port,inb(port)|(1u<<(irq&7)));
    }
    irq_mask_pic(irq,!__atomic_load_n(&handlers[0x20u+irq],__ATOMIC_ACQUIRE) && netdev_pic_irq()!=irq);
}

static void io_wait(void) {
    outb(0x80u, 0);
}

static void tty_put_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char buf[19];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; ++i) {
        buf[2 + i] = digits[(value >> (60 - i * 4)) & 0xfu];
    }
    buf[18] = '\0';
    tty_puts(buf);
}

static void debug_put_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char buf[19];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; ++i) {
        buf[2 + i] = digits[(value >> (60 - i * 4)) & 0xfu];
    }
    buf[18] = '\0';
    debug_log_puts(buf);
}

static void idt_set_gate(uint8_t vector, uint64_t handler) {
    idt[vector].offset_low = (uint16_t)handler;
    idt[vector].selector = KERNEL_CODE_SELECTOR;
    idt[vector].ist = 0;
    if (vector == 8u) idt[vector].ist = 1u;
    if (vector == 2u) idt[vector].ist = 2u;
    if (vector == 18u) idt[vector].ist = 3u;
    if (vector == 1u) idt[vector].ist = 4u;
    /* x86-64 permits software INT3 from CPL3 only when the breakpoint gate
       itself has DPL=3.  Keep every other exception/IRQ gate ring-0-only. */
    idt[vector].type_attr = vector == 3u
        ? IDT_USER_BREAKPOINT_GATE : IDT_INTERRUPT_GATE;
    idt[vector].offset_mid = (uint16_t)(handler >> 16u);
    idt[vector].offset_high = (uint32_t)(handler >> 32u);
    idt[vector].zero = 0;
}

static void pic_remap(void) {
    const uint8_t master_mask = inb(PIC1_DATA);
    const uint8_t slave_mask = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC1_DATA, 0x20u);
    io_wait();
    outb(PIC2_DATA, 0x28u);
    io_wait();
    outb(PIC1_DATA, 0x04u);
    io_wait();
    outb(PIC2_DATA, 0x02u);
    io_wait();
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
}

static void pic_set_masks(void) {
    /* LAPIC is the scheduler clock. Keep PIT IRQ0 masked so enabling the
     * PIC route cannot double-count time; enable keyboard and slave cascade. */
    outb(PIC1_DATA, 0xf9u);
    outb(PIC2_DATA, 0xe7u);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8u) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

/* Returns non-zero if irq7 or irq15 is spurious (PIC ISR bit not set). */
static int pic_is_spurious(uint8_t vector) {
    if (vector == 0x27u) {
        outb(PIC1_COMMAND, PIC_OCW3_READ_ISR);
        return (inb(PIC1_COMMAND) & 0x80u) == 0;
    }
    if (vector == 0x2fu) {
        outb(PIC2_COMMAND, PIC_OCW3_READ_ISR);
        if ((inb(PIC2_COMMAND) & 0x80u) == 0) {
            outb(PIC1_COMMAND, PIC_EOI); /* ack master for cascaded spurious */
            return 1;
        }
    }
    return 0;
}

static void pit_init(void) {
    const uint16_t divisor = (uint16_t)(PIT_BASE_HZ / PIT_TARGET_HZ);
    outb(PIT_COMMAND, 0x36u);
    outb(PIT_CHANNEL0, (uint8_t)divisor);
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8u));
}

/* A fault that originated in user mode (CPL=3, i.e. CS low bits set) must not
 * bring down the kernel. Kill the offending process and reschedule — this is
 * what lets a browser/WebKit crash stay contained instead of freezing the OS.
 * Returns non-zero if the fault was handled (process killed; never actually
 * returns in that case — process_exit_group reschedules away). */
static int fault_kill_user_process(interrupt_frame_t *frame, const char *what) {
    uint64_t cr2;
    if ((frame->cs & 0x3u) == 0u || scheduler_current_user_mode() == 0u) {
        return 0; /* kernel-mode fault — keep the panic */
    }

    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    debug_log_puts("\n*** user fault (");
    debug_log_puts(what);
    debug_log_puts(") — killing process, RIP=");
    debug_put_hex64(frame->rip);
    debug_log_puts(" PID=");
    debug_put_hex64((uint64_t)scheduler_current_pid());
    debug_log_puts(" RSP=");
    debug_put_hex64(frame->rsp);
    debug_log_puts(" RBP=");
    debug_put_hex64(frame->rbp);
    debug_log_puts(" RBP-0x80=");
    debug_put_hex64(frame->rbp - 0x80u);
    debug_log_puts(" target_mod16=");
    debug_put_hex64((frame->rbp - 0x80u) & 0xfu);
    debug_log_puts(" FS_BASE=");
    debug_put_hex64(scheduler_current_fs_base());
    debug_log_puts(" RDI="); debug_put_hex64(frame->rdi);
    debug_log_puts(" RAX="); debug_put_hex64(frame->rax);
    debug_log_puts(" RBX="); debug_put_hex64(frame->rbx);
    if (vex_syscall_user_range_valid(frame->rsp, 64u, 0)) {
        const uint64_t *words = (const uint64_t *)(uintptr_t)frame->rsp;
        debug_log_puts(" STACK_WORDS=");
        for (unsigned word = 0; word < 8; ++word) {
            debug_put_hex64(words[word]); debug_log_puts(" ");
        }
    }
    debug_log_puts(" VEC=");
    debug_put_hex64(frame->vector);
    debug_log_puts(" CR2=");
    debug_put_hex64(cr2);
    debug_log_puts(" ERR=");
    debug_put_hex64(frame->error_code);
    {
        process_t *fault_process = scheduler_current_process();
        void *fault_pml4 = fault_process == NULL
            ? NULL : vmm_cr3_to_address_space(fault_process->cr3);
        const uint64_t rip_flags = fault_pml4 == NULL
            ? 0u : vmm_get_page_flags(fault_pml4, frame->rip);
        const uint64_t cr2_flags = fault_pml4 == NULL
            ? 0u : vmm_get_page_flags(fault_pml4, cr2);
        debug_log_puts(" RIP_FLAGS=");
        debug_put_hex64(rip_flags);
        debug_log_puts(" CR2_FLAGS=");
        debug_put_hex64(cr2_flags);
        debug_log_puts(" RIP_REGION=");
        debug_put_hex64((uint64_t)(fault_process != NULL &&
            process_memory_region_contains(fault_process, frame->rip, 1u)));
        debug_log_puts(" CR2_REGION=");
        debug_put_hex64((uint64_t)(fault_process != NULL &&
            process_memory_region_contains(fault_process, cr2, 1u)));
        debug_log_puts(" CR2_LAZY=");
        debug_put_hex64((uint64_t)(fault_process != NULL &&
            process_memory_region_is_lazy(fault_process, cr2, 1u)));
        if (fault_process != NULL) {
            char map_dump[8192];
            size_t map_dump_len = 0u;
            if (process_memory_format_maps(fault_process->pid, map_dump,
                                           sizeof(map_dump), &map_dump_len) == 0) {
                (void)map_dump_len;
                debug_log_puts("\nFAULT_MAPS_BEGIN\n");
                debug_log_puts(map_dump);
                debug_log_puts("FAULT_MAPS_END");
            }
        }
    }
    debug_log_puts("\n");

    /* Mark the process (and its threads) dead and switch to another task.
       Status 139 = 128 + SIGSEGV, the conventional "killed by segfault" code. */
    process_exit_group(139);

    /* process_exit_group does not return for a live system. */
    return 1;
}

static int deliver_user_sync_fault(interrupt_frame_t *frame,
                                   uint64_t fault_address,
                                   uint8_t address_valid) {
    process_t *process = scheduler_current_process();
    vex_sync_fault_t fault;
    int delivered = 0;

    if (frame == NULL || process == NULL || process->user_mode == 0u ||
        (frame->cs & 3u) != 3u) {
        return 0;
    }
    memset(&fault, 0, sizeof(fault));
    fault.vector = frame->vector;
    fault.error_code = frame->error_code;
    fault.address = fault_address;
    fault.address_valid = address_valid;
    fault.instruction = frame->rip;
    if (frame->vector == 14u) {
        fault.access = (frame->error_code & 0x10u) != 0u ? 4u :
            ((frame->error_code & 0x2u) != 0u ? 2u : 1u);
    }

    /* Preserve extended user register state across the exception handler.
       The event engine restores it before handler entry and rt_sigreturn. */
    scheduler_capture_fpu_state(process->event_saved_fpu_state);
    process->event_saved_fpu_valid = 1u;
    if (frame->vector == 16u) {
        const uint8_t *fp = process->event_saved_fpu_state;
        uint16_t fcw = 0u;
        uint16_t fsw = 0u;
        uint16_t fop = 0u;
        uint64_t fip = 0u;
        uint64_t fdp = 0u;
        uint32_t mxcsr = 0u;
        memcpy(&fcw, fp + 0u, sizeof(fcw));
        memcpy(&fsw, fp + 2u, sizeof(fsw));
        memcpy(&fop, fp + 6u, sizeof(fop));
        memcpy(&fip, fp + 8u, sizeof(fip));
        memcpy(&fdp, fp + 16u, sizeof(fdp));
        memcpy(&mxcsr, fp + 24u, sizeof(mxcsr));
        debug_log_puts("x87-state: pid="); debug_put_hex64(process->pid);
        debug_log_puts(" fcw="); debug_put_hex64(fcw);
        debug_log_puts(" fsw="); debug_put_hex64(fsw);
        debug_log_puts(" fop="); debug_put_hex64(fop);
        debug_log_puts(" fip="); debug_put_hex64(fip);
        debug_log_puts(" fdp="); debug_put_hex64(fdp);
        debug_log_puts(" mxcsr="); debug_put_hex64(mxcsr);
        if (vex_syscall_user_range_valid(frame->rip, 8u, 0) != 0) {
            debug_log_puts(" code=");
            for (unsigned int i = 0u; i < 8u; ++i) {
                debug_put_hex64(*(const uint8_t *)(uintptr_t)(frame->rip + i));
            }
        }
        if (vex_syscall_user_range_valid(fip, 8u, 0) != 0) {
            debug_log_puts(" fip_code=");
            for (unsigned int i = 0u; i < 8u; ++i) {
                debug_put_hex64(*(const uint8_t *)(uintptr_t)(fip + i));
            }
        }
        debug_log_puts("\n");
    }
    if (process->abi == VEX_ABI_LINUX) {
        delivered = compat_linux_deliver_sync_fault(frame, &fault);
    } else {
        delivered = vex_event_deliver_sync_native(frame, &fault) == 0;
    }
    if (delivered != 0) {
        debug_log_puts("sync-fault: handler entered pid=");
        debug_put_hex64(process->pid);
        debug_log_puts(" vector=");
        debug_put_hex64(fault.vector);
        debug_log_puts(" rip=0x");
        debug_put_hex64(fault.instruction);
        debug_log_puts(" addr=0x");
        debug_put_hex64(fault.address);
        debug_log_puts("\n");
        return 1;
    }
    process->event_saved_fpu_valid = 0u;
    return 0;
}

static void default_exception(interrupt_frame_t *frame) {
    const uint8_t vec = (uint8_t)frame->vector;
    const char *name = (vec < 22u && exception_names[vec] != NULL)
        ? exception_names[vec]
        : "Unknown Exception";

    if ((frame->cs & 3u) == 3u &&
        deliver_user_sync_fault(frame, 0u, 0u)) {
        return;
    }
    if (fault_kill_user_process(frame, name)) {
        return;
    }
    if(boot_console_active()) {
        boot_console_puts("\nCPU exception ");boot_console_puts(name);
        boot_console_puts(" RIP=");boot_console_hex(frame->rip);
        boot_console_puts(" error=");boot_console_hex(frame->error_code);
        boot_console_fail("unhandled kernel CPU exception");
    }

    tty_puts("\n*** KERNEL PANIC: ");
    tty_puts(name);
    tty_puts(" (#");
    tty_putchar((char)('0' + (vec / 10u) % 10u));
    tty_putchar((char)('0' + vec % 10u));
    tty_puts(") ***\n  RIP=");
    tty_put_hex64(frame->rip);
    tty_puts("  err=");
    tty_put_hex64(frame->error_code);
    tty_puts("\n  RSP=");
    tty_put_hex64(frame->rsp);
    tty_puts("  RDI=");
    tty_put_hex64(frame->rdi);
    tty_puts("\n  RSI=");
    tty_put_hex64(frame->rsi);
    tty_puts("  R8=");
    tty_put_hex64(frame->r8);
    tty_puts("  RCX=");
    tty_put_hex64(frame->rcx);
    tty_puts("\n  RBX=");
    tty_put_hex64(frame->rbx);
    tty_puts("  RAX=");
    tty_put_hex64(frame->rax);
    tty_puts("\n");
    debug_log_puts("\n*** KERNEL PANIC: ");
    debug_log_puts(name);
    debug_log_puts(" RIP=");
    debug_put_hex64(frame->rip);
    debug_log_puts(" err=");
    debug_put_hex64(frame->error_code);
    debug_log_puts(" RSP=");
    debug_put_hex64(frame->rsp);
    debug_log_puts(" RDI=");
    debug_put_hex64(frame->rdi);
    debug_log_puts(" RSI=");
    debug_put_hex64(frame->rsi);
    debug_log_puts(" R8=");
    debug_put_hex64(frame->r8);
    debug_log_puts(" RCX=");
    debug_put_hex64(frame->rcx);
    debug_log_puts(" RBX=");
    debug_put_hex64(frame->rbx);
    debug_log_puts(" RAX=");
    debug_put_hex64(frame->rax);
    debug_log_puts("\n");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void page_fault_handler(interrupt_frame_t *frame) {
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

    /* Large anonymous mmap ranges are deliberately non-present until first
     * touch.  Resolve only a user, non-present fault belonging to the current
     * process; all protection violations continue to the normal kill path. */
    if (frame != NULL && (frame->error_code & 0x4u) != 0u &&
        (frame->error_code & 0x1u) == 0u) {
        process_t *process = scheduler_current_process();
        if (process != NULL && process_memory_file_fault_is_bus(process, cr2) == 0) {
            const uint32_t memory_owner = scheduler_current_memory_owner_pid();
            const int jit_result = vex_jit_materialize_page(
                memory_owner, process->cr3, cr2,
                frame->error_code);
            if (jit_result == 1) {
                return;
            }
            if (jit_result == 0 && process_memory_materialize_page(
                    memory_owner, process->cr3, cr2,
                    frame->error_code) == 0) {
                return;
            }
        }
    }

    if (frame != NULL && (frame->cs & 3u) == 3u &&
        deliver_user_sync_fault(frame, cr2, 1u)) {
        return;
    }

    if (fault_kill_user_process(frame, "page fault")) {
        return;
    }
    if(boot_console_active()) {
        boot_console_puts("\nKernel page fault RIP=");boot_console_hex(frame->rip);
        boot_console_puts(" CR2=");boot_console_hex(cr2);
        boot_console_puts(" error=");boot_console_hex(frame->error_code);
        boot_console_fail("kernel page fault during boot");
    }

    tty_puts("\n*** KERNEL PANIC: Page Fault (#14) ***\n  RIP=");
    tty_put_hex64(frame->rip);
    tty_puts("  CS=");
    tty_put_hex64(frame->cs);
    tty_puts("  err=");
    tty_put_hex64(frame->error_code);
    tty_puts("\n  CR2=");
    tty_put_hex64(cr2);
    tty_puts("  RSP=");
    tty_put_hex64(frame->rsp);
    tty_puts("\n");

    debug_log_puts("\n*** KERNEL PANIC: Page Fault (#14) ***\n  RIP=");
    debug_put_hex64(frame->rip);
    debug_log_puts("  CS=");
    debug_put_hex64(frame->cs);
    debug_log_puts("  err=");
    debug_put_hex64(frame->error_code);
    debug_log_puts("\n  CR2=");
    debug_put_hex64(cr2);
    debug_log_puts("  RSP=");
    debug_put_hex64(frame->rsp);
    debug_log_puts("\n");

    /* Fatal-kernel diagnostics only. Walk bounded mapped frame records; never
     * dereference an unchecked address while reporting the original fault. */
    uint64_t fault_cr3;__asm__ volatile("mov %%cr3,%0":"=r"(fault_cr3));
    void *fault_pml4=vmm_cr3_to_address_space(fault_cr3);
    debug_log_puts(" fault pid=");debug_put_hex64(scheduler_current_process_id());
    debug_log_puts(" tid=");debug_put_hex64(scheduler_current_tid());
    debug_log_puts(" cr3=");debug_put_hex64(fault_cr3);
    debug_log_puts(" R10=");debug_put_hex64(frame->r10);
    debug_log_puts(" RCX=");debug_put_hex64(frame->rcx);
    debug_log_puts(" RDX=");debug_put_hex64(frame->rdx);
    debug_log_puts(" RDI=");debug_put_hex64(frame->rdi);
    debug_log_puts(" RSI=");debug_put_hex64(frame->rsi);
    debug_log_puts("\n");
    uint64_t fault_bp=frame->rbp;
    for(unsigned depth=0;depth<16;depth++){
        if(fault_bp<UINT64_C(0xffff800000000000) || (fault_bp&7) || fault_bp>UINT64_MAX-15 ||
           !(vmm_get_page_flags(fault_pml4,fault_bp)&VMM_PAGE_PRESENT) ||
           !(vmm_get_page_flags(fault_pml4,fault_bp+15)&VMM_PAGE_PRESENT))break;
        uint64_t next,return_ip;memcpy(&next,(void *)(uintptr_t)fault_bp,8);memcpy(&return_ip,(void *)(uintptr_t)(fault_bp+8),8);
        debug_log_puts(" kernel return=");debug_put_hex64(return_ip);debug_log_puts(" bp=");debug_put_hex64(fault_bp);debug_log_puts("\n");
        if(next<=fault_bp || next-fault_bp>65536)break;fault_bp=next;
    }

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static volatile uint64_t timer_ticks;
static uint64_t lv_timer_ms;
static void advance_timer_clock(void) {
    scheduler_tick();
    if (smp_current_index() != 0u) return;
    timer_ticks=scheduler_ticks_now();
    uint64_t now_ms=scheduler_time_ns()/1000000ull;
    uint64_t delta=now_ms-lv_timer_ms;lv_timer_ms=now_ms;
    while(delta>UINT32_MAX){lv_tick_inc(UINT32_MAX);delta-=UINT32_MAX;}
    if(delta)lv_tick_inc((uint32_t)delta);
}
static void sample_user_instruction(const interrupt_frame_t *frame){
    if((frame->cs&3u)!=3u)return;
    vex_cpu_t *cpu=smp_current_cpu();if(!cpu)return;
    unsigned slot=cpu->user_sample_next%64u;
    __atomic_add_fetch(&cpu->user_sample_epoch,1u,__ATOMIC_ACQ_REL);
    __atomic_store_n(&cpu->user_sample_tid[slot],scheduler_current_tid(),__ATOMIC_RELAXED);
    __atomic_store_n(&cpu->user_sample_rip[slot],frame->rip,__ATOMIC_RELAXED);
    __atomic_store_n(&cpu->user_sample_next,cpu->user_sample_next+1u,__ATOMIC_RELAXED);
    __atomic_add_fetch(&cpu->user_sample_epoch,1u,__ATOMIC_RELEASE);
}
uint32_t irq_copy_user_samples(uint32_t index,irq_user_sample_t samples[64]) {
    vex_cpu_t *cpu=smp_cpu_at(index);if(!cpu || !samples)return 0;
    for(unsigned retry=0;retry<3;++retry) {
        uint32_t before=__atomic_load_n(&cpu->user_sample_epoch,__ATOMIC_ACQUIRE);
        if(before&1u)continue;
        uint32_t next=__atomic_load_n(&cpu->user_sample_next,__ATOMIC_RELAXED);
        uint32_t count=next<64u?next:64u;
        for(uint32_t i=0;i<count;++i) {
            unsigned slot=(next-1u-i)%64u;
            samples[i].tid=__atomic_load_n(&cpu->user_sample_tid[slot],__ATOMIC_RELAXED);
            samples[i].rip=__atomic_load_n(&cpu->user_sample_rip[slot],__ATOMIC_RELAXED);
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(before==__atomic_load_n(&cpu->user_sample_epoch,__ATOMIC_RELAXED))return count;
    }
    return 0; /* A racing diagnostic is skipped, never used as scheduler state. */
}
uint64_t irq_sampled_user_rip(uint32_t tid){
    irq_user_sample_t samples[64];
    for(uint32_t cpu=0;cpu<smp_cpu_count();++cpu) {
        uint32_t count=irq_copy_user_samples(cpu,samples);
        for(uint32_t i=0;i<count;++i)if(samples[i].tid==tid)return samples[i].rip;
    }
    return 0;
}

static void timer_handler(interrupt_frame_t *frame) {
    sample_user_instruction(frame);
    advance_timer_clock();
}

/* ── Local APIC timer ────────────────────────────────────────────────────────
 * The LAPIC timer drives preemption. Device IRQs still use the existing PIC;
 * they require an explicit virtual-wire route after taking over from EFI. */
#define LAPIC_REG_ID          0x020u
#define LAPIC_REG_TPR         0x080u
#define LAPIC_REG_EOI         0x0b0u
#define LAPIC_REG_SVR         0x0f0u
#define LAPIC_REG_LVT_TIMER   0x320u
#define LAPIC_REG_LVT_LINT0   0x350u
#define LAPIC_REG_TIMER_INIT  0x380u
#define LAPIC_REG_TIMER_CUR   0x390u
#define LAPIC_REG_TIMER_DIV   0x3e0u
#define LAPIC_SVR_ENABLE      0x100u
#define LAPIC_TIMER_PERIODIC  0x20000u
#define LAPIC_TIMER_VECTOR    0x30u
#define LAPIC_LVT_EXTINT      (7u << 8u)
#define IA32_APIC_BASE_MSR    0x1bu

static volatile uint32_t *g_lapic = NULL;
static uint8_t lapic_x2;
/* Temporary passive probe for FPU state across the periodic timer path. */
volatile uint16_t irq_lapic_fcw_before_probe;
volatile uint16_t irq_lapic_fcw_after_probe;
volatile uint64_t irq_lapic_cs_probe;
volatile uint16_t irq_kernel_fcw_before_probe;
volatile uint16_t irq_kernel_fcw_after_probe;
volatile uint64_t irq_kernel_vector_probe;
volatile uint64_t irq_kernel_cs_probe;
volatile uint16_t irq_kernel_fcw_change_before_probe;
volatile uint16_t irq_kernel_fcw_change_after_probe;
volatile uint64_t irq_kernel_fcw_change_vector_probe;
volatile uint64_t irq_kernel_fcw_change_cs_probe;

static uint64_t lapic_rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void lapic_wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr" :: "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
    if (lapic_x2) lapic_wrmsr(0x800u + reg / 16u, value);
    else g_lapic[reg / 4u] = value;
}
static inline uint32_t lapic_read(uint32_t reg) {
    return lapic_x2 ? (uint32_t)lapic_rdmsr(0x800u + reg / 16u) : g_lapic[reg / 4u];
}

static uint8_t timer_calibration_lock;
static uint8_t calibration_input(void *context, uint16_t port) {
    (void)context; return inb(port);
}
static void calibration_output(void *context, uint16_t port, uint8_t value) {
    (void)context; outb(port, value);
}
static uint32_t calibration_apic_read(void *context, uint32_t reg) {
    (void)context; return lapic_read(reg);
}
static void calibration_apic_write(void *context, uint32_t reg, uint32_t value) {
    (void)context; lapic_write(reg, value);
}
static uint64_t calibration_cycles(void *context) {
    (void)context; return sched_clock_cycles();
}
static void calibration_value(const char *name, uint64_t value) {
    unsigned digits=value>UINT32_MAX?16u:8u;
    char text[19];text[0]='0';text[1]='x';
    for(unsigned i=0;i<digits;++i)text[2+i]="0123456789ABCDEF"[(value>>((digits-1-i)*4))&15];
    text[2+digits]=0;
    boot_console_puts(name);boot_console_puts(text);
    debug_log_puts(name);debug_log_puts(text);
}
static void calibration_decimal(const char *name,uint64_t value) {
    char digits[21];unsigned n=0;
    do{digits[n++]=(char)('0'+value%10);value/=10;}while(value);
    boot_console_puts(name);debug_log_puts(name);
    while(n){char c[2]={digits[--n],0};boot_console_puts(c);debug_log_puts(c);}
}
static void calibration_line(void) {boot_console_puts("\n");debug_log_puts("\n");}
static void calibration_sample(const char *label,const timer_calibration_sample_t *s) {
    calibration_value(label,s->pit);calibration_value(" bytes(lo/hi)=",s->low);
    calibration_value("/",s->high);calibration_value(" status/port=",s->status);
    calibration_value("/",s->port);calibration_line();
}
static int lapic_calibrate_timer(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    while(__atomic_test_and_set(&timer_calibration_lock,__ATOMIC_ACQUIRE))smp_relax();
    /* IRQ0 is masked and IF=0: PIT2 OUT, not interrupt delivery, is the
     * reference. The calibrated TSC is used only as a failure watchdog. */
    boot_console_stage("LAPIC: PIT2 pre-zero window / separate OUT check",0);
    const timer_calibration_io_t io={NULL,calibration_input,calibration_output,
        calibration_apic_read,calibration_apic_write,calibration_cycles,
        sched_clock_frequency()};
    timer_calibration_result_t result;
    int failed=timer_calibration_measure(&io,&result)!=TIMER_CAL_OK;
    calibration_decimal("  CPU (dec)=",smp_current_index());
    calibration_value(" APIC_BASE=",lapic_rdmsr(IA32_APIC_BASE_MSR));
    calibration_decimal(" TSC Hz (dec)=",result.watchdog_cycles);calibration_line();
    calibration_value("  PIT mode command=",0xb0);calibration_value(" readback=",0xc8);
    calibration_value(" reload/target/loaded=",result.reload);
    calibration_value("/",result.target_ticks);calibration_value("/",result.pit_loaded);calibration_line();
    calibration_value("  PIT port before/armed=",result.port_before);
    calibration_value("/",result.port_armed);calibration_value(" armed status=",result.status_armed);calibration_line();
    calibration_sample("  PIT START count=",&result.start);
    calibration_sample("  PIT END   count=",&result.end);
    calibration_decimal("  captured start/end (dec)=",result.start_valid);
    calibration_decimal("/",result.end_valid);
    calibration_decimal(" OUT in samples start/end (dec)=",!!(result.start.status&0x80));
    calibration_decimal("/",!!(result.end.status&0x80));calibration_line();
    calibration_value("  SEPARATE terminal count (NOT an operand)=",result.terminal_pit);
    calibration_value(" status/port=",result.terminal_status);calibration_value("/",result.terminal_port);calibration_line();
    calibration_value("  LAPIC DIV/LVT=",result.apic_div);calibration_value("/",result.apic_lvt);
    calibration_value(" chosen start/end=",result.start.apic);calibration_value("/",result.end.apic);calibration_line();
    calibration_value("  LAPIC latch brackets START=",result.start.apic_before);
    calibration_value("/",result.start.apic_after);calibration_value(" END=",result.end.apic_before);
    calibration_value("/",result.end.apic_after);calibration_line();
    calibration_value("  TSC latch brackets START=",result.start.tsc_before);
    calibration_value("/",result.start.tsc_after);calibration_line();
    calibration_value("  TSC latch brackets END=",result.end.tsc_before);
    calibration_value("/",result.end.tsc_after);calibration_line();
    calibration_value("  TSC chosen START/END=",result.start.tsc);
    calibration_value("/",result.end.tsc);calibration_value(" delta cycles=",result.cycles_elapsed);calibration_line();
    uint64_t us=result.watchdog_cycles ?
        (result.cycles_elapsed/result.watchdog_cycles)*1000000u+
        ((result.cycles_elapsed%result.watchdog_cycles)*1000000u)/result.watchdog_cycles : 0;
    calibration_decimal("  measure elapsed us (DECIMAL)=",us);
    calibration_decimal(" PIT/APIC ticks (dec)=",result.reference_ticks);
    calibration_decimal("/",result.apic_ticks);calibration_line();
    calibration_value("  rate numerator/denominator=",result.rate_numerator);
    calibration_value("/",result.rate_denominator);
    calibration_decimal(" candidate count/ms (dec)=",result.rate_raw);calibration_line();
    calibration_decimal("  published count/ms (dec)=",result.count_per_ms);
    calibration_decimal(" formula evaluated (dec)=",result.rate_denominator!=0);
    calibration_decimal(" error code (dec)=",result.error);calibration_line();
    calibration_decimal("  iterations PIT-load/APIC-load/samples/terminal (dec)=",result.pit_load_iterations);
    calibration_decimal("/",result.apic_load_iterations);calibration_decimal("/",result.sample_iterations);
    calibration_decimal("/",result.terminal_iterations);calibration_line();
    calibration_value("  separate terminal TSC=",result.terminal_tsc);
    calibration_value(" overall setup/exit=",result.setup_tsc);calibration_value("/",result.exit_tsc);calibration_line();
    boot_console_puts("\n  exit: ");boot_console_puts(timer_calibration_exit_name(result.exit_reason));
    debug_log_puts("\n  exit: ");debug_log_puts(timer_calibration_exit_name(result.exit_reason));
    boot_console_puts("\n  result: ");boot_console_puts(timer_calibration_error_name(result.error));
    boot_console_puts("\n");debug_log_puts("\n  result: ");
    debug_log_puts(timer_calibration_error_name(result.error));debug_log_puts("\n");
    if(!failed) {
        lapic_write(LAPIC_REG_LVT_TIMER,LAPIC_TIMER_VECTOR|LAPIC_TIMER_PERIODIC);
        lapic_write(LAPIC_REG_TIMER_INIT,result.count_per_ms);
        boot_console_stage("LAPIC: PIT-reference timer calibrated",1);
    } else boot_console_stage(timer_calibration_error_name(result.error),0);
    __atomic_clear(&timer_calibration_lock,__ATOMIC_RELEASE);
    __asm__ volatile("pushq %0; popfq"::"r"(flags):"memory","cc");
    char line[96];snprintf(line,sizeof(line),"lapic: cpu=%u PIT-reference count_per_ms=%u result=%d\n",smp_current_index(),result.count_per_ms,failed);debug_log_puts(line);
    return failed?-1:0;
}

static void lapic_timer_init(void) {
    uint64_t apic_base_msr = lapic_rdmsr(IA32_APIC_BASE_MSR);
    lapic_x2 = (apic_base_msr & (1ull << 10u)) != 0u;
    const uint64_t apic_base_phys = apic_base_msr & 0xfffff000ull;
    const uint64_t hhdm = (hhdm_request.response != NULL) ? hhdm_request.response->offset : 0u;
    const uint64_t lapic_virt = hhdm + apic_base_phys;

    /* Globally enable the Local APIC (bit 11 of IA32_APIC_BASE). Limine usually
     * leaves it enabled, but make sure — a hardware-disabled LAPIC ignores all
     * MMIO register writes. */
    apic_base_msr |= (1ull << 11);
    lapic_wrmsr(IA32_APIC_BASE_MSR, apic_base_msr);

    /* Map the LAPIC MMIO page uncached into kernel space (it may not already be
     * covered by the HHDM, which only guarantees usable RAM). */
    if (!lapic_x2) vmm_map_kernel_page(lapic_virt, apic_base_phys,
                        VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_PCD);
    g_lapic = (volatile uint32_t *)(uintptr_t)lapic_virt;

    /* Enable the LAPIC and set a (parked) spurious vector. */
    lapic_write(LAPIC_REG_SVR, LAPIC_SVR_ENABLE | 0xffu);

    /* EFI/Limine can leave LINT0 masked (observed with VirtualBox EFI).
     * Merely unmasking the 8259 PIC then delivers no device interrupts at
     * all. Existing polling hid this until the GUI started sleeping.
     * Route the BSP's PIC through unmasked, edge-triggered ExtINT LINT0;
     * the PIC retains its existing vectors and EOI protocol. No IO-APIC
     * routes are enabled here, so each device has a single delivery path. */
    debug_log_puts("lapic: inherited LINT0=");
    debug_put_hex64(lapic_read(LAPIC_REG_LVT_LINT0));
    lapic_write(LAPIC_REG_TPR, 0u);
    lapic_write(LAPIC_REG_LVT_LINT0, LAPIC_LVT_EXTINT);
    debug_log_puts(" -> ExtINT enabled (PIT masked)\n");

    /* Divide the timer input clock by 16. */
    lapic_write(LAPIC_REG_TIMER_DIV, 0x3u);

    /* Measure the divided LAPIC clock against the PIT peripheral clock.
       Public scheduler ticks remain 10ms units; hardware IRQ period is 1ms. */
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_TIMER_VECTOR | LAPIC_TIMER_PERIODIC);
    /* Release Limine's AP parking loops before calibrating device clocks.
       Our APs use their private stack/GS and halt with interrupts enabled. */
    boot_console_stage("SMP: release APs",0);
    if (smp_start()!=0) boot_console_fail("SMP: AP startup failed");
    boot_console_stage("SMP: APs online",1);
    sched_clock_calibrate();
    if(lapic_calibrate_timer()!=0) {
        boot_console_fail("LAPIC: PIT-reference timer calibration failed");
    }

    debug_log_puts("lapic: timer armed\n");
}

static void lapic_timer_handler(interrupt_frame_t *frame) {
    vex_cpu_t *cpu=smp_current_cpu();
    if (cpu) ++cpu->timer_count;
    const int boot_probe=!cpu || cpu->index==0u;
    if(boot_probe) {
        irq_lapic_cs_probe = frame == NULL ? 0u : frame->cs;
        __asm__ volatile ("fnstcw %0" : "=m"(irq_lapic_fcw_before_probe) : : "memory");
    }
    sample_user_instruction(frame);
    profiler_sample(frame);
    if (!cpu || cpu->index == 0u || cpu->scheduler_enabled) advance_timer_clock();
    lapic_write(LAPIC_REG_EOI, 0u);

    /* Request a reschedule at the next syscall boundary — this time-slices a
     * process that stays in kernel mode by looping fast syscalls. */
    /* scheduler_tick requests preemption only for an expired service request or wake. */

    /* A user-mode task can be switched immediately. RFLAGS is part of the
     * scheduler context, so an IRQ-resumed task keeps interrupts masked until
     * its own iretq restores the user flags. */
    if ((!cpu || cpu->index == 0u || cpu->scheduler_enabled) && (frame->cs & 0x3u) != 0u) {
        scheduler_preempt_user(frame);
    }
    if(boot_probe)__asm__ volatile ("fnstcw %0" : "=m"(irq_lapic_fcw_after_probe) : : "memory");
}

uint64_t irq_timer_ticks(void) {
    return timer_ticks;
}

static void keyboard_handler(interrupt_frame_t *frame) {
    static uint8_t reported;
    (void)frame;
    keyboard_irq_handler();
    if (!reported) {
        reported = 1u;
        debug_log_puts("irq: keyboard delivery confirmed\n");
    }
}

static void mouse_handler(interrupt_frame_t *frame) {
    static uint8_t reported;
    (void)frame;
    mouse_irq_handler();
    if (!reported) {
        reported = 1u;
        debug_log_puts("irq: mouse delivery confirmed\n");
    }
}

static void network_handler(interrupt_frame_t *frame) {
    if(netdev_pic_irq()==frame->vector-0x20u)netdev_irq_handler();
}

void irq_register_handler(uint8_t vector, void (*handler)(interrupt_frame_t *frame)) {
    __atomic_store_n(&handlers[vector],handler,__ATOMIC_RELEASE);
}
static void smp_ipi_handler(interrupt_frame_t *frame) {
    smp_handle_ipi();irq_lapic_eoi();
    vex_cpu_t *cpu=smp_current_cpu();
    if((!cpu || cpu->index==0u || cpu->scheduler_enabled) && (frame->cs&3u))scheduler_preempt_user(frame);
}

static void irq_account_return(unsigned *user){if(*user)scheduler_execution_mode(1);}
void interrupt_dispatch(interrupt_frame_t *frame) {
    unsigned from_user __attribute__((cleanup(irq_account_return)))=(frame->cs&3u)!=0;
    if(from_user)scheduler_execution_mode(0);
    vex_cpu_t *cpu=smp_current_cpu();
    if (cpu) ++cpu->irq_count;
    smp_poll_requests();
    const uint8_t vector = (uint8_t)frame->vector;
    /* Legacy diagnostic exports describe BSP only, not concurrent AP state. */
    const uint8_t kernel_interrupt = (uint8_t)((frame->cs & 3u) == 0u && (!cpu || cpu->index==0u));

    if (kernel_interrupt != 0u && vector != 7u && vector != 8u) {
        irq_kernel_vector_probe = vector;
        irq_kernel_cs_probe = frame->cs;
        __asm__ volatile ("fnstcw %0" : "=m"(irq_kernel_fcw_before_probe) : : "memory");
    }

    if ((vector == 0x27u || vector == 0x2fu) && pic_is_spurious(vector)) {
        return;
    }

    void (*handler)(interrupt_frame_t *frame) = __atomic_load_n(&handlers[vector],__ATOMIC_ACQUIRE);
    /* INTx can share the SCI wire. Each source acknowledges its own device;
       the PIC EOI remains one operation after all sources have run. */
    if(vector>=0x20u && vector<=0x2fu && handler!=network_handler &&
       netdev_pic_irq()==vector-0x20u)netdev_irq_handler();
    if (handler != NULL) {
        handler(frame);
    } else if (vector < 32u) {
        default_exception(frame);
    }

    if (vector >= 0x20u && vector <= 0x2fu) {
        pic_send_eoi((uint8_t)(vector - 0x20u));
    }

    /* Any serviced device IRQ can have made a flow runnable, including I/O.
     * EOI precedes selection. Only CPL3 is preempted here; kernel work resumes
     * until a syscall boundary or an explicit transaction-free safe point. */
    if (vector >= 0x20u && vector <= 0x2fu &&
        (frame->cs & 0x3u) != 0u) {
        scheduler_preempt_user(frame);
    }
    if (kernel_interrupt != 0u && vector != 7u && vector != 8u) {
        __asm__ volatile ("fnstcw %0" : "=m"(irq_kernel_fcw_after_probe) : : "memory");
        if (irq_kernel_fcw_before_probe != irq_kernel_fcw_after_probe) {
            irq_kernel_fcw_change_before_probe = irq_kernel_fcw_before_probe;
            irq_kernel_fcw_change_after_probe = irq_kernel_fcw_after_probe;
            irq_kernel_fcw_change_vector_probe = vector;
            irq_kernel_fcw_change_cs_probe = frame->cs;
        }
    }
}

void irq_prepare_idt(void) {
    for (uint16_t i = 0; i < IDT_ENTRIES; ++i) {
        idt_set_gate((uint8_t)i, (uint64_t)interrupt_stub_table[i]);
        handlers[i] = NULL;
    }

    const idt_ptr_t ptr = {
        .limit = sizeof(idt) - 1u,
        .base = (uint64_t)idt,
    };

    __asm__ volatile ("lidt %0" :: "m"(ptr));
    irq_register_handler(0x0eu, page_fault_handler);
}
void irq_init(void) {
    irq_prepare_idt();
    pic_remap();
    pit_init();
    irq_register_handler(0x0eu, page_fault_handler);
    irq_register_handler(0x20u, timer_handler);
    irq_register_handler(0x21u, keyboard_handler);
    irq_register_handler(0x2bu, network_handler);
    irq_register_handler(0x2cu, mouse_handler);
    irq_register_handler(LAPIC_TIMER_VECTOR, lapic_timer_handler);
    irq_register_handler(VEX_IPI_VECTOR, smp_ipi_handler);
    pic_set_masks();
    lapic_timer_init();
}

void irq_lapic_eoi(void) { lapic_write(LAPIC_REG_EOI,0u); }
void irq_send_ipi(uint32_t id, uint8_t vector) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    if (lapic_x2) lapic_wrmsr(0x830u, ((uint64_t)id << 32u) | vector);
    else {
        while (lapic_read(0x300u)&(1u<<12u)) smp_relax();
        lapic_write(0x310u,id<<24u);lapic_write(0x300u,vector);
        while (lapic_read(0x300u)&(1u<<12u)) smp_relax();
    }
    __asm__ volatile("pushq %0; popfq"::"r"(flags):"memory","cc");
}
static void irq_arm_cpu_timer(void *result) {
    *(int *)result=lapic_calibrate_timer();
}
void irq_start_ap_timers(void) {
    for(uint32_t i=1u;i<smp_cpu_count();++i) {
        int result=-1;
        if(smp_boot_call(i,irq_arm_cpu_timer,&result)!=0 || result!=0)
            boot_console_fail("AP LAPIC calibration failed (measurements above)");
    }
}
void irq_init_cpu(void) {
    const idt_ptr_t ptr={sizeof(idt)-1u,(uint64_t)idt};
    __asm__ volatile("lidt %0"::"m"(ptr));
    uint64_t base=lapic_rdmsr(IA32_APIC_BASE_MSR);
    lapic_wrmsr(IA32_APIC_BASE_MSR,base|(1ull<<11u));
    lapic_write(LAPIC_REG_SVR,LAPIC_SVR_ENABLE|0xffu);
    lapic_write(LAPIC_REG_TPR,0u);
    /* PIC virtual-wire interrupts have exactly one owner. Local timers/IPIs
       are independently enabled on every AP, not routed through the BSP. */
    lapic_write(LAPIC_REG_LVT_LINT0,1u<<16u);
    lapic_write(0x360u,1u<<16u);
    lapic_write(LAPIC_REG_LVT_TIMER,LAPIC_TIMER_VECTOR|(1u<<16u));
    lapic_write(LAPIC_REG_TIMER_INIT,0u);
}

void irq_enable(void) {
    __asm__ volatile ("sti");
}

void irq_disable(void) {
    __asm__ volatile ("cli");
}

