#define FD_INVENTORY_TESTING
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdatomic.h>
#include "../kernel/include/unos/fd_borrow.h"
#define LADYBIRD_MAX_FDS 128
typedef struct vex_web_fd_object {uint32_t refcount;} ladybird_fd_object_t;
typedef struct {uint32_t owner_pid;int fd;uint8_t close_on_exec,reserved[3];ladybird_fd_object_t *object;} ladybird_fd_slot_t;
typedef struct process {uint32_t pid;vex_fd_borrow_t *fd_borrows;} process_t;
static _Thread_local process_t *current;
static process_t tasks[8];
static void *kmalloc(size_t n){return malloc(n);}
static void kfree(void *p){free(p);}
#include "../kernel/src/vex_fd_table.inc"
static void ladybird_posix_init(void){}
static uint32_t ladybird_fd_owner_pid(void){return 1u;}
static process_t *scheduler_current_process(void){return current;}
static _Atomic unsigned closed,cancelled;
static int ladybird_fd_release(ladybird_fd_object_t *o) {
    uint64_t flags=fd_inventory_lock();assert(o->refcount);
    int last=!--o->refcount;fd_inventory_unlock(flags);
    if(last)++closed;
    return 0;
}
#define FD_BORROW_CANCEL_OBJECT(object,tid) (++cancelled)
#include "../kernel/src/fd_borrow.inc"
static HANDLE start,parked[8],resume;
static DWORD WINAPI worker(void *argument) {
    unsigned index=(unsigned)(uintptr_t)argument;current=&tasks[index];current->pid=index+10u;
    assert(WaitForSingleObject(start,10000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<10000u;++i) {
        VEX_FD_BORROW(a);VEX_FD_BORROW(b);
        assert(vex_fd_borrow_acquire(&a,0));assert(vex_fd_borrow_acquire(&b,0));
        assert(current->fd_borrows==&b && b.next==&a && a.previous==&b);
        vex_fd_borrow_release(&a);assert(b.next==NULL);
        vex_fd_borrow_release(&a); /* explicit release + cleanup is idempotent */
    }
    assert(!current->fd_borrows);
    {
        VEX_FD_BORROW(pinned);
        assert(!vex_fd_borrow_acquire(&pinned,-1));
        assert(!vex_fd_borrow_acquire(&pinned,200));
        assert(vex_fd_borrow_acquire(&pinned,0));
        assert(SetEvent(parked[index]));
        /* Stack remains valid but no caller executes against this lease. */
        assert(WaitForSingleObject(resume,30000)==WAIT_OBJECT_0);
        assert(!pinned.object && !current->fd_borrows);
    }
    return 0;
}
int main(void) {
    ladybird_fd_object_t object={.refcount=1u};
    ladybird_process_fd_table_t *table=ladybird_fd_get_table(1);assert(table);
    table->slots[0]=(ladybird_fd_slot_t){.object=&object,.owner_pid=1};
    start=CreateEventW(NULL,TRUE,FALSE,NULL);resume=CreateEventW(NULL,TRUE,FALSE,NULL);assert(start && resume);
    HANDLE threads[8];
    for(unsigned i=0;i<8u;++i) {
        parked[i]=CreateEventW(NULL,TRUE,FALSE,NULL);assert(parked[i]);
        threads[i]=CreateThread(NULL,0,worker,(void *)(uintptr_t)i,0,NULL);assert(threads[i]);
    }
    assert(SetEvent(start));assert(WaitForMultipleObjects(8,parked,TRUE,30000)==WAIT_OBJECT_0);
    table=ladybird_fd_detach_table(1);assert(table);kfree(table);
    ladybird_fd_release(&object);assert(object.refcount==8u && !closed);
    for(unsigned i=0;i<8u;++i)vex_fd_borrow_thread_stopped(&tasks[i]);
    assert(!object.refcount && closed==1u && cancelled==8u);
    assert(SetEvent(resume));assert(WaitForMultipleObjects(8,threads,TRUE,10000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<8u;++i){CloseHandle(threads[i]);CloseHandle(parked[i]);}
    CloseHandle(start);CloseHandle(resume);
    puts("SMP FD borrows: 160000 nested acquisitions, invalid handles, close while borrowed, stopped cleanup and final close exactly once PASS");
}
