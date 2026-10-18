#include <unos/profiler.h>
#include <unos/sched_clock.h>
#include <unos/syscall.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/heap.h>
#include <unos/compat.h>
#include <unos/debug_log.h>
#include <unos/events.h>
#include <unos/gdt.h>
#include <unos/keyboard.h>
#include <unos/scheduler.h>
#include <unos/tty.h>
#include <unos/vex_syscall.h>
#include <unos/syscall_wire.h>
#include <unos/usercopy.h>

#define SYSCALL_TABLE_CAPACITY 128ull
#define FNV1A_OFFSET_BASIS 14695981039346656037ull
#define FNV1A_PRIME 1099511628211ull

#define MSR_EFER  0xc0000080u
#define MSR_STAR  0xc0000081u
#define MSR_LSTAR 0xc0000082u
#define MSR_FMASK 0xc0000084u

#define EFER_SYSCALL_ENABLE 0x1ull
#define RFLAGS_INTERRUPT_FLAG 0x200ull
#define RFLAGS_DIRECTION_FLAG 0x400ull

#define KERNEL_CODE_SELECTOR 0x08ull
#define USER_CODE_SELECTOR   0x1bull

extern void syscall_entry(void);
extern volatile uint16_t scheduler_user_iret_fcw_probe;
extern volatile uint16_t debug_log_fcw_entry_probe;
extern volatile uint16_t debug_log_fcw_exit_probe;
extern volatile uint16_t debug_log_fcw_after_ring_probe;
extern volatile uint16_t debug_log_fcw_after_putchar_probe;
extern volatile uint8_t debug_log_probe_first_char;
extern volatile uint8_t debug_log_port_probe_active;
extern volatile uint8_t debug_log_port_probe_in_seen;
extern volatile uint8_t debug_log_port_probe_out_seen;
extern volatile uint8_t debug_log_port_probe_status;
extern volatile uint16_t debug_log_fcw_before_inb_probe;
extern volatile uint16_t debug_log_fcw_after_inb_probe;
extern volatile uint16_t debug_log_fcw_before_outb_probe;
extern volatile uint16_t debug_log_fcw_after_outb_probe;
extern volatile uint16_t irq_lapic_fcw_before_probe;
extern volatile uint16_t irq_lapic_fcw_after_probe;
extern volatile uint64_t irq_lapic_cs_probe;
extern volatile uint16_t irq_kernel_fcw_before_probe;
extern volatile uint16_t irq_kernel_fcw_after_probe;
extern volatile uint64_t irq_kernel_vector_probe;
extern volatile uint64_t irq_kernel_cs_probe;
extern volatile uint16_t irq_kernel_fcw_change_before_probe;
extern volatile uint16_t irq_kernel_fcw_change_after_probe;
extern volatile uint64_t irq_kernel_fcw_change_vector_probe;
extern volatile uint64_t irq_kernel_fcw_change_cs_probe;

static syscall_entry_t *syscall_table;
static size_t syscall_count;
static bool syscall_ready;
static uint32_t syscall_fpu_trace_count;
volatile uint16_t syscall_entry_fcw_probe;
static volatile uint16_t syscall_trace_fcw_samples[6];

static inline void syscall_capture_fcw_sample(size_t index) {
    __asm__ volatile ("fnstcw %0" : "=m"(syscall_trace_fcw_samples[index]) : : "memory");
}

static void syscall_log_u64_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char text[17];
    for (unsigned int i = 0u; i < 16u; ++i) {
        text[15u - i] = digits[value & 15u];
        value >>= 4u;
    }
    text[16] = '\0';
    debug_log_puts(text);
}

static uint16_t syscall_read_live_fcw(void) {
    uint16_t value;
    __asm__ volatile ("fnstcw %0" : "=m"(value));
    return value;
}

static uint16_t syscall_trace_masked_log(const char *text) {
    uint64_t saved_flags;
    uint16_t current_fcw;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(saved_flags) : : "memory", "cc");
    debug_log_probe_first_char = 1u;
    debug_log_port_probe_active = 1u;
    debug_log_puts(text);
    debug_log_port_probe_active = 0u;
    current_fcw = syscall_read_live_fcw();
    if ((saved_flags & RFLAGS_INTERRUPT_FLAG) != 0u) {
        __asm__ volatile ("sti" : : : "memory", "cc");
    }
    return current_fcw;
}

