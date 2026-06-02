#ifndef UNOS_UDP_H
#define UNOS_UDP_H

#include <stddef.h>
#include <stdint.h>

typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port, const void *data, size_t size);

void udp_init(void);
int udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, const void *data, size_t size);
int udp_recv(uint16_t port, void *buffer, size_t *size);
void udp_register_handler(uint16_t port, udp_handler_t handler);

#endif

