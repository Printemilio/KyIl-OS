#include <unos/dhcp.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/boot_time.h>
#include <unos/ethernet.h>
#include <unos/fs.h>
#include <unos/ipv4.h>
#include <unos/netdev.h>
#include <unos/scheduler.h>
#include <unos/tty.h>
#include <unos/udp.h>
#include <unos/debug_log.h>
#include <stdio.h>
#include <string.h>
#include <unos/network_state.h>

#define DHCP_CLIENT_PORT 68u
#define DHCP_SERVER_PORT 67u
#define DHCP_MAGIC_COOKIE 0x63825363u
#define DHCP_OP_BOOTREQUEST 1u
#define DHCP_OP_BOOTREPLY 2u
#define DHCP_HTYPE_ETHERNET 1u
#define DHCP_HLEN_ETHERNET 6u
#define DHCP_OPTION_MESSAGE_TYPE 53u
#define DHCP_OPTION_REQUESTED_IP 50u
#define DHCP_OPTION_SERVER_IDENTIFIER 54u
#define DHCP_OPTION_PARAMETER_REQUEST_LIST 55u
#define DHCP_OPTION_ROUTER 3u
#define DHCP_OPTION_DNS_SERVER 6u
#define DHCP_OPTION_END 255u
#define DHCP_DISCOVER 1u
#define DHCP_OFFER 2u
#define DHCP_REQUEST 3u
#define DHCP_ACK 5u
#define IPV4_BROADCAST 0xffffffffu
#define DHCP_OFFER_TIMEOUT_MS 1500ull
#define DHCP_OFFER_RETRY_MS 150ull
#define DHCP_ACK_TIMEOUT_MS 500ull
#define DHCP_POLL_YIELD_INTERVAL 64u
#define DHCP_MIN_PACKET_SIZE 300u
#define DHCP_BACKGROUND_PRIORITY 0u
#define DHCP_CONFIG_PATH "/sys/net/dhcp.conf"
#define DHCP_VBOX_FALLBACK_IP 0x0a00020fu
#define DHCP_VBOX_FALLBACK_MASK 0xffffff00u
#define DHCP_VBOX_FALLBACK_GATEWAY 0x0a000202u
#define DHCP_VBOX_FALLBACK_DNS 0x0a000203u

typedef struct {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[312];
} __attribute__((packed)) dhcp_packet_t;

static uint32_t transaction_id;
static uint32_t offered_ip_addr;
static uint32_t server_ip_addr;
static uint32_t netmask_addr;
static uint32_t gateway_addr;
static uint32_t dns_server_addr;
static int have_offer;
static int configured;
static size_t tx_packet_size;
static uint64_t offer_wait_ms;
static uint64_t ack_wait_ms;
static int worker_started;
static int ack_announced;
static int loaded_from_config;
static int saved_config;
enum {DHCP_IDLE,DHCP_WAIT_LINK,DHCP_SELECTING,DHCP_REQUESTING,DHCP_BOUND,DHCP_FAILED};
static unsigned lease_state,attempts;
static uint64_t deadline_ms,started_ms;

static uint16_t bswap16(uint16_t value) {
    return (uint16_t)((value << 8u) | (value >> 8u));
}

static uint32_t bswap32(uint32_t value) {
    return ((value & 0x000000ffu) << 24u)
        | ((value & 0x0000ff00u) << 8u)
        | ((value & 0x00ff0000u) >> 8u)
        | ((value & 0xff000000u) >> 24u);
}

static void zero_bytes(void *dst, size_t size) {
    uint8_t *d = dst;
    for (size_t i = 0; i < size; ++i) {
        d[i] = 0;
    }
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

static void append_string(char *dst, size_t capacity, size_t *length, const char *src) {
    while (src != 0 && *src != '\0' && *length + 1u < capacity) {
        dst[*length] = *src;
        ++(*length);
        ++src;
    }
    dst[*length] = '\0';
}

static void append_u32(char *dst, size_t capacity, size_t *length, uint32_t value) {
    char digits[10];
    size_t count = 0;

    if (value == 0) {
        append_string(dst, capacity, length, "0");
        return;
    }

    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count > 0) {
        char digit[2] = { digits[--count], '\0' };
        append_string(dst, capacity, length, digit);
    }
}

static void append_ipv4(char *dst, size_t capacity, size_t *length, uint32_t ip) {
    append_u32(dst, capacity, length, (ip >> 24u) & 0xffu);
    append_string(dst, capacity, length, ".");
    append_u32(dst, capacity, length, (ip >> 16u) & 0xffu);
    append_string(dst, capacity, length, ".");
    append_u32(dst, capacity, length, (ip >> 8u) & 0xffu);
    append_string(dst, capacity, length, ".");
    append_u32(dst, capacity, length, ip & 0xffu);
}

