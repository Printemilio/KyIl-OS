ladybird_fd_object_t *vex_fd_borrow_acquire(vex_fd_borrow_t *ref,int fd) {
    if(!ref || ref->object)return NULL;
    ladybird_posix_init();
    uint64_t flags=fd_inventory_lock();
    ladybird_fd_slot_t *slot=ladybird_fd_find_slot(ladybird_fd_owner_pid(),fd);
    ladybird_fd_object_t *object=slot?slot->object:NULL;
    process_t *thread=scheduler_current_process();
    if(!object || object->refcount==UINT32_MAX || !thread) {
        fd_inventory_unlock(flags);return NULL;
    }
    ++object->refcount;ref->object=object;ref->owner=thread;
    ref->next=thread->fd_borrows;
    if(ref->next)ref->next->previous=ref;
    thread->fd_borrows=ref;
    fd_inventory_unlock(flags);return object;
}
void vex_fd_borrow_release(vex_fd_borrow_t *ref) {
    if(!ref)return;
    uint64_t flags=fd_inventory_lock();
    ladybird_fd_object_t *object=ref->object;
    if(object) {
        if(ref->previous)ref->previous->next=ref->next;
        else ref->owner->fd_borrows=ref->next;
        if(ref->next)ref->next->previous=ref->previous;
        *ref=(vex_fd_borrow_t){0};
    }
    fd_inventory_unlock(flags);
    if(object) {
#ifdef FD_BORROW_PRESERVE_ERRNO
        int saved=*ladybird_errno_location();ladybird_fd_release(object);*ladybird_errno_location()=saved;
#else
        ladybird_fd_release(object);
#endif
    }
}
void vex_fd_borrow_thread_stopped(process_t *thread) {
    /* The scheduler pins the stopped stack for this callback. No concurrent
       normal return can touch these RAII nodes after CPU quiescence. */
    while(thread && thread->fd_borrows) {
        FD_BORROW_CANCEL_OBJECT(thread->fd_borrows->object,thread->pid);
        vex_fd_borrow_release(thread->fd_borrows);
    }
}
