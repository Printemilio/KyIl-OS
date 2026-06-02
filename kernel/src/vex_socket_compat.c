#include <unos/vex_legacy_compat.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/debug_log.h>
#include <string.h>
#include <unos/dns.h>
#include <unos/heap.h>
#include <unos/ipc.h>
#include <unos/tcp.h>
#include <unos/tls.h>

typedef struct ladybird_socket_ctx {
    int domain;
    int type;
    int protocol;
    int tls_handle;
    int tcp_handle;
    int last_error;
    uint16_t port;
    uint32_t ip;
    uint32_t recv_timeout_ms;
    uint32_t send_timeout_ms;
    char hostname[256];
    uint8_t connected;
} ladybird_socket_ctx_t;

typedef struct ladybird_socketpair_ctx {
    int read_pipe_id;
    int write_pipe_id;
    uint32_t *shared_refs;
} ladybird_socketpair_ctx_t;

static ssize_t socket_read_op(void *ctx, void *buffer, size_t count);
static ssize_t socket_write_op(void *ctx, const void *buffer, size_t count);
static int socket_close_op(void *ctx);
static int socket_fstat_op(void *ctx, struct stat *st);
static int socket_seek_op(void *ctx, off_t offset, int whence, off_t *result);
static int socket_fsync_op(void *ctx);
static int socket_poll_op(void *ctx, short events, short *revents);
static ssize_t socketpair_read_op(void *ctx, void *buffer, size_t count);
static ssize_t socketpair_write_op(void *ctx, const void *buffer, size_t count);
static int socketpair_close_op(void *ctx);
static int socketpair_fstat_op(void *ctx, struct stat *st);
static int socketpair_seek_op(void *ctx, off_t offset, int whence, off_t *result);
static int socketpair_fsync_op(void *ctx);
static int socketpair_poll_op(void *ctx, short events, short *revents);

static const ladybird_fd_ops_t socket_ops = {
    socket_read_op,
    socket_write_op,
    socket_close_op,
    socket_fstat_op,
    socket_seek_op,
    socket_fsync_op,
    socket_poll_op
};

static const ladybird_fd_ops_t socketpair_ops = {
    socketpair_read_op,
    socketpair_write_op,
    socketpair_close_op,
    socketpair_fstat_op,
    socketpair_seek_op,
    socketpair_fsync_op,
    socketpair_poll_op
};

static uint16_t ntohs16(uint16_t value) {
    return (uint16_t)((value >> 8) | (value << 8));
}

