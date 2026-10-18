#include <unos/network_state.h>
#include <unos/dns.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/arp.h>
#include <unos/debug_log.h>
#include <unos/dhcp.h>
#include <unos/ethernet.h>
#include <unos/ipv4.h>
#include <unos/irq.h>
#include <unos/scheduler.h>
#include <unos/udp.h>

#define DNS_VBOX_NAT_IP 0x0a000203u
#define DNS_VBOX_GATEWAY_IP 0x0a000202u
#define DNS_SERVER_PORT 53u
#define DNS_CLIENT_PORT 53053u
#define DNS_QUERY_TIMEOUT_TICKS 500u
#define DNS_ARP_TIMEOUT_TICKS 500u
#define DNS_MAX_PACKET_SIZE 512u
#define DNS_TYPE_A 1u
#define DNS_CLASS_IN 1u
#define DNS_FLAG_QUERY_RECURSION_DESIRED 0x0100u
#define DNS_FLAG_RESPONSE_MASK 0x8000u
#define DNS_RCODE_MASK 0x000fu
#define DNS_PENDING_QUERY_COUNT 16u

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

typedef struct {
    uint16_t id;
    uint16_t reserved;
    uint32_t server_ip;
    uint32_t tid;
    uint32_t ip;
    volatile uint8_t done;
    int8_t result;
    uint8_t used;
} dns_pending_query_t;

static uint16_t transaction_id = 0x554eu;
static dns_pending_query_t pending_queries[DNS_PENDING_QUERY_COUNT];

static uint64_t dns_lock(void) {
#ifdef VEX_NETWORK_HOST_TEST
    return 0u;
#else
    return network_state_lock();
#endif
}

static void dns_unlock(uint64_t flags) {
#ifdef VEX_NETWORK_HOST_TEST
    (void)flags;
#else
    network_state_unlock(flags);
#endif
}

static uint16_t bswap16(uint16_t value) {
    return (uint16_t)((value << 8u) | (value >> 8u));
}

static size_t string_length(const char *str) {
    size_t length = 0;

    while (str != NULL && str[length] != '\0') {
        ++length;
    }

    return length;
}

static int string_equals(const char *left, const char *right) {
    size_t i = 0;

    if (left == NULL || right == NULL) {
        return 0;
    }

    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }

    return left[i] == '\0' && right[i] == '\0';
}

#define DNS_CACHE_SIZE 16u
typedef struct { char hostname[64]; uint32_t ip; } dns_cache_entry_t;
static dns_cache_entry_t dns_cache[DNS_CACHE_SIZE];
static size_t dns_cache_count;
static size_t dns_cache_next_slot;

static char dns_to_lower_ascii(char value) {
    if (value >= 'A' && value <= 'Z') {
        return (char)(value - 'A' + 'a');
    }
    return value;
}

static int dns_string_equals_case_insensitive(const char *left, const char *right) {
    size_t i = 0;

    if (left == NULL || right == NULL) {
        return 0;
    }

    while (left[i] != '\0' && right[i] != '\0') {
        if (dns_to_lower_ascii(left[i]) != dns_to_lower_ascii(right[i])) {
            return 0;
        }
        ++i;
    }

    return left[i] == '\0' && right[i] == '\0';
}

static int dns_cache_lookup(const char *hostname, uint32_t *ip) {
    const uint64_t flags = dns_lock();
    for (size_t i = 0; i < dns_cache_count; ++i) {
        if (dns_string_equals_case_insensitive(dns_cache[i].hostname, hostname)) {
            *ip = dns_cache[i].ip;
            dns_unlock(flags);
            return 0;
        }
    }
    dns_unlock(flags);
    return -1;
}

static void dns_cache_store(const char *hostname, uint32_t ip) {
    if (string_length(hostname) >= 64u) return;
    const uint64_t flags = dns_lock();
    size_t slot = dns_cache_count < DNS_CACHE_SIZE ? dns_cache_count++ : dns_cache_next_slot;
    const char *src = hostname;
    char *dst = dns_cache[slot].hostname;
    size_t i = 0;
    while (i < 63u && src[i] != '\0') { dst[i] = dns_to_lower_ascii(src[i]); ++i; }
    dst[i] = '\0';
    dns_cache[slot].ip = ip;
    if (dns_cache_count >= DNS_CACHE_SIZE) {
        dns_cache_next_slot = (dns_cache_next_slot + 1u) % DNS_CACHE_SIZE;
    }
    dns_unlock(flags);
}

static int dns_static_resolve(const char *hostname, uint32_t *ip) {
    if (string_equals(hostname, "api.duckduckgo.com") || string_equals(hostname, "duckduckgo.com")) {
        *ip = (52u << 24u) | (142u << 16u) | (124u << 8u) | 215u;
        return 0;
    }

    if (string_equals(hostname, "en.wikipedia.org") || string_equals(hostname, "dyna.wikimedia.org")) {
        *ip = (185u << 24u) | (15u << 16u) | (58u << 8u) | 224u;
        return 0;
    }

    return -1;
}

