#include <unos/vex_legacy_compat.h>
#include <unos/fd_borrow.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/debug_log.h>
#include <string.h>
#include <unos/dns.h>
#include <unos/heap.h>
#include <unos/ipc.h>
#include <unos/message_ipc.h>
#include <unos/rendezvous.h>
#include <unos/security.h>
#include <unos/scheduler.h>
#include <unos/tcp.h>
#include <unos/udp.h>
#include <unos/ipv4.h>
#include <unos/tls.h>
#include <unos/irq.h>

typedef struct ladybird_socket_ctx {
    int domain;
    int type;
    int protocol;
    int tls_handle;
    int tcp_handle;
    int udp_handle;
    int last_error;
    uint16_t port;
    uint32_t ip;
    uint32_t recv_timeout_ms;
    uint32_t send_timeout_ms;
    char hostname[256];
    uint8_t connected,connecting,nonblock;
} ladybird_socket_ctx_t;

typedef struct ladybird_socketpair_ctx {
    int read_pipe_id;
    int write_pipe_id;
    uint32_t *shared_refs;
    vex_message_endpoint_t *message_endpoint;
    uint8_t message_mode;
    vex_web_fd_object_t *origin;
    void (*close_hook)(void *);
    void *hook_context;
    uint8_t transferable_hook;
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
    socket_poll_op,
    NULL,
    NULL
};

static const ladybird_fd_ops_t socketpair_ops = {
    socketpair_read_op,
    socketpair_write_op,
    socketpair_close_op,
    socketpair_fstat_op,
    socketpair_seek_op,
    socketpair_fsync_op,
    socketpair_poll_op,
    NULL,
    NULL
};

