#include <unos/syscall.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/heap.h>
#include <unos/compat.h>
#include <unos/debug_log.h>
#include <unos/keyboard.h>
#include <unos/scheduler.h>
#include <unos/tty.h>
#include <unos/vex_syscall.h>

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
#define USER_CODE_SELECTOR   0x18ull

extern void syscall_entry(void);

static syscall_entry_t *syscall_table;
static size_t syscall_count;
static bool syscall_ready;

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

static void install_syscall_msrs(void) {
    const uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SYSCALL_ENABLE);

    /* STAR defines the code/data selectors used by syscall/sysret. The current
       boot GDT uses the conventional Limine x86_64 selector layout. */
    wrmsr(MSR_STAR, (KERNEL_CODE_SELECTOR << 32u) | (USER_CODE_SELECTOR << 48u));
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* Mask IF and DF while running the kernel syscall path. */
    wrmsr(MSR_FMASK, RFLAGS_INTERRUPT_FLAG | RFLAGS_DIRECTION_FLAG);
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

    install_syscall_msrs();

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
    if (str == NULL) {
        return (uint64_t)-1;
    }

    debug_log_puts(str);
    tty_puts(str);
    return 0;
}

static uint64_t syscall_read_user_line(char *buffer, uint64_t capacity) {
    if (buffer == NULL || capacity == 0) {
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

        buffer[length++] = c;
        tty_putchar(c);
    }

    buffer[length] = '\0';
    return length;
}

uint64_t syscall_dispatch_raw(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    if (scheduler_current_abi() == VEX_ABI_LINUX) {
        return compat_linux_syscall(number, arg1, arg2, arg3, arg4, arg5, arg6);
    }

    if (scheduler_current_abi() == VEX_ABI_PE) {
        if (number == VEX_PE_SYSCALL_CALL) {
            return compat_pe_syscall(arg1, arg2, arg3, arg4, arg5, arg6, 0);
        }
        return (uint64_t)-1;
    }

    (void)arg4;
    (void)arg5;
    (void)arg6;

    switch (number) {
    case SYS_WRITE:
        return syscall_write_user_string((const char *)arg1);
    case SYS_EXIT:
        process_exit();
        return 0;
    case SYS_READ:
        return syscall_read_user_line((char *)arg1, arg2);
    default:
        {
            const uint64_t result = vex_syscall_dispatch(number, arg1, arg2, arg3);
            if (result != 0) {
                return result;
            }
        }
        syscall_dispatch(number, (void *)arg1);
        return 0;
    }
}
