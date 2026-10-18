#ifndef UNOS_ETHERNET_H
#define UNOS_ETHERNET_H

#include <stddef.h>
#include <stdint.h>

#define ETHERNET_TYPE_IPV4 0x0800u
#define ETHERNET_TYPE_ARP  0x0806u
#define ETHERNET_TYPE_IPV6 0x86ddu

typedef void (*ethernet_handler_t)(const uint8_t *src_mac, const void *payload, size_t size);

void ethernet_init(void);
void ethernet_service_start(void);
void ethernet_wake(void);
void ethernet_wake_irq(void);
int ethernet_send(const uint8_t dest_mac[6], uint16_t ethertype, const void *payload, size_t size);
int ethernet_poll(void);
void ethernet_register_handler(uint16_t ethertype, ethernet_handler_t handler);
const uint8_t *ethernet_mac(void);

#endif

