#include <unos/arp.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/ethernet.h>

#define ARP_CACHE_SIZE 32u
#define ARP_HTYPE_ETHERNET 0x0001u
#define ARP_PTYPE_IPV4 0x0800u
#define ARP_HLEN_ETHERNET 6u
#define ARP_PLEN_IPV4 4u
#define ARP_OP_REQUEST 1u
#define ARP_OP_REPLY 2u

typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint32_t spa;
    uint8_t tha[6];
    uint32_t tpa;
} __attribute__((packed)) arp_packet_t;

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    int used;
} arp_entry_t;

static arp_entry_t cache[ARP_CACHE_SIZE];
static uint32_t local_ip;

static uint16_t bswap16(uint16_t value) {
    return (uint16_t)((value << 8u) | (value >> 8u));
}

static uint32_t bswap32(uint32_t value) {
    return ((value & 0x000000ffu) << 24u)
        | ((value & 0x0000ff00u) << 8u)
        | ((value & 0x00ff0000u) >> 8u)
        | ((value & 0xff000000u) >> 24u);
}

static void copy_mac(uint8_t dst[6], const uint8_t src[6]) {
    for (size_t i = 0; i < 6u; ++i) {
        dst[i] = src[i];
    }
}

static void zero_mac(uint8_t mac[6]) {
    for (size_t i = 0; i < 6u; ++i) {
        mac[i] = 0;
    }
}

static int mac_is_zero(const uint8_t mac[6]) {
    for (size_t i = 0; i < 6u; ++i) {
        if (mac[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void cache_store(uint32_t ip, const uint8_t mac[6]) {
    arp_entry_t *free_entry = NULL;

    for (size_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (cache[i].used && cache[i].ip == ip) {
            copy_mac(cache[i].mac, mac);
            return;
        }
        if (!cache[i].used && free_entry == NULL) {
            free_entry = &cache[i];
        }
    }

    if (free_entry == NULL) {
        free_entry = &cache[0];
    }

    free_entry->used = 1;
    free_entry->ip = ip;
    copy_mac(free_entry->mac, mac);
}

static void arp_send(uint16_t op, const uint8_t dest_mac[6], uint32_t target_ip, const uint8_t target_mac[6]) {
    arp_packet_t packet;
    const uint8_t *local_mac = ethernet_mac();

    packet.htype = bswap16(ARP_HTYPE_ETHERNET);
    packet.ptype = bswap16(ARP_PTYPE_IPV4);
    packet.hlen = ARP_HLEN_ETHERNET;
    packet.plen = ARP_PLEN_IPV4;
    packet.oper = bswap16(op);
    copy_mac(packet.sha, local_mac);
    packet.spa = bswap32(local_ip);
    copy_mac(packet.tha, target_mac);
    packet.tpa = bswap32(target_ip);

    (void)ethernet_send(dest_mac, ETHERNET_TYPE_ARP, &packet, sizeof(packet));
}

static void arp_handle(const uint8_t *src_mac, const void *payload, size_t size) {
    if (size < sizeof(arp_packet_t)) {
        return;
    }

    const arp_packet_t *packet = payload;
    if (bswap16(packet->htype) != ARP_HTYPE_ETHERNET ||
        bswap16(packet->ptype) != ARP_PTYPE_IPV4 ||
        packet->hlen != ARP_HLEN_ETHERNET ||
        packet->plen != ARP_PLEN_IPV4) {
        return;
    }

    const uint16_t op = bswap16(packet->oper);
    const uint32_t sender_ip = bswap32(packet->spa);
    const uint32_t target_ip = bswap32(packet->tpa);

    cache_store(sender_ip, packet->sha);

    if (op == ARP_OP_REQUEST && local_ip != 0 && target_ip == local_ip) {
        arp_send(ARP_OP_REPLY, src_mac, sender_ip, packet->sha);
    }
}

void arp_init(void) {
    local_ip = 0;

    for (size_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        cache[i].used = 0;
        cache[i].ip = 0;
        zero_mac(cache[i].mac);
    }

    ethernet_register_handler(ETHERNET_TYPE_ARP, arp_handle);
}

void arp_set_local_ip(uint32_t ip) {
    local_ip = ip;
}

int arp_resolve(uint32_t ip, uint8_t mac[6]) {
    if (mac == NULL) {
        return -1;
    }

    if (arp_lookup(ip, mac) == 0) {
        return 0;
    }

    (void)arp_request(ip);
    return -1;
}

int arp_lookup(uint32_t ip, uint8_t mac[6]) {
    if (mac == NULL) {
        return -1;
    }

    for (size_t i = 0; i < ARP_CACHE_SIZE; ++i) {
        if (cache[i].used && cache[i].ip == ip && !mac_is_zero(cache[i].mac)) {
            copy_mac(mac, cache[i].mac);
            return 0;
        }
    }
    return -1;
}

int arp_request(uint32_t target_ip) {
    static const uint8_t broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t zero[6];

    zero_mac(zero);
    arp_send(ARP_OP_REQUEST, broadcast, target_ip, zero);
    return 0;
}
