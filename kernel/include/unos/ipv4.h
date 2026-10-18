#ifndef UNOS_IPV4_H
#define UNOS_IPV4_H

#include <stddef.h>
#include <stdint.h>

#define IPV4_PROTOCOL_ICMP 1u
#define IPV4_PROTOCOL_TCP  6u
#define IPV4_PROTOCOL_UDP  17u

typedef void (*ipv4_handler_t)(uint32_t src_ip,uint32_t dest_ip,const void *payload,size_t size);

void ipv4_init(void);
void ipv4_set_local_ip(uint32_t ip);
uint32_t ipv4_get_local_ip(void);
int ipv4_send(uint32_t dest_ip, uint8_t protocol, const void *payload, size_t size);
void ipv4_register_handler(uint8_t protocol, ipv4_handler_t handler);

#endif

