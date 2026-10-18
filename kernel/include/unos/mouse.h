#ifndef UNOS_MOUSE_H
#define UNOS_MOUSE_H

#include <stdint.h>

typedef struct {
    int dx;
    int dy;
    uint8_t buttons;
    int wheel;
} mouse_packet_t;

void mouse_init(void);
void mouse_irq_handler(void);
void mouse_update(void);
int mouse_events_pending(void);
int mouse_get_event(int *dx, int *dy, uint8_t *buttons);
int mouse_poll(mouse_packet_t *packet);
int mouse_take_wheel(void);
int mouse_decode_packet(const uint8_t *bytes,uint32_t length,mouse_packet_t *packet);

#endif
