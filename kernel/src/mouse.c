#include <unos/mouse.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/irq.h>

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_INPUT_FULL 0x02u
#define PS2_STATUS_AUX_DATA 0x20u
#define PS2_CMD_ENABLE_AUX 0xa8u
#define PS2_CMD_READ_CONFIG 0x20u
#define PS2_CMD_WRITE_CONFIG 0x60u
#define PS2_CMD_WRITE_AUX 0xd4u
#define PS2_MOUSE_SET_DEFAULTS 0xf6u
#define PS2_MOUSE_ENABLE_STREAMING 0xf4u
#define MOUSE_EVENT_BUFFER_SIZE 128u

static uint8_t packet_bytes[3];
static uint8_t packet_index;
static uint8_t mouse_ready;
static volatile mouse_packet_t event_buffer[MOUSE_EVENT_BUFFER_SIZE];
static volatile uint8_t event_head;
static volatile uint8_t event_tail;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static void io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" :: "a"(0));
}

static int wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000u; ++i) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0) {
            return 0;
        }
    }
    return -1;
}

static int wait_output_full(void) {
    for (uint32_t i = 0; i < 100000u; ++i) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
            return 0;
        }
    }
    return -1;
}

static void ps2_command(uint8_t command) {
    if (wait_input_clear() == 0) {
        outb(PS2_STATUS_PORT, command);
        io_wait();
    }
}

static void mouse_write(uint8_t value) {
    if (wait_input_clear() != 0) {
        return;
    }
    outb(PS2_STATUS_PORT, PS2_CMD_WRITE_AUX);
    io_wait();

    if (wait_input_clear() != 0) {
        return;
    }
    outb(PS2_DATA_PORT, value);
    io_wait();

    if (wait_output_full() == 0) {
        (void)inb(PS2_DATA_PORT);
    }
}

static void push_event(int dx, int dy, uint8_t buttons) {
    const uint8_t next = (uint8_t)((event_head + 1u) % MOUSE_EVENT_BUFFER_SIZE);
    if (next == event_tail) {
        return;
    }

    event_buffer[event_head].dx = dx;
    event_buffer[event_head].dy = dy;
    event_buffer[event_head].buttons = buttons;
    event_head = next;
}

static int pop_event(int *dx, int *dy, uint8_t *buttons) {
    if (event_tail == event_head) {
        return 0;
    }

    *dx = event_buffer[event_tail].dx;
    *dy = event_buffer[event_tail].dy;
    *buttons = event_buffer[event_tail].buttons;
    event_tail = (uint8_t)((event_tail + 1u) % MOUSE_EVENT_BUFFER_SIZE);
    return 1;
}

static void consume_packet_byte(uint8_t byte) {
    if (packet_index == 0 && (byte & 0x08u) == 0) {
        return;
    }

    packet_bytes[packet_index++] = byte;
    if (packet_index < 3u) {
        return;
    }

    packet_index = 0;
    if ((packet_bytes[0] & 0xC0u) != 0u) {
        return;
    }
    push_event((int)(int8_t)packet_bytes[1], -(int)(int8_t)packet_bytes[2], (uint8_t)(packet_bytes[0] & 0x07u));
}

void mouse_init(void) {
    packet_index = 0;
    mouse_ready = 0;
    event_head = 0;
    event_tail = 0;

    ps2_command(PS2_CMD_ENABLE_AUX);
    ps2_command(PS2_CMD_READ_CONFIG);

    uint8_t config = 0;
    if (wait_output_full() == 0) {
        config = inb(PS2_DATA_PORT);
    }

    config |= 0x02u;   /* Enable IRQ12 in case interrupts are wired later. */
    config &= ~0x20u;  /* Enable mouse clock. */

    ps2_command(PS2_CMD_WRITE_CONFIG);
    if (wait_input_clear() == 0) {
        outb(PS2_DATA_PORT, config);
    }

    mouse_write(PS2_MOUSE_SET_DEFAULTS);
    mouse_write(PS2_MOUSE_ENABLE_STREAMING);
    mouse_ready = 1;
}

void mouse_irq_handler(void) {
    const uint8_t status = inb(PS2_STATUS_PORT);
    if (!mouse_ready || (status & PS2_STATUS_OUTPUT_FULL) == 0 || (status & PS2_STATUS_AUX_DATA) == 0) {
        return;
    }

    consume_packet_byte(inb(PS2_DATA_PORT));
}

/* Drain all pending mouse bytes from the PS/2 output buffer without
   waiting for IRQ12.  Called from the GUI loop as a polling fallback. */
void mouse_update(void) {
    uint32_t i;
    irq_disable();
    for (i = 0; i < 64u; ++i) {
        const uint8_t status = inb(PS2_STATUS_PORT);
        if (!mouse_ready || (status & PS2_STATUS_OUTPUT_FULL) == 0 || (status & PS2_STATUS_AUX_DATA) == 0) {
            break;
        }
        consume_packet_byte(inb(PS2_DATA_PORT));
    }
    irq_enable();
}

int mouse_get_event(int *dx, int *dy, uint8_t *buttons) {
    if (!mouse_ready || dx == NULL || dy == NULL || buttons == NULL) {
        return 0;
    }

    return pop_event(dx, dy, buttons);
}

int mouse_poll(mouse_packet_t *packet) {
    if (packet == NULL) {
        return 0;
    }

    return mouse_get_event(&packet->dx, &packet->dy, &packet->buttons);
}