void vex_message_fd_admission(vex_web_fd_object_t *o,unsigned rights){vex_message_endpoint_admission(((ladybird_socketpair_ctx_t *)o->ctx)->message_endpoint,rights);}
unsigned vex_message_fd_rights(vex_web_fd_object_t *o){
    if(!o || o->ops!=&socketpair_ops || !((ladybird_socketpair_ctx_t *)o->ctx)->message_mode)return 0;
    return vex_message_endpoint_rights(((ladybird_socketpair_ctx_t *)o->ctx)->message_endpoint);
}
int vex_message_fd_has_lifetime(vex_web_fd_object_t *o){
    if(!o || o->ops!=&socketpair_ops || !o->ctx || !((ladybird_socketpair_ctx_t *)o->ctx)->message_mode)return 0;
    while(o){ladybird_socketpair_ctx_t *c=o->ctx;if(c->transferable_hook)return 1;o=c->origin;}return 0;
}
void vex_message_fd_lifetime_hook(vex_web_fd_object_t *o,void (*hook)(void *),void *context){
    ladybird_socketpair_ctx_t *c=o->ctx;c->transferable_hook=1;c->hook_context=context;__atomic_store_n(&c->close_hook,hook,__ATOMIC_RELEASE);
}
int vex_message_fd_pair(vex_web_fd_object_t **left,vex_web_fd_object_t **right){
    *left=*right=NULL;vex_message_endpoint_t *a,*b;if(vex_message_pair_create(0,&a,&b))return -ENOMEM;
    ladybird_socketpair_ctx_t *x=kmalloc(sizeof(*x)),*y=kmalloc(sizeof(*y));
    if(!x || !y){kfree(x);kfree(y);vex_message_endpoint_close(a);vex_message_endpoint_close(b);return -ENOMEM;}
    memset(x,0,sizeof(*x));memset(y,0,sizeof(*y));x->message_endpoint=a;x->message_mode=1;y->message_endpoint=b;y->message_mode=1;
    *left=ladybird_fd_object_create(x,&socketpair_ops);*right=ladybird_fd_object_create(y,&socketpair_ops);
    if(!*left || !*right){if(*left)ladybird_fd_release(*left);else {vex_message_endpoint_close(a);kfree(x);}
        if(*right)ladybird_fd_release(*right);else {vex_message_endpoint_close(b);kfree(y);}*left=*right=NULL;return -ENOMEM;}return 0;
}
void vex_message_fd_close_hook(vex_web_fd_object_t *o,void (*hook)(void *),void *context) {
    ladybird_socketpair_ctx_t *c=o->ctx;
    if(hook){c->hook_context=context;__atomic_store_n(&c->close_hook,hook,__ATOMIC_RELEASE);}
    else {__atomic_store_n(&c->close_hook,NULL,__ATOMIC_RELEASE);c->hook_context=NULL;}
}
int vex_message_fd_cancel(vex_web_fd_object_t *o) {
    if(!o || o->ops!=&socketpair_ops || !((ladybird_socketpair_ctx_t *)o->ctx)->message_mode)return -EINVAL;
    return vex_message_endpoint_cancel(((ladybird_socketpair_ctx_t *)o->ctx)->message_endpoint);
}
int vex_message_fd_revoke(vex_web_fd_object_t *o){
    if(!o || o->ops!=&socketpair_ops || !((ladybird_socketpair_ctx_t *)o->ctx)->message_mode)return -EINVAL;
    return vex_message_endpoint_revoke(((ladybird_socketpair_ctx_t *)o->ctx)->message_endpoint);
}
void *vex_message_fd_hook_context(vex_web_fd_object_t *o,void (*hook)(void *)) {
    if(!o || o->ops!=&socketpair_ops || !o->ctx || !((ladybird_socketpair_ctx_t *)o->ctx)->message_mode)return NULL;
    ladybird_socketpair_ctx_t *c=o->ctx;return c->close_hook==hook?c->hook_context:NULL;
}
int vex_message_fd_transfer_allowed(vex_web_fd_object_t *o) {
    if(!o || o->ops!=&socketpair_ops || !((ladybird_socketpair_ctx_t *)o->ctx)->message_mode)return 1;
    for(vex_web_fd_object_t *a=o;a;) {
        ladybird_socketpair_ctx_t *c=a->ctx;
        if(__atomic_load_n(&c->close_hook,__ATOMIC_ACQUIRE) && !c->transferable_hook)return 0; /* Commit a reservation before delegation. */
        a=c->origin;
    }
    return (vex_message_endpoint_rights(((ladybird_socketpair_ctx_t *)o->ctx)->message_endpoint)&VEX_MESSAGE_RIGHT_TRANSFER)!=0;
}
vex_web_fd_object_t *vex_message_fd_clone(vex_web_fd_object_t *o,unsigned rights,int *error) {
    *error=-EINVAL;
    if(!o || o->ops!=&socketpair_ops || !rights || (rights&~VEX_MESSAGE_RIGHT_ALL))return NULL;
    ladybird_socketpair_ctx_t *old=o->ctx;
    if(!old || !old->message_mode)return NULL;
    *error=-EACCES;if(rights&~vex_message_endpoint_rights(old->message_endpoint))return NULL;
    *error=-ENOMEM;ladybird_socketpair_ctx_t *ctx=kmalloc(sizeof(*ctx));if(!ctx)return NULL;
    memset(ctx,0,sizeof(*ctx));ctx->message_mode=1;
    ctx->message_endpoint=vex_message_endpoint_clone(old->message_endpoint,rights);
    if(!ctx->message_endpoint){kfree(ctx);return NULL;}
    /* Flatten ordinary view chains. A reservation-owning object is retained
       as origin so cloning cannot prematurely abort its pending handoff. */
    ctx->origin=__atomic_load_n(&old->close_hook,__ATOMIC_ACQUIRE)?o:(old->origin?old->origin:o);
    for(;;) {
        ladybird_socketpair_ctx_t *parent=ctx->origin->ctx;
        if(__atomic_load_n(&parent->close_hook,__ATOMIC_ACQUIRE) || !parent->origin)break;
        ctx->origin=parent->origin;
    }
    uint32_t refs=__atomic_load_n(&ctx->origin->refcount,__ATOMIC_ACQUIRE);
    do {if(!refs || refs==UINT32_MAX){vex_message_endpoint_close(ctx->message_endpoint);kfree(ctx);return NULL;}}
    while(!__atomic_compare_exchange_n(&ctx->origin->refcount,&refs,refs+1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
    vex_web_fd_object_t *copy=ladybird_fd_object_create(ctx,&socketpair_ops);
    if(!copy){ladybird_fd_release(ctx->origin);vex_message_endpoint_close(ctx->message_endpoint);kfree(ctx);return NULL;}
    *error=0;return copy;
}
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

static int network_socket_allowed(void) {
    return scheduler_current_user_mode()==0u ||
        scheduler_current_has_capability(VEX_CAP_NET_ADMIN) ||
        scheduler_current_has_capability(VEX_CAP_NET_CONNECT);
}
static int network_connect_allowed(uint16_t port) {
    return port!=0u && network_socket_allowed();
}

static void socket_refresh(ladybird_socket_ctx_t *s){
    if(s->connecting){int status=tcp_connection_status(s->tcp_handle);
        if(status){s->connecting=0;s->connected=status>0;
            if(status<0)s->last_error=tcp_connection_error(s->tcp_handle)==2?ETIMEDOUT:ECONNREFUSED;}}
}
int vex_socket_fault_errno(const udp_transport_fault_t *f){
    if(f->type==11)return 113;
    if(f->type==12)return 71;
    switch(f->code){case 0:return 101;case 2:return 92;case 3:return 111;
        case 4:return 90;case 5:return 95;case 9:case 10:case 13:return 13;default:return 113;}
}
static int socket_udp_error(ladybird_socket_ctx_t *s){udp_transport_fault_t fault;
    if(udp_endpoint_fault_status(s->udp_handle,&fault,1))return vex_socket_fault_errno(&fault);
    return s->udp_handle>=0?udp_endpoint_error(s->udp_handle):0;
}
static ssize_t socket_read_mode(ladybird_socket_ctx_t *s,void *buffer,size_t count,int nonblock){
    if(!count)return 0;if(!s || !buffer)return set_socket_errno(EINVAL);
    if(s->type==SOCK_DGRAM){
        int error=socket_udp_error(s);if(error)return set_socket_errno(error);
        for(;;){int n=udp_endpoint_receive(s->udp_handle,buffer,count,NULL,NULL,0,0);
            if(n!=-11)return n<0?set_socket_errno(-n):n;if(nonblock)return set_socket_errno(EAGAIN);process_sleep_ticks(1);}
    }
    socket_refresh(s);if(!s->connected)return set_socket_errno(s->last_error?s->last_error:ENOTCONN);
    if(s->tls_handle>=0){size_t size=count;
        if(nonblock && !tls_can_read(s->tls_handle))return set_socket_errno(EAGAIN);
        if(tls_recv_timeout(s->tls_handle,buffer,&size,socket_timeout_to_polls(s->recv_timeout_ms,220000u)))return set_socket_ctx_errno(s,ECONNRESET);
        return (ssize_t)size;
    }
    uint64_t start=irq_timer_ticks(),limit=(s->recv_timeout_ms+9u)/10u;
    for(;;){int64_t n=tcp_try_read(s->tcp_handle,buffer,count);
        if(n>=0)return n;if(n!=-2)return set_socket_ctx_errno(s,ECONNRESET);
        if(nonblock || (limit && irq_timer_ticks()-start>=limit))return set_socket_errno(EAGAIN);
        process_sleep_ticks(1);
    }
}
static ssize_t socket_write_mode(ladybird_socket_ctx_t *s,const void *buffer,size_t count,int nonblock){
    if(!count)return 0;if(!s || !buffer)return set_socket_errno(EINVAL);
    if(s->type==SOCK_DGRAM){
        int error=socket_udp_error(s);if(error)return set_socket_errno(error);
        if(!s->connected)return set_socket_errno(ENOTCONN);
        for(;;){int n=udp_endpoint_send(s->udp_handle,s->ip,s->port,buffer,count);
            if(n!=-11)return n<0?set_socket_errno(-n):n;if(nonblock)return set_socket_errno(EAGAIN);process_sleep_ticks(1);}
    }
    socket_refresh(s);if(!s->connected)return set_socket_errno(s->last_error?s->last_error:ENOTCONN);
    if(s->tls_handle>=0){
        if(nonblock && !tls_can_write(s->tls_handle))return set_socket_errno(EAGAIN);
        if(tls_send_timeout(s->tls_handle,buffer,count,socket_timeout_to_polls(s->send_timeout_ms,220000u)))return set_socket_ctx_errno(s,EPIPE);
        return (ssize_t)count;
    }
    uint64_t start=irq_timer_ticks(),limit=(s->send_timeout_ms+9u)/10u;
    for(;;){int64_t n=tcp_try_write(s->tcp_handle,buffer,count);
        if(n>=0)return n;if(n!=-2)return set_socket_ctx_errno(s,EPIPE);
        if(nonblock || (limit && irq_timer_ticks()-start>=limit))return set_socket_errno(EAGAIN);
        process_sleep_ticks(1);
    }
}
static ssize_t socket_read_op(void *ctx,void *buffer,size_t count){ladybird_socket_ctx_t *s=ctx;return socket_read_mode(s,buffer,count,s?s->nonblock:1);}
static ssize_t socket_write_op(void *ctx,const void *buffer,size_t count){ladybird_socket_ctx_t *s=ctx;return socket_write_mode(s,buffer,count,s?s->nonblock:1);}

static int socket_close_op(void *ctx) {
    ladybird_socket_ctx_t *socket = ctx;

    if (socket == NULL) {
        return 0;
    }

    if (socket->tls_handle>=0 || socket->tcp_handle>=0) {
        if (socket->tls_handle >= 0) {
            tls_close(socket->tls_handle);
        } else if (socket->tcp_handle >= 0) {
            tcp_close(socket->tcp_handle);
        }
    }

    if(socket->udp_handle>=0)udp_endpoint_close(socket->udp_handle);
    kfree(socket);
    return 0;
}

static int socket_fstat_op(void *ctx, struct stat *st) {
    (void)ctx;

    if (st == NULL) {
        return set_socket_errno_int(EINVAL);
    }

    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFSOCK;
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
    if(socket->type==SOCK_DGRAM){
        if(udp_endpoint_fault_pending(socket->udp_handle))*revents|=POLLERR;
        if((events&POLLIN) && udp_endpoint_readable(socket->udp_handle))*revents|=POLLIN;
        if((events&POLLOUT) && (socket->udp_handle<0 || udp_endpoint_writable(socket->udp_handle)))*revents|=POLLOUT;
        return 0;
    }socket_refresh(socket);
    if(socket->connecting)return 0;
    if (!socket->connected) {
        *revents = POLLHUP | (socket->last_error?POLLERR|POLLOUT:0);
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

    if (socketpair->message_mode != 0u) {
        read_result = (int)vex_message_receive(socketpair->message_endpoint,
                                                buffer, count, 0u);
    } else {
        read_result = pipe_read(socketpair->read_pipe_id, buffer, count);
    }
    if (read_result < 0) {
        return set_socket_errno(socketpair->message_mode != 0u ? -read_result : EIO);
    }

    return (ssize_t)read_result;
}

static ssize_t socketpair_write_op(void *ctx, const void *buffer, size_t count) {
    ladybird_socketpair_ctx_t *socketpair = ctx;
    int write_result;

    if (socketpair == NULL || buffer == NULL) {
        return set_socket_errno(EBADF);
    }

    if (socketpair->message_mode != 0u) {
        write_result = (int)vex_message_send(socketpair->message_endpoint,
                                              buffer, count, 0u);
    } else {
        write_result = pipe_write(socketpair->write_pipe_id, buffer, count);
    }
    if (write_result < 0) {
        return set_socket_errno(socketpair->message_mode != 0u ? -write_result : EIO);
    }

    return (ssize_t)write_result;
}

static int socketpair_close_op(void *ctx) {
    ladybird_socketpair_ctx_t *socketpair = ctx;

    if (socketpair == NULL) {
        return 0;
    }

    if (socketpair->message_mode != 0u) {
        if(socketpair->close_hook)socketpair->close_hook(socketpair->hook_context);
        (void)vex_message_endpoint_close(socketpair->message_endpoint);
        if(socketpair->origin)ladybird_fd_release(socketpair->origin);
        kfree(socketpair);
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
    if (socketpair->message_mode != 0u) {
        const int result = vex_message_poll(socketpair->message_endpoint, events, revents);
        return result == 0 ? 0 : set_socket_errno_int(-result);
    }
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
    if (domain != AF_INET) {
        return set_socket_errno_int(EAFNOSUPPORT);
    }
    int socket_type=type & ~(SOCK_NONBLOCK|SOCK_CLOEXEC);
    if((socket_type!=SOCK_STREAM && socket_type!=SOCK_DGRAM) || (protocol!=0 && protocol!=(socket_type==SOCK_DGRAM?17:6)))return set_socket_errno_int(ENOTSUP);
    if (!network_socket_allowed()) {
        debug_log_puts("net-socket-deny: owner=");
        net_debug_u32(scheduler_current_pid());
        debug_log_puts(" tid=");
        net_debug_u32(scheduler_current_tid());
        debug_log_puts(" ppid=");
        net_debug_u32(scheduler_current_ppid());
        debug_log_puts(" caps=");
        net_debug_u32(scheduler_current_capabilities());
        debug_log_puts(" user=");
        net_debug_u32(scheduler_current_user_mode());
        debug_log_puts(" abi=");
        net_debug_u32(scheduler_current_abi());
        debug_log_puts("\n");
        security_audit_log_event("net-socket", "policy", scheduler_current_pid(),
                                 scheduler_current_uid(), scheduler_current_capabilities(), 0u);
        return set_socket_errno_int(EPERM);
    }

    socket_ctx = kmalloc(sizeof(*socket_ctx));
    if (socket_ctx == NULL) {
        return set_socket_errno_int(ENOMEM);
    }

    memset(socket_ctx, 0, sizeof(*socket_ctx));
    socket_ctx->domain = domain;
    socket_ctx->type = socket_type;
    socket_ctx->nonblock=(type&SOCK_NONBLOCK)!=0;
    socket_ctx->protocol = protocol;
    socket_ctx->tls_handle = -1;
    socket_ctx->tcp_handle = -1;socket_ctx->udp_handle=-1;
    socket_ctx->last_error = 0;
    socket_ctx->recv_timeout_ms = 0;
    socket_ctx->send_timeout_ms = 0;

    object = ladybird_fd_object_create(socket_ctx, &socket_ops);
    if (object == NULL) {
        kfree(socket_ctx);
        return set_socket_errno_int(ENOMEM);
    }

    return ladybird_fd_install_flags(object, -1, (type&SOCK_CLOEXEC)!=0);
}

int connect(int sockfd, const struct sockaddr *addr, uint32_t addrlen) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object;
    ladybird_socket_ctx_t *socket_ctx;
    const struct sockaddr_in *in_addr;

    ladybird_posix_init();
    object = vex_fd_borrow_acquire(&operation_ref,sockfd);
    if (object == NULL) {
        return set_socket_errno_int(EBADF);
    }

    if(object->ops!=&socket_ops){vex_fd_borrow_release(&operation_ref);return set_socket_errno_int(ENOTSOCK);}
    socket_ctx = (ladybird_socket_ctx_t *)object->ctx;
    socket_refresh(socket_ctx);
    if(socket_ctx->connected || socket_ctx->connecting){int error=socket_ctx->connected?EISCONN:EALREADY;vex_fd_borrow_release(&operation_ref);return set_socket_errno_int(error);}
    if (socket_ctx == NULL || addr == NULL || addrlen < sizeof(struct sockaddr_in)) {
        vex_fd_borrow_release(&operation_ref);
        return set_socket_errno_int(EINVAL);
    }

    in_addr = (const struct sockaddr_in *)addr;
    if (in_addr->sin_family != AF_INET) {
        vex_fd_borrow_release(&operation_ref);
        return set_socket_errno_int(EAFNOSUPPORT);
    }

    uint32_t value=in_addr->sin_addr.s_addr;
    socket_ctx->ip=((value&255u)<<24)|((value&0xff00u)<<8)|((value>>8)&0xff00u)|(value>>24);
    socket_ctx->port = ntohs16(in_addr->sin_port);
    socket_ctx->hostname[0] = '\0';
    if (!network_connect_allowed(socket_ctx->port)) {
        security_audit_log_event("net-connect", "policy", scheduler_current_pid(),
                                 scheduler_current_uid(), scheduler_current_capabilities(), 0u);
        vex_fd_borrow_release(&operation_ref);
        return set_socket_errno_int(EPERM);
    }

    if(socket_ctx->type==SOCK_DGRAM){
        if(socket_ctx->udp_handle<0)socket_ctx->udp_handle=udp_endpoint_open(0);
        int result=socket_ctx->udp_handle<0?socket_ctx->udp_handle:udp_endpoint_peer(socket_ctx->udp_handle,socket_ctx->ip,socket_ctx->port);
        socket_ctx->connected=!result;vex_fd_borrow_release(&operation_ref);return result<0?set_socket_errno_int(-result):0;
    }
    /* A socket is a raw transport. TLS is selected by its caller, not by port. */
    socket_ctx->tcp_handle=socket_ctx->nonblock?tcp_connect_start(socket_ctx->ip,socket_ctx->port):tcp_connect(socket_ctx->ip,socket_ctx->port);
    if(socket_ctx->tcp_handle<0){vex_fd_borrow_release(&operation_ref);return set_socket_errno_int(ECONNREFUSED);}
    if(socket_ctx->nonblock){socket_ctx->connecting=1;vex_fd_borrow_release(&operation_ref);return set_socket_errno_int(EINPROGRESS);}
    socket_ctx->connected = 1u;
    vex_fd_borrow_release(&operation_ref);
    return 0;
}

ssize_t send(int sockfd, const void *buffer, size_t length, int flags) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,sockfd);
    ssize_t result;

    if (object == NULL || (object->ops != &socket_ops && object->ops != &socketpair_ops)) {
        if(object)vex_fd_borrow_release(&operation_ref);return set_socket_errno(ENOTSOCK);
    }

    if (object->ops == &socketpair_ops &&
        ((ladybird_socketpair_ctx_t *)object->ctx)->message_mode != 0u) {
        ladybird_socketpair_ctx_t *pair = object->ctx;
        result = vex_message_send(pair->message_endpoint, buffer, length,
            (flags & MSG_DONTWAIT) != 0 ? VEX_MESSAGE_NONBLOCK : 0u);
        vex_fd_borrow_release(&operation_ref);
        return result < 0 ? set_socket_errno((int)-result) : result;
    }

    if(flags & ~(MSG_DONTWAIT|0x4000)){vex_fd_borrow_release(&operation_ref);return set_socket_errno(ENOTSUP);}
    result = object->ops==&socket_ops?socket_write_mode(object->ctx,buffer,length,
        ((ladybird_socket_ctx_t *)object->ctx)->nonblock || (flags&MSG_DONTWAIT)):
        object->ops->write(object->ctx,buffer,length);
    vex_fd_borrow_release(&operation_ref);
    return result;
}

ssize_t recv(int sockfd, void *buffer, size_t length, int flags) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,sockfd);
    ssize_t result;

    if (object == NULL || (object->ops != &socket_ops && object->ops != &socketpair_ops)) {
        if(object)vex_fd_borrow_release(&operation_ref);return set_socket_errno(ENOTSOCK);
    }

    if (object->ops == &socketpair_ops &&
        ((ladybird_socketpair_ctx_t *)object->ctx)->message_mode != 0u) {
        ladybird_socketpair_ctx_t *pair = object->ctx;
        result = vex_message_receive(pair->message_endpoint, buffer, length,
            (flags & MSG_DONTWAIT) != 0 ? VEX_MESSAGE_NONBLOCK : 0u);
        vex_fd_borrow_release(&operation_ref);
        return result < 0 ? set_socket_errno((int)-result) : result;
    }

    if(flags & ~MSG_DONTWAIT){vex_fd_borrow_release(&operation_ref);return set_socket_errno(ENOTSUP);}
    result = object->ops==&socket_ops?socket_read_mode(object->ctx,buffer,length,
        ((ladybird_socket_ctx_t *)object->ctx)->nonblock || (flags&MSG_DONTWAIT)):
        object->ops->read(object->ctx,buffer,length);
    vex_fd_borrow_release(&operation_ref);
    return result;
}

