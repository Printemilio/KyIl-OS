#include <unos/tcp.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/arp.h>
#include <unos/debug_log.h>
#include <unos/dhcp.h>
#include <unos/ethernet.h>
#include <unos/heap.h>
#include <unos/ipv4.h>
#include <unos/tty.h>

#define TCP_DEBUG 0
#define TCP_MAX_CONNECTIONS 8u
#define TCP_BUFFER_SIZE 65536u
#define TCP_MAX_SEGMENT_SIZE 1460u
#define TCP_ARP_TIMEOUT 250000u
#define TCP_ARP_RETRY_POLLS 256u
#define TCP_CONNECT_TIMEOUT 120000u
#define TCP_IO_TIMEOUT 180000u
#define TCP_CLOSE_TIMEOUT 20000u
#define TCP_RETRANSMIT_COUNT 2u
#define TCP_FIN 0x01u
#define TCP_SYN 0x02u
#define TCP_RST 0x04u
#define TCP_PSH 0x08u
#define TCP_ACK 0x10u
#define TCP_WINDOW_DEFAULT 65535u
#define VBOX_NAT_GATEWAY_IP 0x0a000202u

#if TCP_DEBUG == 0
static void tcp_debug_puts(const char *str) {
    (void)str;
}

static void tcp_debug_putchar(char c) {
    (void)c;
}

#define tty_puts tcp_debug_puts
#define tty_putchar tcp_debug_putchar
#endif

typedef enum {
    TCP_CLOSED,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT,
} tcp_state_t;

typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_offset_reserved;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_header_t;

typedef struct {
    int used;
    tcp_state_t state;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t send_seq;
    uint32_t send_unacked;
    uint32_t recv_seq;
    uint16_t remote_window;
    uint8_t *rx_buffer;
    size_t rx_size;
} tcp_socket_t;

static tcp_socket_t sockets[TCP_MAX_CONNECTIONS];
static uint16_t next_local_port = 49152u;
static uint32_t next_initial_seq = 0x554e4f53u;
static int tcp_initialized;

static uint16_t bswap16(uint16_t value) {
    return (uint16_t)((value << 8u) | (value >> 8u));
}

static uint32_t bswap32(uint32_t value) {
    return ((value & 0x000000ffu) << 24u)
        | ((value & 0x0000ff00u) << 8u)
        | ((value & 0x00ff0000u) >> 8u)
        | ((value & 0xff000000u) >> 24u);
}

static void print_u32(uint32_t value) {
    char digits[10];
    size_t count = 0;

    if (value == 0) {
        tty_putchar('0');
        return;
    }

    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count > 0) {
        tty_putchar(digits[--count]);
    }
}

static void print_ipv4(uint32_t ip) {
    print_u32((ip >> 24u) & 0xffu);
    tty_putchar('.');
    print_u32((ip >> 16u) & 0xffu);
    tty_putchar('.');
    print_u32((ip >> 8u) & 0xffu);
    tty_putchar('.');
    print_u32(ip & 0xffu);
}

static void tcp_log_u32(uint32_t value) {
    char digits[10];
    size_t count = 0;

    if (value == 0u) {
        debug_log_puts("0");
        return;
    }

    while (value > 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count > 0u) {
        char out[2] = { digits[--count], '\0' };
        debug_log_puts(out);
    }
}

static void tcp_log_ipv4(uint32_t ip) {
    tcp_log_u32((ip >> 24u) & 0xffu);
    debug_log_puts(".");
    tcp_log_u32((ip >> 16u) & 0xffu);
    debug_log_puts(".");
    tcp_log_u32((ip >> 8u) & 0xffu);
    debug_log_puts(".");
    tcp_log_u32(ip & 0xffu);
}

static void copy_bytes(void *dst, const void *src, size_t size) {
    uint8_t *d = dst;
    const uint8_t *s = src;

    for (size_t i = 0; i < size; ++i) {
        d[i] = s[i];
    }
}

