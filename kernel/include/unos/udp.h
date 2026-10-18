#ifndef UNOS_UDP_H
#define UNOS_UDP_H

#include <stddef.h>
#include <stdint.h>

typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port, const void *data, size_t size);

void udp_init(void);
int udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, const void *data, size_t size);
int udp_recv(uint16_t port, void *buffer, size_t *size);
void udp_register_handler(uint16_t port, udp_handler_t handler);

int udp_endpoint_open(uint16_t port);
int udp_endpoint_peer(int handle,uint32_t ip,uint16_t port);
uint16_t udp_endpoint_port(int handle);
int udp_endpoint_send(int handle,uint32_t ip,uint16_t port,const void *data,size_t size);
int udp_endpoint_receive(int handle,void *data,size_t size,uint32_t *ip,uint16_t *port,int peek,int truncated);
int udp_endpoint_readable(int handle);
int udp_endpoint_writable(int handle);
int udp_endpoint_error(int handle);
/* Transport faults retain the offending packet, independently of the payload
 * receive queue. ICMP metadata stays transport-native; personalities translate it. */
typedef struct {
    uint32_t reporter_ip, destination_ip, detail;
    uint16_t destination_port, payload_size;
    uint8_t type, code;
    uint8_t payload[128];
} udp_transport_fault_t;
int udp_endpoint_fault_reporting(int handle, int enabled);
int udp_endpoint_fault_pending(int handle);
int udp_endpoint_fault_status(int handle,udp_transport_fault_t *fault,int clear);
int udp_endpoint_fault_receive(int handle, udp_transport_fault_t *fault);
void udp_transport_fault(uint32_t reporter_ip, uint8_t type, uint8_t code,
                         uint32_t detail, const void *quoted_packet, size_t size);
void udp_endpoint_close(int handle);
int udp_service(void);
#endif