static void syscall_trace_fpu(const char *phase, uint64_t number) {
    if(!VEX_FPU_TRACE)return;
    syscall_capture_fcw_sample(0u);
    if (scheduler_current_pid() != 6u || syscall_fpu_trace_count >= 96u) {
        return;
    }
    syscall_capture_fcw_sample(1u);
    ++syscall_fpu_trace_count;
    const uint16_t masked_log_cw = syscall_fpu_trace_count <= 3u
        ? syscall_trace_masked_log("fpu-syscall: ") : 0u;
    if (syscall_fpu_trace_count <= 3u) {
        syscall_trace_fcw_samples[2] = masked_log_cw;
    } else {
        debug_log_puts("fpu-syscall: "); syscall_capture_fcw_sample(2u);
    }
    const uint16_t log_entry = debug_log_fcw_entry_probe;
    const uint16_t log_exit = debug_log_fcw_exit_probe;
    debug_log_puts(phase); syscall_capture_fcw_sample(3u);
    debug_log_puts(" nr="); syscall_capture_fcw_sample(4u);
    syscall_log_u64_hex(number); syscall_capture_fcw_sample(5u);
    debug_log_puts(" cw="); syscall_log_u64_hex(syscall_read_live_fcw());
    debug_log_puts(" entry-cw="); syscall_log_u64_hex(syscall_entry_fcw_probe);
    debug_log_puts(" iret-cw="); syscall_log_u64_hex(scheduler_user_iret_fcw_probe);
    debug_log_puts(" samples=");
    for (size_t i = 0u; i < 6u; ++i) {
        syscall_log_u64_hex(syscall_trace_fcw_samples[i]);
        if (i != 5u) debug_log_puts(",");
    }
    debug_log_puts(" log="); syscall_log_u64_hex(log_entry);
    debug_log_puts("/"); syscall_log_u64_hex(log_exit);
    debug_log_puts(" io="); syscall_log_u64_hex(debug_log_fcw_after_ring_probe);
    debug_log_puts("/"); syscall_log_u64_hex(debug_log_fcw_after_putchar_probe);
    debug_log_puts(" port="); syscall_log_u64_hex(debug_log_fcw_before_inb_probe);
    debug_log_puts("-"); syscall_log_u64_hex(debug_log_fcw_after_inb_probe);
    debug_log_puts("/"); syscall_log_u64_hex(debug_log_fcw_before_outb_probe);
    debug_log_puts("-"); syscall_log_u64_hex(debug_log_fcw_after_outb_probe);
    debug_log_puts(" status="); syscall_log_u64_hex(debug_log_port_probe_status);
    debug_log_puts(" irq="); syscall_log_u64_hex(irq_lapic_fcw_before_probe);
    debug_log_puts("/"); syscall_log_u64_hex(irq_lapic_fcw_after_probe);
    debug_log_puts(" cs="); syscall_log_u64_hex(irq_lapic_cs_probe);
    debug_log_puts(" irq-kernel="); syscall_log_u64_hex(irq_kernel_vector_probe);
    debug_log_puts("/"); syscall_log_u64_hex(irq_kernel_cs_probe);
    debug_log_puts(" "); syscall_log_u64_hex(irq_kernel_fcw_before_probe);
    debug_log_puts("/"); syscall_log_u64_hex(irq_kernel_fcw_after_probe);
    debug_log_puts(" irq-change="); syscall_log_u64_hex(irq_kernel_fcw_change_vector_probe);
    debug_log_puts("/"); syscall_log_u64_hex(irq_kernel_fcw_change_cs_probe);
    debug_log_puts(" "); syscall_log_u64_hex(irq_kernel_fcw_change_before_probe);
    debug_log_puts("/"); syscall_log_u64_hex(irq_kernel_fcw_change_after_probe);
    debug_log_puts("\n");
}

static uint64_t fnv1a64_string(const char *name) {
    uint64_t hash = FNV1A_OFFSET_BASIS;

    for (size_t i = 0; name[i] != '\0'; ++i) {
        hash ^= (uint8_t)name[i];
        hash *= FNV1A_PRIME;
    }

    return hash;
}

static uint64_t rdmsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32u) | low;
}

static void wrmsr(uint32_t msr, uint64_t value) {
    const uint32_t low = (uint32_t)value;
    const uint32_t high = (uint32_t)(value >> 32u);

    __asm__ volatile ("wrmsr" :: "c"(msr), "a"(low), "d"(high) : "memory");
}

static bool valid_priority(uint8_t priority) {
    return priority <= SYSCALL_PRIORITY_LAMBDA;
}

static syscall_entry_t *find_entry(uint64_t hash) {
    for (size_t i = 0; i < syscall_count; ++i) {
        if (syscall_table[i].hash == hash) {
            return &syscall_table[i];
        }
    }

    return NULL;
}

