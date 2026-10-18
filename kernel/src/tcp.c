#include <unos/network_state.h>
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
#include <unos/irq.h>
#include <unos/scheduler.h>

#define TCP_DEBUG 0
#define TCP_MAX_CONNECTIONS 8u
#define TCP_BUFFER_SIZE 65536u
#define TCP_MAX_SEGMENT_SIZE 1460u
#define TCP_INITIAL_CWND_SEGMENTS 10u
#define TCP_INITIAL_RTO_TICKS 100u
#define TCP_MAX_RTO_TICKS 6000u
#define TCP_MIN_SSTHRESH (2u * TCP_MAX_SEGMENT_SIZE)
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
    uint8_t *tx_buffer;
    size_t tx_size,tx_inflight;
    uint32_t tx_sequence,retries;
    uint32_t congestion_window, slow_start_threshold, retransmit_ticks;
    uint8_t duplicate_acks, fast_retransmit_pending;
    uint64_t last_send_tick,connect_tick;
    uint8_t syn_sent,reset,timeout,closing;

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
            sockets[i]=(tcp_socket_t){0};
            sockets[i].used = 1;
            sockets[i].state = TCP_CLOSED;
            sockets[i].rx_size = 0;
            sockets[i].remote_window = TCP_WINDOW_DEFAULT;
            sockets[i].congestion_window = TCP_INITIAL_CWND_SEGMENTS * TCP_MAX_SEGMENT_SIZE;
            sockets[i].slow_start_threshold = TCP_WINDOW_DEFAULT;
            sockets[i].retransmit_ticks = TCP_INITIAL_RTO_TICKS;
            sockets[i].rx_buffer = kmalloc(TCP_BUFFER_SIZE);
            if (sockets[i].rx_buffer == NULL) {
                sockets[i].used = 0;
                return NULL;
            }
            sockets[i].tx_buffer=kmalloc(TCP_BUFFER_SIZE);
            if(!sockets[i].tx_buffer){kfree(sockets[i].rx_buffer);sockets[i].rx_buffer=NULL;sockets[i].used=0;return NULL;}
            return &sockets[i];
        }
    }
    return NULL;
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

