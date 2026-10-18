#include <unos/ora.h>
#include <unos/kernel_allocator.h>
#include <unos/versioned_store.h>
#include <unos/named_content.h>
#include <windows.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Hosted provider for the real ora.c, not an alternate storage engine. */
static volatile LONG allocations, fail_after=-1, destructions, failed;
void *kernel_allocator_alloc(uint32_t owner,size_t size) {
    (void)owner;
    if(fail_after>=0 && InterlockedDecrement(&fail_after)<0)return NULL;
    void *ptr=_aligned_malloc(size?size:1u,4096);
    if(ptr)InterlockedIncrement(&allocations);
    return ptr;
}
void kernel_allocator_free(void *ptr) {
    if(ptr){InterlockedDecrement(&allocations);_aligned_free(ptr);}
}
int kernel_allocator_physical_page(const void *ptr,size_t offset,uint64_t *out) {
    (void)ptr;(void)offset;(void)out;return -1; /* Hardware projection is tested in VM. */
}
static void check(int condition,const char *name) {
    if(!condition){fprintf(stderr,"FAIL: %s\n",name);exit(1);}
}
static void destroy_storage(void *state) {
    InterlockedIncrement(&destructions);kernel_allocator_free(state);
}
static DWORD WINAPI race(LPVOID state) {
    ora_ref_t anchor=state;
    for(unsigned i=0;i<4000;++i) {
        ora_ref_t object=ora_create("common-content",15,ORA_CANONICAL);
        ora_ref_t held=ora_retain(object);
        if(!held || (anchor && object!=anchor) ||
           (object && memcmp(ora_data(object),"common-content",15)))InterlockedIncrement(&failed);
        ora_release(held);ora_release(object);
    }
    return 0;
}
static volatile LONG mutate=1;
static unsigned char input[8192];
typedef struct catalog_race_state {
    named_content_map_t map;
    ora_ref_t left,right;
} catalog_race_state_t;
static DWORD WINAPI catalog_race(LPVOID argument) {
    catalog_race_state_t *state=argument;
    for(unsigned i=0;i<2000;++i) {
        if(i%5==0)named_content_remove(&state->map,"shared-name");
        else if(named_content_bind(&state->map,"shared-name",i%2?state->left:state->right,NAMED_UPSERT))
            InterlockedIncrement(&failed);
        ora_ref_t held=named_content_acquire(&state->map,"shared-name");
        if(held && held!=state->left && held!=state->right)InterlockedIncrement(&failed);
        if(i%113==0)named_content_destroy(&state->map);
        if(held && memcmp(ora_data(held),held==state->left?"left":"right",ora_size(held)))
            InterlockedIncrement(&failed);
        ora_release(held);
    }
    return 0;
}
static DWORD WINAPI writer(LPVOID unused) {
    (void)unused;
    unsigned generation=0;
    while(InterlockedCompareExchange(&mutate,1,1)) {
        ++generation;
        for(size_t i=0;i<sizeof(input);++i)InterlockedExchange8((volatile char *)&input[i],(char)(i+generation));
    }
    return 0;
}
int main(void) {
    ora_ref_t a=ora_create("X",1,ORA_CANONICAL),b=ora_create("X",1,ORA_CANONICAL);
    check(a && a==b,"A/B share X");
    ora_ref_t old=a;a=ora_create("Y",1,ORA_CANONICAL);ora_release(old);
    check(a && a!=b && !memcmp(ora_data(b),"X",1),"rebind A preserves B");
    ora_stats_t before,after;ora_get_stats(&before);
    for(unsigned i=0;i<10000;++i){check(ora_retain(b)==b,"retain");ora_release(b);}
    ora_get_stats(&after);check(before.fingerprints==after.fingerprints,"retain does not hash");
    ora_view_t view={0};
    check(!ora_view_acquire(b,0,1,&view),"view acquisition");
    check(ora_view_acquire(b,0,1,&view)!=0 && view.object==b,"occupied view refuses overwrite without leak");
    ora_release(b);check(!memcmp(ora_view_data(&view),"X",1),"view survives owner exit");
    ora_view_release(&view);ora_release(a);
    a=ora_test_create_fingerprint("aaa",3,7);b=ora_test_create_fingerprint("bbb",3,7);
    ora_ref_t c=ora_test_create_fingerprint("aaa",3,7);
    check(a && b && a!=b && a==c,"exact collision comparison");
    ora_release(a);ora_release(b);ora_release(c);
    void *left=kernel_allocator_alloc(0,4096),*right=kernel_allocator_alloc(0,4096);
    memset(left,0,4096);memset(right,0,4096);
    a=ora_storage_create(4096,left,destroy_storage);b=ora_storage_create(4096,right,destroy_storage);
    check(a && b && a!=b,"equal mutable storage never merges");
    *(int *)ora_storage_state(a)=42;
    check(!*(int *)ora_storage_state(b),"independent mutable storage");
    check(ora_retain(a)==a,"storage transfer");ora_release(a);
    check(*(int *)ora_storage_state(a)==42 && destructions==0,"storage survives producer");
    ora_release(a);ora_release(b);check(destructions==2,"exact final storage destruction");
    a=ora_create_zero_extended("abc",3,8192,ORA_CANONICAL);
    check(a && !ora_view_acquire(a,4096,4096,&view),"bounded split view");
    check(!((unsigned char *)ora_view_data(&view))[0],"zero extension");
    ora_release(a);ora_view_release(&view);
    check(ora_view_acquire(NULL,0,1,&view)!=0,"invalid view");
    a=ora_create("bounds",6,ORA_CANONICAL);
    check(ora_view_acquire(a,SIZE_MAX,2,&view)!=0,"overflow bounds");ora_release(a);
    for(LONG fail=0;fail<2;++fail) {
        LONG baseline=allocations;fail_after=fail;
        a=ora_create("oom",3,ORA_CANONICAL);
        check(!a,"injected OOM");fail_after=-1;
        check(allocations==baseline,"OOM rollback");
    }
    a=ora_create("common-content",15,ORA_CANONICAL);
    HANDLE threads[8];
    for(unsigned i=0;i<8;++i)threads[i]=CreateThread(NULL,0,race,a,0,NULL);
    check(WaitForMultipleObjects(8,threads,TRUE,INFINITE)==WAIT_OBJECT_0,"concurrent join");
    for(unsigned i=0;i<8;++i)CloseHandle(threads[i]);
    check(!failed,"concurrent create/retain/release");ora_release(a);
    /* No anchor pins the index now: final release races new publication. */
    for(unsigned i=0;i<8;++i)threads[i]=CreateThread(NULL,0,race,NULL,0,NULL);
    check(WaitForMultipleObjects(8,threads,TRUE,INFINITE)==WAIT_OBJECT_0,"final-release race join");
    for(unsigned i=0;i<8;++i)CloseHandle(threads[i]);
    check(!failed,"concurrent last-release/publication");
    HANDLE thread=CreateThread(NULL,0,writer,NULL,0,NULL);
    for(unsigned i=0;i<200;++i) {
        a=ora_create(input,sizeof(input),ORA_CANONICAL);
        b=ora_create(ora_data(a),ora_size(a),ORA_CANONICAL);
        check(a && a==b,"fingerprint matches copied concurrent input");
        ora_release(a);ora_release(b);
    }
    InterlockedExchange(&mutate,0);WaitForSingleObject(thread,INFINITE);CloseHandle(thread);
    uint32_t version=0;
    check(!versioned_put(12,42,"old",3,&version) && version==1,"versioned put");
    versioned_info_t info;
    a=versioned_acquire(12,42,0,&info);
    check(a && info.version==1 && info.size==3,"versioned acquire current");
    check(!versioned_put(12,42,"new",3,&version) && version==2,"versioned rebind");
    check(versioned_count(12,42)==2 && !memcmp(ora_data(a),"old",3),"borrow survives rebind");
    check(!versioned_acquire(13,42,0,NULL),"process-local keys");
    versioned_remove_all(12,42);versioned_reap(12);
    check(!memcmp(ora_data(a),"old",3),"borrow survives owner teardown");ora_release(a);
    for(LONG fail=0;fail<5;++fail) {
        LONG baseline=allocations;fail_after=fail;
        check(versioned_put(12,42,"oom",3,&version)!=0,"versioned OOM");
        fail_after=-1;check(allocations==baseline,"versioned transactional rollback");
    }
    named_content_map_t catalog={0};
    check(!named_content_put(&catalog,"A","X",1,NAMED_CREATE),"catalog A");
    check(!named_content_put(&catalog,"B","X",1,NAMED_CREATE),"catalog B");
    a=named_content_acquire(&catalog,"A");b=named_content_acquire(&catalog,"B");
    check(a==b,"catalog dedup distinct names");ora_release(b);
    ora_get_stats(&before);
    check(!named_content_move(&catalog,"A","C"),"catalog identity rename");
    ora_get_stats(&after);check(after.fingerprints==before.fingerprints,"rename no rehash");
    check(!named_content_put(&catalog,"C","Y",1,NAMED_REPLACE),"catalog rebind");
    b=named_content_acquire(&catalog,"B");check(b==a && !memcmp(ora_data(a),"X",1),"catalog old lease survives");ora_release(b);
    named_content_destroy(&catalog);check(!memcmp(ora_data(a),"X",1),"catalog destroy lease survives");ora_release(a);
    for(LONG fail=0;fail<3;++fail) {
        LONG baseline=allocations;fail_after=fail;
        check(named_content_put(&catalog,"OOM","Z",1,NAMED_CREATE)!=0,"catalog OOM");
        fail_after=-1;check(allocations==baseline,"catalog rollback");
    }
    catalog_race_state_t state={0};
    state.left=ora_create("left",4,ORA_CANONICAL);state.right=ora_create("right",5,ORA_CANONICAL);
    check(state.left && state.right,"catalog race anchors");ora_get_stats(&before);
    for(unsigned i=0;i<8;++i)threads[i]=CreateThread(NULL,0,catalog_race,&state,0,NULL);
    check(WaitForMultipleObjects(8,threads,TRUE,INFINITE)==WAIT_OBJECT_0,"catalog race join");
    for(unsigned i=0;i<8;++i)CloseHandle(threads[i]);
    ora_get_stats(&after);
    check(!failed && before.fingerprints==after.fingerprints,"catalog acquire/rebind/remove/destroy races without rehash");
    named_content_destroy(&state.map);ora_release(state.left);ora_release(state.right);
    ora_get_stats(&after);
    check(!after.objects && !allocations,"no final object/allocation leak");
    printf("ORA storage: sharing/rebind, collisions, mutable independence, lifetime, views, zero-rehash, OOM, concurrency, ingestion: PASS\n");
    return 0;
}
