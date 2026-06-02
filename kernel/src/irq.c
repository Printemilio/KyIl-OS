#include <unos/irq.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/debug_log.h>
#include <unos/keyboard.h>
#include <unos/mouse.h>
#include <unos/netdev.h>
#include <unos/scheduler.h>
#include <unos/tty.h>

#include "lvgl/lvgl.h"

#define IDT_ENTRIES 256u
#define KERNEL_CODE_SELECTOR 0x08u
#define IDT_INTERRUPT_GATE 0x8eu

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

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
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
    idt[vector].type_attr = IDT_INTERRUPT_GATE;
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
    /* Unmask IRQ0 timer, IRQ1 keyboard, IRQ2 cascade, IRQ11 network and IRQ12 mouse. */
    outb(PIC1_DATA, 0xf8u);
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

static void default_exception(interrupt_frame_t *frame) {
    const uint8_t vec = (uint8_t)frame->vector;
    const char *name = (vec < 22u && exception_names[vec] != NULL)
        ? exception_names[vec]
        : "Unknown Exception";

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

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static volatile uint64_t timer_ticks;

static void timer_handler(interrupt_frame_t *frame) {
    (void)frame;
    ++timer_ticks;
    lv_tick_inc(10);
    scheduler_tick();
}

uint64_t irq_timer_ticks(void) {
    return timer_ticks;
}

static void keyboard_handler(interrupt_frame_t *frame) {
    (void)frame;
    keyboard_irq_handler();
}

static void mouse_handler(interrupt_frame_t *frame) {
    (void)frame;
    mouse_irq_handler();
}

static void e1000_handler(interrupt_frame_t *frame) {
    (void)frame;
    netdev_irq_handler();
}

void irq_register_handler(uint8_t vector, void (*handler)(interrupt_frame_t *frame)) {
    handlers[vector] = handler;
}

void interrupt_dispatch(interrupt_frame_t *frame) {
    const uint8_t vector = (uint8_t)frame->vector;

    if ((vector == 0x27u || vector == 0x2fu) && pic_is_spurious(vector)) {
        return;
    }

    void (*handler)(interrupt_frame_t *frame) = handlers[vector];
    if (handler != NULL) {
        handler(frame);
    } else if (vector < 32u) {
        default_exception(frame);
    }

    if (vector >= 0x20u && vector <= 0x2fu) {
        pic_send_eoi((uint8_t)(vector - 0x20u));
    }
}

void irq_init(void) {
    for (uint16_t i = 0; i < IDT_ENTRIES; ++i) {
        idt_set_gate((uint8_t)i, (uint64_t)interrupt_stub_table[i]);
        handlers[i] = NULL;
    }

    const idt_ptr_t ptr = {
        .limit = sizeof(idt) - 1u,
        .base = (uint64_t)idt,
    };

    __asm__ volatile ("lidt %0" :: "m"(ptr));
    pic_remap();
    pit_init();
    irq_register_handler(0x0eu, page_fault_handler);
    irq_register_handler(0x20u, timer_handler);
    irq_register_handler(0x21u, keyboard_handler);
    irq_register_handler(0x2bu, e1000_handler);
    irq_register_handler(0x2cu, mouse_handler);
    pic_set_masks();
}

void irq_enable(void) {
    __asm__ volatile ("sti");
}

void irq_disable(void) {
    __asm__ volatile ("cli");
}
