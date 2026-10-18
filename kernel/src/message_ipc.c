#include <unos/smp.h>
#include <unos/message_ipc.h>
#include <unos/fs_root_provider.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/heap.h>
#include <unos/debug_log.h>
#include <unos/compositor_surface.h>
#include <unos/scheduler.h>
#include <unos/vex_legacy_compat.h>

#define VEX_MESSAGE_MAX_SIZE       (64u * 1024u)
#define VEX_MESSAGE_MAX_COUNT      128u
#define VEX_MESSAGE_MAX_BYTES      (1024u * 1024u)

typedef struct vex_message_node {
    struct vex_message_node *next;
    size_t length;
    uint8_t *data;
    vex_message_attachment_t *attachments;
    size_t attachment_count;
    flow_context_t flow;
    vex_message_sender_t sender;
} vex_message_node_t;

typedef struct vex_message_queue {
    vex_message_node_t *head;
    vex_message_node_t *tail;
    size_t count;
    size_t bytes;
} vex_message_queue_t;

typedef struct vex_message_channel vex_message_channel_t;

typedef struct vex_message_waiter {
    struct vex_message_waiter *next;
    vex_message_channel_t *channel;
    uint32_t pid;
    uint32_t tid;
    volatile uint8_t woken;
    uint8_t cancelled;
    uint8_t channel_ref_dropped;
    vex_message_node_t *pending_send;
} vex_message_waiter_t;

struct vex_message_channel {
    struct vex_message_channel *global_next;
    uint32_t references;
    uint32_t endpoints[2];
    uint8_t closed[2];
    uint8_t revoked;
    vex_message_queue_t queues[2];
    vex_message_waiter_t *waiters;
    uint32_t consumer[2];
    uint8_t consumer_multiple[2];
};

static vex_message_channel_t *message_channels;
static uint8_t message_lock_word;

/* Bounded diagnostic of the renderer's actual messages. No dispatch depends
 * on this payload; channel/side identify where a frame or ACK is consumed. */
static unsigned message_frame_trace_count;
static void message_trace_hex(uint64_t value) {
    char digits[17];
    for (unsigned i=0; i<16; ++i) digits[i]="0123456789abcdef"[(value >> ((15-i)*4)) & 15];
    digits[16]=0; debug_log_puts(digits);
}
static void message_trace_frame(const char *op, vex_message_channel_t *channel,
                               unsigned side, const vex_message_node_t *node) {
    uint64_t words[2];
    if (!node || node->length != 24 || !node->data) return;
    memcpy(words,node->data,sizeof(words));
    if (!VEX_COMPAT_TRACE || words[0] != UINT64_C(0x5657504546524d31) || message_frame_trace_count++ >= 96) return;
    debug_log_puts("message-frame: "); debug_log_puts(op);
    debug_log_puts(" tid="); message_trace_hex(scheduler_current_tid());
    debug_log_puts(" channel="); message_trace_hex((uintptr_t)channel);
    debug_log_puts(" side="); message_trace_hex(side);
    debug_log_puts(" sequence="); message_trace_hex(words[1]);
    debug_log_puts(" attachments="); message_trace_hex(node->attachment_count);
    debug_log_puts("\n");
}

struct vex_message_endpoint {
    vex_message_channel_t *channel;
    uint8_t side;
    uint8_t admission;
    uint32_t admission_rights;
    uint8_t closed;
    uint32_t flags;
    uint32_t rights;
};

/* IRQ-save spin lock: protects channel topology, queues, waiter links and refs. */
static uint64_t message_lock(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    while (__atomic_test_and_set(&message_lock_word, __ATOMIC_ACQUIRE)) {
        smp_relax();
    }
    return flags;
}

