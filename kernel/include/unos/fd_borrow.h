#ifndef UNOS_FD_BORROW_H
#define UNOS_FD_BORROW_H
/* Kernel-only operation leases, distinct from FD slots and transferred refs.
   An abandoned blocked operation retains its object until its thread stops. */
struct process;
struct vex_web_fd_object;
typedef struct vex_fd_borrow {
    struct vex_web_fd_object *object;
    struct process *owner;
    struct vex_fd_borrow *previous,*next;
} vex_fd_borrow_t;
struct vex_web_fd_object *vex_fd_borrow_acquire(vex_fd_borrow_t *ref,int fd);
void vex_fd_borrow_release(vex_fd_borrow_t *ref);
void vex_fd_borrow_thread_stopped(struct process *thread);
#define VEX_FD_BORROW(name) vex_fd_borrow_t name __attribute__((cleanup(vex_fd_borrow_release)))={0}
#endif