static int parse_ipv4_value(const char *text, size_t size, size_t *offset, uint32_t *ip) {
    uint32_t octets[4] = {0, 0, 0, 0};

    for (size_t part = 0; part < 4u; ++part) {
        uint32_t value = 0;
        size_t digits = 0;

        while (*offset < size && text[*offset] >= '0' && text[*offset] <= '9') {
            value = value * 10u + (uint32_t)(text[*offset] - '0');
            ++(*offset);
            ++digits;
        }

        if (digits == 0 || value > 255u) {
            return -1;
        }

        octets[part] = value;
        if (part < 3u) {
            if (*offset >= size || text[*offset] != '.') {
                return -1;
            }
            ++(*offset);
        }
    }

    *ip = (octets[0] << 24u) | (octets[1] << 16u) | (octets[2] << 8u) | octets[3];
    return 0;
}

static int key_at(const char *text, size_t size, size_t offset, const char *key) {
    size_t i = 0;

    while (key[i] != '\0') {
        if (offset + i >= size || text[offset + i] != key[i]) {
            return 0;
        }
        ++i;
    }

    return 1;
}

static int load_config(void) {
    ora_view_t fs_lease_0 __attribute__((cleanup(ora_view_release)))={0};
    void *data = 0;
    size_t size = 0;
    uint32_t ip = 0;
    uint32_t mask = 0;
    uint32_t gateway = 0;
    uint32_t dns = 0;

    if (fs_read_view(DHCP_CONFIG_PATH,&data,&size, &fs_lease_0) != 0 || data == 0 || size == 0) {
        return -1;
    }

    const char *text = data;
    for (size_t offset = 0; offset < size;) {
        if (key_at(text, size, offset, "ip=")) {
            offset += 3u;
            if (parse_ipv4_value(text, size, &offset, &ip) != 0) {
                return -1;
            }
        } else if (key_at(text, size, offset, "mask=")) {
            offset += 5u;
            if (parse_ipv4_value(text, size, &offset, &mask) != 0) {
                return -1;
            }
        } else if (key_at(text, size, offset, "gateway=")) {
            offset += 8u;
            if (parse_ipv4_value(text, size, &offset, &gateway) != 0) {
                return -1;
            }
        } else if (key_at(text, size, offset, "dns=")) {
            offset += 4u;
            if (parse_ipv4_value(text, size, &offset, &dns) != 0) {
                return -1;
            }
        }

        while (offset < size && text[offset] != '\n') {
            ++offset;
        }
        if (offset < size && text[offset] == '\n') {
            ++offset;
        }
    }

    if (ip == 0 || mask == 0) {
        return -1;
    }

    offered_ip_addr = ip;
    netmask_addr = mask;
    gateway_addr = gateway;
    dns_server_addr = dns;
    ipv4_set_local_ip(ip);
    configured = 1;
    loaded_from_config = 1;
    tty_puts("dhcp: loading config\n");
    return 0;
}

static void save_config(void) {
    char text[128];
    size_t length = 0;

    if (offered_ip_addr == 0 || netmask_addr == 0) {
        return;
    }

    append_string(text, sizeof(text), &length, "ip=");
    append_ipv4(text, sizeof(text), &length, offered_ip_addr);
    append_string(text, sizeof(text), &length, "\nmask=");
    append_ipv4(text, sizeof(text), &length, netmask_addr);
    append_string(text, sizeof(text), &length, "\ngateway=");
    append_ipv4(text, sizeof(text), &length, gateway_addr);
    append_string(text, sizeof(text), &length, "\ndns=");
    append_ipv4(text, sizeof(text), &length, dns_server_addr);
    append_string(text, sizeof(text), &length, "\n");
    if (fs_write(DHCP_CONFIG_PATH, text, length) == 0) {
        saved_config = 1;
        tty_puts("dhcp: saving config\n");
    }
}

static void announce_ack(void) {
    if (ack_announced != 0) {
        return;
    }

    tty_puts("dhcp: ack ip ");
    print_ipv4(offered_ip_addr);
    tty_putchar('\n');
    ack_announced = 1;
}

