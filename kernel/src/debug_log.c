#include <unos/debug_log.h>

#include <stddef.h>
#include <stdint.h>
#ifndef DEBUG_LOG_TESTING
#include <unos/smp.h>
#endif

#define COM1_PORT 0x3f8u
#define DEBUG_LOG_RING_SIZE 8192u

static int serial_ready;
static size_t serial_fifo_capacity = 1u;
static uint8_t output_lock;
static char debug_log_ring[DEBUG_LOG_RING_SIZE];
static size_t debug_log_head;
static size_t debug_log_count;
static uint32_t debug_log_sequence;
volatile uint16_t debug_log_fcw_entry_probe;
volatile uint16_t debug_log_fcw_exit_probe;
volatile uint16_t debug_log_fcw_after_ring_probe;
volatile uint16_t debug_log_fcw_after_putchar_probe;
volatile uint8_t debug_log_probe_first_char;
volatile uint8_t debug_log_port_probe_active;
volatile uint8_t debug_log_port_probe_in_seen;
volatile uint8_t debug_log_port_probe_out_seen;
volatile uint8_t debug_log_port_probe_status;
volatile uint16_t debug_log_fcw_before_inb_probe;
volatile uint16_t debug_log_fcw_after_inb_probe;
volatile uint16_t debug_log_fcw_before_outb_probe;
volatile uint16_t debug_log_fcw_after_outb_probe;

static inline void outb(uint16_t port, uint8_t value) {
#ifdef DEBUG_LOG_TESTING
    extern void debug_log_test_outb(uint16_t, uint8_t);
    debug_log_test_outb(port, value);
#else
    if (debug_log_port_probe_active != 0u && debug_log_port_probe_out_seen == 0u) {
        __asm__ volatile("fnstcw %0" : "=m"(debug_log_fcw_before_outb_probe));
        __asm__ volatile("outb %0, %1" :: "a"(value), "Nd"(port) : "memory");
        __asm__ volatile("fnstcw %0" : "=m"(debug_log_fcw_after_outb_probe));
        debug_log_port_probe_out_seen = 1u;
    } else {
        __asm__ volatile("outb %0, %1" :: "a"(value), "Nd"(port) : "memory");
    }
#endif
}

static inline uint8_t inb(uint16_t port) {
#ifdef DEBUG_LOG_TESTING
    extern uint8_t debug_log_test_inb(uint16_t);
    return debug_log_test_inb(port);
#else
    uint8_t value;
    if (debug_log_port_probe_active != 0u && debug_log_port_probe_in_seen == 0u) {
        __asm__ volatile("fnstcw %0" : "=m"(debug_log_fcw_before_inb_probe));
        __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
        __asm__ volatile("fnstcw %0" : "=m"(debug_log_fcw_after_inb_probe));
        debug_log_port_probe_status = value;
        debug_log_port_probe_in_seen = 1u;
    } else {
        __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    }
    return value;
#endif
}

void debug_log_init(void) {
    outb(COM1_PORT + 1u, 0x00u);
    outb(COM1_PORT + 3u, 0x80u);
    outb(COM1_PORT + 0u, 0x03u);
    outb(COM1_PORT + 1u, 0x00u);
    outb(COM1_PORT + 3u, 0x03u);
    outb(COM1_PORT + 2u, 0xc7u);
    outb(COM1_PORT + 4u, 0x0bu);
    /* Only use the 16550's 16-byte TX FIFO when the UART confirms it is
       enabled and functional. Older/non-FIFO UARTs retain single-byte writes. */
    serial_fifo_capacity = (inb(COM1_PORT + 2u) & 0xc0u) == 0xc0u ? 16u : 1u;
    serial_ready = 1;
}

static void debug_log_serial_write(const char *bytes, size_t count) {
    if (!serial_ready) {
        return;
    }

    for (uint32_t spins = 0; spins < 100000u; ++spins) {
        if ((inb(COM1_PORT + 5u) & 0x20u) != 0u) {
            /* THRE means the TX FIFO is empty. Never enqueue more bytes than
               its detected capacity before checking readiness again. */
            for (size_t i = 0u; i < count; ++i) outb(COM1_PORT, (uint8_t)bytes[i]);
            return;
        }
    }
}

