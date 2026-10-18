#ifndef UNOS_MESSAGE_IPC_H
#define UNOS_MESSAGE_IPC_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <unos/vex_abi.h>

/* Native VexOS message-channel flags.  Linux socket constants never cross
 * this interface. */
#define VEX_MESSAGE_NONBLOCK 0x00000001u

/* Attachments are native VexOS object capabilities.  The message layer never
 * carries a Linux descriptor number; the compatibility layer supplies an
 * acquired kernel object reference and receives one back on delivery. */
#define VEX_MESSAGE_ATTACHMENT_FD 1u
#define VEX_MESSAGE_ATTACHMENT_SURFACE 2u
#define VEX_MESSAGE_MAX_ATTACHMENTS 32u

typedef struct vex_message_endpoint vex_message_endpoint_t;
#define VEX_MESSAGE_RIGHT_SEND 1u
#define VEX_MESSAGE_RIGHT_RECEIVE 2u
#define VEX_MESSAGE_RIGHT_TRANSFER 4u
#define VEX_MESSAGE_RIGHT_REVOKE 8u
#define VEX_MESSAGE_RIGHT_ALL 15u
vex_message_endpoint_t *vex_message_endpoint_clone(vex_message_endpoint_t *,uint32_t);
uint32_t vex_message_endpoint_rights(vex_message_endpoint_t *);
int vex_message_endpoint_revoke(vex_message_endpoint_t *);
int vex_message_endpoint_cancel(vex_message_endpoint_t *);
typedef struct vex_message_attachment {
    void *object;
    uint32_t rights;
    uint32_t type;
} vex_message_attachment_t;

int vex_message_pair_create(uint32_t flags,
                            vex_message_endpoint_t **left_out,
                            vex_message_endpoint_t **right_out);
int vex_message_endpoint_close(vex_message_endpoint_t *endpoint);
ssize_t vex_message_send(vex_message_endpoint_t *endpoint,
                         const void *buffer, size_t length, uint32_t flags);
ssize_t vex_message_send_attached(vex_message_endpoint_t *endpoint,
                                  const void *buffer, size_t length,
                                  vex_message_attachment_t *attachments,
                                  size_t attachment_count,
                                  uint32_t flags);
ssize_t vex_message_receive(vex_message_endpoint_t *endpoint,
                            void *buffer, size_t capacity, uint32_t flags);
ssize_t vex_message_receive_attached(vex_message_endpoint_t *endpoint,
                                     void *buffer, size_t capacity,
                                     vex_message_attachment_t *attachments,
                                     size_t *attachment_count,
                                     uint32_t flags);
void vex_message_attachment_release(vex_message_attachment_t *attachment);
ssize_t vex_message_receive_authenticated(vex_message_endpoint_t *,void *,size_t,
    vex_message_attachment_t *,size_t *,uint32_t,vex_message_sender_t *);
ssize_t vex_socketpair_receive_authenticated(int,void *,size_t,int,
    vex_message_attachment_t *,size_t *,vex_message_sender_t *);
typedef struct vex_web_fd_object vex_web_fd_object_t;
void vex_message_endpoint_admission(vex_message_endpoint_t *,unsigned);
void vex_message_fd_admission(vex_web_fd_object_t *,unsigned);
unsigned vex_message_fd_rights(vex_web_fd_object_t *);
int vex_message_fd_has_lifetime(vex_web_fd_object_t *);
int vex_message_fd_revoke(vex_web_fd_object_t *);
int vex_message_fd_pair(vex_web_fd_object_t **,vex_web_fd_object_t **);
void vex_message_fd_lifetime_hook(vex_web_fd_object_t *,void (*)(void *),void *);
int vex_message_poll(vex_message_endpoint_t *endpoint, short events,
                     short *revents);
int vex_message_observe(vex_message_endpoint_t *,vex_message_observe_req_t *);
int vex_fd_message_observe(int,vex_message_observe_req_t *);
int vex_message_set_flags(vex_message_endpoint_t *endpoint, uint32_t flags);
uint32_t vex_message_get_flags(const vex_message_endpoint_t *endpoint);
void vex_message_cancel_pid(uint32_t pid);
/* Exact execution-context cleanup, only after that thread has stopped. */
void vex_message_thread_stopped(uint32_t tid);

#endif