static void copy_hostname(char *dst, size_t dst_size, const char *src) {
    size_t i = 0;

    if (dst == NULL || dst_size == 0u) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (i + 1u < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';
}

static ssize_t set_socket_errno(int value) {
    *ladybird_errno_location() = value;
    return -1;
}

static int set_socket_errno_int(int value) {
    *ladybird_errno_location() = value;
    return -1;
}

static ssize_t set_socket_ctx_errno(ladybird_socket_ctx_t *socket, int value) {
    if (socket != NULL) {
        socket->last_error = value;
    }
    *ladybird_errno_location() = value;
    return -1;
}

static int set_socket_ctx_errno_int(ladybird_socket_ctx_t *socket, int value) {
    if (socket != NULL) {
        socket->last_error = value;
    }
    *ladybird_errno_location() = value;
    return -1;
}

static uint32_t timeval_to_millis(const struct timeval *value) {
    uint64_t millis;

    if (value == NULL || value->tv_sec < 0 || value->tv_usec < 0) {
        return 0u;
    }

    millis = (uint64_t)value->tv_sec * 1000u;
    millis += (uint64_t)value->tv_usec / 1000u;
    if (millis > 0xffffffffu) {
        return 0xffffffffu;
    }
    return (uint32_t)millis;
}

static void millis_to_timeval(uint32_t millis, struct timeval *value) {
    if (value == NULL) {
        return;
    }
    value->tv_sec = (time_t)(millis / 1000u);
    value->tv_usec = (long)((millis % 1000u) * 1000u);
}

static uint32_t socket_timeout_to_polls(uint32_t timeout_ms, uint32_t fallback_polls) {
    uint64_t polls;
    if (timeout_ms == 0u) {
        return fallback_polls;
    }
    polls = (uint64_t)timeout_ms * 100u;
    if (polls > 0xffffffffu) {
        return 0xffffffffu;
    }
    return (uint32_t)polls;
}

static void net_debug_u32(uint32_t value) {
    char digits[11];
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

static void net_debug_ipv4(uint32_t ip) {
    net_debug_u32((ip >> 24u) & 0xffu);
    debug_log_puts(".");
    net_debug_u32((ip >> 16u) & 0xffu);
    debug_log_puts(".");
    net_debug_u32((ip >> 8u) & 0xffu);
    debug_log_puts(".");
    net_debug_u32(ip & 0xffu);
}

static ssize_t socket_read_op(void *ctx, void *buffer, size_t count) {
    ladybird_socket_ctx_t *socket = ctx;
    size_t size = count;

    if (socket == NULL || !socket->connected || buffer == NULL) {
        return set_socket_errno(ENOTCONN);
    }

    if (socket->tls_handle >= 0) {
        if (tls_recv_timeout(socket->tls_handle, buffer, &size, socket_timeout_to_polls(socket->recv_timeout_ms, 220000u)) != 0) {
            return set_socket_ctx_errno(socket, ECONNRESET);
        }
        if (size == 0u) {
            socket->connected = 0u;
            return 0;
        }
        return (ssize_t)size;
    }

    if (socket->tcp_handle < 0 || tcp_recv_timeout(socket->tcp_handle, buffer, &size, socket_timeout_to_polls(socket->recv_timeout_ms, 180000u)) != 0) {
        return set_socket_ctx_errno(socket, ECONNRESET);
    }
    if (size == 0u) {
        socket->connected = 0u;
        return 0;
    }

    return (ssize_t)size;
}

static ssize_t socket_write_op(void *ctx, const void *buffer, size_t count) {
    ladybird_socket_ctx_t *socket = ctx;

    if (socket == NULL || !socket->connected || buffer == NULL) {
        return set_socket_errno(ENOTCONN);
    }

    if (socket->tls_handle >= 0) {
        if (tls_send_timeout(socket->tls_handle, buffer, count, socket_timeout_to_polls(socket->send_timeout_ms, 220000u)) != 0) {
            return set_socket_ctx_errno(socket, EPIPE);
        }
        return (ssize_t)count;
    }

    if (socket->tcp_handle < 0 || tcp_send_timeout(socket->tcp_handle, buffer, count, socket_timeout_to_polls(socket->send_timeout_ms, 180000u)) != 0) {
        return set_socket_ctx_errno(socket, EPIPE);
    }

    return (ssize_t)count;
}

static int socket_close_op(void *ctx) {
    ladybird_socket_ctx_t *socket = ctx;

    if (socket == NULL) {
        return 0;
    }

    if (socket->connected) {
        if (socket->tls_handle >= 0) {
            tls_close(socket->tls_handle);
        } else if (socket->tcp_handle >= 0) {
            tcp_close(socket->tcp_handle);
        }
    }

    kfree(socket);
    return 0;
}

static int socket_fstat_op(void *ctx, struct stat *st) {
    (void)ctx;

    if (st == NULL) {
        return set_socket_errno_int(EINVAL);
    }

    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    return 0;
}

static int socket_seek_op(void *ctx, off_t offset, int whence, off_t *result) {
    (void)ctx;
    (void)offset;
    (void)whence;
    (void)result;
    return set_socket_errno_int(ESPIPE);
}

static int socket_fsync_op(void *ctx) {
    (void)ctx;
    return 0;
}

static int socket_poll_op(void *ctx, short events, short *revents) {
    ladybird_socket_ctx_t *socket = ctx;

    if (socket == NULL || revents == NULL) {
        return set_socket_errno_int(EINVAL);
    }

    *revents = 0;
    if (!socket->connected) {
        *revents = POLLHUP;
        return 0;
    }

    if (socket->last_error != 0) {
        *revents |= POLLERR;
    }

    if ((socket->tls_handle >= 0 && tls_is_closed(socket->tls_handle)) ||
        (socket->tcp_handle >= 0 && tcp_is_closed(socket->tcp_handle))) {
        *revents |= POLLHUP;
    }

    if ((events & POLLIN) != 0 &&
        ((socket->tls_handle >= 0 && tls_can_read(socket->tls_handle)) ||
         (socket->tcp_handle >= 0 && tcp_can_read(socket->tcp_handle)))) {
        *revents |= POLLIN;
    }
    if ((events & POLLOUT) != 0 &&
        ((socket->tls_handle >= 0 && tls_can_write(socket->tls_handle)) ||
         (socket->tcp_handle >= 0 && tcp_can_write(socket->tcp_handle)))) {
        *revents |= POLLOUT;
    }
    return 0;
}

static ssize_t socketpair_read_op(void *ctx, void *buffer, size_t count) {
    ladybird_socketpair_ctx_t *socketpair = ctx;
    int read_result;

    if (socketpair == NULL || buffer == NULL) {
        return set_socket_errno(EBADF);
    }

    read_result = pipe_read(socketpair->read_pipe_id, buffer, count);
    if (read_result < 0) {
        return set_socket_errno(EIO);
    }

    return (ssize_t)read_result;
}

static ssize_t socketpair_write_op(void *ctx, const void *buffer, size_t count) {
    ladybird_socketpair_ctx_t *socketpair = ctx;
    int write_result;

    if (socketpair == NULL || buffer == NULL) {
        return set_socket_errno(EBADF);
    }

    write_result = pipe_write(socketpair->write_pipe_id, buffer, count);
    if (write_result < 0) {
        return set_socket_errno(EIO);
    }

    return (ssize_t)write_result;
}

static int socketpair_close_op(void *ctx) {
    ladybird_socketpair_ctx_t *socketpair = ctx;

    if (socketpair == NULL) {
        return 0;
    }

    if (socketpair->shared_refs != NULL && *socketpair->shared_refs > 0u) {
        --(*socketpair->shared_refs);
        if (*socketpair->shared_refs == 0u) {
            pipe_close(socketpair->read_pipe_id);
            pipe_close(socketpair->write_pipe_id);
            kfree(socketpair->shared_refs);
        }
    }

    kfree(socketpair);
    return 0;
}

static int socketpair_fstat_op(void *ctx, struct stat *st) {
    (void)ctx;

    if (st == NULL) {
        return set_socket_errno_int(EINVAL);
    }

    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    return 0;
}

static int socketpair_seek_op(void *ctx, off_t offset, int whence, off_t *result) {
    (void)ctx;
    (void)offset;
    (void)whence;
    (void)result;
    return set_socket_errno_int(ESPIPE);
}

static int socketpair_fsync_op(void *ctx) {
    (void)ctx;
    return 0;
}

static int socketpair_poll_op(void *ctx, short events, short *revents) {
    ladybird_socketpair_ctx_t *socketpair = ctx;

    if (socketpair == NULL || revents == NULL) {
        return set_socket_errno_int(EINVAL);
    }

    *revents = 0;
    if ((events & POLLIN) != 0 && pipe_readable(socketpair->read_pipe_id) != 0u) {
        *revents |= POLLIN;
    }
    if ((events & POLLOUT) != 0 && pipe_writable(socketpair->write_pipe_id) != 0u) {
        *revents |= POLLOUT;
    }
    return 0;
}

int socket(int domain, int type, int protocol) {
    ladybird_socket_ctx_t *socket_ctx;
    ladybird_fd_object_t *object;

    ladybird_posix_init();
    if (domain != AF_INET || type != SOCK_STREAM) {
        return set_socket_errno_int(EAFNOSUPPORT);
    }

    socket_ctx = kmalloc(sizeof(*socket_ctx));
    if (socket_ctx == NULL) {
        return set_socket_errno_int(ENOMEM);
    }

    memset(socket_ctx, 0, sizeof(*socket_ctx));
    socket_ctx->domain = domain;
    socket_ctx->type = type;
    socket_ctx->protocol = protocol;
    socket_ctx->tls_handle = -1;
    socket_ctx->tcp_handle = -1;
    socket_ctx->last_error = 0;
    socket_ctx->recv_timeout_ms = 0;
    socket_ctx->send_timeout_ms = 0;

    object = ladybird_fd_object_create(socket_ctx, &socket_ops);
    if (object == NULL) {
        kfree(socket_ctx);
        return set_socket_errno_int(ENOMEM);
    }

    return ladybird_fd_install(object, -1);
}

int connect(int sockfd, const struct sockaddr *addr, uint32_t addrlen) {
    ladybird_fd_object_t *object;
    ladybird_socket_ctx_t *socket_ctx;
    const struct sockaddr_in *in_addr;

    ladybird_posix_init();
    object = ladybird_fd_acquire(sockfd);
    if (object == NULL) {
        return set_socket_errno_int(EBADF);
    }

    socket_ctx = (ladybird_socket_ctx_t *)object->ctx;
    if (socket_ctx == NULL || addr == NULL || addrlen < sizeof(struct sockaddr_in)) {
        ladybird_fd_release(object);
        return set_socket_errno_int(EINVAL);
    }

    in_addr = (const struct sockaddr_in *)addr;
    if (in_addr->sin_family != AF_INET) {
        ladybird_fd_release(object);
        return set_socket_errno_int(EAFNOSUPPORT);
    }

    socket_ctx->ip = in_addr->sin_addr.s_addr;
    socket_ctx->port = ntohs16(in_addr->sin_port);
    socket_ctx->hostname[0] = '\0';

    if (socket_ctx->port == 80u) {
        socket_ctx->tcp_handle = tcp_connect(socket_ctx->ip, socket_ctx->port);
    } else {
        socket_ctx->tls_handle = tls_connect(socket_ctx->ip, socket_ctx->port, socket_ctx->hostname[0] != '\0' ? socket_ctx->hostname : "webkit");
    }
    if (socket_ctx->tls_handle < 0 && socket_ctx->tcp_handle < 0) {
        ladybird_fd_release(object);
        return set_socket_errno_int(ECONNREFUSED);
    }

    socket_ctx->connected = 1u;
    ladybird_fd_release(object);
    return 0;
}

ssize_t send(int sockfd, const void *buffer, size_t length, int flags) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(sockfd);
    ssize_t result;

    (void)flags;
    if (object == NULL || object->ops == NULL || object->ops->write == NULL) {
        return set_socket_errno(EBADF);
    }

    result = object->ops->write(object->ctx, buffer, length);
    ladybird_fd_release(object);
    return result;
}

ssize_t recv(int sockfd, void *buffer, size_t length, int flags) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(sockfd);
    ssize_t result;

    (void)flags;
    if (object == NULL || object->ops == NULL || object->ops->read == NULL) {
        return set_socket_errno(EBADF);
    }

    result = object->ops->read(object->ctx, buffer, length);
    ladybird_fd_release(object);
    return result;
}

