#ifndef UNOS_DNS_H
#define UNOS_DNS_H

#include <stdint.h>

int dns_resolve(const char *hostname, uint32_t *ip);

#endif
