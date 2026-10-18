/* BSD sockets on native Vex calls. IPv4 TCP and UDP sockets, and local
 * socketpairs; there is no IPv6 stack (EAFNOSUPPORT from the kernel). */
#define _GNU_SOURCE
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include "syscall.h"

int socket(int domain, int type, int protocol)
{
	return (int)__vex_legacy_ret(__vex_call3(VEX_SYS_SOCKET, (uint64_t)(unsigned)domain,
		(uint64_t)(unsigned)type, (uint64_t)(unsigned)protocol));
}

int socketpair(int domain, int type, int protocol, int fd[2])
{
	vex_socketpair_req_t req = { .domain = domain, .type = type, .protocol = protocol, .sv = fd };
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_SOCKETPAIR, __vex_ptr(&req)));
}

static int address_call(uint64_t n, int fd, const struct sockaddr *addr, socklen_t len)
{
	vex_sockaddr_req_t req = { .sockfd = fd, .addr = addr, .addrlen = len };
	return (int)__vex_legacy_ret(__vex_call1(n, __vex_ptr(&req)));
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
	return address_call(VEX_SYS_CONNECT, fd, addr, len);
}

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	return address_call(VEX_SYS_BIND, fd, addr, len);
}

int listen(int fd, int backlog)
{
	return (int)__vex_legacy_ret(__vex_call2(VEX_SYS_LISTEN, (uint64_t)(unsigned)fd,
		(uint64_t)(unsigned)backlog));
}

int accept4(int fd, struct sockaddr *restrict addr, socklen_t *restrict len, int flags)
{
	if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK)) {
		errno = EINVAL;
		return -1;
	}
	vex_accept_req_t req = { .sockfd = fd, .addr = addr, .addrlen = len };
	int r = (int)__vex_legacy_ret(__vex_call1(VEX_SYS_ACCEPT, __vex_ptr(&req)));
	if (r < 0) return -1;
	if (flags & SOCK_CLOEXEC) fcntl(r, F_SETFD, FD_CLOEXEC);
	if (flags & SOCK_NONBLOCK) fcntl(r, F_SETFL, fcntl(r, F_GETFL) | O_NONBLOCK);
	return r;
}

int accept(int fd, struct sockaddr *restrict addr, socklen_t *restrict len)
{
	return accept4(fd, addr, len, 0);
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *addr, socklen_t alen)
{
	vex_socket_op_req_t req = {
		.fd = fd, .op = VEX_SOCKET_SENDTO, .flags = flags,
		.buffer = (void *)buf, .length = len, .addr = (void *)addr, .addrlen = alen,
	};
	if (!addr) {
		/* Connected sockets and socketpairs (musl's send() lands here). */
		vex_send_recv_req_t s = { .sockfd = fd, .buffer = (void *)buf, .length = len, .flags = flags };
		return __vex_legacy_ret(__vex_call1(VEX_SYS_SEND, __vex_ptr(&s)));
	}
	return __vex_ret(__vex_call1(VEX_SYS_SOCKET_OP, __vex_ptr(&req)));
}

ssize_t recvfrom(int fd, void *restrict buf, size_t len, int flags, struct sockaddr *restrict addr, socklen_t *restrict alen)
{
	if (!addr) {
		/* musl's recv() lands here. */
		vex_send_recv_req_t r = { .sockfd = fd, .buffer = buf, .length = len, .flags = flags };
		return __vex_legacy_ret(__vex_call1(VEX_SYS_RECV, __vex_ptr(&r)));
	}
	vex_socket_op_req_t req = {
		.fd = fd, .op = VEX_SOCKET_RECVFROM, .flags = flags,
		.buffer = buf, .length = len, .addr = addr, .addrlen = addr && alen ? *alen : 0,
	};
	ssize_t r = __vex_ret(__vex_call1(VEX_SYS_SOCKET_OP, __vex_ptr(&req)));
	if (r >= 0 && addr && alen) *alen = req.addrlen;
	return r;
}

static int name_call(unsigned op, int fd, struct sockaddr *restrict addr, socklen_t *restrict len)
{
	vex_socket_op_req_t req = { .fd = fd, .op = op, .addr = addr, .addrlen = len ? *len : 0 };
	if (__vex_ret(__vex_call1(VEX_SYS_SOCKET_OP, __vex_ptr(&req))) < 0) return -1;
	if (len) *len = req.addrlen;
	return 0;
}

int getsockname(int fd, struct sockaddr *restrict addr, socklen_t *restrict len)
{
	return name_call(VEX_SOCKET_GETSOCKNAME, fd, addr, len);
}

int getpeername(int fd, struct sockaddr *restrict addr, socklen_t *restrict len)
{
	return name_call(VEX_SOCKET_GETPEERNAME, fd, addr, len);
}

int getsockopt(int fd, int level, int name, void *restrict value, socklen_t *restrict len)
{
	vex_sockopt_req_t req = { .sockfd = fd, .level = level, .option_name = name,
		.option_value = value, .option_len = len };
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_GETSOCKOPT, __vex_ptr(&req)));
}

int setsockopt(int fd, int level, int name, const void *value, socklen_t len)
{
	socklen_t l = len;
	vex_sockopt_req_t req = { .sockfd = fd, .level = level, .option_name = name,
		.option_value = (void *)value, .option_len = &l };
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_SETSOCKOPT, __vex_ptr(&req)));
}

/* Scatter/gather messages without ancillary data. */
ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
	size_t total = 0;
	if (msg->msg_controllen) {
		errno = EOPNOTSUPP;
		return -1;
	}
	for (int i = 0; i < (int)msg->msg_iovlen; i++) total += msg->msg_iov[i].iov_len;
	char stage[total ? total : 1], *p = stage;
	if (total > 65536) {
		errno = EMSGSIZE;
		return -1;
	}
	for (int i = 0; i < (int)msg->msg_iovlen; i++) {
		memcpy(p, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
		p += msg->msg_iov[i].iov_len;
	}
	return sendto(fd, stage, total, flags, msg->msg_name, msg->msg_namelen);
}

ssize_t recvmsg(int fd, struct msghdr *msg, int flags)
{
	size_t total = 0;
	for (int i = 0; i < (int)msg->msg_iovlen; i++) total += msg->msg_iov[i].iov_len;
	if (total > 65536) total = 65536;
	char stage[total ? total : 1];
	socklen_t alen = msg->msg_namelen;
	ssize_t r = recvfrom(fd, stage, total, flags, msg->msg_name, msg->msg_name ? &alen : 0);
	if (r < 0) return -1;
	msg->msg_namelen = msg->msg_name ? alen : 0;
	msg->msg_controllen = 0;
	msg->msg_flags = 0;
	size_t left = (size_t)r;
	const char *p = stage;
	for (int i = 0; i < (int)msg->msg_iovlen && left; i++) {
		size_t n = msg->msg_iov[i].iov_len < left ? msg->msg_iov[i].iov_len : left;
		memcpy(msg->msg_iov[i].iov_base, p, n);
		p += n;
		left -= n;
	}
	return r;
}

/* The kernel TCP stack has no half-close yet. */
int shutdown(int fd, int how)
{
	if (how < SHUT_RD || how > SHUT_RDWR) {
		errno = EINVAL;
		return -1;
	}
	if (fcntl(fd, F_GETFD) < 0) return -1;
	errno = EOPNOTSUPP;
	return -1;
}