static void move_bytes(void *dst, const void *src, size_t size) {
    uint8_t *d = dst;
    const uint8_t *s = src;

    if (d < s) {
        for (size_t i = 0; i < size; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = size; i > 0; --i) {
            d[i - 1u] = s[i - 1u];
        }
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

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dest_ip, const void *segment, size_t segment_size) {
    uint8_t checksum_data[12u + sizeof(tcp_header_t) + TCP_MAX_SEGMENT_SIZE];
    uint32_t src_be = bswap32(src_ip);
    uint32_t dest_be = bswap32(dest_ip);
    uint16_t len_be = bswap16((uint16_t)segment_size);

    if (segment_size > sizeof(tcp_header_t) + TCP_MAX_SEGMENT_SIZE) {
        return 0;
    }

    copy_bytes(&checksum_data[0], &src_be, sizeof(src_be));
    copy_bytes(&checksum_data[4], &dest_be, sizeof(dest_be));
    checksum_data[8] = 0;
    checksum_data[9] = IPV4_PROTOCOL_TCP;
    copy_bytes(&checksum_data[10], &len_be, sizeof(len_be));
    copy_bytes(&checksum_data[12], segment, segment_size);

    return checksum16(checksum_data, 12u + segment_size);
}

static tcp_socket_t *socket_from_handle(int sock) {
    if (sock < 0 || (size_t)sock >= TCP_MAX_CONNECTIONS || !sockets[sock].used) {
        return NULL;
    }

    return &sockets[sock];
}

static int socket_index(tcp_socket_t *socket) {
    return (int)(socket - sockets);
}

static tcp_socket_t *find_socket(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port) {
    for (size_t i = 0; i < TCP_MAX_CONNECTIONS; ++i) {
        if (sockets[i].used &&
            sockets[i].remote_ip == remote_ip &&
            sockets[i].remote_port == remote_port &&
            sockets[i].local_port == local_port) {
            return &sockets[i];
        }
    }

    return NULL;
}

static tcp_socket_t *alloc_socket(void) {
    for (size_t i = 0; i < TCP_MAX_CONNECTIONS; ++i) {
        if (!sockets[i].used) {
            sockets[i].used = 1;
            sockets[i].state = TCP_CLOSED;
            sockets[i].rx_size = 0;
            sockets[i].remote_window = TCP_WINDOW_DEFAULT;
            sockets[i].rx_buffer = kmalloc(TCP_BUFFER_SIZE);
            if (sockets[i].rx_buffer == NULL) {
                sockets[i].used = 0;
                return NULL;
            }
            return &sockets[i];
        }
    }

    return NULL;
}

static int wait_for_arp(uint32_t ip) {
    uint8_t mac[6];
    const uint32_t local_ip = ipv4_get_local_ip();
    uint32_t netmask = dhcp_netmask();
    uint32_t next_hop = ip;

    if (netmask == 0 && (local_ip & 0xffffff00u) == 0x0a000200u) {
        netmask = 0xffffff00u;
    }

    if (netmask != 0 && (ip & netmask) != (local_ip & netmask)) {
        next_hop = dhcp_gateway();
        if (next_hop == 0) {
            next_hop = VBOX_NAT_GATEWAY_IP;
        }
    }

    debug_log_puts("tcp: wait_for_arp ");
    tcp_log_ipv4(next_hop);
    if (next_hop != ip) {
        debug_log_puts(" gateway for ");
        tcp_log_ipv4(ip);
    }
    debug_log_puts("\n");

    tty_puts("tcp: wait_for_arp ");
    print_ipv4(next_hop);
    if (next_hop != ip) {
        tty_puts(" gateway for ");
        print_ipv4(ip);
    }
    tty_putchar('\n');

    (void)arp_request(next_hop);
    for (uint32_t i = 0; i < TCP_ARP_TIMEOUT; ++i) {
        if (arp_lookup(next_hop, mac) == 0) {
            debug_log_puts("tcp: arp ok\n");
            tty_puts("tcp: arp resolved after ");
            print_u32(i);
            tty_puts(" polls\n");
            return 0;
        }

        if ((i % TCP_ARP_RETRY_POLLS) == (TCP_ARP_RETRY_POLLS - 1u)) {
            (void)arp_request(next_hop);
        }

        if (i != 0 && (i % 100000u) == 0) {
            tty_puts("tcp: arp polls ");
            print_u32(i);
            tty_putchar('\n');
        }

        (void)ethernet_poll();
    }

    if (next_hop != VBOX_NAT_GATEWAY_IP) {
        debug_log_puts("tcp: arp fallback gateway ");
        tcp_log_ipv4(VBOX_NAT_GATEWAY_IP);
        debug_log_puts("\n");
        (void)arp_request(VBOX_NAT_GATEWAY_IP);
        for (uint32_t i = 0; i < TCP_ARP_TIMEOUT; ++i) {
            if (arp_lookup(VBOX_NAT_GATEWAY_IP, mac) == 0) {
                debug_log_puts("tcp: arp gateway ok\n");
                return 0;
            }
            if ((i % TCP_ARP_RETRY_POLLS) == (TCP_ARP_RETRY_POLLS - 1u)) {
                (void)arp_request(VBOX_NAT_GATEWAY_IP);
            }
            (void)ethernet_poll();
        }
    }

    tty_puts("tcp: arp timeout\n");
    debug_log_puts("tcp: arp timeout\n");
    return -1;
}

static int tcp_send_segment(tcp_socket_t *socket, uint8_t flags, const void *data, size_t size) {
    if (socket == NULL || size > TCP_MAX_SEGMENT_SIZE) {
        return -1;
    }

    uint8_t segment[sizeof(tcp_header_t) + TCP_MAX_SEGMENT_SIZE];
    tcp_header_t *header = (tcp_header_t *)segment;

    header->src_port = bswap16(socket->local_port);
    header->dest_port = bswap16(socket->remote_port);
    header->seq = bswap32(socket->send_seq);
    header->ack = bswap32(socket->recv_seq);
    header->data_offset_reserved = (uint8_t)((sizeof(tcp_header_t) / 4u) << 4u);
    header->flags = flags;
    {
        size_t available = socket->rx_size < TCP_BUFFER_SIZE ? TCP_BUFFER_SIZE - socket->rx_size : 0u;
        if (available > 65535u) available = 65535u;
        header->window = bswap16((uint16_t)available);
    }
    header->checksum = 0;
    header->urgent = 0;

    if (data != NULL && size != 0) {
        copy_bytes(segment + sizeof(tcp_header_t), data, size);
    }

    const uint16_t checksum = tcp_checksum(ipv4_get_local_ip(), socket->remote_ip, segment, sizeof(tcp_header_t) + size);
    header->checksum = bswap16(checksum);
    if (flags == TCP_SYN && size == 0) {
        tty_puts("tcp: checksum calculated\n");
    }

    if (flags == TCP_SYN && size == 0) {
        if (tcp_checksum(ipv4_get_local_ip(), socket->remote_ip, segment, sizeof(tcp_header_t)) == 0) {
            tty_puts("tcp: syn checksum ok\n");
        } else {
            tty_puts("tcp: syn checksum bad\n");
        }
    }

    const int result = ipv4_send(socket->remote_ip, IPV4_PROTOCOL_TCP, segment, sizeof(tcp_header_t) + size);
    if (flags == TCP_SYN && size == 0) {
        tty_puts("tcp: ipv4_send returned ");
        print_u32((uint32_t)(result < 0 ? 999999u : (uint32_t)result));
        tty_putchar('\n');
    }

    return result;
}

static void tcp_handle_ipv4(uint32_t src_ip, const void *payload, size_t size) {
    if (payload == NULL || size < sizeof(tcp_header_t)) {
        return;
    }

    const tcp_header_t *header = payload;
    const uint16_t src_port = bswap16(header->src_port);
    const uint16_t dest_port = bswap16(header->dest_port);
    const uint8_t header_size = (uint8_t)((header->data_offset_reserved >> 4u) * 4u);

    if (header_size < sizeof(tcp_header_t) || header_size > size) {
        return;
    }

    tty_puts("tcp: rx src ");
    print_ipv4(src_ip);
    tty_puts(" sport ");
    print_u32(src_port);
    tty_puts(" dport ");
    print_u32(dest_port);
    tty_puts(" flags ");
    print_u32(header->flags);
    tty_puts(" data ");
    print_u32((uint32_t)(size - header_size));
    tty_putchar('\n');

    tcp_socket_t *socket = find_socket(src_ip, src_port, dest_port);
    if (socket == NULL) {
        tty_puts("tcp: rx no socket\n");
        return;
    }

    const uint32_t seq = bswap32(header->seq);
    const uint32_t ack = bswap32(header->ack);
    const size_t data_size = size - header_size;
    const uint8_t *data = (const uint8_t *)payload + header_size;

    socket->remote_window = bswap16(header->window);

    if ((header->flags & TCP_RST) != 0) {
        tty_puts("tcp: rx rst\n");
        socket->state = TCP_CLOSED;
        return;
    }

    if (socket->state == TCP_SYN_SENT &&
        (header->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) &&
        ack == socket->send_seq) {
        socket->recv_seq = seq + 1u;
        socket->send_unacked = ack;
        socket->state = TCP_ESTABLISHED;
        (void)tcp_send_segment(socket, TCP_ACK, NULL, 0);
        return;
    }

    if (socket->state == TCP_ESTABLISHED && (header->flags & TCP_ACK) != 0 && ack > socket->send_unacked) {
        socket->send_unacked = ack;
        tty_puts("tcp: ack advanced ");
        print_u32(ack);
        tty_putchar('\n');
    }

    if (socket->state == TCP_ESTABLISHED && data_size != 0 && seq == socket->recv_seq) {
        if (socket->rx_size + data_size > TCP_BUFFER_SIZE) {
            tty_puts("tcp: rx buffer full\n");
            (void)tcp_send_segment(socket, TCP_ACK, NULL, 0);
            return;
        }

        copy_bytes(socket->rx_buffer + socket->rx_size, data, data_size);
        socket->rx_size += data_size;
        socket->recv_seq += (uint32_t)data_size;
        tty_puts("tcp: data accepted ");
        print_u32((uint32_t)data_size);
        tty_putchar('\n');
        tty_puts("tcp: send data ack ");
        print_u32(socket->recv_seq);
        tty_putchar('\n');
        const int ack_result = tcp_send_segment(socket, TCP_ACK, NULL, 0);
        tty_puts("tcp: data ack ret ");
        print_u32((uint32_t)(ack_result < 0 ? 999999u : (uint32_t)ack_result));
        tty_putchar('\n');
    } else if (socket->state == TCP_ESTABLISHED && data_size != 0) {
        tty_puts("tcp: data ignored seq ");
        print_u32(seq);
        tty_puts(" expected ");
        print_u32(socket->recv_seq);
        tty_putchar('\n');
        tty_puts("tcp: send duplicate ack ");
        print_u32(socket->recv_seq);
        tty_putchar('\n');
        const int ack_result = tcp_send_segment(socket, TCP_ACK, NULL, 0);
        tty_puts("tcp: duplicate ack ret ");
        print_u32((uint32_t)(ack_result < 0 ? 999999u : (uint32_t)ack_result));
        tty_putchar('\n');
    }

    if ((header->flags & TCP_FIN) != 0) {
        tty_puts("tcp: rx fin\n");
        socket->recv_seq = seq + 1u;
        (void)tcp_send_segment(socket, TCP_ACK, NULL, 0);
        socket->state = TCP_CLOSED;
    }
}

static void tcp_init_once(void) {
    if (tcp_initialized) {
        return;
    }

    for (size_t i = 0; i < TCP_MAX_CONNECTIONS; ++i) {
        sockets[i].used = 0;
        sockets[i].state = TCP_CLOSED;
        sockets[i].rx_buffer = NULL;
        sockets[i].rx_size = 0;
    }

    ipv4_register_handler(IPV4_PROTOCOL_TCP, tcp_handle_ipv4);
    tcp_initialized = 1;
}

int tcp_connect(uint32_t ip, uint16_t port) {
    debug_log_puts("tcp: connect begin ");
    tcp_log_ipv4(ip);
    debug_log_puts(":");
    tcp_log_u32(port);
    debug_log_puts("\n");
    tty_puts("tcp: entry\n");
    tcp_init_once();
    tty_puts("tcp: after init\n");

    if (ip == 0) {
        debug_log_puts("tcp: failed ip 0\n");
        tty_puts("tcp: preconnect failed ip 0\n");
        return -1;
    }

    if (port == 0) {
        debug_log_puts("tcp: failed port 0\n");
        tty_puts("tcp: preconnect failed port 0\n");
        return -1;
    }

    if (ipv4_get_local_ip() == 0) {
        debug_log_puts("tcp: failed local ip 0\n");
        tty_puts("tcp: preconnect failed local ip 0\n");
        return -1;
    }

    if (wait_for_arp(ip) != 0) {
        debug_log_puts("tcp: failed arp\n");
        tty_puts("tcp: preconnect failed arp\n");
        return -1;
    }
    tty_puts("tcp: after preconnect\n");

    tcp_socket_t *socket = alloc_socket();
    if (socket == NULL) {
        debug_log_puts("tcp: failed no socket\n");
        tty_puts("tcp: no socket\n");
        return -1;
    }
    tty_puts("tcp: after socket alloc\n");

    socket->remote_ip = ip;
    socket->remote_port = port;
    socket->local_port = next_local_port++;
    socket->send_seq = next_initial_seq;
    socket->send_unacked = socket->send_seq;
    socket->recv_seq = 0;
    socket->state = TCP_SYN_SENT;
    next_initial_seq += 0x10000u;

    const uint32_t syn_seq = socket->send_seq;
    for (uint32_t attempt = 0; attempt < TCP_RETRANSMIT_COUNT && socket->state != TCP_ESTABLISHED; ++attempt) {
        tty_puts("tcp: connect dst ");
        print_ipv4(ip);
        tty_puts(" port ");
        print_u32(port);
        tty_puts(" attempt ");
        print_u32(attempt + 1u);
        tty_putchar('\n');

        socket->send_seq = syn_seq;
        socket->send_unacked = syn_seq;
        debug_log_puts("tcp: syn send attempt ");
        tcp_log_u32(attempt + 1u);
        debug_log_puts("\n");
        if (tcp_send_segment(socket, TCP_SYN, NULL, 0) != 0) {
            debug_log_puts("tcp: syn send failed\n");
            tty_puts("tcp: SYN send failed\n");
        } else {
            debug_log_puts("tcp: syn sent\n");
            tty_puts("tcp: SYN sent\n");
        }
        socket->send_seq = syn_seq + 1u;

        uint32_t polls = 0;
        for (; polls < TCP_CONNECT_TIMEOUT && socket->state != TCP_ESTABLISHED; ++polls) {
            (void)ethernet_poll();
        }

        tty_puts("tcp: polls ");
        print_u32(polls);
        tty_putchar('\n');
    }

    if (socket->state != TCP_ESTABLISHED) {
        debug_log_puts("tcp: connect timeout\n");
        tty_puts("tcp: connect timeout\n");
        kfree(socket->rx_buffer);
        socket->rx_buffer = NULL;
        socket->used = 0;
        socket->state = TCP_CLOSED;
        return -1;
    }

    tty_puts("tcp: connected\n");
    debug_log_puts("tcp: connected\n");
    return socket_index(socket);
}

int tcp_send(int sock, const void *data, size_t size) {
    return tcp_send_timeout(sock, data, size, TCP_IO_TIMEOUT);
}

int tcp_send_timeout(int sock, const void *data, size_t size, uint32_t timeout_polls) {
    tcp_socket_t *socket = socket_from_handle(sock);
    if (socket == NULL || socket->state != TCP_ESTABLISHED || data == NULL) {
        return -1;
    }

    const uint8_t *bytes = data;
    size_t sent = 0;

    while (sent < size) {
        const size_t chunk_size = (size - sent) > TCP_MAX_SEGMENT_SIZE ? TCP_MAX_SEGMENT_SIZE : (size - sent);
        const uint32_t seq = socket->send_seq;
        socket->send_unacked = seq;

        for (uint32_t attempt = 0; attempt < TCP_RETRANSMIT_COUNT; ++attempt) {
            socket->send_seq = seq;
            if (tcp_send_segment(socket, TCP_PSH | TCP_ACK, bytes + sent, chunk_size) != 0) {
                return -1;
            }

            socket->send_seq = seq + (uint32_t)chunk_size;
            for (uint32_t i = 0; i < timeout_polls; ++i) {
                if (socket->send_unacked >= socket->send_seq) {
                    sent += chunk_size;
                    goto next_chunk;
                }
                (void)ethernet_poll();
            }
        }

        return -1;

next_chunk:
        continue;
    }

    return 0;
}

int tcp_recv(int sock, void *buffer, size_t *size) {
    return tcp_recv_timeout(sock, buffer, size, TCP_IO_TIMEOUT);
}

int tcp_recv_timeout(int sock, void *buffer, size_t *size, uint32_t timeout_polls) {
    tcp_socket_t *socket = socket_from_handle(sock);
    if (socket == NULL || buffer == NULL || size == NULL) {
        return -1;
    }

    if (socket->state == TCP_CLOSED && socket->rx_size == 0u) {
        *size = 0u;
        return 0;
    }

    for (uint32_t i = 0; i < timeout_polls && socket->rx_size == 0; ++i) {
        (void)ethernet_poll();
    }

    if (socket->rx_size == 0) {
        if (socket->state == TCP_CLOSED) {
            *size = 0u;
            return 0;
        }
        return -1;
    }

    const size_t copy_size = *size < socket->rx_size ? *size : socket->rx_size;
    copy_bytes(buffer, socket->rx_buffer, copy_size);

    if (copy_size < socket->rx_size) {
        move_bytes(socket->rx_buffer, socket->rx_buffer + copy_size, socket->rx_size - copy_size);
    }

    socket->rx_size -= copy_size;
    *size = copy_size;
    return 0;
}

int tcp_can_read(int sock) {
    tcp_socket_t *socket = socket_from_handle(sock);
    if (socket == NULL) {
        return 0;
    }
    return socket->rx_size != 0u || socket->state == TCP_CLOSED;
}

int tcp_can_write(int sock) {
    tcp_socket_t *socket = socket_from_handle(sock);
    if (socket == NULL) {
        return 0;
    }
    return socket->state == TCP_ESTABLISHED;
}

int tcp_is_closed(int sock) {
    tcp_socket_t *socket = socket_from_handle(sock);
    if (socket == NULL) {
        return 1;
    }
    return socket->state == TCP_CLOSED;
}

void tcp_close(int sock) {
    tcp_socket_t *socket = socket_from_handle(sock);
    if (socket == NULL) {
        return;
    }

    if (socket->state == TCP_ESTABLISHED) {
        socket->state = TCP_FIN_WAIT;
        (void)tcp_send_segment(socket, TCP_FIN | TCP_ACK, NULL, 0);
        ++socket->send_seq;
        for (uint32_t i = 0; i < TCP_CLOSE_TIMEOUT; ++i) {
            (void)ethernet_poll();
            if (socket->send_unacked >= socket->send_seq) {
                break;
            }
        }
    }

    socket->state = TCP_CLOSED;
    kfree(socket->rx_buffer);
    socket->rx_buffer = NULL;
    socket->rx_size = 0;
    socket->used = 0;
}