int ladybird_connect_hostname(int sockfd, const char *hostname, uint16_t port) {
    ladybird_fd_object_t *object = ladybird_fd_acquire(sockfd);
    ladybird_socket_ctx_t *socket_ctx;
    uint32_t ip = 0;

    debug_log_puts("LB net: connect_hostname ");
    debug_log_puts(hostname != NULL ? hostname : "(null)");
    debug_log_puts(":");
    net_debug_u32(port);
    debug_log_puts("\n");

    if (object == NULL) {
        debug_log_puts("LB net: bad fd\n");
        return set_socket_errno_int(EBADF);
    }

    socket_ctx = (ladybird_socket_ctx_t *)object->ctx;
    if (socket_ctx == NULL || hostname == NULL || hostname[0] == '\0' || port == 0u) {
        ladybird_fd_release(object);
        debug_log_puts("LB net: invalid connect args\n");
        return set_socket_errno_int(EINVAL);
    }

    if (dns_resolve(hostname, &ip) != 0 || ip == 0u) {
        ladybird_fd_release(object);
        debug_log_puts("LB net: dns failed\n");
        return set_socket_errno_int(ENETUNREACH);
    }
    debug_log_puts("LB net: dns ok ");
    net_debug_ipv4(ip);
    debug_log_puts("\n");

    socket_ctx->ip = ip;
    socket_ctx->port = port;
    copy_hostname(socket_ctx->hostname, sizeof(socket_ctx->hostname), hostname);
    if (port == 80u) {
        debug_log_puts("LB net: tcp_connect begin\n");
        socket_ctx->tcp_handle = tcp_connect(ip, port);
    } else {
        debug_log_puts("LB net: tls_connect begin\n");
        socket_ctx->tls_handle = tls_connect(ip, port, socket_ctx->hostname);
    }
    if (socket_ctx->tls_handle < 0 && socket_ctx->tcp_handle < 0) {
        ladybird_fd_release(object);
        debug_log_puts("LB net: transport connect failed\n");
        return set_socket_errno_int(ECONNREFUSED);
    }

    socket_ctx->connected = 1u;
    ladybird_fd_release(object);
    debug_log_puts("LB net: connect ok\n");
    return 0;
}