/* SCM_RIGHTS is translated at the Linux boundary into these native message
 * operations.  The endpoint still owns a Vex capability; this wrapper only
 * pins the FD object while the queue operation runs. */
ssize_t vex_socketpair_send_attached(int sockfd, const void *buffer,
                                     size_t length, int flags,
                                     vex_message_attachment_t *attachments,
                                     size_t attachment_count) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,sockfd);
    ladybird_socketpair_ctx_t *pair;
    ssize_t result;
    uint32_t native_flags;

    if (object == NULL || object->ops != &socketpair_ops ||
        object->ctx == NULL) {
        if (object != NULL) {
            vex_fd_borrow_release(&operation_ref);
        }
        return set_socket_errno(EBADF);
    }
    pair = (ladybird_socketpair_ctx_t *)object->ctx;
    if (pair->message_mode == 0u || pair->message_endpoint == NULL) {
        vex_fd_borrow_release(&operation_ref);
        return set_socket_errno(ENOTSUP);
    }
    native_flags = (flags & MSG_DONTWAIT) != 0 ? VEX_MESSAGE_NONBLOCK : 0u;
    result = vex_message_send_attached(pair->message_endpoint, buffer, length,
                                       attachments, attachment_count,
                                       native_flags);
    vex_fd_borrow_release(&operation_ref);
    if (result < 0) {
        return set_socket_errno((int)-result);
    }
    return result;
}

