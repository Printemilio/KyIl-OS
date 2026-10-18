#define FD_INVENTORY_TESTING
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdatomic.h>
#define LADYBIRD_MAX_FDS 128
typedef struct {uint32_t refcount;} ladybird_fd_object_t;
typedef struct {uint32_t owner_pid;int fd;uint8_t close_on_exec,reserved[3];ladybird_fd_object_t *object;} ladybird_fd_slot_t;
static _Atomic unsigned allocations;
static void *kmalloc(size_t n){void *p=malloc(n);if(p)++allocations;return p;}
static void kfree(void *p){if(p){--allocations;free(p);}}
#include "../kernel/src/vex_fd_table.inc"
static ladybird_fd_object_t objects[16];
static HANDLE start;
static DWORD WINAPI worker(void *argument) {
    unsigned index=(unsigned)(uintptr_t)argument;
    assert(WaitForSingleObject(start,10000)==WAIT_OBJECT_0);
    for(unsigned cycle=0;cycle<2000u;++cycle) {
        /* Concurrent double-checked publication of the same empty table. */
        assert(ladybird_fd_get_table(42));
        uint32_t owner=100u+index*2000u+cycle;
        assert(ladybird_fd_clone_inventory(1,owner)==0);
        uint64_t flags=fd_inventory_lock();
        for(unsigned i=0;i<16u;++i) {
            ladybird_fd_slot_t *s=ladybird_fd_find_slot(owner,(int)i);
            assert(s && s->object==&objects[i] && s->owner_pid==owner &&
                   s->close_on_exec==(i&1u));
        }
        fd_inventory_unlock(flags);
        ladybird_process_fd_table_t *t=ladybird_fd_detach_table(owner);assert(t);
        flags=fd_inventory_lock();
        for(unsigned i=0;i<16u;++i)assert(t->slots[i].object->refcount-- > 1u);
        fd_inventory_unlock(flags);
        kfree(t);
    }
    return 0;
}
int main(void) {
    ladybird_process_fd_table_t *parent=ladybird_fd_get_table(1);assert(parent);
    for(unsigned i=0;i<16u;++i) {
        objects[i].refcount=1u;
        parent->slots[i]=(ladybird_fd_slot_t){.owner_pid=1,.fd=(int)i,
            .close_on_exec=i&1u,.object=&objects[i]};
    }
    start=CreateEventW(NULL,TRUE,FALSE,NULL);assert(start);
    HANDLE threads[8];
    for(unsigned i=0;i<8u;++i){threads[i]=CreateThread(NULL,0,worker,(void *)(uintptr_t)i,0,NULL);assert(threads[i]);}
    assert(SetEvent(start));
    assert(WaitForMultipleObjects(8,threads,TRUE,60000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<8u;++i)assert(CloseHandle(threads[i]));
    assert(CloseHandle(start));
    for(unsigned i=0;i<16u;++i)assert(objects[i].refcount==1u);
    assert(allocations==2u);
    kfree(ladybird_fd_detach_table(42));kfree(ladybird_fd_detach_table(1));
    assert(!allocations && !ladybird_fd_tables);
    puts("SMP FD inventory: 8 threads, 16000 clone/detach cycles, 256000 retained slots, duplicate publication/refcounts/flags PASS");
}