int bind(int sockfd, const struct sockaddr *addr, uint32_t addrlen) {
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return set_socket_errno_int(ENOSYS);
}

int listen(int sockfd, int backlog) {
    (void)sockfd;
    (void)backlog;
    return set_socket_errno_int(ENOSYS);
}

int accept(int sockfd, struct sockaddr *addr, uint32_t *addrlen) {
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return set_socket_errno_int(ENOSYS);
}

int setsockopt(int sockfd, int level, int option_name, const void *option_value, uint32_t option_len) {
    ladybird_fd_object_t *object;
    ladybird_socket_ctx_t *socket_ctx;

    object = ladybird_fd_acquire(sockfd);
    if (object == NULL) {
        return set_socket_errno_int(EBADF);
    }

    socket_ctx = (ladybird_socket_ctx_t *)object->ctx;
    if (socket_ctx == NULL || option_value == NULL) {
        ladybird_fd_release(object);
        return set_socket_ctx_errno_int(socket_ctx, EINVAL);
    }

    if (level != SOL_SOCKET) {
        ladybird_fd_release(object);
        return set_socket_ctx_errno_int(socket_ctx, ENOTSUP);
    }

    switch (option_name) {
    case SO_RCVTIMEO:
        if (option_len < sizeof(struct timeval)) {
            ladybird_fd_release(object);
            return set_socket_ctx_errno_int(socket_ctx, EINVAL);
        }
        socket_ctx->recv_timeout_ms = timeval_to_millis((const struct timeval *)option_value);
        break;
    case SO_SNDTIMEO:
        if (option_len < sizeof(struct timeval)) {
            ladybird_fd_release(object);
            return set_socket_ctx_errno_int(socket_ctx, EINVAL);
        }
        socket_ctx->send_timeout_ms = timeval_to_millis((const struct timeval *)option_value);
        break;
    default:
        ladybird_fd_release(object);
        return set_socket_ctx_errno_int(socket_ctx, ENOTSUP);
    }

    socket_ctx->last_error = 0;
    ladybird_fd_release(object);
    return 0;
}

