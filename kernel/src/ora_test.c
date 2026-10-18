#include <unos/ora.h>
#include <unos/kernel_allocator.h>
#include <unos/versioned_store.h>
#include <string.h>

static unsigned storage_destroyed;
static void destroy_test_storage(void *state) {
    ++storage_destroyed;
    kernel_allocator_free(state);
}
int ora_self_test(void) {
    const uint32_t raw_owner=0xfffff900u;
    void *raw=kernel_allocator_alloc(raw_owner,8192);
    if(!raw)return -1;
    memset(raw,0x64,8192);
    void *shrunk=kernel_allocator_realloc(raw_owner,raw,4);
    if(!shrunk){kernel_allocator_free(raw);return -1;}
    if(*(uint8_t *)shrunk!=0x64){kernel_allocator_free(shrunk);return -1;}
    kernel_allocator_free(shrunk);
    raw=kernel_allocator_alloc(raw_owner,4);if(!raw)return -1;
    memset(raw,0x49,4);
    void *grown=kernel_allocator_realloc(raw_owner,raw,32);
    if(!grown){kernel_allocator_free(raw);return -1;}
    int raw_valid=*(uint8_t *)grown==0x49;
    kernel_allocator_free(grown);kernel_allocator_reap(raw_owner);
    if(!raw_valid)return -1;
    ora_stats_t initial, before, after;
    ora_get_stats(&initial);
    ora_ref_t a=ora_create("X",1,ORA_CANONICAL), b=ora_create("X",1,ORA_CANONICAL);
    if(!a || !b || a!=b)goto fail;
    ora_ref_t next=ora_create("Y",1,ORA_CANONICAL);
    if(!next)goto fail;
    ora_release(a);a=next;
    if(a==b || memcmp(ora_data(b),"X",1))goto fail;
    ora_get_stats(&before);
    for(unsigned i=0;i<1000;++i){if(!ora_retain(b))goto fail;ora_release(b);}
    ora_get_stats(&after);
    if(before.fingerprints!=after.fingerprints)goto fail;
    ora_release(a);a=NULL;ora_release(b);b=NULL;
    uint8_t *bytes=kernel_allocator_alloc(0,8192);
    if(!bytes)return -1;
    memset(bytes,0x35,8192);
    a=ora_create(bytes,8192,ORA_CANONICAL);
    kernel_allocator_free(bytes);
    if(!a)goto fail;
    ora_view_t left={0},right={0};
    if(ora_view_acquire(a,0,4096,&left) || ora_view_acquire(a,4096,4096,&right)) {
        ora_view_release(&left);ora_view_release(&right);goto fail;
    }
    uint64_t p0=0,p1=0;
    if(ora_view_physical_page(&left,0,&p0) || ora_view_physical_page(&right,0,&p1) || !p0 || !p1 || p0==p1) {
        ora_view_release(&left);ora_view_release(&right);goto fail;
    }
    ora_release(a);a=NULL;
    int valid=*(const uint8_t *)ora_view_data(&left)==0x35 && *(const uint8_t *)ora_view_data(&right)==0x35;
    ora_view_release(&left);ora_view_release(&right);
    if(!valid)goto fail;
    unsigned destroyed=storage_destroyed;
    void *state=kernel_allocator_alloc(0,64);
    if(!state)goto fail;
    a=ora_storage_create(64,state,destroy_test_storage);
    if(!a){kernel_allocator_free(state);goto fail;}
    if(!ora_retain(a))goto fail;
    ora_release(a);
    if(storage_destroyed!=destroyed)goto fail;
    ora_release(a);a=NULL;
    if(storage_destroyed!=destroyed+1)goto fail;
    /* A retired raw arena must preserve a provider's live bytes, then release
       them at the last reference rather than retaining a dead PID's cache. */
    for(unsigned cycle=0;cycle<64;++cycle) {
        uint32_t owner=0xfffffa00u+cycle;
        state=kernel_allocator_alloc(owner,8192);
        if(!state)goto fail;
        memset(state,0x71,8192);
        a=ora_storage_create(8192,state,destroy_test_storage);
        if(!a){kernel_allocator_free(state);goto fail;}
        if(!ora_retain(a))goto fail;
        kernel_allocator_reap(owner);
        ora_release(a);
        if(*(const uint8_t *)ora_storage_state(a)!=0x71)goto fail;
        ora_release(a);a=NULL;
    }
    if(storage_destroyed!=destroyed+65)goto fail;
    uint32_t version=0;
    if(versioned_put(0x73656c66u,42,"old",3,&version) || version!=1)goto fail;
    a=versioned_acquire(0x73656c66u,42,1,NULL);
    if(!a || versioned_put(0x73656c66u,42,"new",3,&version) || version!=2) {
        versioned_reap(0x73656c66u);goto fail;
    }
    versioned_reap(0x73656c66u);
    if(memcmp(ora_data(a),"old",3))goto fail;
    ora_release(a);a=NULL;
    ora_get_stats(&after);
    return after.objects==initial.objects && after.bytes==initial.bytes ? 0 : -1;
fail:
    ora_release(a);ora_release(b);return -1;
}
