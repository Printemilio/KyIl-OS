/* Host-side regression for Vex's DNS/ARP blocking path. */
#define VEX_NETWORK_HOST_TEST 1
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unos/arp.h>
#include <unos/dhcp.h>
#include <unos/dns.h>
#include <unos/ethernet.h>
#include <unos/ipv4.h>
#include <unos/scheduler.h>
#include <unos/udp.h>

#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr,"dns-wait test failed at line %d: %s\n",__LINE__,#condition); abort(); \
} } while (0)

#define DNS_CLIENT_PORT 53053u
#define DNS_SERVER_PORT 53u
#define DNS_TYPE_A 1u
#define DNS_CLASS_IN 1u
#define DNS_MAX_PACKET_SIZE 512u
#define ARP_HTYPE_ETHERNET 1u
#define ARP_PTYPE_IPV4 0x0800u
#define ARP_HLEN_ETHERNET 6u
#define ARP_PLEN_IPV4 4u
#define ARP_OP_REPLY 2u

typedef struct {
    uint16_t id, flags, qdcount, ancount, nscount, arcount;
} __attribute__((packed)) test_dns_header_t;
typedef struct {
    uint16_t htype, ptype;
    uint8_t hlen, plen;
    uint16_t oper;
    uint8_t sha[6];
    uint32_t spa;
    uint8_t tha[6];
    uint32_t tpa;
} __attribute__((packed)) test_arp_packet_t;

static uint16_t swap16(uint16_t value) { return (uint16_t)((value << 8u) | (value >> 8u)); }
static uint32_t swap32(uint32_t value) {
    return ((value & 0xffu) << 24u) | ((value & 0xff00u) << 8u) |
           ((value & 0xff0000u) >> 8u) | ((value >> 24u) & 0xffu);
}
static int contains(const char *text, const char *needle) {
    size_t n = strlen(needle);
    for (; *text; ++text) if (strncmp(text,needle,n)==0) return 1;
    return 0;
}

static uint64_t ticks;
static uint32_t current_tid = 1u;
static uint32_t wake_count;
static uint32_t wait_count;
static uint32_t poll_count;
static uint32_t local_ip = 0x0a00020fu;
static uint32_t dns_server = 0x0a000203u;
static uint32_t gateway = 0x0a000202u;
static uint32_t pending_arp_ip;
static int arp_request_pending;
static int timeout_mode;
static int negative_mode;
static int nested_mode;
static uint32_t nested_ip;
static ethernet_handler_t arp_callback;
static udp_handler_t dns_callback;

typedef struct {
    uint32_t tid;
    uint16_t id;
    uint32_t ip;
    char hostname[64];
    uint8_t used;
    uint8_t delivered;
} pending_test_query_t;
static pending_test_query_t queries[16];
static unsigned query_count;

const uint8_t *ethernet_mac(void) {
    static const uint8_t mac[6] = {2, 0, 0, 0, 0, 1};
    return mac;
}

void ethernet_register_handler(uint16_t type, ethernet_handler_t handler) {
    if (type == ETHERNET_TYPE_ARP) arp_callback = handler;
}

int ethernet_send(const uint8_t dest[6], uint16_t type, const void *payload, size_t size) {
    (void)dest;
    if (type == ETHERNET_TYPE_ARP && size == sizeof(test_arp_packet_t)) {
        const test_arp_packet_t *request = payload;
        pending_arp_ip = swap32(request->tpa);
        arp_request_pending = 1;
    }
    return 0;
}

uint32_t scheduler_current_tid(void) { return current_tid; }
uint64_t scheduler_ticks_now(void) { return ticks; }
void process_wake(uint32_t tid) { assert(tid != 0u); ++wake_count; }
void debug_log_puts(const char *text) { (void)text; }
uint32_t ipv4_get_local_ip(void) { return local_ip; }
uint32_t dhcp_netmask(void) { return 0xffffff00u; }
uint32_t dhcp_gateway(void) { return gateway; }
uint32_t dhcp_dns_server(void) { return dns_server; }

void udp_register_handler(uint16_t port, udp_handler_t handler) {
    assert(port == DNS_CLIENT_PORT);
    dns_callback = handler;
}

static void decode_hostname(const uint8_t *packet, size_t size, char out[64]) {
    size_t offset = sizeof(test_dns_header_t), used = 0u;
    while (offset < size) {
        uint8_t length = packet[offset++];
        if (length == 0u) break;
        if ((length & 0xc0u) != 0u || offset + length > size || used + length + 1u >= 64u) {
            out[0] = 0; return;
        }
        if (used != 0u) out[used++] = '.';
        memcpy(out + used, packet + offset, length);
        used += length;
        offset += length;
    }
    out[used] = 0;
}