int vex_fd_message_observe(int fd,vex_message_observe_req_t *out) {
    VEX_FD_BORROW(ref);
    ladybird_fd_object_t *object=vex_fd_borrow_acquire(&ref,fd);
    if(!object || object->ops!=&socketpair_ops || !object->ctx)return -EBADF;
    ladybird_socketpair_ctx_t *pair=object->ctx;
    if(!pair->message_mode || !pair->message_endpoint)return -ENOTSUP;
    return vex_message_observe(pair->message_endpoint,out);
}

ssize_t vex_socketpair_receive_authenticated(int sockfd, void *buffer,
                                        size_t length, int flags,
                                        vex_message_attachment_t *attachments,
                                        size_t *attachment_count,vex_message_sender_t *sender) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,sockfd);
    ladybird_socketpair_ctx_t *pair;
    ssize_t result;
    uint32_t native_flags;

    if (object == NULL || object->ops != &socketpair_ops ||
        object->ctx == NULL) {
        if (object != NULL) {
            vex_fd_borrow_release(&operation_ref);
        }
        return set_socket_errno(EBADF);
    }
    pair = (ladybird_socketpair_ctx_t *)object->ctx;
    if (pair->message_mode == 0u || pair->message_endpoint == NULL) {
        vex_fd_borrow_release(&operation_ref);
        return set_socket_errno(ENOTSUP);
    }
    native_flags = (flags & MSG_DONTWAIT) != 0 ? VEX_MESSAGE_NONBLOCK : 0u;
    result = vex_message_receive_authenticated(pair->message_endpoint, buffer,
                                          length, attachments,
                                          attachment_count, native_flags,sender);
    vex_fd_borrow_release(&operation_ref);
    if (result < 0) {
        return set_socket_errno((int)-result);
    }
    return result;
}