static void build_packet(dhcp_packet_t *packet, uint8_t message_type) {
    zero_bytes(packet, sizeof(*packet));
    const uint8_t *mac = netdev_mac();

    packet->op = DHCP_OP_BOOTREQUEST;
    packet->htype = DHCP_HTYPE_ETHERNET;
    packet->hlen = DHCP_HLEN_ETHERNET;
    packet->xid = bswap32(transaction_id);
    packet->flags = bswap16(0x8000u);
    packet->magic = bswap32(DHCP_MAGIC_COOKIE);
    for (size_t i = 0; i < 6u; ++i) {
        packet->chaddr[i] = mac[i];
    }

    size_t o = 0;
    packet->options[o++] = DHCP_OPTION_MESSAGE_TYPE;
    packet->options[o++] = 1;
    packet->options[o++] = message_type;

    if (message_type == DHCP_REQUEST) {
        packet->options[o++] = DHCP_OPTION_REQUESTED_IP;
        packet->options[o++] = 4;
        packet->options[o++] = (uint8_t)(offered_ip_addr >> 24u);
        packet->options[o++] = (uint8_t)(offered_ip_addr >> 16u);
        packet->options[o++] = (uint8_t)(offered_ip_addr >> 8u);
        packet->options[o++] = (uint8_t)offered_ip_addr;

        if (server_ip_addr != 0) {
            packet->options[o++] = DHCP_OPTION_SERVER_IDENTIFIER;
            packet->options[o++] = 4;
            packet->options[o++] = (uint8_t)(server_ip_addr >> 24u);
            packet->options[o++] = (uint8_t)(server_ip_addr >> 16u);
            packet->options[o++] = (uint8_t)(server_ip_addr >> 8u);
            packet->options[o++] = (uint8_t)server_ip_addr;
        }
    }

    packet->options[o++] = DHCP_OPTION_PARAMETER_REQUEST_LIST;
    packet->options[o++] = 3;
    packet->options[o++] = 1;
    packet->options[o++] = 3;
    packet->options[o++] = 6;
    packet->options[o++] = DHCP_OPTION_END;
    tx_packet_size = offsetof(dhcp_packet_t, options) + o;
    if (tx_packet_size < DHCP_MIN_PACKET_SIZE) {
        tx_packet_size = DHCP_MIN_PACKET_SIZE;
    }
}

static uint8_t dhcp_message_type(const dhcp_packet_t *packet, size_t size) {
    const uint8_t *options = packet->options;
    size_t i = 0;
    const size_t option_size = size > offsetof(dhcp_packet_t, options) ? size - offsetof(dhcp_packet_t, options) : 0;

    while (i < option_size) {
        const uint8_t code = options[i++];
        if (code == DHCP_OPTION_END) {
            break;
        }
        if (code == 0) {
            continue;
        }
        if (i >= option_size) {
            break;
        }
        const uint8_t len = options[i++];
        if (i + len > option_size) {
            break;
        }
        if (code == DHCP_OPTION_MESSAGE_TYPE && len == 1) {
            return options[i];
        }
        i += len;
    }

    return 0;
}

static uint32_t dhcp_option_ipv4(const dhcp_packet_t *packet, size_t size, uint8_t target_code) {
    const uint8_t *options = packet->options;
    size_t i = 0;
    const size_t option_size = size > offsetof(dhcp_packet_t, options) ? size - offsetof(dhcp_packet_t, options) : 0;

    while (i < option_size) {
        const uint8_t code = options[i++];
        if (code == DHCP_OPTION_END) {
            break;
        }
        if (code == 0) {
            continue;
        }
        if (i >= option_size) {
            break;
        }
        const uint8_t len = options[i++];
        if (i + len > option_size) {
            break;
        }
        if (code == target_code && len >= 4) {
            return ((uint32_t)options[i] << 24u)
                | ((uint32_t)options[i + 1u] << 16u)
                | ((uint32_t)options[i + 2u] << 8u)
                | (uint32_t)options[i + 3u];
        }
        i += len;
    }

    return 0;
}

static void dhcp_handle(uint32_t src_ip, uint16_t src_port, const void *data, size_t size) {
    if(src_port!=DHCP_SERVER_PORT)return;

    if (size < offsetof(dhcp_packet_t, options)) {
        return;
    }

    const dhcp_packet_t *packet = data;

    if (packet->op != DHCP_OP_BOOTREPLY || bswap32(packet->xid) != transaction_id ||
        bswap32(packet->magic) != DHCP_MAGIC_COOKIE) {
        return;
    }

    if(packet->htype!=1 || packet->hlen!=6 || memcmp(packet->chaddr,netdev_mac(),6))return;
    const uint8_t type = dhcp_message_type(packet, size);
    if (type == DHCP_OFFER && lease_state==DHCP_SELECTING) {
        offered_ip_addr = bswap32(packet->yiaddr);
        server_ip_addr = src_ip;
        if(!offered_ip_addr)return;
        have_offer = 1;
        return;
    }

    if (type == DHCP_ACK && lease_state==DHCP_REQUESTING && src_ip==server_ip_addr) {
        uint32_t address=bswap32(packet->yiaddr),mask=dhcp_option_ipv4(packet,size,1);
        if(!address || address!=offered_ip_addr || !mask || ((~mask)&((~mask)+1u)))return;
        netmask_addr=mask;gateway_addr=dhcp_option_ipv4(packet,size,DHCP_OPTION_ROUTER);
        dns_server_addr=dhcp_option_ipv4(packet,size,DHCP_OPTION_DNS_SERVER);
        ipv4_set_local_ip(address);
        configured = 1;lease_state=DHCP_BOUND;
        ack_wait_ms=boot_time_ms()-started_ms;
        char line[160];snprintf(line,sizeof(line),"dhcp: real ACK server=%x address=%x mask=%x DNS=%x\n",server_ip_addr,offered_ip_addr,netmask_addr,dns_server_addr);debug_log_puts(line);
        save_config();
        announce_ack();
    }
}

