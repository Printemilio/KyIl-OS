#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>
#include <unos/vex_abi.h>

#include "vex_syscall.h"

int socket(int domain, int type, int protocol) {
    return (int)vex_syscall3(VEX_SYS_SOCKET, (uint64_t)domain, (uint64_t)type, (uint64_t)protocol);
}

int connect(int sockfd, const struct sockaddr* addr, uint32_t addrlen) {
    vex_sockaddr_req_t req;
    req.sockfd = sockfd;
    req.addr = addr;
    req.addrlen = addrlen;
    return (int)vex_syscall1(VEX_SYS_CONNECT, (uint64_t)&req);
}

ssize_t send(int sockfd, const void* buffer, size_t length, int flags) {
    vex_send_recv_req_t req;
    req.sockfd = sockfd;
    req.buffer = (void*)buffer;
    req.length = length;
    req.flags = flags;
    return (ssize_t)vex_syscall1(VEX_SYS_SEND, (uint64_t)&req);
}

ssize_t recv(int sockfd, void* buffer, size_t length, int flags) {
    vex_send_recv_req_t req;
    req.sockfd = sockfd;
    req.buffer = buffer;
    req.length = length;
    req.flags = flags;
    return (ssize_t)vex_syscall1(VEX_SYS_RECV, (uint64_t)&req);
}

int ladybird_connect_hostname(int sockfd, const char* hostname, uint16_t port) {
    vex_connect_hostname_req_t req;
    req.sockfd = sockfd;
    req.hostname = hostname;
    req.port = port;
    int result = (int)vex_syscall1(VEX_SYS_CONNECT_HOSTNAME, (uint64_t)&req);
    if (result < 0) {
        *ladybird_errno_location() = -result;
        return -1;
    }
    return result;
}

int bind(int sockfd, const struct sockaddr* addr, uint32_t addrlen) {
    vex_sockaddr_req_t req;
    req.sockfd = sockfd;
    req.addr = addr;
    req.addrlen = addrlen;
    return (int)vex_syscall1(VEX_SYS_BIND, (uint64_t)&req);
}

int listen(int sockfd, int backlog) {
    return (int)vex_syscall2(VEX_SYS_LISTEN, (uint64_t)sockfd, (uint64_t)backlog);
}

int accept(int sockfd, struct sockaddr* addr, uint32_t* addrlen) {
    vex_accept_req_t req;
    req.sockfd = sockfd;
    req.addr = addr;
    req.addrlen = addrlen;
    return (int)vex_syscall1(VEX_SYS_ACCEPT, (uint64_t)&req);
}

int setsockopt(int sockfd, int level, int option_name, const void* option_value, uint32_t option_len) {
    vex_sockopt_req_t req;
    req.sockfd = sockfd;
    req.level = level;
    req.option_name = option_name;
    req.option_value = (void*)option_value;
    req.option_len = &option_len;
    return (int)vex_syscall1(VEX_SYS_SETSOCKOPT, (uint64_t)&req);
}

int getsockopt(int sockfd, int level, int option_name, void* option_value, uint32_t* option_len) {
    vex_sockopt_req_t req;
    req.sockfd = sockfd;
    req.level = level;
    req.option_name = option_name;
    req.option_value = option_value;
    req.option_len = option_len;
    return (int)vex_syscall1(VEX_SYS_GETSOCKOPT, (uint64_t)&req);
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
    vex_socketpair_req_t req;
    req.domain = domain;
    req.type = type;
    req.protocol = protocol;
    req.sv = sv;
    return (int)vex_syscall1(VEX_SYS_SOCKETPAIR, (uint64_t)&req);
}