int getsockopt(int sockfd, int level, int option_name, void *option_value, uint32_t *option_len) {
    ladybird_fd_object_t *object;
    ladybird_socket_ctx_t *socket_ctx;

    object = ladybird_fd_acquire(sockfd);
    if (object == NULL) {
        return set_socket_errno_int(EBADF);
    }

    socket_ctx = (ladybird_socket_ctx_t *)object->ctx;
    if (socket_ctx == NULL || option_value == NULL || option_len == NULL) {
        ladybird_fd_release(object);
        return set_socket_ctx_errno_int(socket_ctx, EINVAL);
    }

    if (level != SOL_SOCKET) {
        ladybird_fd_release(object);
        return set_socket_ctx_errno_int(socket_ctx, ENOTSUP);
    }

    switch (option_name) {
    case SO_TYPE:
        if (*option_len < sizeof(int)) {
            ladybird_fd_release(object);
            return set_socket_ctx_errno_int(socket_ctx, EINVAL);
        }
        *(int *)option_value = socket_ctx->type;
        *option_len = (uint32_t)sizeof(int);
        break;
    case SO_ERROR:
        if (*option_len < sizeof(int)) {
            ladybird_fd_release(object);
            return set_socket_ctx_errno_int(socket_ctx, EINVAL);
        }
        *(int *)option_value = socket_ctx->last_error;
        *option_len = (uint32_t)sizeof(int);
        socket_ctx->last_error = 0;
        break;
    case SO_RCVTIMEO:
        if (*option_len < sizeof(struct timeval)) {
            ladybird_fd_release(object);
            return set_socket_ctx_errno_int(socket_ctx, EINVAL);
        }
        millis_to_timeval(socket_ctx->recv_timeout_ms, (struct timeval *)option_value);
        *option_len = (uint32_t)sizeof(struct timeval);
        break;
    case SO_SNDTIMEO:
        if (*option_len < sizeof(struct timeval)) {
            ladybird_fd_release(object);
            return set_socket_ctx_errno_int(socket_ctx, EINVAL);
        }
        millis_to_timeval(socket_ctx->send_timeout_ms, (struct timeval *)option_value);
        *option_len = (uint32_t)sizeof(struct timeval);
        break;
    default:
        ladybird_fd_release(object);
        return set_socket_ctx_errno_int(socket_ctx, ENOTSUP);
    }

    ladybird_fd_release(object);
    return 0;
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
    int pipe_a_to_b;
    int pipe_b_to_a;
    uint32_t *shared_refs;
    ladybird_socketpair_ctx_t *left_ctx;
    ladybird_socketpair_ctx_t *right_ctx;
    ladybird_fd_object_t *left_object;
    ladybird_fd_object_t *right_object;

    if (sv == NULL) {
        return set_socket_errno_int(EINVAL);
    }
    if ((domain != AF_UNSPEC && domain != AF_INET) || type != SOCK_STREAM || protocol != 0) {
        return set_socket_errno_int(EAFNOSUPPORT);
    }

    pipe_a_to_b = pipe_create();
    if (pipe_a_to_b < 0) {
        return set_socket_errno_int(ENFILE);
    }

    pipe_b_to_a = pipe_create();
    if (pipe_b_to_a < 0) {
        pipe_close(pipe_a_to_b);
        return set_socket_errno_int(ENFILE);
    }

    shared_refs = kmalloc(sizeof(*shared_refs));
    left_ctx = kmalloc(sizeof(*left_ctx));
    right_ctx = kmalloc(sizeof(*right_ctx));
    if (shared_refs == NULL || left_ctx == NULL || right_ctx == NULL) {
        pipe_close(pipe_a_to_b);
        pipe_close(pipe_b_to_a);
        kfree(shared_refs);
        kfree(left_ctx);
        kfree(right_ctx);
        return set_socket_errno_int(ENOMEM);
    }

    *shared_refs = 2u;
    left_ctx->read_pipe_id = pipe_b_to_a;
    left_ctx->write_pipe_id = pipe_a_to_b;
    left_ctx->shared_refs = shared_refs;
    right_ctx->read_pipe_id = pipe_a_to_b;
    right_ctx->write_pipe_id = pipe_b_to_a;
    right_ctx->shared_refs = shared_refs;

    left_object = ladybird_fd_object_create(left_ctx, &socketpair_ops);
    right_object = ladybird_fd_object_create(right_ctx, &socketpair_ops);
    if (left_object == NULL || right_object == NULL) {
        if (left_object != NULL) {
            ladybird_fd_release(left_object);
        }
        if (right_object != NULL) {
            ladybird_fd_release(right_object);
        }
        return set_socket_errno_int(ENOMEM);
    }

    sv[0] = ladybird_fd_install(left_object, -1);
    if (sv[0] < 0) {
        return -1;
    }

    sv[1] = ladybird_fd_install(right_object, -1);
    if (sv[1] < 0) {
        close(sv[0]);
        sv[0] = -1;
        return -1;
    }

    return 0;
}
