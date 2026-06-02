#ifndef UNOS_DHCP_H
#define UNOS_DHCP_H

#include <stdint.h>

void dhcp_init(void);
void dhcp_start(void);
void dhcp_start_async(void);
uint32_t dhcp_offered_ip(void);
uint32_t dhcp_netmask(void);
uint32_t dhcp_gateway(void);
uint32_t dhcp_dns_server(void);
uint64_t dhcp_offer_wait_ms(void);
uint64_t dhcp_ack_wait_ms(void);
int dhcp_config_loaded(void);
int dhcp_config_saved(void);

#endif
