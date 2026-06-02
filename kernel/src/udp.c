#include <unos/udp.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/heap.h>
#include <unos/ipv4.h>
#include <unos/tty.h>

#define UDP_HEADER_SIZE 8u
#define UDP_MAX_DATAGRAM 1472u
#define UDP_SOCKET_COUNT 16u
#define UDP_SOCKET_BUFFER_SIZE 2048u

typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

typedef struct {
    uint16_t port;
    udp_handler_t handler;
    uint8_t *buffer;
    size_t size;
    uint32_t src_ip;
    uint16_t src_port;
    int has_data;
} udp_socket_t;

static udp_socket_t sockets[UDP_SOCKET_COUNT];
static uint8_t udp_tx_packet[UDP_HEADER_SIZE + UDP_MAX_DATAGRAM];
static uint8_t udp_checksum_data[12u + UDP_HEADER_SIZE + UDP_MAX_DATAGRAM];

static uint16_t bswap16(uint16_t value) {
    return (uint16_t)((value << 8u) | (value >> 8u));
}

static uint32_t bswap32(uint32_t value) {
    return ((value & 0x000000ffu) << 24u)
        | ((value & 0x0000ff00u) << 8u)
        | ((value & 0x00ff0000u) >> 8u)
        | ((value & 0xff000000u) >> 24u);
}

static void copy_bytes(void *dst, const void *src, size_t size) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < size; ++i) {
        d[i] = s[i];
    }
}

static uint16_t checksum16(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint32_t sum = 0;

    while (size > 1u) {
        sum += (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
        bytes += 2u;
        size -= 2u;
    }

    if (size != 0) {
        sum += (uint16_t)((uint16_t)bytes[0] << 8u);
    }

    while ((sum >> 16u) != 0) {
        sum = (sum & 0xffffu) + (sum >> 16u);
    }

    return (uint16_t)~sum;
}

static uint16_t udp_checksum(uint32_t src_ip, uint32_t dest_ip, const void *udp_packet, size_t udp_size) {
    uint32_t src_be = bswap32(src_ip);
    uint32_t dest_be = bswap32(dest_ip);
    uint16_t len_be = bswap16((uint16_t)udp_size);

    copy_bytes(&udp_checksum_data[0], &src_be, sizeof(src_be));
    copy_bytes(&udp_checksum_data[4], &dest_be, sizeof(dest_be));
    udp_checksum_data[8] = 0;
    udp_checksum_data[9] = IPV4_PROTOCOL_UDP;
    copy_bytes(&udp_checksum_data[10], &len_be, sizeof(len_be));
    copy_bytes(&udp_checksum_data[12], udp_packet, udp_size);

    const uint16_t checksum = checksum16(udp_checksum_data, 12u + udp_size);
    return checksum == 0 ? 0xffffu : checksum;
}

static udp_socket_t *find_socket(uint16_t port) {
    for (size_t i = 0; i < UDP_SOCKET_COUNT; ++i) {
        if (sockets[i].port == port) {
            return &sockets[i];
        }
    }
    return NULL;
}

static udp_socket_t *ensure_socket(uint16_t port) {
    udp_socket_t *socket = find_socket(port);
    if (socket != NULL) {
        return socket;
    }

    for (size_t i = 0; i < UDP_SOCKET_COUNT; ++i) {
        if (sockets[i].port == 0) {
            sockets[i].port = port;
            sockets[i].buffer = kmalloc(UDP_SOCKET_BUFFER_SIZE);
            sockets[i].size = 0;
            sockets[i].has_data = 0;
            return sockets[i].buffer == NULL ? NULL : &sockets[i];
        }
    }

    return NULL;
}

static void udp_handle_ipv4(uint32_t src_ip, const void *payload, size_t size) {
    if (size < UDP_HEADER_SIZE) {
        return;
    }

    const udp_header_t *header = payload;
    const uint16_t src_port = bswap16(header->src_port);
    const uint16_t dest_port = bswap16(header->dest_port);
    const uint16_t length = bswap16(header->length);

    if (length < UDP_HEADER_SIZE || length > size) {
        return;
    }

    udp_socket_t *socket = find_socket(dest_port);
    if (socket == NULL) {
        return;
    }

    const uint8_t *data = (const uint8_t *)payload + UDP_HEADER_SIZE;
    const size_t data_size = length - UDP_HEADER_SIZE;

    if (socket->buffer != NULL && data_size <= UDP_SOCKET_BUFFER_SIZE) {
        copy_bytes(socket->buffer, data, data_size);
        socket->size = data_size;
        socket->src_ip = src_ip;
        socket->src_port = src_port;
        socket->has_data = 1;
    }

    if (socket->handler != NULL) {
        socket->handler(src_ip, src_port, data, data_size);
    }
}

void udp_init(void) {
    for (size_t i = 0; i < UDP_SOCKET_COUNT; ++i) {
        sockets[i].port = 0;
        sockets[i].handler = NULL;
        sockets[i].buffer = NULL;
        sockets[i].size = 0;
        sockets[i].has_data = 0;
    }

    ipv4_register_handler(IPV4_PROTOCOL_UDP, udp_handle_ipv4);
}

int udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, const void *data, size_t size) {
    if (data == NULL || size > UDP_MAX_DATAGRAM) {
        return -1;
    }

    udp_header_t *header = (udp_header_t *)udp_tx_packet;

    header->src_port = bswap16(src_port);
    header->dest_port = bswap16(dest_port);
    header->length = bswap16((uint16_t)(UDP_HEADER_SIZE + size));
    header->checksum = 0;
    copy_bytes(udp_tx_packet + UDP_HEADER_SIZE, data, size);
    header->checksum = bswap16(udp_checksum(ipv4_get_local_ip(), dest_ip, udp_tx_packet, UDP_HEADER_SIZE + size));

    const int result = ipv4_send(dest_ip, IPV4_PROTOCOL_UDP, udp_tx_packet, UDP_HEADER_SIZE + size);
    return result;
}

int udp_recv(uint16_t port, void *buffer, size_t *size) {
    if (buffer == NULL || size == NULL) {
        return -1;
    }

    udp_socket_t *socket = ensure_socket(port);
    if (socket == NULL || !socket->has_data || *size < socket->size) {
        return -1;
    }

    copy_bytes(buffer, socket->buffer, socket->size);
    *size = socket->size;
    socket->has_data = 0;
    return 0;
}

void udp_register_handler(uint16_t port, udp_handler_t handler) {
    udp_socket_t *socket = ensure_socket(port);
    if (socket != NULL) {
        socket->handler = handler;
    }
}