static int encode_qname(const char *hostname, uint8_t *packet, size_t *offset) {
    size_t label_start = 0;
    const size_t hostname_len = string_length(hostname);

    if (hostname_len == 0) {
        return -1;
    }

    while (label_start < hostname_len) {
        size_t label_len = 0;

        while (label_start + label_len < hostname_len && hostname[label_start + label_len] != '.') {
            ++label_len;
        }

        if (label_len == 0 || label_len > 63u || *offset + 1u + label_len >= DNS_MAX_PACKET_SIZE) {
            return -1;
        }

        packet[(*offset)++] = (uint8_t)label_len;
        for (size_t i = 0; i < label_len; ++i) {
            packet[(*offset)++] = (uint8_t)hostname[label_start + i];
        }

        label_start += label_len;
        if (label_start < hostname_len && hostname[label_start] == '.') {
            ++label_start;
        }
    }

    if (*offset >= DNS_MAX_PACKET_SIZE) {
        return -1;
    }

    packet[(*offset)++] = 0;
    return 0;
}

static int skip_name(const uint8_t *packet, size_t size, size_t *offset) {
    size_t current = *offset;

    while (current < size) {
        const uint8_t len = packet[current++];
        if (len == 0) {
            *offset = current;
            return 0;
        }

        if ((len & 0xc0u) == 0xc0u) {
            if (current >= size) {
                return -1;
            }
            *offset = current + 1u;
            return 0;
        }

        if ((len & 0xc0u) != 0 || current + len > size) {
            return -1;
        }

        current += len;
    }

    return -1;
}

static uint16_t read_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8u) | data[1]);
}

static uint32_t read_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24u)
        | ((uint32_t)data[1] << 16u)
        | ((uint32_t)data[2] << 8u)
        | (uint32_t)data[3];
}

static void dns_handle(uint32_t src_ip, uint16_t src_port, const void *data, size_t size) {
    if (src_port != DNS_SERVER_PORT || data == NULL || size < sizeof(dns_header_t)) {
        return;
    }

    const uint8_t *packet = data;
    const dns_header_t *header = data;
    const uint16_t id = bswap16(header->id);
    const uint16_t response_flags = bswap16(header->flags);
    if ((response_flags & DNS_FLAG_RESPONSE_MASK) == 0u) return;

    uint64_t flags = dns_lock();
    dns_pending_query_t *query = NULL;
    for (size_t i = 0; i < DNS_PENDING_QUERY_COUNT; ++i) {
        if (pending_queries[i].used && pending_queries[i].id == id &&
            pending_queries[i].server_ip == src_ip) {
            query = &pending_queries[i];
            break;
        }
    }
    if (query == NULL || query->done) { dns_unlock(flags); return; }
    if ((response_flags & DNS_RCODE_MASK) != 0u) {
        query->result = -1;
        query->done = 1u;
        process_wake(query->tid);
        dns_unlock(flags);
        return;
    }

    size_t offset = sizeof(dns_header_t);
    const uint16_t qdcount = bswap16(header->qdcount);
    const uint16_t ancount = bswap16(header->ancount);

    for (uint16_t i = 0; i < qdcount; ++i) {
        if (skip_name(packet, size, &offset) != 0 || offset + 4u > size) {
            dns_unlock(flags);
            return;
        }
        offset += 4u;
    }

    for (uint16_t i = 0; i < ancount; ++i) {
        if (skip_name(packet, size, &offset) != 0 || offset + 10u > size) {
            dns_unlock(flags);
            return;
        }

        const uint16_t type = read_be16(&packet[offset]);
        const uint16_t class_code = read_be16(&packet[offset + 2u]);
        const uint16_t rdlength = read_be16(&packet[offset + 8u]);
        offset += 10u;

        if (offset + rdlength > size) {
            dns_unlock(flags);
            return;
        }

        if (type == DNS_TYPE_A && class_code == DNS_CLASS_IN && rdlength == 4u) {
            query->ip = read_be32(&packet[offset]);
            query->result = 0;
            query->done = 1u;
            process_wake(query->tid);
            dns_unlock(flags);
            return;
        }

        offset += rdlength;
    }
    /* A syntactically valid answer without an A record is a real negative
     * result; do not leave the caller spinning until the global timeout. */
    query->result = -1;
    query->done = 1u;
    process_wake(query->tid);
    dns_unlock(flags);
}

