#include <unos/debug_log.h>

#include <stddef.h>
#include <stdint.h>

#define COM1_PORT 0x3f8u
#define DEBUG_LOG_RING_SIZE 8192u

static int serial_ready;
static char debug_log_ring[DEBUG_LOG_RING_SIZE];
static size_t debug_log_head;
static size_t debug_log_count;
static uint32_t debug_log_sequence;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void debug_log_init(void) {
    outb(COM1_PORT + 1u, 0x00u);
    outb(COM1_PORT + 3u, 0x80u);
    outb(COM1_PORT + 0u, 0x03u);
    outb(COM1_PORT + 1u, 0x00u);
    outb(COM1_PORT + 3u, 0x03u);
    outb(COM1_PORT + 2u, 0xc7u);
    outb(COM1_PORT + 4u, 0x0bu);
    serial_ready = 1;
}

static void debug_log_putchar(char c) {
    if (!serial_ready) {
        return;
    }

    for (uint32_t spins = 0; spins < 100000u; ++spins) {
        if ((inb(COM1_PORT + 5u) & 0x20u) != 0u) {
            outb(COM1_PORT, (uint8_t)c);
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

void debug_log_puts(const char *text) {
    if (text == 0) {
        return;
    }

    while (*text != '\0') {
        if (*text == '\n') {
            debug_log_ring_push('\n');
            debug_log_putchar('\r');
        } else {
            debug_log_ring_push(*text);
        }
        debug_log_putchar(*text++);
    }
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