ssize_t vex_socketpair_receive_attached(int fd,void *b,size_t n,int f,vex_message_attachment_t *a,size_t *c){
    return vex_socketpair_receive_authenticated(fd,b,n,f,a,c,NULL);
}

int ladybird_connect_hostname(int sockfd, const char *hostname, uint16_t port) {
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *object = vex_fd_borrow_acquire(&operation_ref,sockfd);
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
        vex_fd_borrow_release(&operation_ref);
        debug_log_puts("LB net: invalid connect args\n");
        return set_socket_errno_int(EINVAL);
    }

    if (dns_resolve(hostname, &ip) != 0 || ip == 0u) {
        vex_fd_borrow_release(&operation_ref);
        debug_log_puts("LB net: dns failed\n");
        return set_socket_errno_int(ENETUNREACH);
    }
    debug_log_puts("LB net: dns ok ");
    net_debug_ipv4(ip);
    debug_log_puts("\n");

    socket_ctx->ip = ip;
    socket_ctx->port = port;
    copy_hostname(socket_ctx->hostname, sizeof(socket_ctx->hostname), hostname);
    if (!network_connect_allowed(port)) {
        security_audit_log_event("net-connect", hostname, scheduler_current_pid(),
                                 scheduler_current_uid(), scheduler_current_capabilities(), 0u);
        vex_fd_borrow_release(&operation_ref);
        return set_socket_errno_int(EPERM);
    }
    if (port == 80u) {
        debug_log_puts("LB net: tcp_connect begin\n");
        socket_ctx->tcp_handle = tcp_connect(ip, port);
    } else {
        debug_log_puts("LB net: tls_connect begin\n");
        socket_ctx->tls_handle = tls_connect(ip, port, socket_ctx->hostname);
    }
    if (socket_ctx->tls_handle < 0 && socket_ctx->tcp_handle < 0) {
        vex_fd_borrow_release(&operation_ref);
        debug_log_puts("LB net: transport connect failed\n");
        return set_socket_errno_int(ECONNREFUSED);
    }

    socket_ctx->connected = 1u;
    vex_fd_borrow_release(&operation_ref);
    debug_log_puts("LB net: connect ok\n");
    return 0;
}

int bind(int sockfd, const struct sockaddr *addr, uint32_t addrlen) {
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    if (!scheduler_current_has_capability(VEX_CAP_NET_ADMIN)) {
        security_audit_log_event("net-bind", "policy", scheduler_current_pid(),
                                 scheduler_current_uid(), scheduler_current_capabilities(), 0u);
        return set_socket_errno_int(EPERM);
    }
    security_audit_log_event("net-bind", "unsupported", scheduler_current_pid(),
                             scheduler_current_uid(), scheduler_current_capabilities(), 0u);
    return set_socket_errno_int(ENOSYS);
}

int listen(int sockfd, int backlog) {
    (void)sockfd;
    (void)backlog;
    if (!scheduler_current_has_capability(VEX_CAP_NET_ADMIN)) {
        security_audit_log_event("net-listen", "policy", scheduler_current_pid(),
                                 scheduler_current_uid(), scheduler_current_capabilities(), 0u);
        return set_socket_errno_int(EPERM);
    }
    security_audit_log_event("net-listen", "unsupported", scheduler_current_pid(),
                             scheduler_current_uid(), scheduler_current_capabilities(), 0u);
    return set_socket_errno_int(ENOSYS);
}