int udp_send(uint32_t ip, uint16_t src_port, uint16_t dest_port,
             const void *data, size_t size) {
    assert(ip == dns_server || ip == gateway);
    assert(src_port == DNS_CLIENT_PORT && dest_port == DNS_SERVER_PORT);
    assert(size >= sizeof(test_dns_header_t));
    assert(query_count < sizeof(queries) / sizeof(queries[0]));
    pending_test_query_t *query = &queries[query_count++];
    query->tid = current_tid;
    query->id = swap16(((const test_dns_header_t *)data)->id);
    query->ip = ip;
    query->used = 1u;
    decode_hostname(data, size, query->hostname);
    assert(query->hostname[0] != 0);

    if (!nested_mode && strcmp(query->hostname, "outer.test") == 0) {
        nested_mode = 1;
        current_tid = 2u;
        assert(dns_resolve("inner.test", &nested_ip) == 0);
        current_tid = 1u;
        nested_mode = 0;
    }
    return 0;
}

static void deliver_arp_reply(void) {
    assert(arp_callback != NULL && pending_arp_ip != 0u);
    test_arp_packet_t reply = {0};
    reply.htype = swap16(ARP_HTYPE_ETHERNET);
    reply.ptype = swap16(ARP_PTYPE_IPV4);
    reply.hlen = ARP_HLEN_ETHERNET;
    reply.plen = ARP_PLEN_IPV4;
    reply.oper = swap16(ARP_OP_REPLY);
    const uint8_t remote_mac[6] = {2, 0, 0, 0, 0, 2};
    memcpy(reply.sha, remote_mac, sizeof(remote_mac));
    reply.spa = swap32(pending_arp_ip);
    reply.tha[0] = 2;
    reply.tpa = swap32(local_ip);
    arp_request_pending = 0;
    arp_callback(remote_mac, &reply, sizeof(reply));
}

static void deliver_dns_response(pending_test_query_t *query) {
    uint8_t response[DNS_MAX_PACKET_SIZE] = {0};
    const uint8_t request_prefix[sizeof(test_dns_header_t)] = {
        (uint8_t)(query->id >> 8), (uint8_t)query->id
    };
    memcpy(response, request_prefix, sizeof(request_prefix));
    test_dns_header_t *header = (test_dns_header_t *)response;
    header->flags = swap16(negative_mode ? 0x8183u : 0x8180u);
    header->qdcount = swap16(1u);
    header->ancount = swap16(negative_mode ? 0u : 1u);
    /* Reconstruct the query question from the recorded name. */
    size_t qend = sizeof(test_dns_header_t);
    const char *p = query->hostname;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t length = dot ? (size_t)(dot - p) : strlen(p);
        response[qend++] = (uint8_t)length;
        memcpy(response + qend, p, length);
        qend += length;
        if (!dot) break;
        p = dot + 1;
    }
    response[qend++] = 0;
    response[qend++] = 0; response[qend++] = DNS_TYPE_A;
    response[qend++] = 0; response[qend++] = DNS_CLASS_IN;
    if (!negative_mode) {
        response[qend++] = 0xc0; response[qend++] = 0x0c;
        response[qend++] = 0; response[qend++] = DNS_TYPE_A;
        response[qend++] = 0; response[qend++] = DNS_CLASS_IN;
        response[qend++] = 0; response[qend++] = 0; response[qend++] = 0; response[qend++] = 60;
        response[qend++] = 0; response[qend++] = 4;
        uint32_t address = contains(query->hostname, "inner") ? 0xcb007116u : 0xcb00710bu;
        response[qend++] = (uint8_t)(address >> 24);
        response[qend++] = (uint8_t)(address >> 16);
        response[qend++] = (uint8_t)(address >> 8);
        response[qend++] = (uint8_t)address;
    }
    query->delivered = 1u;
    dns_callback(query->ip, DNS_SERVER_PORT, response, qend);
}

void process_wait_event_timeout(const volatile uint8_t *woken, uint64_t wait_ticks) {
    ++wait_count;
    if (*woken != 0u) return;
    if (arp_request_pending) {
        deliver_arp_reply();
        return;
    }
    if (timeout_mode) {
        ticks += wait_ticks;
        return;
    }
    for (size_t i = query_count; i > 0u; --i) {
        pending_test_query_t *query = &queries[i - 1u];
        if (query->used && !query->delivered && query->tid == current_tid) {
            deliver_dns_response(query);
            return;
        }
    }
    assert(!"DNS waiter slept without a matching response");
}

int main(void) {
    arp_init();
    arp_set_local_ip(local_ip);
    assert(dns_resolve("outer.test", &pending_arp_ip) == 0);
    assert(pending_arp_ip == 0xcb00710bu);
    assert(nested_ip == 0xcb007116u);
    assert(query_count == 2u && queries[0].id != queries[1].id);
    assert(queries[0].delivered && queries[1].delivered);
    assert(wait_count >= 3u && wake_count >= 3u); /* ARP + both DNS replies. */
    const unsigned before_cached = query_count;
    uint32_t cached = 0;
    assert(dns_resolve("inner.test", &cached) == 0 && cached == nested_ip);
    assert(query_count == before_cached);

    negative_mode = 1;
    assert(dns_resolve("negative.test", &cached) != 0);
    negative_mode = 0;
    timeout_mode = 1;
    assert(dns_resolve("timeout.test", &cached) != 0);
    timeout_mode = 0;
    assert(poll_count == 0u);
    puts("DNS/ARP: per-request concurrent IDs, scheduler wakeups, cache, negative response and bounded timeout PASS");
    return 0;
}
