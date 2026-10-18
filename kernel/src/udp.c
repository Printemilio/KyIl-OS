#include <unos/network_state.h>
#include <unos/udp.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/heap.h>
#include <unos/ipv4.h>
#include <unos/tty.h>
#include <unos/ethernet.h>
#include <unos/irq.h>
#include <unos/debug_log.h>
#include <string.h>

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

typedef struct {uint32_t ip;uint16_t port,size;uint8_t bytes[UDP_MAX_DATAGRAM];} udp_packet_t;
typedef struct {
    uint16_t port;
    udp_handler_t handler;
    uint8_t *buffer;
    size_t size;
    uint32_t src_ip;
    uint16_t src_port;
    int has_data;
    udp_packet_t *queue;
    unsigned head,tail,count;
    udp_packet_t *tx;
    uint64_t tx_started;
    uint32_t peer_ip;uint16_t peer_port;
    uint8_t endpoint,tx_pending,error;
    uint8_t fault_reporting, fault_head, fault_tail, fault_count;
    udp_transport_fault_t faults[4];
    udp_transport_fault_t pending_fault;
    uint8_t fault_status;
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
            sockets[i].buffer = kmalloc(UDP_SOCKET_BUFFER_SIZE);
            if(!sockets[i].buffer)return NULL;
            sockets[i].port = port;
            sockets[i].size = 0;
            sockets[i].has_data = 0;
            return sockets[i].buffer == NULL ? NULL : &sockets[i];
        }
    }

    return NULL;
}