int accept(int sockfd, struct sockaddr *addr, uint32_t *addrlen) {
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    if (!scheduler_current_has_capability(VEX_CAP_NET_ADMIN)) {
        security_audit_log_event("net-accept", "policy", scheduler_current_pid(),
                                 scheduler_current_uid(), scheduler_current_capabilities(), 0u);
        return set_socket_errno_int(EPERM);
    }
    security_audit_log_event("net-accept", "unsupported", scheduler_current_pid(),
                             scheduler_current_uid(), scheduler_current_capabilities(), 0u);
    return set_socket_errno_int(ENOSYS);
}

int setsockopt(int sockfd, int level, int option_name, const void *option_value, uint32_t option_len) {
    ladybird_fd_object_t *object;
    ladybird_socket_ctx_t *socket_ctx;

    object = ladybird_fd_acquire(sockfd);
    if (object == NULL) {
        return set_socket_errno_int(EBADF);
    }

    /* socketpair contexts are not network socket contexts.  In particular,
     * never interpret their endpoint pointer as `tcp_handle`/`last_error`:
     * those offsets overlap the native message endpoint and would corrupt its
     * lifetime state. */
    if (object->ops == &socketpair_ops) {
        ladybird_fd_release(object);
        return set_socket_errno_int(ENOTSUP);
    }

    if(object->ops!=&socket_ops){ladybird_fd_release(object);return set_socket_errno_int(ENOTSOCK);}
    socket_ctx = (ladybird_socket_ctx_t *)object->ctx;
    socket_refresh(socket_ctx);
    if (socket_ctx == NULL || option_value == NULL) {
        ladybird_fd_release(object);
        return set_socket_errno_int(EINVAL);
    }

    if(level==6 && option_name==1 && option_len>=sizeof(int) && *(const int *)option_value==1){
        ladybird_fd_release(object);return 0;
    }
    if(level==0 && option_name==11 && socket_ctx->type==SOCK_DGRAM){
        if(option_len<sizeof(int)){ladybird_fd_release(object);return set_socket_errno_int(EINVAL);}
        if(socket_ctx->udp_handle<0)socket_ctx->udp_handle=udp_endpoint_open(0);
        int r=socket_ctx->udp_handle<0?socket_ctx->udp_handle:
            udp_endpoint_fault_reporting(socket_ctx->udp_handle,*(const int *)option_value);
        ladybird_fd_release(object);return r<0?set_socket_errno_int(-r):0;
    }
    if (level != SOL_SOCKET) {
        ladybird_fd_release(object);
        return set_socket_errno_int(ENOTSUP);
    }

    switch (option_name) {
    case SO_RCVTIMEO:
        if (option_len < sizeof(struct timeval)) {
            ladybird_fd_release(object);
            return set_socket_errno_int(EINVAL);
        }
        socket_ctx->recv_timeout_ms = timeval_to_millis((const struct timeval *)option_value);
        break;
    case SO_SNDTIMEO:
        if (option_len < sizeof(struct timeval)) {
            ladybird_fd_release(object);
            return set_socket_errno_int(EINVAL);
        }
        socket_ctx->send_timeout_ms = timeval_to_millis((const struct timeval *)option_value);
        break;
    default:
        ladybird_fd_release(object);
        return set_socket_errno_int(ENOTSUP);
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

    if (object->ops == &socketpair_ops) {
        int value;

        if (level != SOL_SOCKET || object->ctx == NULL ||
            option_value == NULL || option_len == NULL) {
            ladybird_fd_release(object);
            return set_socket_errno_int(EINVAL);
        }
        switch (option_name) {
        case SO_TYPE:
            value = ((ladybird_socketpair_ctx_t *)object->ctx)->message_mode != 0u
                ? SOCK_SEQPACKET : SOCK_STREAM;
            break;
        case SO_ERROR:
            value = 0;
            break;
        default:
            ladybird_fd_release(object);
            return set_socket_errno_int(ENOTSUP);
        }
        if (*option_len < sizeof(value)) {
            ladybird_fd_release(object);
            return set_socket_errno_int(EINVAL);
        }
        *(int *)option_value = value;
        *option_len = (uint32_t)sizeof(value);
        ladybird_fd_release(object);
        return 0;
    }

    if(object->ops!=&socket_ops){ladybird_fd_release(object);return set_socket_errno_int(ENOTSOCK);}
    socket_ctx = (ladybird_socket_ctx_t *)object->ctx;
    socket_refresh(socket_ctx);
    if (socket_ctx == NULL || option_value == NULL || option_len == NULL) {
        ladybird_fd_release(object);
        return set_socket_errno_int(EINVAL);
    }

    if (level != SOL_SOCKET) {
        ladybird_fd_release(object);
        return set_socket_errno_int(ENOTSUP);
    }

    switch (option_name) {
    case SO_TYPE:
        if (*option_len < sizeof(int)) {
            ladybird_fd_release(object);
            return set_socket_errno_int(EINVAL);
        }
        *(int *)option_value = socket_ctx->type;
        *option_len = (uint32_t)sizeof(int);
        break;
    case SO_ERROR:
        if (*option_len < sizeof(int)) {
            ladybird_fd_release(object);
            return set_socket_errno_int(EINVAL);
        }
        *(int *)option_value = socket_ctx->type==SOCK_DGRAM?socket_udp_error(socket_ctx):socket_ctx->last_error;
        *option_len = (uint32_t)sizeof(int);
        socket_ctx->last_error = 0;
        break;
    case SO_RCVTIMEO:
        if (*option_len < sizeof(struct timeval)) {
            ladybird_fd_release(object);
            return set_socket_errno_int(EINVAL);
        }
        millis_to_timeval(socket_ctx->recv_timeout_ms, (struct timeval *)option_value);
        *option_len = (uint32_t)sizeof(struct timeval);
        break;
    case SO_SNDTIMEO:
        if (*option_len < sizeof(struct timeval)) {
            ladybird_fd_release(object);
            return set_socket_errno_int(EINVAL);
        }
        millis_to_timeval(socket_ctx->send_timeout_ms, (struct timeval *)option_value);
        *option_len = (uint32_t)sizeof(struct timeval);
        break;
    default:
        ladybird_fd_release(object);
        return set_socket_errno_int(ENOTSUP);
    }

    ladybird_fd_release(object);
    return 0;
}

/* A native message endpoint is a local, unnamed socketpair endpoint. Linux
 * needs AF_UNIX to classify the descriptor, but VexOS must not manufacture a
 * filesystem pathname or namespace for it. */
int vex_socketpair_getsockname(int sockfd, struct sockaddr *address,
                               uint32_t capacity, uint32_t *actual_length) {
    ladybird_fd_object_t *object;
    ladybird_socketpair_ctx_t *pair;

    if (actual_length == NULL) {
        return set_socket_errno_int(EINVAL);
    }
    object = ladybird_fd_acquire(sockfd);
    if (object == NULL) {
        return set_socket_errno_int(EBADF);
    }
    if(object->ops==&socket_ops){
        ladybird_socket_ctx_t *s=object->ctx;struct sockaddr_in a={.sin_family=AF_INET};
        uint16_t port=s->type==SOCK_DGRAM?udp_endpoint_port(s->udp_handle):tcp_local_port(s->tcp_handle);
        uint32_t ip=port?ipv4_get_local_ip():0;
        a.sin_port=ntohs16(port);a.sin_addr.s_addr=((ip&255u)<<24)|((ip&0xff00u)<<8)|((ip>>8)&0xff00u)|(ip>>24);
        *actual_length=sizeof(a);if(address)memcpy(address,&a,capacity<sizeof(a)?capacity:sizeof(a));
        ladybird_fd_release(object);return 0;
    }
    if (object->ops != &socketpair_ops || object->ctx == NULL) {
        ladybird_fd_release(object);
        return set_socket_errno_int(ENOTSUP);
    }
    pair = (ladybird_socketpair_ctx_t *)object->ctx;
    if (pair->message_mode == 0u || pair->message_endpoint == NULL) {
        ladybird_fd_release(object);
        return set_socket_errno_int(ENOTSUP);
    }

    *actual_length = (uint32_t)sizeof(uint16_t);
    if (address != NULL && capacity != 0u) {
        struct sockaddr unnamed;
        const uint32_t copied = capacity < *actual_length
            ? capacity : *actual_length;
        memset(&unnamed, 0, sizeof(unnamed));
        unnamed.sa_family = AF_UNIX;
        memcpy(address, &unnamed, copied);
    }
    ladybird_fd_release(object);
    return 0;
}

/* Both ends of an unnamed local socketpair have the same Linux-visible
 * address shape. The native endpoint identity remains the process-owned Vex
 * capability; this is only the compatibility metadata view. */
int vex_socketpair_getpeername(int sockfd, struct sockaddr *address,
                               uint32_t capacity, uint32_t *actual_length) {
    ladybird_fd_object_t *o=ladybird_fd_acquire(sockfd);if(!o)return set_socket_errno_int(EBADF);
    if(o->ops==&socket_ops){
        ladybird_socket_ctx_t *s=o->ctx;socket_refresh(s);
        if(!s->connected){ladybird_fd_release(o);return set_socket_errno_int(ENOTCONN);}
        struct sockaddr_in a={.sin_family=AF_INET,.sin_port=ntohs16(s->port)};uint32_t ip=s->ip;
        a.sin_addr.s_addr=((ip&255u)<<24)|((ip&0xff00u)<<8)|((ip>>8)&0xff00u)|(ip>>24);
        if(!actual_length){ladybird_fd_release(o);return set_socket_errno_int(EINVAL);}
        *actual_length=sizeof(a);if(address)memcpy(address,&a,capacity<sizeof(a)?capacity:sizeof(a));
        ladybird_fd_release(o);return 0;
    }ladybird_fd_release(o);
    return vex_socketpair_getsockname(sockfd, address, capacity, actual_length);
}

static int message_socketpair_create(uint32_t flags, int sv[2]) {
    vex_message_endpoint_t *left_endpoint = NULL;
    vex_message_endpoint_t *right_endpoint = NULL;
    ladybird_socketpair_ctx_t *left_ctx = NULL;
    ladybird_socketpair_ctx_t *right_ctx = NULL;
    ladybird_fd_object_t *left_object = NULL;
    ladybird_fd_object_t *right_object = NULL;
    const uint8_t cloexec = (flags & SOCK_CLOEXEC) != 0u;
    const uint32_t message_flags = (flags & SOCK_NONBLOCK) != 0u
        ? VEX_MESSAGE_NONBLOCK : 0u;

    if (sv == NULL || vex_message_pair_create(message_flags, &left_endpoint,
                                               &right_endpoint) != 0) {
        return set_socket_errno_int(ENOMEM);
    }
    left_ctx = kmalloc(sizeof(*left_ctx));
    right_ctx = kmalloc(sizeof(*right_ctx));
    if (left_ctx == NULL || right_ctx == NULL) {
        kfree(left_ctx);
        kfree(right_ctx);
        (void)vex_message_endpoint_close(left_endpoint);
        (void)vex_message_endpoint_close(right_endpoint);
        return set_socket_errno_int(ENOMEM);
    }
    memset(left_ctx, 0, sizeof(*left_ctx));
    memset(right_ctx, 0, sizeof(*right_ctx));
    left_ctx->message_endpoint = left_endpoint;
    left_ctx->message_mode = 1u;
    right_ctx->message_endpoint = right_endpoint;
    right_ctx->message_mode = 1u;
    left_object = ladybird_fd_object_create(left_ctx, &socketpair_ops);
    right_object = ladybird_fd_object_create(right_ctx, &socketpair_ops);
    if (left_object == NULL || right_object == NULL) {
        if (left_object != NULL) {
            ladybird_fd_release(left_object);
        } else {
            (void)vex_message_endpoint_close(left_endpoint);
            kfree(left_ctx);
        }
        if (right_object != NULL) {
            ladybird_fd_release(right_object);
        } else {
            (void)vex_message_endpoint_close(right_endpoint);
            kfree(right_ctx);
        }
        return set_socket_errno_int(ENOMEM);
    }
    sv[0] = ladybird_fd_install_flags(left_object, -1, cloexec);
    if (sv[0] < 0) {
        ladybird_fd_release(right_object);
        return -1;
    }
    sv[1] = ladybird_fd_install_flags(right_object, -1, cloexec);
    if (sv[1] < 0) {
        close(sv[0]);
        sv[0] = -1;
        return -1;
    }
    return 0;
}

int vex_native_message_pair(uint32_t flags, int sv[2]) {
    if ((flags & ~(uint32_t)(SOCK_NONBLOCK | SOCK_CLOEXEC)) != 0u) {
        return set_socket_errno_int(EINVAL);
    }
    return message_socketpair_create(flags, sv);
}

int vex_socketpair_fcntl(ladybird_fd_object_t *object, int command, int arg) {
    ladybird_socketpair_ctx_t *ctx;
    if(object && object->ops==&socket_ops && object->ctx){
        ladybird_socket_ctx_t *s=object->ctx;
        if(command==F_GETFL)return O_RDWR|(s->nonblock?O_NONBLOCK:0);
        if(command==F_SETFL){if(arg&~(O_ACCMODE|O_NONBLOCK))return set_socket_errno_int(EINVAL);s->nonblock=(arg&O_NONBLOCK)!=0;return 0;}
        return -2;
    }
    if (object == NULL || object->ops != &socketpair_ops) {
        return -2;
    }
    ctx = (ladybird_socketpair_ctx_t *)object->ctx;
    if (ctx == NULL || ctx->message_mode == 0u) {
        return -2;
    }
    if (command == F_GETFL) {
        return (vex_message_get_flags(ctx->message_endpoint) & VEX_MESSAGE_NONBLOCK) != 0u
            ? O_NONBLOCK : 0;
    }
    if (command == F_SETFL) {
        const uint32_t flags = (arg & O_NONBLOCK) != 0 ? VEX_MESSAGE_NONBLOCK : 0u;
        return vex_message_set_flags(ctx->message_endpoint, flags) == 0
            ? 0 : set_socket_errno_int(EINVAL);
    }
    return -2;
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
    if (domain == AF_UNIX && (type & 0xf) == SOCK_SEQPACKET && protocol == 0) {
        if ((type & ~(SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC)) != 0) {
            return set_socket_errno_int(EINVAL);
        }
        return message_socketpair_create((uint32_t)type &
            (uint32_t)(SOCK_NONBLOCK | SOCK_CLOEXEC), sv);
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

ssize_t vex_socket_sendto(int fd,const void *buffer,size_t length,int flags,const struct sockaddr *address,uint32_t address_length){
    VEX_FD_BORROW(operation_ref);
    if(!address)return send(fd,buffer,length,flags);
    if(flags&~(MSG_DONTWAIT|0x4000))return set_socket_errno(ENOTSUP);
    if(address_length<sizeof(struct sockaddr_in) || address->sa_family!=AF_INET)return set_socket_errno(EINVAL);
    ladybird_fd_object_t *o=vex_fd_borrow_acquire(&operation_ref,fd);
    if(!o)return set_socket_errno(EBADF);
    if(o->ops!=&socket_ops){vex_fd_borrow_release(&operation_ref);return set_socket_errno(ENOTSOCK);}
    ladybird_socket_ctx_t *s=o->ctx;const struct sockaddr_in *a=(const void *)address;
    uint32_t v=a->sin_addr.s_addr,ip=((v&255u)<<24)|((v&0xff00u)<<8)|((v>>8)&0xff00u)|(v>>24);
    uint16_t port=ntohs16(a->sin_port);
    if(s->type!=SOCK_DGRAM || !network_connect_allowed(port)){int e=s->type==SOCK_DGRAM?EPERM:ENOTSUP;vex_fd_borrow_release(&operation_ref);return set_socket_errno(e);}
    if(s->udp_handle<0)s->udp_handle=udp_endpoint_open(0);
    int error=socket_udp_error(s);if(error){vex_fd_borrow_release(&operation_ref);return set_socket_errno(error);}
    int n=s->udp_handle;
    if(n>=0)for(;;){n=udp_endpoint_send(s->udp_handle,ip,port,buffer,length);if(n!=-11 || s->nonblock || (flags&MSG_DONTWAIT))break;process_sleep_ticks(1);}
    vex_fd_borrow_release(&operation_ref);return n<0?set_socket_errno(-n):n;
}
ssize_t vex_socket_recvfrom(int fd,void *buffer,size_t length,int flags,struct sockaddr *address,uint32_t capacity,uint32_t *actual){
    VEX_FD_BORROW(operation_ref);
    ladybird_fd_object_t *o=vex_fd_borrow_acquire(&operation_ref,fd);if(!o)return set_socket_errno(EBADF);
    if(o->ops!=&socket_ops){vex_fd_borrow_release(&operation_ref);return recv(fd,buffer,length,flags);}
    ladybird_socket_ctx_t *s=o->ctx;if(s->type!=SOCK_DGRAM){vex_fd_borrow_release(&operation_ref);if(actual)*actual=0;return recv(fd,buffer,length,flags);}
    if(flags&~(MSG_DONTWAIT|2|0x20)){vex_fd_borrow_release(&operation_ref);return set_socket_errno(ENOTSUP);}
    int error=socket_udp_error(s);if(error){vex_fd_borrow_release(&operation_ref);return set_socket_errno(error);}
    uint32_t ip=0;uint16_t port=0;int n;
    for(;;){n=udp_endpoint_receive(s->udp_handle,buffer,length,&ip,&port,flags&2,flags&0x20);if(n!=-11 || s->nonblock || (flags&MSG_DONTWAIT))break;process_sleep_ticks(1);}
    if(n>=0 && actual){struct sockaddr_in a={.sin_family=AF_INET,.sin_port=ntohs16(port)};
        a.sin_addr.s_addr=((ip&255u)<<24)|((ip&0xff00u)<<8)|((ip>>8)&0xff00u)|(ip>>24);
        if(address)memcpy(address,&a,capacity<sizeof(a)?capacity:sizeof(a));*actual=sizeof(a);}
    vex_fd_borrow_release(&operation_ref);return n<0?set_socket_errno(-n):n;
}

int vex_socket_network_fd(int fd){ladybird_fd_object_t *o=ladybird_fd_acquire(fd);int result=o && o->ops==&socket_ops;if(o)ladybird_fd_release(o);return result;}
int vex_socket_receive_fault(int fd,udp_transport_fault_t *fault){
    ladybird_fd_object_t *o=ladybird_fd_acquire(fd);if(!o)return set_socket_errno_int(EBADF);
    int r=o->ops!=&socket_ops || ((ladybird_socket_ctx_t *)o->ctx)->type!=SOCK_DGRAM?-ENOTSUP:
        udp_endpoint_fault_receive(((ladybird_socket_ctx_t *)o->ctx)->udp_handle,fault);
    ladybird_fd_release(o);return r<0?set_socket_errno_int(-r):0;
}