static void message_unlock(uint64_t flags) {
    __atomic_clear(&message_lock_word, __ATOMIC_RELEASE);
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

static void message_wake_all_locked(vex_message_channel_t *channel) {
    for (vex_message_waiter_t *waiter = channel->waiters; waiter != NULL;
         waiter = waiter->next) {
        waiter->woken = 1u;
        process_wake(waiter->tid);
    }
}

static void message_unlink_waiter_locked(vex_message_waiter_t *waiter) {
    vex_message_waiter_t **link;
    if (waiter == NULL || waiter->channel == NULL) return;
    link = &waiter->channel->waiters;
    while (*link != NULL) {
        if (*link == waiter) {
            *link = waiter->next;
            waiter->next = NULL;
            waiter->channel = NULL;
            return;
        }
        link = &(*link)->next;
    }
    waiter->channel = NULL;
}

static void message_waiter_arm_locked(vex_message_channel_t *channel,
                                      vex_message_waiter_t *waiter) {
    memset(waiter, 0, sizeof(*waiter));
    waiter->channel = channel;
    waiter->pid = scheduler_current_pid();
    waiter->tid = scheduler_current_tid();
    process_t *thread=scheduler_current_process();
    if(thread)thread->scheduling.wait_order=scheduler_time_ns();
    waiter->next = channel->waiters;
    channel->waiters = waiter;
}

/* Caller has linked waiter while holding message_lock. A wake between unlock
 * and scheduling sets woken under the same lock; process_wait_event then sees it. */
static int message_wait_linked(vex_message_waiter_t *waiter) {
    process_wait_event(&waiter->woken);
    scheduler_wait_dependency_clear();
    const uint64_t flags = message_lock();
    if (waiter->channel != NULL) message_unlink_waiter_locked(waiter);
    const int cancelled = waiter->cancelled != 0u;
    message_unlock(flags);
    return cancelled || !scheduler_process_alive(waiter->tid) ? -1 : 0;
}

static void vex_message_attachment_release_unlocked(vex_message_attachment_t *attachment) {
    if (attachment == NULL || attachment->object == NULL) return;
    if (attachment->type == VEX_MESSAGE_ATTACHMENT_FD) {
        (void)ladybird_fd_release((ladybird_fd_object_t *)attachment->object);
    } else if (attachment->type == VEX_MESSAGE_ATTACHMENT_SURFACE) {
        compositor_surface_attachment_release(attachment->object);
    }
    attachment->object = NULL;
    attachment->rights = 0u;
    attachment->type = 0u;
}

static void message_queue_discard(vex_message_queue_t *queue) {
    vex_message_node_t *node = queue->head;
    while (node != NULL) {
        vex_message_node_t *next = node->next;
        for (size_t i = 0u; i < node->attachment_count; ++i)
            vex_message_attachment_release_unlocked(&node->attachments[i]);
        kfree(node->attachments);
        kfree(node->data);
        kfree(node);
        node = next;
    }
    memset(queue, 0, sizeof(*queue));
}

void vex_message_attachment_release(vex_message_attachment_t *attachment) {
    vex_message_attachment_release_unlocked(attachment);
}

/* Removes a dead channel from the global index under lock; destruction and
 * attachment releases happen after unlock because closing a transferred FD
 * can re-enter this module. */
static vex_message_channel_t *message_channel_release_locked(
    vex_message_channel_t *channel) {
    if (channel == NULL || channel->references == 0u) return NULL;
    if (--channel->references != 0u) return NULL;
    vex_message_channel_t **link = &message_channels;
    while (*link != NULL && *link != channel) link = &(*link)->global_next;
    if (*link == channel) *link = channel->global_next;
    return channel;
}

static void message_channel_destroy(vex_message_channel_t *channel) {
    if (channel == NULL) return;
    message_queue_discard(&channel->queues[0]);
    message_queue_discard(&channel->queues[1]);
    kfree(channel);
}

static int message_channel_hold_locked(vex_message_endpoint_t *endpoint,
                                       vex_message_channel_t **channel_out) {
    if (endpoint == NULL || endpoint->closed != 0u || endpoint->channel == NULL ||
        endpoint->channel->references == UINT32_MAX) return -1;
    ++endpoint->channel->references;
    *channel_out = endpoint->channel;
    return 0;
}

static void message_channel_drop(vex_message_channel_t *channel) {
    const uint64_t flags = message_lock();
    vex_message_channel_t *dead = message_channel_release_locked(channel);
    message_unlock(flags);
    message_channel_destroy(dead);
}

int vex_message_pair_create(uint32_t flags,
                            vex_message_endpoint_t **left_out,
                            vex_message_endpoint_t **right_out) {
    vex_message_channel_t *channel;
    vex_message_endpoint_t *left;
    vex_message_endpoint_t *right;
    if (left_out == NULL || right_out == NULL || left_out == right_out ||
        (flags & ~VEX_MESSAGE_NONBLOCK) != 0u) return -EINVAL;
    *left_out = NULL;
    *right_out = NULL;
    channel = kmalloc(sizeof(*channel));
    left = kmalloc(sizeof(*left));
    right = kmalloc(sizeof(*right));
    if (channel == NULL || left == NULL || right == NULL) {
        kfree(channel); kfree(left); kfree(right); return -ENOMEM;
    }
    memset(channel, 0, sizeof(*channel));
    channel->references = 2u;
    channel->endpoints[0]=channel->endpoints[1]=1;
    memset(left, 0, sizeof(*left));
    memset(right, 0, sizeof(*right));
    left->channel = right->channel = channel;
    left->flags = right->flags = flags;
    left->rights=right->rights=VEX_MESSAGE_RIGHT_ALL;
    right->side = 1u;
    const uint64_t lock_flags = message_lock();
    channel->global_next = message_channels;
    message_channels = channel;
    message_unlock(lock_flags);
    *left_out = left;
    *right_out = right;
    return 0;
}

vex_message_endpoint_t *vex_message_endpoint_clone(vex_message_endpoint_t *source,uint32_t rights) {
    if(!rights || (rights&~VEX_MESSAGE_RIGHT_ALL))return NULL;
    vex_message_endpoint_t *copy=kmalloc(sizeof(*copy));if(!copy)return NULL;
    uint64_t flags=message_lock();
    if(!source || source->closed || !source->channel || (rights&~source->rights) ||
       source->channel->references==UINT32_MAX || source->channel->endpoints[source->side]==UINT32_MAX) {
        message_unlock(flags);kfree(copy);return NULL;
    }
    *copy=*source;copy->rights=rights;copy->channel->references++;copy->channel->endpoints[copy->side]++;
    message_unlock(flags);return copy;
}
uint32_t vex_message_endpoint_rights(vex_message_endpoint_t *e) {
    uint64_t flags=message_lock();uint32_t rights=e && !e->closed?e->rights:0;message_unlock(flags);return rights;
}
void vex_message_endpoint_admission(vex_message_endpoint_t *e,unsigned rights){e->admission=1;e->admission_rights=rights;}
int vex_message_endpoint_revoke(vex_message_endpoint_t *e){
    vex_message_queue_t old[2];uint64_t flags=message_lock();
    if(!e || e->closed){message_unlock(flags);return -EBADF;}
    if(!(e->rights&VEX_MESSAGE_RIGHT_REVOKE)){message_unlock(flags);return -EACCES;}
    vex_message_channel_t *c=e->channel;old[0]=c->queues[0];old[1]=c->queues[1];memset(c->queues,0,sizeof(c->queues));
    c->revoked=1;c->closed[0]=c->closed[1]=1;message_wake_all_locked(c);message_unlock(flags);
    message_queue_discard(&old[0]);message_queue_discard(&old[1]);return 0;
}
int vex_message_endpoint_cancel(vex_message_endpoint_t *endpoint) {
    uint64_t flags=message_lock();
    if(!endpoint || !endpoint->channel){message_unlock(flags);return -EBADF;}
    endpoint->closed=1;message_wake_all_locked(endpoint->channel);message_unlock(flags);return 0;
}
int vex_message_endpoint_close(vex_message_endpoint_t *endpoint) {
    if (endpoint == NULL) return -EBADF;
    const uint64_t flags = message_lock();
    if (endpoint->channel == NULL) {
        message_unlock(flags); return -EBADF;
    }
    endpoint->closed = 1u;
    vex_message_channel_t *channel = endpoint->channel;
    if(!--channel->endpoints[endpoint->side])channel->closed[endpoint->side]=1u;
    endpoint->channel = NULL;
    message_wake_all_locked(channel);
    vex_message_channel_t *dead = message_channel_release_locked(channel);
    message_unlock(flags);
    message_channel_destroy(dead);
    kfree(endpoint);
    return 0;
}

ssize_t vex_message_send_attached(vex_message_endpoint_t *endpoint,
                                  const void *buffer, size_t length,
                                  vex_message_attachment_t *attachments,
                                  size_t attachment_count, uint32_t flags) {
    vex_message_node_t *node;
    vex_message_channel_t *channel;
    uint32_t nonblock;
    if ((buffer == NULL && length != 0u) ||
        (attachment_count != 0u && attachments == NULL) ||
        attachment_count > VEX_MESSAGE_MAX_ATTACHMENTS) return -EINVAL;
    if (length > VEX_MESSAGE_MAX_SIZE) return -EMSGSIZE;
    for (size_t i = 0u; i < attachment_count; ++i) {
        const int valid_fd = attachments[i].type == VEX_MESSAGE_ATTACHMENT_FD &&
            attachments[i].rights == VEX_MESSAGE_ATTACHMENT_DUP;
        const int valid_surface = attachments[i].type == VEX_MESSAGE_ATTACHMENT_SURFACE &&
            compositor_surface_attachment_valid(&attachments[i]);
        if (attachments[i].object == NULL || (!valid_fd && !valid_surface)) return -EINVAL;
        if(valid_fd && !vex_fd_transfer_allowed(attachments[i].object))return -EACCES;
        /* A service must never keep its own lifetime alive through a queued
           request attachment. Ordinary IPC can still delegate domain caps. */
        if(valid_fd && endpoint && endpoint->admission && vex_message_fd_has_lifetime(attachments[i].object))return -EACCES;
    }
    uint64_t lock_flags = message_lock();
    if(endpoint && !(endpoint->rights&VEX_MESSAGE_RIGHT_SEND)){message_unlock(lock_flags);return -EACCES;}
    if (message_channel_hold_locked(endpoint, &channel) != 0) {
        message_unlock(lock_flags); return -EBADF;
    }
    nonblock = (flags | endpoint->flags) & VEX_MESSAGE_NONBLOCK;
    message_unlock(lock_flags);

    node = kmalloc(sizeof(*node));
    if (node == NULL) { message_channel_drop(channel); return -ENOMEM; }
    memset(node, 0, sizeof(*node));
    node->data = length == 0u ? NULL : kmalloc(length);
    if (length != 0u && node->data == NULL) {
        kfree(node); message_channel_drop(channel); return -ENOMEM;
    }
    if (length != 0u) memcpy(node->data, buffer, length);
    node->attachments = attachment_count == 0u ? NULL
        : kmalloc(attachment_count * sizeof(*node->attachments));
    if (attachment_count != 0u && node->attachments == NULL) {
        kfree(node->data); kfree(node); message_channel_drop(channel); return -ENOMEM;
    }
    if (attachment_count != 0u) {
        memcpy(node->attachments, attachments, attachment_count * sizeof(*attachments));
    }
    node->attachment_count = attachment_count;
    node->length = length;
    node->flow=scheduler_flow_current();
    node->sender=(vex_message_sender_t){scheduler_current_process_id(),scheduler_current_tid(),scheduler_current_uid(),endpoint->admission_rights};

    for (;;) {
        lock_flags = message_lock();
        if (channel->closed[endpoint->side ^ 1u] != 0u || endpoint->closed != 0u) {
            int error=channel->revoked?-ENOTCONN:-EPIPE;
            message_unlock(lock_flags);
            kfree(node->attachments); kfree(node->data); kfree(node);
            message_channel_drop(channel); return error;
        }
        vex_message_queue_t *queue = &channel->queues[endpoint->side];
        if (queue->count < VEX_MESSAGE_MAX_COUNT &&
            length <= VEX_MESSAGE_MAX_BYTES - queue->bytes) {
            if (queue->tail == NULL) queue->head = node;
            else queue->tail->next = node;
            queue->tail = node;
            ++queue->count;
            queue->bytes += length;
            message_trace_frame("enqueue",channel,endpoint->side,node);
            for (size_t i = 0u; i < attachment_count; ++i) {
                attachments[i].object = NULL;
                attachments[i].rights = 0u;
                attachments[i].type = 0u;
            }
            message_wake_all_locked(channel);
            vex_message_channel_t *dead = message_channel_release_locked(channel);
            message_unlock(lock_flags);
            message_channel_destroy(dead);
            return (ssize_t)length;
        }
        if (nonblock != 0u) {
            message_unlock(lock_flags);
            kfree(node->attachments); kfree(node->data); kfree(node);
            message_channel_drop(channel); return -EAGAIN;
        }
        vex_message_waiter_t waiter;
        message_waiter_arm_locked(channel, &waiter);
        waiter.pending_send=node;
        /* A full sender queue is drained by the receiver, not by the sender
           or creator. Donate only when the service association is unambiguous. */
        unsigned peer=endpoint->side^1u;
        if(channel->consumer[peer] && !channel->consumer_multiple[peer])
            (void)scheduler_wait_dependency(channel->consumer[peer]);
        message_unlock(lock_flags);
        if (message_wait_linked(&waiter) != 0) {
            kfree(node->attachments); kfree(node->data); kfree(node);
            if (waiter.channel_ref_dropped == 0u) message_channel_drop(channel);
            return -EINTR;
        }
    }
}

ssize_t vex_message_send(vex_message_endpoint_t *endpoint,
                         const void *buffer, size_t length, uint32_t flags) {
    return vex_message_send_attached(endpoint, buffer, length, NULL, 0u, flags);
}

ssize_t vex_message_receive_authenticated(vex_message_endpoint_t *endpoint,
                                     void *buffer, size_t capacity,
                                     vex_message_attachment_t *attachments,
                                     size_t *attachment_count, uint32_t flags, vex_message_sender_t *sender) {
    if(sender)memset(sender,0,sizeof(*sender));
    vex_message_channel_t *channel;
    size_t attachment_capacity;
    uint32_t nonblock;
    if ((buffer == NULL && capacity != 0u) || attachment_count == NULL) return -EINVAL;
    attachment_capacity = *attachment_count;
    *attachment_count = 0u;
    if (attachment_capacity != 0u && attachments == NULL) return -EINVAL;
    uint64_t lock_flags = message_lock();
    if(endpoint && !(endpoint->rights&VEX_MESSAGE_RIGHT_RECEIVE)){message_unlock(lock_flags);return -EACCES;}
    if (message_channel_hold_locked(endpoint, &channel) != 0) {
        message_unlock(lock_flags); return -EBADF;
    }
    nonblock = (flags | endpoint->flags) & VEX_MESSAGE_NONBLOCK;
    message_unlock(lock_flags);

    for (;;) {
        lock_flags = message_lock();
        vex_message_queue_t *queue = &channel->queues[endpoint->side ^ 1u];
        uint32_t consumer=scheduler_current_tid();
        if(channel->consumer[endpoint->side] && channel->consumer[endpoint->side]!=consumer)
            channel->consumer_multiple[endpoint->side]=1;
        else channel->consumer[endpoint->side]=consumer;
        vex_message_node_t *node = queue->head;
        if (node != NULL) {
            if (capacity < node->length || attachment_capacity < node->attachment_count) {
                vex_message_channel_t *dead = message_channel_release_locked(channel);
                message_unlock(lock_flags);
                message_channel_destroy(dead);
                return -EMSGSIZE;
            }
            queue->head = node->next;
            if (queue->head == NULL) queue->tail = NULL;
            --queue->count;
            queue->bytes -= node->length;
            message_trace_frame("dequeue",channel,endpoint->side,node);
            message_wake_all_locked(channel);
            message_unlock(lock_flags);
            scheduler_flow_adopt(node->flow);
            if (node->length != 0u) memcpy(buffer, node->data, node->length);
            if (node->attachment_count != 0u) {
                memcpy(attachments, node->attachments,
                       node->attachment_count * sizeof(*attachments));
                *attachment_count = node->attachment_count;
            }
            if(sender)*sender=node->sender;
            const size_t length = node->length;
            kfree(node->attachments); kfree(node->data); kfree(node);
            message_channel_drop(channel);
            return (ssize_t)length;
        }
        if (channel->closed[endpoint->side ^ 1u] != 0u) {
            int result=channel->revoked?-ENOTCONN:0;
            vex_message_channel_t *dead = message_channel_release_locked(channel);
            message_unlock(lock_flags);
            message_channel_destroy(dead);
            return result;
        }
        if (endpoint->closed != 0u) {
            vex_message_channel_t *dead = message_channel_release_locked(channel);
            message_unlock(lock_flags);
            message_channel_destroy(dead);
            return -EBADF;
        }
        if (nonblock != 0u) {
            vex_message_channel_t *dead = message_channel_release_locked(channel);
            message_unlock(lock_flags);
            message_channel_destroy(dead);
            return -EAGAIN;
        }
        vex_message_waiter_t waiter;
        message_waiter_arm_locked(channel, &waiter);
        message_unlock(lock_flags);
        if (message_wait_linked(&waiter) != 0) {
            if (waiter.channel_ref_dropped == 0u) message_channel_drop(channel);
            return -EINTR;
        }
    }
}

ssize_t vex_message_receive_attached(vex_message_endpoint_t *e,void *b,size_t n,
    vex_message_attachment_t *a,size_t *c,uint32_t f){return vex_message_receive_authenticated(e,b,n,a,c,f,NULL);}

ssize_t vex_message_receive(vex_message_endpoint_t *endpoint,
                            void *buffer, size_t capacity, uint32_t flags) {
    vex_message_attachment_t attachments[VEX_MESSAGE_MAX_ATTACHMENTS];
    size_t count = VEX_MESSAGE_MAX_ATTACHMENTS;
    ssize_t result = vex_message_receive_attached(endpoint, buffer, capacity,
                                                   attachments, &count, flags);
    for (size_t i = 0u; i < count; ++i) vex_message_attachment_release(&attachments[i]);
    return result;
}

int vex_message_observe(vex_message_endpoint_t *endpoint,vex_message_observe_req_t *out) {
    const uint64_t flags=message_lock();
    if(!endpoint || endpoint->closed || !endpoint->channel){message_unlock(flags);return -EBADF;}
    if(!(endpoint->rights&VEX_MESSAGE_RIGHT_RECEIVE)){message_unlock(flags);return -EACCES;}
    vex_message_channel_t *channel=endpoint->channel;
    vex_message_queue_t *queue=&channel->queues[endpoint->side^1u];
    out->messages=queue->count;out->bytes=queue->bytes;
    out->first_length=queue->head?queue->head->length:0;
    out->copied=out->first_length<out->capacity?out->first_length:out->capacity;
    out->peer_closed=channel->closed[endpoint->side^1u]!=0;
    if(out->copied)memcpy(out->prefix,queue->head->data,out->copied);
    message_unlock(flags);return 0;
}

int vex_message_poll(vex_message_endpoint_t *endpoint, short events, short *revents) {
    if (revents == NULL) return -EINVAL;
    const uint64_t flags = message_lock();
    if (endpoint == NULL || endpoint->closed != 0u || endpoint->channel == NULL) {
        message_unlock(flags); return -EBADF;
    }
    vex_message_channel_t *channel = endpoint->channel;
    vex_message_queue_t *queue = &channel->queues[endpoint->side ^ 1u];
    *revents = 0;
    if ((endpoint->rights&VEX_MESSAGE_RIGHT_RECEIVE) && (events & POLLIN) != 0 &&
        (queue->count != 0u || channel->closed[endpoint->side ^ 1u] != 0u)) *revents |= POLLIN;
    if ((endpoint->rights&VEX_MESSAGE_RIGHT_SEND) && (events & POLLOUT) != 0 && channel->closed[endpoint->side ^ 1u] == 0u &&
        queue->count < VEX_MESSAGE_MAX_COUNT && queue->bytes < VEX_MESSAGE_MAX_BYTES) *revents |= POLLOUT;
    if (channel->closed[endpoint->side ^ 1u] != 0u) *revents |= POLLHUP;
    message_unlock(flags);
    return 0;
}

int vex_message_set_flags(vex_message_endpoint_t *endpoint, uint32_t flags) {
    const uint64_t lock_flags = message_lock();
    if (endpoint == NULL || endpoint->closed != 0u || endpoint->channel == NULL ||
        (flags & ~VEX_MESSAGE_NONBLOCK) != 0u) {
        message_unlock(lock_flags); return -EINVAL;
    }
    endpoint->flags = flags;
    message_unlock(lock_flags);
    return 0;
}

uint32_t vex_message_get_flags(const vex_message_endpoint_t *endpoint) {
    const uint64_t flags = message_lock();
    const uint32_t result = endpoint == NULL || endpoint->closed != 0u
        ? 0u : endpoint->flags;
    message_unlock(flags);
    return result;
}

static void message_cancel(uint32_t pid,int exact_thread) {
    if (pid == 0u) return;
    const uint64_t flags = message_lock();
    vex_message_channel_t *dead_channels = NULL;
    vex_message_node_t *abandoned=NULL;
    for (vex_message_channel_t *channel = message_channels; channel != NULL;) {
        vex_message_channel_t *next_channel = channel->global_next;
        vex_message_waiter_t **link = &channel->waiters;
        while (*link != NULL) {
            vex_message_waiter_t *waiter = *link;
            if (waiter->tid == pid || (!exact_thread && waiter->pid == pid)) {
                waiter->cancelled = 1u;
                __atomic_store_n(&waiter->woken,1u,__ATOMIC_RELEASE);
                if(!exact_thread) {
                    /* Cancellation is a request. The executing caller still
                       owns its stack/node/channel until it unregisters. */
                    process_wake(waiter->tid);link=&waiter->next;continue;
                }
                if(waiter->pending_send) {
                    waiter->pending_send->next=abandoned;
                    abandoned=waiter->pending_send;waiter->pending_send=NULL;
                }
                *link = waiter->next;
                waiter->next = NULL;
                waiter->channel = NULL;
                waiter->channel_ref_dropped = 1u;
                process_wake(waiter->tid);
                vex_message_channel_t *dead = message_channel_release_locked(channel);
                if (dead != NULL) {
                    dead->global_next = dead_channels;
                    dead_channels = dead;
                }
                continue;
            }
            link = &waiter->next;
        }
        channel = next_channel;
    }
    message_unlock(flags);
    while(abandoned) {
        vex_message_node_t *next=abandoned->next;
        for(size_t i=0;i<abandoned->attachment_count;++i)
            vex_message_attachment_release(&abandoned->attachments[i]);
        kfree(abandoned->attachments);kfree(abandoned->data);kfree(abandoned);
        abandoned=next;
    }
    while (dead_channels != NULL) {
        vex_message_channel_t *next = dead_channels->global_next;
        dead_channels->global_next = NULL;
        message_channel_destroy(dead_channels);
        dead_channels = next;
    }
}

void vex_message_cancel_pid(uint32_t pid) {message_cancel(pid,0);}
void vex_message_thread_stopped(uint32_t tid) {message_cancel(tid,1);}

