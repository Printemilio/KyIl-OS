#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#define FD_INVENTORY_TESTING
#define LADYBIRD_MAX_FDS 128
typedef struct {uint32_t refcount;} ladybird_fd_object_t;
typedef struct {uint32_t owner_pid;int fd;uint8_t close_on_exec,reserved[3];ladybird_fd_object_t *object;} ladybird_fd_slot_t;
static int fail_allocation,allocations;
static void *kmalloc(size_t n){if(fail_allocation)return NULL;void *p=malloc(n);if(p)++allocations;return p;}
static void kfree(void *p){if(p){--allocations;free(p);}}
#include "../../kernel/src/vex_fd_table.inc"
int main(void) {
    ladybird_fd_object_t objects[128];memset(objects,0,sizeof(objects));
    ladybird_process_fd_table_t *parent=ladybird_fd_get_table(10);
    assert(parent && allocations==1);
    for(unsigned i=0;i<128;i++) {
        objects[i].refcount=1;
        parent->slots[i]=(ladybird_fd_slot_t){.owner_pid=10,.fd=i,.close_on_exec=i&1,.object=&objects[i]};
    }
    for(unsigned owner=11;owner<19;owner++) {
        assert(ladybird_fd_clone_inventory(10,owner)==0);
        for(int fd=0;fd<128;fd++) {
            ladybird_fd_slot_t *slot=ladybird_fd_find_slot(owner,fd);
            assert(slot && slot->object==&objects[fd] && slot->owner_pid==owner && slot->close_on_exec==(fd&1));
        }
    }
    assert(!ladybird_fd_find_slot(100,127) && !ladybird_fd_find_slot(10,128));
    assert(ladybird_fd_clone_inventory(10,11)==-EINVAL && objects[0].refcount==9);
    fail_allocation=1;assert(ladybird_fd_clone_inventory(10,20)==-ENOMEM);
    assert(!ladybird_fd_find_table(20) && allocations==9 && objects[127].refcount==9);
    fail_allocation=0;objects[100].refcount=UINT32_MAX;
    assert(ladybird_fd_clone_inventory(10,20)==-EMFILE);
    assert(!ladybird_fd_find_table(20) && allocations==9);
    for(int i=0;i<100;i++)assert(objects[i].refcount==9);
    objects[100].refcount=9;
    /* Producer disappears before any consumer. The retained identities and
     * descriptor flags remain independent of its table and lifetime. */
    for(unsigned owner=10;owner<19;owner++) {
        ladybird_process_fd_table_t *t=ladybird_fd_detach_table(owner);assert(t);
        for(int fd=0;fd<128;fd++)--t->slots[fd].object->refcount;
        kfree(t);assert(!ladybird_fd_find_table(owner));
        if(owner<18)assert(ladybird_fd_find_slot(18,127)->object==&objects[127]);
    }
    for(int fd=0;fd<128;fd++)assert(!objects[fd].refcount);
    assert(!allocations && !ladybird_fd_tables);
    for(unsigned cycle=0;cycle<1000;cycle++) {
        assert(ladybird_fd_get_table(10));assert(ladybird_fd_clone_inventory(10,11)==0);
        kfree(ladybird_fd_detach_table(11));kfree(ladybird_fd_detach_table(10));
    }
    assert(!allocations);
    puts("Process FD inventory: 1152 slots, isolation, inheritance, CLOEXEC, atomic OOM/overflow rollback, producer lifetime, 1000 cycles PASS");
}