static void debug_log_ring_push(char c) {
    debug_log_ring[debug_log_head] = c;
    debug_log_head = (debug_log_head + 1u) % DEBUG_LOG_RING_SIZE;
    if (debug_log_count < DEBUG_LOG_RING_SIZE) {
        ++debug_log_count;
    }
    ++debug_log_sequence;
}

void debug_log_write(const void *data, size_t count) {
    const char *text = data;
    uint8_t probe_first = debug_log_probe_first_char;
    debug_log_probe_first_char = 0u;
    if (probe_first) __asm__ volatile ("fnstcw %0" : "=m"(debug_log_fcw_entry_probe) : : "memory");
    if (text == 0) {
        return;
    }

    uint8_t pending_lf = 0u;
    while (count || pending_lf) {
        char output[16];
        size_t used = 0u;
        /* Protect each bounded FIFO burst and its ring updates against IRQ,
           preemption and another CPU. Do not disable IRQ for an entire log. */
        uint64_t flags = 0u;
#ifndef DEBUG_LOG_TESTING
        __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
#endif
        while (__atomic_test_and_set(&output_lock, __ATOMIC_ACQUIRE)) {
#ifndef DEBUG_LOG_TESTING
            smp_relax();
#else
            __asm__ volatile("pause");
#endif
        }
        while (used < serial_fifo_capacity && (count || pending_lf)) {
            if (pending_lf) {
                output[used++] = '\n';
                pending_lf = 0u;
                continue;
            }
            char c = *text++;
            --count;
            debug_log_ring_push(c);
            if (c == '\n') { output[used++] = '\r'; pending_lf = 1u; }
            else output[used++] = c;
        }
        if (probe_first) __asm__ volatile("fnstcw %0" : "=m"(debug_log_fcw_after_ring_probe) :: "memory");
        debug_log_serial_write(output, used);
        if (probe_first) __asm__ volatile("fnstcw %0" : "=m"(debug_log_fcw_after_putchar_probe) :: "memory");
        __atomic_clear(&output_lock, __ATOMIC_RELEASE);
#ifndef DEBUG_LOG_TESTING
        __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
#else
        (void)flags;
#endif
    }
    if (probe_first) __asm__ volatile ("fnstcw %0" : "=m"(debug_log_fcw_exit_probe) : : "memory");
}

void debug_log_puts(const char *text) {
    if (!text) return;
    size_t count = 0u;
    while (text[count]) ++count;
    debug_log_write(text, count);
}

size_t debug_log_copy_recent(char *buffer, size_t capacity, uint32_t *seq_inout) {
    uint32_t oldest_seq;
    uint32_t requested_seq;
    uint32_t available;
    size_t to_copy;
    size_t start;

    if (buffer == 0 || capacity == 0u || seq_inout == 0) {
        return 0u;
    }

    oldest_seq = debug_log_sequence - (uint32_t)debug_log_count;
    requested_seq = *seq_inout;
    if (requested_seq < oldest_seq) {
        requested_seq = oldest_seq;
    }
    if (requested_seq > debug_log_sequence) {
        requested_seq = debug_log_sequence;
    }

    available = debug_log_sequence - requested_seq;
    to_copy = (size_t)available;
    if (to_copy > capacity) {
        to_copy = capacity;
    }

    start = (debug_log_head + DEBUG_LOG_RING_SIZE - (debug_log_sequence - requested_seq)) % DEBUG_LOG_RING_SIZE;
    for (size_t i = 0; i < to_copy; ++i) {
        buffer[i] = debug_log_ring[(start + i) % DEBUG_LOG_RING_SIZE];
    }
    *seq_inout = requested_seq + (uint32_t)to_copy;
    return to_copy;
}
