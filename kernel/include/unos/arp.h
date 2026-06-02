#ifndef UNOS_ARP_H
#define UNOS_ARP_H

#include <stdint.h>

void arp_init(void);
void arp_set_local_ip(uint32_t ip);
int arp_lookup(uint32_t ip, uint8_t mac[6]);
int arp_resolve(uint32_t ip, uint8_t mac[6]);
int arp_request(uint32_t target_ip);

#endif