static void tcp_handle_ipv4(uint32_t src_ip,uint32_t dest_ip,const void *payload,size_t size) {
    if (payload == NULL || size < sizeof(tcp_header_t)) {
        return;
    }

    if(size>sizeof(tcp_header_t)+TCP_MAX_SEGMENT_SIZE || tcp_checksum(src_ip,dest_ip,payload,size)!=0)return;
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
        socket->reset=1;
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

    if ((socket->state == TCP_ESTABLISHED || socket->state == TCP_FIN_WAIT) &&
        (header->flags & TCP_ACK) && (int32_t)(ack-socket->send_unacked)>0 &&
        (int32_t)(socket->send_seq-ack)>=0) {
        const uint32_t acknowledged = ack - socket->send_unacked;
        socket->send_unacked=ack;
        if(socket->tx_inflight && (int32_t)(ack-socket->tx_sequence)>0){
            size_t count=(uint32_t)(ack-socket->tx_sequence);
            if(count>socket->tx_inflight)count=socket->tx_inflight;
            move_bytes(socket->tx_buffer,socket->tx_buffer+count,socket->tx_size-count);
            socket->tx_size-=count;socket->tx_inflight-=count;
            socket->tx_sequence+=(uint32_t)count;socket->retries=0;
        }
        if (acknowledged != 0u) {
            if (socket->tx_inflight == 0u) socket->tx_sequence = ack;
            socket->duplicate_acks = 0u;
            socket->fast_retransmit_pending = 0u;
            socket->last_send_tick = irq_timer_ticks();
            socket->retransmit_ticks = TCP_INITIAL_RTO_TICKS;
            if (socket->congestion_window < socket->slow_start_threshold) {
                socket->congestion_window += acknowledged;
            } else {
                uint64_t increase = (uint64_t)TCP_MAX_SEGMENT_SIZE * acknowledged /
                                    socket->congestion_window;
                if (increase == 0u) increase = 1u;
                socket->congestion_window += (uint32_t)increase;
            }
            if (socket->congestion_window > TCP_WINDOW_DEFAULT)
                socket->congestion_window = TCP_WINDOW_DEFAULT;
        }
    } else if (socket->state == TCP_ESTABLISHED &&
               (header->flags & TCP_ACK) != 0u && data_size == 0u &&
               ack == socket->send_unacked && socket->tx_inflight != 0u) {
        if (socket->duplicate_acks < 255u) ++socket->duplicate_acks;
        if (socket->duplicate_acks == 3u) {
            uint32_t flight = socket->send_seq - socket->send_unacked;
            socket->slow_start_threshold = flight / 2u;
            if (socket->slow_start_threshold < TCP_MIN_SSTHRESH)
                socket->slow_start_threshold = TCP_MIN_SSTHRESH;
            socket->congestion_window = TCP_MAX_SEGMENT_SIZE;
            socket->fast_retransmit_pending = 1u;
        }
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

    if ((header->flags & TCP_FIN) != 0 && seq+(uint32_t)data_size==socket->recv_seq) {
        tty_puts("tcp: rx fin\n");
        socket->recv_seq += 1u;
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

static uint64_t tcp_lock(void){
#ifdef VEX_NETWORK_HOST_TEST
    return 0;
#else
    return network_state_lock();
#endif
}
static void tcp_unlock(uint64_t f){
#ifdef VEX_NETWORK_HOST_TEST
    (void)f;
#else
    network_state_unlock(f);
#endif
}
int tcp_connect_start(uint32_t ip,uint16_t port) {
    uint64_t flags=tcp_lock();tcp_init_once();
    if(!ip || !port || !ipv4_get_local_ip()){tcp_unlock(flags);return -1;}
    tcp_socket_t *s=alloc_socket();if(!s){tcp_unlock(flags);return -1;}
    s->remote_ip=ip;s->remote_port=port;s->local_port=next_local_port++;
    s->send_seq=s->send_unacked=next_initial_seq;next_initial_seq+=0x10000u;
    s->state=TCP_SYN_SENT;s->connect_tick=irq_timer_ticks();
    int handle=socket_index(s);tcp_unlock(flags);ethernet_wake();return handle;
}
int tcp_connection_status(int handle){
    uint64_t flags=tcp_lock();tcp_socket_t *s=socket_from_handle(handle);
    int result=!s || s->state==TCP_CLOSED?-1:s->state==TCP_SYN_SENT?0:1;
    tcp_unlock(flags);return result;
}
int tcp_connection_error(int handle){
    tcp_socket_t *s=socket_from_handle(handle);
    return !s?1:s->timeout?2:s->reset?1:0;
}
/* Called by the native network worker. Keep a bounded sliding window in flight
 * instead of waiting for an ACK after every 1460-byte segment. This is the
 * difference between stop-and-wait throughput (one MSS per RTT) and a usable
 * TCP stream. ACKs advance the oldest unacknowledged byte and grow the window;
 * loss recovery retransmits only that oldest segment under strict W^X-safe,
 * ordinary network semantics. */
int tcp_service(void){
    uint64_t flags=tcp_lock(),now=irq_timer_ticks();int pending=0;
    for(unsigned i=0;i<TCP_MAX_CONNECTIONS;i++){
        tcp_socket_t *s=&sockets[i];if(!s->used)continue;
        if(s->closing && (s->state==TCP_CLOSED ||
           (s->state==TCP_FIN_WAIT && ((int32_t)(s->send_unacked-s->send_seq)>=0 || now-s->last_send_tick>=200)))){
            kfree(s->rx_buffer);kfree(s->tx_buffer);*s=(tcp_socket_t){0};continue;
        }
        if(s->closing)pending=1;
        if(s->closing && s->state==TCP_ESTABLISHED && !s->tx_size){
            (void)tcp_send_segment(s,TCP_FIN|TCP_ACK,NULL,0);s->send_seq++;
            s->state=TCP_FIN_WAIT;s->last_send_tick=now;continue;
        }
        if(s->state==TCP_SYN_SENT){
            pending=1;
            if(now-s->connect_tick>=1000){s->state=TCP_CLOSED;s->timeout=1;continue;}
            uint32_t hop=s->remote_ip,mask=dhcp_netmask();uint8_t mac[6];
            if(mask && (hop&mask)!=(ipv4_get_local_ip()&mask))hop=dhcp_gateway();
            if(arp_lookup(hop,mac)){
                if(!s->retries || now-s->last_send_tick>=100){arp_request(hop);s->last_send_tick=now;s->retries++;}
                continue;
            }
            if(!s->syn_sent || now-s->last_send_tick>=100){
                uint32_t seq=s->send_seq;if(s->syn_sent)s->send_seq--;
                int result=tcp_send_segment(s,TCP_SYN,NULL,0);
                s->send_seq=seq;
                if(!result){if(!s->syn_sent)s->send_seq++;s->syn_sent=1;}
                s->last_send_tick=now;
            }
        }else if(s->state==TCP_ESTABLISHED && s->tx_size){
            pending=1;
            uint32_t flight = s->send_seq - s->send_unacked;
            const int timed_out = s->tx_inflight != 0u &&
                now - s->last_send_tick >= s->retransmit_ticks;
            if (s->fast_retransmit_pending || timed_out) {
                if (timed_out) {
                    if (s->retries >= 5u) {
                        s->state=TCP_CLOSED;s->timeout=1u;continue;
                    }
                    s->slow_start_threshold = flight / 2u;
                    if (s->slow_start_threshold < TCP_MIN_SSTHRESH)
                        s->slow_start_threshold = TCP_MIN_SSTHRESH;
                    s->congestion_window = TCP_MAX_SEGMENT_SIZE;
                    s->duplicate_acks = 0u;
                    ++s->retries;
                    if (s->retransmit_ticks < TCP_MAX_RTO_TICKS / 2u)
                        s->retransmit_ticks *= 2u;
                    else s->retransmit_ticks = TCP_MAX_RTO_TICKS;
                } else {
                    s->fast_retransmit_pending = 0u;
                }
                size_t n=s->tx_inflight;
                if(n>TCP_MAX_SEGMENT_SIZE)n=TCP_MAX_SEGMENT_SIZE;
                uint32_t seq=s->send_seq;s->send_seq=s->tx_sequence;
                int result=tcp_send_segment(s,TCP_PSH|TCP_ACK,s->tx_buffer,n);
                s->send_seq=seq;
                if(result==0)s->last_send_tick=now;
                continue;
            }

            uint32_t window=s->congestion_window;
            if(window>s->remote_window)window=s->remote_window;
            while(flight<window){
                size_t unsent=s->tx_size-s->tx_inflight;
                size_t allowance=(size_t)(window-flight);
                if(unsent>TCP_MAX_SEGMENT_SIZE)unsent=TCP_MAX_SEGMENT_SIZE;
                if(unsent>allowance)unsent=allowance;
                if(unsent==0u)break;
                if(s->tx_inflight==0u)s->tx_sequence=s->send_unacked;
                uint32_t seq=s->send_seq;
                int result=tcp_send_segment(s,TCP_PSH|TCP_ACK,
                                            s->tx_buffer+s->tx_inflight,unsent);
                if(result!=0){
                    /* Preserve sequence and queued bytes; retry after the next
                     * network event/timer rather than reporting a false send. */
                    s->send_seq=seq;
                    break;
                }
                s->send_seq+=(uint32_t)unsent;
                s->tx_inflight+=unsent;
                flight+=(uint32_t)unsent;
                if(s->tx_inflight==unsent){
                    s->last_send_tick=now;s->retries=0u;
                    s->retransmit_ticks=TCP_INITIAL_RTO_TICKS;
                }
            }
        }
    }
    tcp_unlock(flags);return pending;
}
int64_t tcp_try_write(int handle,const void *data,size_t size){
    uint64_t flags=tcp_lock();tcp_socket_t *s=socket_from_handle(handle);
    if(!s || s->state!=TCP_ESTABLISHED){tcp_unlock(flags);return -1;}
    if(!size){tcp_unlock(flags);return 0;}
    size_t n=TCP_BUFFER_SIZE-s->tx_size;if(n>size)n=size;
    if(!n){tcp_unlock(flags);return -2;}
    copy_bytes(s->tx_buffer+s->tx_size,data,n);s->tx_size+=n;
    tcp_unlock(flags);ethernet_wake();return (int64_t)n;
}
int64_t tcp_try_read(int handle,void *data,size_t size){
    uint64_t flags=tcp_lock();tcp_socket_t *s=socket_from_handle(handle);
    if(!s){tcp_unlock(flags);return -1;}
    if(!size){tcp_unlock(flags);return 0;}
    if(!s->rx_size){int result=s->state==TCP_CLOSED?(s->reset||s->timeout?-1:0):-2;tcp_unlock(flags);return result;}
    size_t n=s->rx_size;if(n>size)n=size;
    copy_bytes(data,s->rx_buffer,n);move_bytes(s->rx_buffer,s->rx_buffer+n,s->rx_size-n);s->rx_size-=n;
    /* Consumption reopens the advertised receive window. */
    if(s->state==TCP_ESTABLISHED)(void)tcp_send_segment(s,TCP_ACK,NULL,0);
    tcp_unlock(flags);return (int64_t)n;
}
int tcp_connect(uint32_t ip,uint16_t port){
    int handle=tcp_connect_start(ip,port);if(handle<0)return -1;
    while(tcp_connection_status(handle)==0){ethernet_poll();tcp_service();process_sleep_ticks(1);}
    if(tcp_connection_status(handle)<0){tcp_close(handle);return -1;}
    return handle;
}
int tcp_send_timeout(int handle,const void *data,size_t size,uint32_t timeout_polls){
    uint64_t start=irq_timer_ticks(),limit=((uint64_t)timeout_polls+999u)/1000u;if(!limit)limit=1;
    size_t sent=0;
    while(sent<size){int64_t n=tcp_try_write(handle,(const uint8_t *)data+sent,size-sent);
        if(n>=0)sent+=(size_t)n;else if(n!=-2)return -1;
        if(sent<size){if(irq_timer_ticks()-start>=limit)return -1;ethernet_poll();tcp_service();process_sleep_ticks(1);}}
    for(;;){tcp_socket_t *s=socket_from_handle(handle);if(!s || s->state!=TCP_ESTABLISHED)return -1;
        if(!s->tx_size)return 0;if(irq_timer_ticks()-start>=limit)return -1;
        ethernet_poll();tcp_service();process_sleep_ticks(1);}
}
int tcp_send(int handle,const void *data,size_t size){return tcp_send_timeout(handle,data,size,TCP_IO_TIMEOUT);}
int tcp_recv_timeout(int handle,void *data,size_t *size,uint32_t timeout_polls){
    uint64_t start=irq_timer_ticks(),limit=((uint64_t)timeout_polls+999u)/1000u;if(!limit)limit=1;
    for(;;){int64_t n=tcp_try_read(handle,data,*size);if(n>=0){*size=(size_t)n;return 0;}
        if(n!=-2 || irq_timer_ticks()-start>=limit)return -1;
        ethernet_poll();tcp_service();process_sleep_ticks(1);}
}
int tcp_recv(int handle,void *data,size_t *size){return tcp_recv_timeout(handle,data,size,TCP_IO_TIMEOUT);}

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
    return socket->state == TCP_ESTABLISHED && socket->tx_size<TCP_BUFFER_SIZE;
}

int tcp_is_closed(int sock) {
    tcp_socket_t *socket = socket_from_handle(sock);
    if (socket == NULL) {
        return 1;
    }
    return socket->state == TCP_CLOSED;
}

void tcp_close(int sock) {
    uint64_t flags=tcp_lock();tcp_socket_t *s=socket_from_handle(sock);
    if(s){
        if(s->state==TCP_CLOSED || s->state==TCP_SYN_SENT){
            kfree(s->rx_buffer);kfree(s->tx_buffer);*s=(tcp_socket_t){0};
        }else s->closing=1;
    }
    tcp_unlock(flags);ethernet_wake();
}

uint16_t tcp_local_port(int sock){tcp_socket_t *s=socket_from_handle(sock);return s?s->local_port:0;}