void dhcp_init(void) {
    transaction_id = 0x554e4f53u;
    offered_ip_addr = 0;
    server_ip_addr = 0;
    netmask_addr = 0;
    gateway_addr = 0;
    dns_server_addr = 0;
    have_offer = 0;
    configured = 0;
    tx_packet_size = 0;
    offer_wait_ms = 0;
    ack_wait_ms = 0;
    worker_started = 0;lease_state=DHCP_IDLE;deadline_ms=started_ms=0;attempts=0;
    ack_announced = 0;
    loaded_from_config = 0;
    saved_config = 0;
    udp_register_handler(DHCP_CLIENT_PORT, dhcp_handle);
    (void)load_config();
}

void dhcp_start(void) {
    uint64_t flags=network_state_lock();
    if(configured && loaded_from_config){lease_state=DHCP_BOUND;network_state_unlock(flags);return;}
    have_offer=configured=0;offered_ip_addr=server_ip_addr=0;
    netmask_addr=gateway_addr=dns_server_addr=0;ipv4_set_local_ip(0);
    ++transaction_id;lease_state=DHCP_WAIT_LINK;attempts=0;deadline_ms=0;
    network_state_unlock(flags);ethernet_wake();
}
/* Called by the existing event-driven Ethernet worker. Zero means wait for
 * a packet/link event; nonzero is the actual next protocol timer in ticks. */
uint64_t dhcp_service(void) {
    uint64_t flags=network_state_lock();uint64_t now=boot_time_ms(),result=0;
    if(lease_state==DHCP_IDLE || lease_state==DHCP_BOUND || lease_state==DHCP_FAILED)goto done;
    if(!netdev_link_up()){lease_state=DHCP_WAIT_LINK;attempts=0;goto done;}
    if(lease_state==DHCP_WAIT_LINK){lease_state=DHCP_SELECTING;started_ms=now;deadline_ms=now;attempts=0;}
    if(lease_state==DHCP_SELECTING && have_offer){
        offer_wait_ms=now-started_ms;lease_state=DHCP_REQUESTING;started_ms=now;deadline_ms=now;attempts=0;
    }
    if(now>=deadline_ms) {
        if(attempts>=8){lease_state=DHCP_FAILED;debug_log_puts("dhcp: failed, no fabricated fallback address\n");goto done;}
        dhcp_packet_t packet;build_packet(&packet,lease_state==DHCP_SELECTING?DHCP_DISCOVER:DHCP_REQUEST);
        int sent=udp_send(IPV4_BROADCAST,DHCP_CLIENT_PORT,DHCP_SERVER_PORT,&packet,tx_packet_size);
        if(!sent){attempts++;debug_log_puts(lease_state==DHCP_SELECTING?"dhcp: DISCOVER queued\n":"dhcp: REQUEST queued\n");}
        deadline_ms=now+1000u;
    }
    result=(deadline_ms-now+9)/10;if(!result)result=1;
done:network_state_unlock(flags);return result;
}
void dhcp_start_async(void) {if(!worker_started && !configured){worker_started=1;dhcp_start();}}

uint32_t dhcp_offered_ip(void) {
    return offered_ip_addr;
}

uint32_t dhcp_netmask(void) {
    return netmask_addr;
}

uint32_t dhcp_gateway(void) {
    return gateway_addr;
}

uint32_t dhcp_dns_server(void) {
    return dns_server_addr;
}

uint64_t dhcp_offer_wait_ms(void) {
    return offer_wait_ms;
}

uint64_t dhcp_ack_wait_ms(void) {
    return ack_wait_ms;
}

int dhcp_config_loaded(void) {
    return loaded_from_config != 0;
}

int dhcp_config_saved(void) {
    return saved_config != 0;
}
