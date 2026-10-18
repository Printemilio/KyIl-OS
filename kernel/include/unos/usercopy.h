#ifndef UNOS_USERCOPY_H
#define UNOS_USERCOPY_H
#include <stddef.h>
#include <stdint.h>
struct process;
typedef struct vex_scratch {
    void *data;
    struct process *owner;
    struct vex_scratch *previous,*next;
} vex_scratch_t;
void *vex_scratch_allocate(vex_scratch_t *ref,size_t length);
void vex_scratch_release(vex_scratch_t *ref);
/* Exchange an owned temporary with an owned retired allocation. NULL detaches
 * the entry. Caller receives the former allocation without freeing it. */
void *vex_scratch_exchange(vex_scratch_t *ref,void *replacement);
void vex_scratch_thread_stopped(struct process *thread);
#define VEX_SCRATCH(name) vex_scratch_t name __attribute__((cleanup(vex_scratch_release)))={0}
/* Each copy validates/materializes and copies within one memory transaction.
   No memory transaction or user pointer is held across a scheduler wait. */
int vex_copy_from_user(void *destination,uint64_t source,size_t length);
int vex_copy_to_user(uint64_t destination,const void *source,size_t length);
int vex_user_string_copy(char *destination,size_t capacity,uint64_t source);
#endif