static void udp_handle_ipv4(uint32_t src_ip,uint32_t dest_ip,const void *payload,size_t size) {
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

    uint16_t verified=header->checksum?udp_checksum(src_ip,dest_ip,payload,length):0xffffu;
    if(verified!=0xffffu)return;
    udp_socket_t *socket = find_socket(dest_port);
    if (socket == NULL) {
        return;
    }

    const uint8_t *data = (const uint8_t *)payload + UDP_HEADER_SIZE;
    const size_t data_size = length - UDP_HEADER_SIZE;

    if(socket->endpoint) {
        if(socket->peer_ip && (socket->peer_ip!=src_ip || socket->peer_port!=src_port))return;
        if(data_size>UDP_MAX_DATAGRAM || socket->count==8)return;
        udp_packet_t *p=&socket->queue[socket->head];p->ip=src_ip;p->port=src_port;p->size=data_size;
        memcpy(p->bytes,data,data_size);socket->head=(socket->head+1)%8;socket->count++;
    }
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

static int udp_send_inner(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, const void *data, size_t size) {
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

int udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, const void *data, size_t size) {
    uint64_t flags=0;
#ifndef VEX_NETWORK_HOST_TEST
    flags=network_state_lock();
#endif
    int result=udp_send_inner(dest_ip,src_port,dest_port,data,size);
#ifndef VEX_NETWORK_HOST_TEST
    network_state_unlock(flags);
#else
    (void)flags;
#endif
    return result;
}

static uint64_t udp_lock(void){
#ifdef VEX_NETWORK_HOST_TEST
    return 0;
#else
    return network_state_lock();
#endif
}
static void udp_unlock(uint64_t f){
#ifndef VEX_NETWORK_HOST_TEST
    network_state_unlock(f);
#else
    (void)f;
#endif
}
static udp_socket_t *udp_endpoint(int h){return h>=0 && (unsigned)h<UDP_SOCKET_COUNT && sockets[h].endpoint?&sockets[h]:NULL;}
int udp_endpoint_open(uint16_t port){
    uint64_t f=udp_lock();static uint16_t next_port=49152;
    if(!port){unsigned tries=0;do{port=next_port++;if(next_port<49152)next_port=49152;}while(find_socket(port) && ++tries<16384);}
    if(find_socket(port)){udp_unlock(f);return -98;}
    for(unsigned i=0;i<UDP_SOCKET_COUNT;i++)if(!sockets[i].port){
        udp_packet_t *q=kmalloc(9*sizeof(*q));if(!q){udp_unlock(f);return -12;}
        sockets[i]=(udp_socket_t){.port=port,.queue=q,.tx=q+8,.endpoint=1};udp_unlock(f);return i;
    }
    udp_unlock(f);return -24;
}
int udp_endpoint_peer(int h,uint32_t ip,uint16_t port){uint64_t f=udp_lock();udp_socket_t *s=udp_endpoint(h);if(s){s->peer_ip=ip;s->peer_port=port;}udp_unlock(f);return s?0:-9;}
uint16_t udp_endpoint_port(int h){udp_socket_t *s=udp_endpoint(h);return s?s->port:0;}
int udp_endpoint_send(int h,uint32_t ip,uint16_t port,const void *data,size_t size){
    if(size>UDP_MAX_DATAGRAM)return -90;
    uint64_t f=udp_lock();udp_socket_t *s=udp_endpoint(h);if(!s){udp_unlock(f);return -9;}
    if(s->tx_pending){udp_unlock(f);return -11;}
    s->tx->ip=ip;s->tx->port=port;s->tx->size=size;memcpy(s->tx->bytes,data,size);
    s->tx_pending=1;s->tx_started=irq_timer_ticks();udp_unlock(f);ethernet_wake();return size;
}
int udp_endpoint_receive(int h,void *data,size_t size,uint32_t *ip,uint16_t *port,int peek,int truncated){
    uint64_t f=udp_lock();udp_socket_t *s=udp_endpoint(h);if(!s){udp_unlock(f);return -9;}
    if(!s->count){udp_unlock(f);return -11;}
    udp_packet_t *p=&s->queue[s->tail];size_t n=size<p->size?size:p->size;
    memcpy(data,p->bytes,n);if(ip)*ip=p->ip;if(port)*port=p->port;int result=truncated?p->size:n;
    if(!peek){s->tail=(s->tail+1)%8;s->count--;}udp_unlock(f);return result;
}
int udp_endpoint_readable(int h){udp_socket_t *s=udp_endpoint(h);return s && s->count;}
int udp_endpoint_writable(int h){udp_socket_t *s=udp_endpoint(h);return s && !s->tx_pending;}
int udp_endpoint_error(int h){udp_socket_t *s=udp_endpoint(h);int e=s?s->error:9;if(s)s->error=0;return e;}
int udp_endpoint_fault_reporting(int h,int enabled){uint64_t f=udp_lock();udp_socket_t *s=udp_endpoint(h);if(s){s->fault_reporting=enabled!=0;if(!enabled)s->fault_count=s->fault_head=s->fault_tail=s->fault_status=0;}udp_unlock(f);return s?0:-9;}
int udp_endpoint_fault_pending(int h){udp_socket_t *s=udp_endpoint(h);return s && (s->fault_count || s->fault_status);}
int udp_endpoint_fault_status(int h,udp_transport_fault_t *fault,int clear){
    uint64_t f=udp_lock();udp_socket_t *s=udp_endpoint(h);int present=s && s->fault_status;
    if(present){*fault=s->pending_fault;if(clear)s->fault_status=0;}udp_unlock(f);return present;
}
int udp_endpoint_fault_receive(int h,udp_transport_fault_t *fault){
    uint64_t f=udp_lock();udp_socket_t *s=udp_endpoint(h);
    int result=!s?-9:(!s->fault_count?-11:0);
    if(!result){*fault=s->faults[s->fault_tail];s->fault_tail=(s->fault_tail+1)%4;s->fault_count--;
        s->fault_status=s->fault_count!=0;if(s->fault_count)s->pending_fault=s->faults[s->fault_tail];}
    udp_unlock(f);return result;
}
void udp_transport_fault(uint32_t reporter,uint8_t type,uint8_t code,uint32_t detail,const void *packet,size_t size){
    const uint8_t *p=packet;
    if(!p || size<28 || p[0]>>4!=4 || p[9]!=17)return;
    size_t header=(p[0]&15)*4u;
    if(header<20 || header>size-8 || checksum16(p,header))return;
    uint32_t src=((uint32_t)p[12]<<24)|((uint32_t)p[13]<<16)|((uint32_t)p[14]<<8)|p[15];
    uint32_t dst=((uint32_t)p[16]<<24)|((uint32_t)p[17]<<16)|((uint32_t)p[18]<<8)|p[19];
    uint16_t sport=((uint16_t)p[header]<<8)|p[header+1],dport=((uint16_t)p[header+2]<<8)|p[header+3];
    uint16_t total=((uint16_t)p[2]<<8)|p[3],ulen=((uint16_t)p[header+4]<<8)|p[header+5];
    if(src!=ipv4_get_local_ip() || total<header+8 || ulen<8 || ulen>total-header || (p[6]&0x3f) || p[7])return;
    if(type!=3 && type!=11 && type!=12)return;
    uint64_t f=udp_lock();udp_socket_t *s=find_socket(sport);
    if(s && s->endpoint && (s->fault_reporting || s->peer_ip) &&
       (!s->peer_ip || (s->peer_ip==dst && s->peer_port==dport))){
        udp_transport_fault_t *e=&s->pending_fault;*e=(udp_transport_fault_t){.reporter_ip=reporter,.destination_ip=dst,.destination_port=dport,.type=type,.code=code,.detail=detail};
        size_t n=size-header-8;if(n>ulen-8u)n=ulen-8u;if(n>sizeof(e->payload))n=sizeof(e->payload);
        e->payload_size=n;memcpy(e->payload,p+header+8,n);s->fault_status=1;
        if(s->fault_reporting && s->fault_count<4){s->faults[s->fault_head]=*e;s->fault_head=(s->fault_head+1)%4;s->fault_count++;}
    }udp_unlock(f);
}
void udp_endpoint_close(int h){uint64_t f=udp_lock();udp_socket_t *s=udp_endpoint(h);if(s){kfree(s->queue);*s=(udp_socket_t){0};}udp_unlock(f);}
int udp_service(void){uint64_t f=udp_lock();int pending=0;
    for(unsigned i=0;i<UDP_SOCKET_COUNT;i++){udp_socket_t *s=&sockets[i];if(!s->endpoint || !s->tx_pending)continue;
        if(!udp_send(s->tx->ip,s->port,s->tx->port,s->tx->bytes,s->tx->size)){
#if VEX_BROWSER_DIAGNOSTIC
            static unsigned sent;if(sent++<16)debug_log_puts("native UDP datagram sent\n");
#endif
            s->tx_pending=0;
        }
        else if(irq_timer_ticks()-s->tx_started>=1000){s->tx_pending=0;s->error=110;}
        else pending=1;
    }udp_unlock(f);return pending;
}
