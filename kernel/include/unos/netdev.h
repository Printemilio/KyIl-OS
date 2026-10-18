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
uint8_t netdev_pic_irq(void); /* Actual PCI INTx line, or 0xff without a source. */
int netdev_send_frame(const void *data, size_t size);
int netdev_recv_frame(void *buffer, size_t *size);
const uint8_t *netdev_mac(void);
const char *netdev_type(void);
void netdev_stats(netdev_stats_t *stats);
typedef struct netdev_driver_ops {
    int (*send)(void *,const void *,size_t);
    int (*receive)(void *,void *,size_t *);
    int (*link)(void *);
    void (*irq)(void *);
    void (*stats)(void *,netdev_stats_t *);
} netdev_driver_ops_t;
int netdev_control_transaction(int (*)(void *),void *);
int netdev_native_suspend(void);
int netdev_native_resume(void);
int netdev_driver_install(const netdev_driver_ops_t *,void *,const uint8_t[6]);
int netdev_driver_detach(void *);
void netdev_native_counters(uint64_t[3]);

#endif