void syscall_init_current_cpu(void) {
    const uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SYSCALL_ENABLE);

    /* STAR defines the code/data selectors used by syscall/sysret. The current
       boot GDT uses the conventional Limine x86_64 selector layout. */
    wrmsr(MSR_STAR, (KERNEL_CODE_SELECTOR << 32u) | (USER_CODE_SELECTOR << 48u));
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* Hardware masks IF before the first instruction: SYSCALL leaves RSP
       and GS in user state, so even an IRQ before the software CLI is unsafe.
       syscall_entry explicitly executes STI after switching stack/GS and
       saving its frame; blocking syscalls still run with IRQs enabled. */
    wrmsr(MSR_FMASK, RFLAGS_DIRECTION_FLAG | (1ull<<9u));
}

static void syscall_schedule_handler(void *args) {
    (void)args;
    schedule();
}

static void syscall_yield_handler(void *args) {
    (void)args;
    process_yield();
}

static void syscall_wait_handler(void *args) {
    (void)args;
    process_wait();
}

void syscall_init(void) {
    syscall_ready = false;
    syscall_count = 0;

    syscall_table = kmalloc(sizeof(syscall_entry_t) * SYSCALL_TABLE_CAPACITY);
    if (syscall_table == NULL) {
        return;
    }

    syscall_init_current_cpu();

    /* Critical syscalls are kernel-owned and installed once during bootstrap. */
    syscall_register("schedule", syscall_schedule_handler, SYSCALL_PRIORITY_CRITICAL);
    syscall_register("process_yield", syscall_yield_handler, SYSCALL_PRIORITY_CRITICAL);
    syscall_register("process_wait", syscall_wait_handler, SYSCALL_PRIORITY_CRITICAL);

    syscall_ready = true;
}

void syscall_register(const char *name, void *handler, uint8_t priority) {
    if (name == NULL || handler == NULL || !valid_priority(priority) || syscall_count >= SYSCALL_TABLE_CAPACITY) {
        return;
    }

    const uint64_t hash = fnv1a64_string(name);
    /* Registered legacy hashes may not occupy the explicit native wire domain. */
    if ((hash & VEX_SYSCALL_DOMAIN_MASK) == VEX_SYSCALL_NATIVE_TAG) return;
    syscall_entry_t *existing = find_entry(hash);

    if (existing != NULL) {
        /* Critical entries are immutable once present. */
        if (existing->priority == SYSCALL_PRIORITY_CRITICAL) {
            return;
        }

        existing->priority = priority;
        existing->handler = handler;
        return;
    }

    syscall_table[syscall_count].hash = hash;
    syscall_table[syscall_count].priority = priority;
    syscall_table[syscall_count].handler = handler;
    ++syscall_count;
}

void *syscall_lookup(uint64_t hash) {
    if (syscall_table == NULL) {
        return NULL;
    }

    syscall_entry_t *entry = find_entry(hash);
    return entry == NULL ? NULL : entry->handler;
}

void syscall_dispatch(uint64_t hash, void *args) {
    if (!syscall_ready) {
        return;
    }

    syscall_entry_t *entry = find_entry(hash);
    if (entry == NULL || !valid_priority(entry->priority) || entry->handler == NULL) {
        return;
    }

    void (*handler)(void *) = entry->handler;
    handler(args);
}

static uint64_t syscall_write_user_string(const char *str) {
    char text[4096];
    if (str == NULL || vex_user_string_copy(text,sizeof(text),(uint64_t)(uintptr_t)str)) {
        return (uint64_t)-1;
    }

    debug_log_puts(text);
    tty_puts(text);
    return 0;
}

static uint64_t syscall_read_user_line(char *buffer, uint64_t capacity) {
    if (buffer == NULL || capacity == 0 || capacity > SIZE_MAX ||
        !vex_syscall_user_range_valid((uint64_t)(uintptr_t)buffer,
            (size_t)capacity, 1)) {
        return (uint64_t)-1;
    }

    uint64_t length = 0;
    while (length + 1u < capacity) {
        const char c = keyboard_getchar();
        if (c == '\n') {
            tty_putchar('\n');
            break;
        }

        if (c == '\b') {
            if (length > 0) {
                --length;
                tty_putchar('\b');
            }
            continue;
        }

        if(vex_copy_to_user((uint64_t)(uintptr_t)buffer+length,&c,1u))return (uint64_t)-1;
        ++length;
        tty_putchar(c);
    }

    const char end=0;
    if(vex_copy_to_user((uint64_t)(uintptr_t)buffer+length,&end,1u))return (uint64_t)-1;
    return length;
}

