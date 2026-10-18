#ifndef UNOS_TCP_H
#define UNOS_TCP_H

#include <stddef.h>
#include <stdint.h>

int tcp_connect_start(uint32_t ip,uint16_t port);
int tcp_connection_status(int handle);
int tcp_connection_error(int handle);
int tcp_service(void);
int64_t tcp_try_write(int handle,const void *data,size_t size);
int64_t tcp_try_read(int handle,void *data,size_t size);
int tcp_connect(uint32_t ip, uint16_t port);
int tcp_send(int sock, const void *data, size_t size);
int tcp_recv(int sock, void *buffer, size_t *size);
int tcp_send_timeout(int sock, const void *data, size_t size, uint32_t timeout_polls);
int tcp_recv_timeout(int sock, void *buffer, size_t *size, uint32_t timeout_polls);
int tcp_can_read(int sock);
int tcp_can_write(int sock);
int tcp_is_closed(int sock);
uint16_t tcp_local_port(int sock);
void tcp_close(int sock);

#endif
