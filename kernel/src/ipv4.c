#include <unos/ipv4.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/arp.h>
#include <unos/dhcp.h>
#include <unos/ethernet.h>
#include <unos/tty.h>
#include <unos/udp.h>

#define IPV4_HEADER_MIN_SIZE 20u
#define IPV4_VERSION 4u
#define IPV4_TTL_DEFAULT 64u
#define IPV4_MAX_PACKET_SIZE 1500u
#define IPV4_HANDLER_COUNT 8u
#define ICMP_TYPE_ECHO_REPLY 0u
#define ICMP_TYPE_ECHO_REQUEST 8u
#define VBOX_NAT_GATEWAY_IP 0x0a000202u

typedef struct {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed)) ipv4_header_t;

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} __attribute__((packed)) icmp_echo_header_t;

typedef struct {
    uint8_t protocol;
    ipv4_handler_t handler;
} ipv4_handler_entry_t;

static uint32_t local_ip;
static uint16_t next_identification;
static ipv4_handler_entry_t handlers[IPV4_HANDLER_COUNT];
static uint8_t ipv4_tx_packet[IPV4_MAX_PACKET_SIZE];
static uint8_t icmp_reply_payload[IPV4_MAX_PACKET_SIZE - IPV4_HEADER_MIN_SIZE];

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

static ipv4_handler_t find_handler(uint8_t protocol) {
    for (size_t i = 0; i < IPV4_HANDLER_COUNT; ++i) {
        if (handlers[i].handler != NULL && handlers[i].protocol == protocol) {
            return handlers[i].handler;
        }
    }

    return NULL;
}

static void icmp_handle(uint32_t src_ip,uint32_t dest_ip,const void *payload,size_t size) {
    (void)dest_ip;
    if (size < sizeof(icmp_echo_header_t)) {
        return;
    }

    const icmp_echo_header_t *request = payload;
    if(checksum16(payload,size)!=0)return;
    if(request->type==3 || request->type==11 || request->type==12){
        const uint8_t *p=payload;
        uint32_t detail=request->type==3 && request->code==4?((uint32_t)p[6]<<8)|p[7]:0;
        udp_transport_fault(src_ip,request->type,request->code,detail,p+8,size-8);
        return;
    }
    if (request->type != ICMP_TYPE_ECHO_REQUEST || request->code != 0) {
        return;
    }

    if (size > sizeof(icmp_reply_payload)) {
        return;
    }

    copy_bytes(icmp_reply_payload, payload, size);
    icmp_echo_header_t *reply_header = (icmp_echo_header_t *)icmp_reply_payload;
    reply_header->type = ICMP_TYPE_ECHO_REPLY;
    reply_header->code = 0;
    reply_header->checksum = 0;
    reply_header->checksum = bswap16(checksum16(icmp_reply_payload, size));

    (void)ipv4_send(src_ip, IPV4_PROTOCOL_ICMP, icmp_reply_payload, size);
}

static void ipv4_handle_ethernet(const uint8_t *src_mac, const void *payload, size_t size) {
    (void)src_mac;

    if (size < IPV4_HEADER_MIN_SIZE) {
        return;
    }

    const ipv4_header_t *header = payload;
    const uint8_t version = header->version_ihl >> 4u;
    const uint8_t ihl = header->version_ihl & 0x0fu;
    const size_t header_size = (size_t)ihl * 4u;
    const uint16_t total_length = bswap16(header->total_length);
    const uint16_t fragment = bswap16(header->flags_fragment);

    if (version != IPV4_VERSION ||
        header_size < IPV4_HEADER_MIN_SIZE ||
        total_length < header_size ||
        total_length > size ||
        checksum16(payload, header_size) != 0) {
        return;
    }

    if (local_ip != 0 && bswap32(header->dest_ip) != local_ip && header->dest_ip != 0xffffffffu) {
        return;
    }

    /* Fragment reassembly is intentionally deferred. Non-initial or fragmented
       packets are dropped until the fragment cache exists. */
    if ((fragment & 0x3fffu) != 0) {
        return;
    }

    ipv4_handler_t handler = find_handler(header->protocol);
    if (handler == NULL) {
        return;
    }

    handler(bswap32(header->src_ip),bswap32(header->dest_ip),(const uint8_t *)payload+header_size,total_length-header_size);
}