static uint64_t syscall_dispatch_raw_inner(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    process_t *caller=scheduler_current_process();
    if(caller && caller->group && caller->group->ultra_space &&
       (number & VEX_SYSCALL_DOMAIN_MASK)!=VEX_SYSCALL_NATIVE_TAG)return (uint64_t)-13;
    /* Select the wire ABI before decoding ANY argument. A personality is not
       a privilege: explicit native calls retain the same user/capability
       checks for Linux and PE. Untagged calls keep their personality ABI. */
    if ((number & VEX_SYSCALL_DOMAIN_MASK) == VEX_SYSCALL_NATIVE_TAG) {
        return vex_syscall_dispatch_user(number & VEX_SYSCALL_NUMBER_MASK,
                                         arg1, arg2, arg3);
    }
    if (scheduler_current_abi() == VEX_ABI_LINUX) {
        return compat_linux_syscall(number, arg1, arg2, arg3, arg4, arg5, arg6);
    }

    if (scheduler_current_abi() == VEX_ABI_PE) {
        /* Historical PE syscall personality has no kernel Win32 engine.
           New PE processes run inside a native userspace runtime. */
        return (uint64_t)-1;
    }

    (void)arg4;
    (void)arg5;
    (void)arg6;
    (void)arg7;

    switch (number) {
    case SYS_WRITE:
        return syscall_write_user_string((const char *)arg1);
    case SYS_EXIT:
        process_exit();
        return 0;
    case SYS_READ:
        return syscall_read_user_line((char *)arg1, arg2);
    default:
        /* Historical untagged native numbers 4..196 are a compatibility
           adapter, never a second dispatch after a successful native call. */
        if (number >= VEX_SYSCALL_LEGACY_NATIVE_FIRST &&
            number <= VEX_SYSCALL_LEGACY_NATIVE_LAST)
            return vex_syscall_dispatch_user(number, arg1, arg2, arg3);
        if (syscall_ready && find_entry(number) != NULL) {
            /* The only registered handler dereferencing an argument writes
               one keyboard byte. Preserve it without trusting a legacy ptr. */
            if (number == fnv1a64_string("keyboard_getchar") &&
                !vex_syscall_user_range_valid(arg1, 1u, 1))
                return (uint64_t)-1;
            if(number==fnv1a64_string("keyboard_getchar")) {
                char byte=0;syscall_dispatch(number,&byte);
                return vex_copy_to_user(arg1,&byte,1u)?(uint64_t)-1:0u;
            }
            syscall_dispatch(number, (void *)arg1);
            return 0;
        }
        return (uint64_t)-1;
    }
}

uint64_t syscall_dispatch_raw(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    syscall_trace_fpu("enter", number);
    const uint64_t profile_start_ns = profiler_active ? sched_clock_ns() : 0u;
    uint64_t result = syscall_dispatch_raw_inner(number, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    if (profile_start_ns != 0u) {
        const uint32_t personality = (number & VEX_SYSCALL_DOMAIN_MASK) == VEX_SYSCALL_NATIVE_TAG ? 2u
            : scheduler_current_abi() == VEX_ABI_LINUX ? 1u : 3u;
        profiler_syscall((personality << 16) | (uint32_t)(number & 0xffffu), sched_clock_ns() - profile_start_ns);
    }
    syscall_trace_fpu("inner-return", number);
    process_t *return_process;
    /* Time-slice: if the timer requested a reschedule while we were in the
     * kernel, yield here at the syscall boundary where it's safe to switch. */
    scheduler_preempt_if_pending();
    syscall_trace_fpu("preempt-return", number);
    /* arch_prctl(ARCH_SET_FS) changes the architectural MSR.  Reapply the
       current thread's saved value after a possible scheduler hand-off so a
       syscall return can never expose a stale/zero FS base to glibc TLS. */
    scheduler_restore_current_fs_base();
    return_process = scheduler_current_process();
    if (return_process != NULL && return_process->event_return_rax_valid != 0u) {
        result = return_process->event_return_rax;
        return_process->event_return_rax_valid = 0u;
        /* Scratch slot 16 belongs to this invocation's kernel stack. A
           CPU-local flag survives preemption and can corrupt another task's
           pending syscall return. Assembly reads this slot with IRQs off. */
        ((uint64_t *)(uintptr_t)return_process->syscall_frame_rsp)[2] = 1u;
    } else {
        if(return_process && !return_process->event_active)return_process->event_saved_rax=result;
        vex_event_deliver_pending();
    }
    scheduler_user_return(scheduler_current_process()->syscall_frame_rsp,result);
    return result;
}