static int wait_for_arp(uint32_t ip) {
    uint8_t mac[6];
    const uint32_t local_ip = ipv4_get_local_ip();
    uint32_t netmask = dhcp_netmask();
    uint32_t next_hop = ip;

    if (netmask == 0u && (local_ip & 0xffffff00u) == 0x0a000200u) {
        netmask = 0xffffff00u;
    }

    if (netmask != 0u && (ip & netmask) != (local_ip & netmask)) {
        next_hop = dhcp_gateway();
        if (next_hop == 0u) {
            next_hop = DNS_VBOX_GATEWAY_IP;
        }
    }

    if (arp_wait_for(next_hop, mac, DNS_ARP_TIMEOUT_TICKS) == 0) return 0;

    if (next_hop != DNS_VBOX_GATEWAY_IP) {
        return arp_wait_for(DNS_VBOX_GATEWAY_IP, mac, DNS_ARP_TIMEOUT_TICKS);
    }
    return -1;
}

static int dns_resolve_with_server(const char *hostname, uint32_t server_ip, uint32_t *ip) {
    if (server_ip == 0u || wait_for_arp(server_ip) != 0) {
        debug_log_puts("dns: arp/server unreachable\n");
        return -1;
    }

    uint8_t packet[DNS_MAX_PACKET_SIZE];
    size_t offset = sizeof(dns_header_t);
    dns_header_t *header = (dns_header_t *)packet;

    dns_pending_query_t *query = NULL;
    uint64_t flags = dns_lock();
    for (size_t i = 0; i < DNS_PENDING_QUERY_COUNT; ++i) {
        if (!pending_queries[i].used) { query = &pending_queries[i]; break; }
    }
    if (query == NULL) { dns_unlock(flags); return -1; }
    uint16_t id = transaction_id++;
    if (transaction_id == 0u) transaction_id = 1u;
    *query = (dns_pending_query_t){
        .id=id, .server_ip=server_ip, .tid=scheduler_current_tid(), .used=1u
    };
    dns_unlock(flags);

    header->id = bswap16(id);
    header->flags = bswap16(DNS_FLAG_QUERY_RECURSION_DESIRED);
    header->qdcount = bswap16(1);
    header->ancount = 0;
    header->nscount = 0;
    header->arcount = 0;

    if (encode_qname(hostname, packet, &offset) != 0 || offset + 4u > sizeof(packet)) {
        flags = dns_lock(); *query = (dns_pending_query_t){0}; dns_unlock(flags);
        return -1;
    }

    packet[offset++] = 0;
    packet[offset++] = DNS_TYPE_A;
    packet[offset++] = 0;
    packet[offset++] = DNS_CLASS_IN;

    udp_register_handler(DNS_CLIENT_PORT, dns_handle);
    if (udp_send(server_ip, DNS_CLIENT_PORT, DNS_SERVER_PORT, packet, offset) != 0) {
        flags = dns_lock(); *query = (dns_pending_query_t){0}; dns_unlock(flags);
        return -1;
    }

    const uint64_t start = scheduler_ticks_now();
    const uint64_t deadline = DNS_QUERY_TIMEOUT_TICKS > UINT64_MAX - start
        ? UINT64_MAX : start + DNS_QUERY_TIMEOUT_TICKS;
    while (query->done == 0u) {
        const uint64_t now = scheduler_ticks_now();
        if (now >= deadline) break;
        process_wait_event_timeout(&query->done, deadline - now);
    }
    flags = dns_lock();
    const int result = query->done != 0u ? query->result : -1;
    if (result == 0) *ip = query->ip;
    *query = (dns_pending_query_t){0};
    dns_unlock(flags);
    return result;
}

int dns_resolve(const char *hostname, uint32_t *ip) {
    if (hostname == NULL || ip == NULL) {
        return -1;
    }

    if (dns_cache_lookup(hostname, ip) == 0) {
        return 0;
    }

    if (dns_static_resolve(hostname, ip) == 0) {
        dns_cache_store(hostname, *ip);
        return 0;
    }

    uint32_t candidates[3];
    candidates[0] = dhcp_dns_server();
    candidates[1] = DNS_VBOX_NAT_IP;
    candidates[2] = dhcp_gateway();

    for (size_t i = 0; i < 3u; ++i) {
        uint32_t server = candidates[i];
        if (server == 0u) {
            continue;
        }
        int duplicate = 0;
        for (size_t j = 0; j < i; ++j) {
            if (candidates[j] == server) {
                duplicate = 1;
            }
        }
        if (duplicate != 0) {
            continue;
        }
        if (dns_resolve_with_server(hostname, server, ip) == 0) {
            dns_cache_store(hostname, *ip);
            return 0;
        }
    }

    return -1;
}

void dns_thread_stopped(uint32_t tid) {
    uint64_t flags=dns_lock();
    for(unsigned i=0;i<DNS_PENDING_QUERY_COUNT;++i)
        if(pending_queries[i].used && pending_queries[i].tid==tid)
            pending_queries[i]=(dns_pending_query_t){0};
    dns_unlock(flags);
}
