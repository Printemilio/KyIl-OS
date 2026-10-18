#ifndef VEX_MOUSE_HOST_TEST
#include <unos/kernel_guard.h>
static kernel_gate_t mouse_gate=KERNEL_GATE_INIT;
#else
#define KERNEL_GUARD(gate) ((void)0)
void gui_input_wake(void);
#endif
#include <unos/mouse.h>
#include <unos/profiler.h>
#include <unos/sched_clock.h>

#include <stddef.h>
#include <stdint.h>
#ifndef VEX_MOUSE_HOST_TEST
#include <unos/gui.h>
#endif
#include <unos/irq.h>
#include <unos/debug_log.h>
#include <stdio.h>

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

static uint8_t packet_bytes[4];
static uint8_t packet_length=3;
static int pending_wheel;
static uint8_t packet_index;
static uint8_t mouse_ready;
static volatile mouse_packet_t event_buffer[MOUSE_EVENT_BUFFER_SIZE];
static volatile uint8_t event_head;
static volatile uint8_t event_tail;
static uint8_t delivered_buttons;
static uint32_t published_packets;

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

static uint64_t event_time_ns[MOUSE_EVENT_BUFFER_SIZE];   /* profiler: IRQ time of each queued event */

static void push_event(int dx, int dy, uint8_t buttons,int wheel) {
    /* Compact at publication, not only at consumption: a delayed GUI must
       not fill its ring with one entry per physical motion report. Preserve
       every button edge, wheel and direction reversal exactly as pop does. */
    if(event_head!=event_tail && !wheel) {
        uint8_t last=(event_head+MOUSE_EVENT_BUFFER_SIZE-1u)%MOUSE_EVENT_BUFFER_SIZE;
        uint8_t prior=last==event_tail?delivered_buttons:
            event_buffer[(last+MOUSE_EVENT_BUFFER_SIZE-1u)%MOUSE_EVENT_BUFFER_SIZE].buttons;
        mouse_packet_t tail=event_buffer[last];
        int64_t sum_x=(int64_t)tail.dx+dx,sum_y=(int64_t)tail.dy+dy;
        if(tail.buttons==buttons && tail.buttons==prior && !tail.wheel &&
           !((tail.dx<0 && dx>0)||(tail.dx>0 && dx<0)||
             (tail.dy<0 && dy>0)||(tail.dy>0 && dy<0)) &&
           sum_x>=INT32_MIN && sum_x<=INT32_MAX && sum_y>=INT32_MIN && sum_y<=INT32_MAX) {
            event_buffer[last].dx=(int)sum_x;event_buffer[last].dy=(int)sum_y;
            ++published_packets;return;
        }
    }
    const uint8_t next = (uint8_t)((event_head + 1u) % MOUSE_EVENT_BUFFER_SIZE);
    if (next == event_tail) {
        return;
    }

    event_buffer[event_head].dx = dx;
    event_buffer[event_head].dy = dy;
    event_buffer[event_head].buttons = buttons;
    event_buffer[event_head].wheel=wheel;
    if(profiler_active)event_time_ns[event_head]=sched_clock_ns();
    event_head = next;
    ++published_packets;
}

static int pop_event(int *dx, int *dy, uint8_t *buttons) {
    if (event_tail == event_head) {
        return 0;
    }

    *dx = event_buffer[event_tail].dx;
    *dy = event_buffer[event_tail].dy;
    *buttons = event_buffer[event_tail].buttons;
    const int button_edge = *buttons != delivered_buttons;
    delivered_buttons = *buttons;
    const int wheel = event_buffer[event_tail].wheel;
    if(profiler_active && event_time_ns[event_tail])
        profiler_latency(PROF_LAT_MOUSE_QUEUE,sched_clock_ns()-event_time_ns[event_tail]);
    event_time_ns[event_tail]=0u;
    pending_wheel+=wheel;
    event_tail = (uint8_t)((event_tail + 1u) % MOUSE_EVENT_BUFFER_SIZE);
    /* Consume a motion run in one GUI turn without swallowing any button
     * edge or wheel event. Keep direction reversals separate: summing those
     * would change the result of screen-edge clamping and drag paths. */
    while (!button_edge && !wheel && event_tail != event_head) {
        const mouse_packet_t next = event_buffer[event_tail];
        if (next.buttons != *buttons || next.wheel ||
            (*dx < 0 && next.dx > 0) || (*dx > 0 && next.dx < 0) ||
            (*dy < 0 && next.dy > 0) || (*dy > 0 && next.dy < 0)) break;
        if((int64_t)*dx+next.dx<INT32_MIN || (int64_t)*dx+next.dx>INT32_MAX ||
           (int64_t)*dy+next.dy<INT32_MIN || (int64_t)*dy+next.dy>INT32_MAX)break;
        *dx += next.dx;
        *dy += next.dy;
        event_tail = (uint8_t)((event_tail + 1u) % MOUSE_EVENT_BUFFER_SIZE);
    }
    return 1;
}

