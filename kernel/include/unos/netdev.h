#ifndef UNOS_NETDEV_H
#define UNOS_NETDEV_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t link_up;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t rx_broadcast;
    uint32_t rx_multicast;
    uint32_t rx_no_buffer;
    uint32_t missed_packets;
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_control;
    uint8_t rx_descriptor_status[4];
} netdev_stats_t;

void netdev_init(void);
int netdev_link_up(void);
void netdev_irq_handler(void);
int netdev_send_frame(const void *data, size_t size);
int netdev_recv_frame(void *buffer, size_t *size);
const uint8_t *netdev_mac(void);
const char *netdev_type(void);
void netdev_stats(netdev_stats_t *stats);

#endif
