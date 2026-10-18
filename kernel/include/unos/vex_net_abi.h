#ifndef VEX_NET_ABI_H
#define VEX_NET_ABI_H

#include <stdint.h>

/* Socket operations beyond VEX_SYS_SEND/RECV: addressed datagrams and the
 * names of an endpoint. Addresses use the socket API layout (sockaddr_in).
 * Returns a byte count (SENDTO/RECVFROM), 0, or a negated errno. */
#define VEX_SYS_SOCKET_OP 214ull

#define VEX_SOCKET_SENDTO      1u
#define VEX_SOCKET_RECVFROM    2u
#define VEX_SOCKET_GETSOCKNAME 3u
#define VEX_SOCKET_GETPEERNAME 4u

typedef struct vex_socket_op_req {
    int32_t fd;
    uint32_t op;
    int32_t flags;          /* MSG_* */
    uint32_t addrlen;       /* in: capacity or length; out: actual length */
    void *buffer;
    uint64_t length;
    void *addr;
} vex_socket_op_req_t;

/* Resolve a host name with the system resolver (DHCP-provided DNS and its
 * cache). Fills up to `capacity` IPv4 addresses in network byte order and
 * returns how many were found, or a negated errno (-ENOENT: no such host).
 * With name == NULL, reports the DNS servers the system uses instead (0 when
 * none is configured), for programs that send DNS queries themselves. */
#define VEX_SYS_RESOLVE 215ull

typedef struct vex_resolve_req {
    const char *name;
    uint32_t *ipv4;
    uint32_t capacity;
    uint32_t reserved;
} vex_resolve_req_t;

#endif
