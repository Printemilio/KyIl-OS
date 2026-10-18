#ifndef UNOS_IRQ_H
#define UNOS_IRQ_H

#include <stdint.h>

typedef struct interrupt_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

/* Diagnostic sample from a real ring-3 timer interrupt, not an unwind of a
 * suspended kernel stack. Zero means no sample for this thread. */
uint64_t irq_sampled_user_rip(uint32_t tid);
typedef struct irq_user_sample { uint32_t tid; uint64_t rip; } irq_user_sample_t;
uint32_t irq_copy_user_samples(uint32_t cpu, irq_user_sample_t samples[64]);

void irq_init(void);
void irq_prepare_idt(void); /* CPU faults are diagnosable before PMM/VMM. */
void irq_init_cpu(void);
void irq_start_ap_timers(void);
void irq_send_ipi(uint32_t apic_id, uint8_t vector);
void irq_lapic_eoi(void);
#define VEX_IPI_VECTOR 0xf1u
void irq_enable(void);
void irq_disable(void);
void irq_register_handler(uint8_t vector, void (*handler)(interrupt_frame_t *frame));
/* Exclusive dynamic PIC route, initially masked; refuses existing handlers. */
int irq_claim_pic(uint8_t irq,void (*handler)(interrupt_frame_t *));
void irq_mask_pic(uint8_t irq,int masked);
void irq_refresh_pic(uint8_t irq); /* Keep a shared wire live for remaining sources. */
int irq_release_pic(uint8_t irq,void (*handler)(interrupt_frame_t *));
void pic_send_eoi(uint8_t irq);
uint64_t irq_timer_ticks(void);

#endif