void ipv4_init(void) {
    local_ip = 0;
    next_identification = 1;

    for (size_t i = 0; i < IPV4_HANDLER_COUNT; ++i) {
        handlers[i].protocol = 0;
        handlers[i].handler = NULL;
    }

    ethernet_register_handler(ETHERNET_TYPE_IPV4, ipv4_handle_ethernet);
    ipv4_register_handler(IPV4_PROTOCOL_ICMP, icmp_handle);
}

void ipv4_set_local_ip(uint32_t ip) {
    local_ip = ip;
    arp_set_local_ip(ip);
}

uint32_t ipv4_get_local_ip(void) {
    return local_ip;
}

static int ipv4_send_inner(uint32_t dest_ip, uint8_t protocol, const void *payload, size_t size) {
    if (payload == NULL || size + IPV4_HEADER_MIN_SIZE > IPV4_MAX_PACKET_SIZE) {
        return -1;
    }

    uint8_t dest_mac[6];
    if (dest_ip == 0xffffffffu) {
        for (size_t i = 0; i < 6u; ++i) {
            dest_mac[i] = 0xffu;
        }
    } else {
        uint32_t netmask = dhcp_netmask();
        uint32_t next_hop = dest_ip;

        if (netmask == 0 && (local_ip & 0xffffff00u) == 0x0a000200u) {
            netmask = 0xffffff00u;
        }

        if (netmask != 0 && (dest_ip & netmask) != (local_ip & netmask)) {
            next_hop = dhcp_gateway();
            if (next_hop == 0) {
                next_hop = VBOX_NAT_GATEWAY_IP;
            }
        }

        if (local_ip == 0 || arp_resolve(next_hop, dest_mac) != 0) {
            if (next_hop != VBOX_NAT_GATEWAY_IP && arp_resolve(VBOX_NAT_GATEWAY_IP, dest_mac) == 0) {
                next_hop = VBOX_NAT_GATEWAY_IP;
            } else {
                return -1;
            }
        }
        if (next_hop == 0) {
            return -1;
        }
    }

    ipv4_header_t *header = (ipv4_header_t *)ipv4_tx_packet;

    header->version_ihl = (IPV4_VERSION << 4u) | 5u;
    header->dscp_ecn = 0;
    header->total_length = bswap16((uint16_t)(IPV4_HEADER_MIN_SIZE + size));
    header->identification = bswap16(next_identification++);
    header->flags_fragment = bswap16(0x4000u);
    header->ttl = IPV4_TTL_DEFAULT;
    header->protocol = protocol;
    header->header_checksum = 0;
    header->src_ip = bswap32(local_ip);
    header->dest_ip = bswap32(dest_ip);
    header->header_checksum = bswap16(checksum16(header, IPV4_HEADER_MIN_SIZE));

    copy_bytes(ipv4_tx_packet + IPV4_HEADER_MIN_SIZE, payload, size);
    const int result = ethernet_send(dest_mac, ETHERNET_TYPE_IPV4, ipv4_tx_packet, IPV4_HEADER_MIN_SIZE + size);
    return result;
}

void ipv4_register_handler(uint8_t protocol, ipv4_handler_t handler) {
    for (size_t i = 0; i < IPV4_HANDLER_COUNT; ++i) {
        if (handlers[i].handler != NULL && handlers[i].protocol == protocol) {
            handlers[i].handler = handler;
            return;
        }
    }

    for (size_t i = 0; i < IPV4_HANDLER_COUNT; ++i) {
        if (handlers[i].handler == NULL) {
            handlers[i].protocol = protocol;
            handlers[i].handler = handler;
            return;
        }
    }
}

int ipv4_send(uint32_t dest_ip, uint8_t protocol, const void *payload, size_t size) {
    uint64_t flags;__asm__ volatile("pushfq; pop %0; cli":"=r"(flags)::"memory");
    int result=ipv4_send_inner(dest_ip,protocol,payload,size);
    __asm__ volatile("push %0; popfq"::"r"(flags):"memory","cc");return result;
}
