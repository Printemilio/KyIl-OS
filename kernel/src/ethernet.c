#include <unos/ethernet.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/netdev.h>
#include <unos/tty.h>

#define ETHERNET_HEADER_SIZE 14u
#define ETHERNET_FRAME_MAX 1518u
#define ETHERNET_PAYLOAD_MAX (ETHERNET_FRAME_MAX - ETHERNET_HEADER_SIZE)
#define ETHERNET_HANDLER_COUNT 8u

typedef struct {
    uint16_t ethertype;
    ethernet_handler_t handler;
} ethernet_handler_entry_t;

static ethernet_handler_entry_t handlers[ETHERNET_HANDLER_COUNT];
static uint8_t rx_frame[ETHERNET_FRAME_MAX];
static uint8_t tx_frame[ETHERNET_FRAME_MAX];

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
}

static void write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8u);
    bytes[1] = (uint8_t)value;
}

static int mac_is_local_or_broadcast(const uint8_t *dest) {
    const uint8_t *local = netdev_mac();
    int broadcast = 1;
    int local_match = 1;

    for (size_t i = 0; i < 6u; ++i) {
        if (dest[i] != 0xffu) {
            broadcast = 0;
        }
        if (dest[i] != local[i]) {
            local_match = 0;
        }
    }

    return broadcast || local_match;
}

static ethernet_handler_t find_handler(uint16_t ethertype) {
    for (size_t i = 0; i < ETHERNET_HANDLER_COUNT; ++i) {
        if (handlers[i].handler != NULL && handlers[i].ethertype == ethertype) {
            return handlers[i].handler;
        }
    }

    return NULL;
}

void ethernet_init(void) {
    netdev_init();

    for (size_t i = 0; i < ETHERNET_HANDLER_COUNT; ++i) {
        handlers[i].ethertype = 0;
        handlers[i].handler = NULL;
    }
}

int ethernet_send(const uint8_t dest_mac[6], uint16_t ethertype, const void *payload, size_t size) {
    if (dest_mac == NULL || payload == NULL || size > ETHERNET_PAYLOAD_MAX) {
        return -1;
    }

    const uint8_t *src_mac = netdev_mac();

    for (size_t i = 0; i < 6u; ++i) {
        tx_frame[i] = dest_mac[i];
        tx_frame[6u + i] = src_mac[i];
    }

    write_be16(&tx_frame[12], ethertype);

    const uint8_t *payload_bytes = payload;
    for (size_t i = 0; i < size; ++i) {
        tx_frame[ETHERNET_HEADER_SIZE + i] = payload_bytes[i];
    }

    const int result = netdev_send_frame(tx_frame, ETHERNET_HEADER_SIZE + size);
    return result;
}

int ethernet_poll(void) {
    size_t size = sizeof(rx_frame);

    if (netdev_recv_frame(rx_frame, &size) != 0) {
        return 0;
    }

    if (size < ETHERNET_HEADER_SIZE || !mac_is_local_or_broadcast(&rx_frame[0])) {
        return -1;
    }

    const uint16_t ethertype = read_be16(&rx_frame[12]);
    ethernet_handler_t handler = find_handler(ethertype);
    if (handler == NULL) {
        return 0;
    }

    handler(&rx_frame[6], &rx_frame[ETHERNET_HEADER_SIZE], size - ETHERNET_HEADER_SIZE);
    return 1;
}

void ethernet_register_handler(uint16_t ethertype, ethernet_handler_t handler) {
    for (size_t i = 0; i < ETHERNET_HANDLER_COUNT; ++i) {
        if (handlers[i].handler != NULL && handlers[i].ethertype == ethertype) {
            handlers[i].handler = handler;
            return;
        }
    }

    for (size_t i = 0; i < ETHERNET_HANDLER_COUNT; ++i) {
        if (handlers[i].handler == NULL) {
            handlers[i].ethertype = ethertype;
            handlers[i].handler = handler;
            return;
        }
    }
}

const uint8_t *ethernet_mac(void) {
    return netdev_mac();
}
