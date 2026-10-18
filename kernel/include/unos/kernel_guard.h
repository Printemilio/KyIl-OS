#ifndef UNOS_KERNEL_GUARD_H
#define UNOS_KERNEL_GUARD_H
#include <stdint.h>
#include <unos/scheduler.h>
#include <unos/gdt.h>
#include <unos/smp.h>
#include <unos/sched_clock.h>
#include <unos/profiler.h>
/* Short, non-sleeping subsystem transactions. Recursive calls from synchronous
 * storage callbacks are allowed; no reference crosses a scheduler sleep here. */
/* `holder_pc` and `acquired_ns` are diagnostics: where the outermost holder took
 * the gate, and (only while profiling) when. */
typedef struct kernel_gate { uint8_t locked; uint64_t owner; unsigned depth; uint64_t holder_pc, acquired_ns; } kernel_gate_t;
#define KERNEL_GATE_INIT {0,UINT64_MAX,0,0,0}
typedef struct kernel_guard { kernel_gate_t *gate; uint64_t flags; } kernel_guard_t;
static inline kernel_guard_t kernel_guard_enter_mode(kernel_gate_t *gate,int irq_safe) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    scheduler_nonpreempt_enter();
    uint64_t owner=((uint64_t)scheduler_current_tid()<<32)|gdt_current_cpu_id();
    if(__atomic_load_n(&gate->owner,__ATOMIC_ACQUIRE)!=owner) {
        if(__atomic_test_and_set(&gate->locked,__ATOMIC_ACQUIRE)) {
            vex_cpu_t *self=smp_current_cpu();
            if(self)self->spin_gate=(uint64_t)(uintptr_t)gate;
            do {
                if(irq_safe && (flags&512u))__asm__ volatile("sti":::"memory");
                smp_relax();
                if(irq_safe && (flags&512u))__asm__ volatile("cli":::"memory");
            } while(__atomic_test_and_set(&gate->locked,__ATOMIC_ACQUIRE));
            if(self)self->spin_gate=0u;
        }
        __atomic_store_n(&gate->owner,owner,__ATOMIC_RELEASE);
        gate->holder_pc=(uint64_t)(uintptr_t)__builtin_return_address(0);   /* caller of the outermost enter */
        if(__atomic_load_n(&profiler_active,__ATOMIC_RELAXED))gate->acquired_ns=sched_clock_ns();
    }
    ++gate->depth;
    if(irq_safe && (flags&512u))__asm__ volatile("sti":::"memory");
    return (kernel_guard_t){gate,flags};
}
static inline kernel_guard_t kernel_guard_enter(kernel_gate_t *gate) {return kernel_guard_enter_mode(gate,0);}
/* Only when CPL0 IRQ paths cannot access this inventory while its interrupted
   thread holds the transaction. Kernel preemption stays disabled; unrelated
   input/timer/IPI IRQs can run. Deferred IRQ work must use a worker. IRQ-owned
   gates and callers already under CLI keep the original exclusion. */
static inline kernel_guard_t kernel_guard_enter_irq_safe(kernel_gate_t *gate) {return kernel_guard_enter_mode(gate,1);}
static inline void kernel_guard_leave(kernel_guard_t *guard) {
    __asm__ volatile("cli":::"memory");
    if(!--guard->gate->depth) {
        if(guard->gate->acquired_ns) {
            if(__atomic_load_n(&profiler_active,__ATOMIC_RELAXED))
                profiler_gate_release(guard->gate,sched_clock_ns()-guard->gate->acquired_ns,guard->gate->holder_pc);
            guard->gate->acquired_ns=0u;
        }
        __atomic_store_n(&guard->gate->owner,UINT64_MAX,__ATOMIC_RELEASE);
        __atomic_clear(&guard->gate->locked,__ATOMIC_RELEASE);
    }
    scheduler_nonpreempt_leave();
    __asm__ volatile("pushq %0; popfq" :: "r"(guard->flags) : "memory","cc");
}
#define KERNEL_GUARD(gate) kernel_guard_t subsystem_guard __attribute__((cleanup(kernel_guard_leave)))=kernel_guard_enter(&(gate))
#endif
