#ifndef KYIL_RENDEZVOUS_H
#define KYIL_RENDEZVOUS_H
#include <unos/vex_legacy_compat.h>
#include <unos/fd_borrow.h>
#include <unos/fs_root.h>
#include <unos/vex_rendezvous_abi.h>
fs_root_t *vex_fs_root_borrow(vex_fd_borrow_t *,int,unsigned,int *);
vex_web_fd_object_t *vex_message_fd_clone(vex_web_fd_object_t *,unsigned,int *);
int vex_message_fd_transfer_allowed(vex_web_fd_object_t *);
int vex_message_fd_cancel(vex_web_fd_object_t *);
void vex_message_fd_close_hook(vex_web_fd_object_t *,void (*)(void *),void *);
void *vex_message_fd_hook_context(vex_web_fd_object_t *,void (*)(void *));
int vex_rendezvous_transfer_allowed(vex_web_fd_object_t *);
void vex_rendezvous_thread_stopped(uint32_t);
int64_t vex_rendezvous_call(vex_rendezvous_req_t *,const uint8_t *);
#endif
