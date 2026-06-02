#ifndef UNOS_TLS_H
#define UNOS_TLS_H

#include <stddef.h>
#include <stdint.h>

int tls_connect(uint32_t ip, uint16_t port, const char *hostname);
int tls_send(int sock, const void *data, size_t size);
int tls_recv(int sock, void *buffer, size_t *size);
int tls_send_timeout(int sock, const void *data, size_t size, uint32_t timeout_polls);
int tls_recv_timeout(int sock, void *buffer, size_t *size, uint32_t timeout_polls);
int tls_can_read(int sock);
int tls_can_write(int sock);
int tls_is_closed(int sock);
void tls_close(int sock);

#endif