int mouse_decode_packet(const uint8_t *bytes,uint32_t length,mouse_packet_t *out) {
    if(!bytes || !out || (length!=3 && length!=4) || !(bytes[0]&8u) || (bytes[0]&0xc0u))return 0;
    /* PS/2 displacements are signed NINE-bit values. The sign lives in
     * byte 0, not in bit 7 of the data byte. Casting to int8 reverses large
     * legitimate movements and corrupts viewport hit-testing. */
    out->dx=(int)bytes[1]-((bytes[0]&0x10u)?256:0);
    out->dy=-((int)bytes[2]-((bytes[0]&0x20u)?256:0));
    out->buttons=bytes[0]&7u;
    out->wheel=length==4?(int)(int8_t)(bytes[3]<<4)/16:0;
    return 1;
}

static void consume_packet_byte(uint8_t byte) {
    if (packet_index == 0 && (byte & 0x08u) == 0) {
        return;
    }

    packet_bytes[packet_index++] = byte;
    if (packet_index < packet_length) {
        return;
    }

    packet_index = 0;
    if ((packet_bytes[0] & 0xC0u) != 0u) {
        return;
    }
    mouse_packet_t decoded;
    if(mouse_decode_packet(packet_bytes,packet_length,&decoded)) {
        push_event(decoded.dx,decoded.dy,decoded.buttons,decoded.wheel);
    }
}

void mouse_init(void) {
    packet_index = 0;
    mouse_ready = 0;
    event_head = 0;
    event_tail = 0;
    delivered_buttons = 0;

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
    /* IntelliMouse negotiation; a plain PS/2 device keeps three-byte packets. */
    mouse_write(0xf3);mouse_write(200);
    mouse_write(0xf3);mouse_write(100);
    mouse_write(0xf3);mouse_write(80);
    mouse_write(0xf2);
    unsigned id=256u;
    if(wait_output_full()==0) {
        id=inb(PS2_DATA_PORT);packet_length=id==3 || id==4?4:3;
    }
    char mouse_log[100];snprintf(mouse_log,sizeof(mouse_log),"mouse: device_id=%u packet_length=%u\n",id,packet_length);debug_log_puts(mouse_log);
    mouse_write(PS2_MOUSE_ENABLE_STREAMING);
    mouse_ready = 1;
}

void mouse_irq_handler(void) {
    mouse_update();
}

/* Service already available bytes from IRQ12 as well as from the GUI.
 * Do not depend on another IRQ edge for bytes already in the controller,
 * and preserve IF when called from an interrupt gate. */
void mouse_update(void) {
    KERNEL_GUARD(mouse_gate);
    uint64_t flags;
    uint32_t i;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    const uint32_t previous_publication = published_packets;
    for (i = 0; i < 64u; ++i) {
        const uint8_t status = inb(PS2_STATUS_PORT);
        if (!mouse_ready || (status & PS2_STATUS_OUTPUT_FULL) == 0 || (status & PS2_STATUS_AUX_DATA) == 0) {
            break;
        }
        consume_packet_byte(inb(PS2_DATA_PORT));
    }
    if (published_packets != previous_publication) {
        gui_input_wake();
    }
    __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

int mouse_events_pending(void) {
    KERNEL_GUARD(mouse_gate);
    return event_tail != event_head;
}

int mouse_get_event(int *dx, int *dy, uint8_t *buttons) {
    KERNEL_GUARD(mouse_gate);
    if (!mouse_ready || dx == NULL || dy == NULL || buttons == NULL) {
        return 0;
    }

    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    int result = pop_event(dx, dy, buttons);
    __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
    return result;
}

int mouse_poll(mouse_packet_t *packet) {
    KERNEL_GUARD(mouse_gate);
    if (packet == NULL) {
        return 0;
    }

    int result=mouse_get_event(&packet->dx, &packet->dy, &packet->buttons);
    packet->wheel=mouse_take_wheel();return result;
}

int mouse_take_wheel(void) {
    KERNEL_GUARD(mouse_gate);int value=pending_wheel;pending_wheel=0;return value;}
