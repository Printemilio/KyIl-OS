#include <unos/versioned_store.h>
#include <unos/kernel_allocator.h>
#include <string.h>

#define VERSIONED_BUCKETS 128u
typedef struct stored_version {
    uint32_t number;
    ora_ref_t content;
    struct stored_version *older;
} stored_version_t;
typedef struct application_key {
    uint64_t key;
    uint32_t next_version;
    stored_version_t *current;
    struct application_key *next;
} application_key_t;
typedef struct application_store {
    uint32_t owner;
    application_key_t *keys[VERSIONED_BUCKETS];
    struct application_store *next;
} application_store_t;
static application_store_t *stores;
static uint8_t store_lock_word;
static uint64_t store_lock(void) {
    uint64_t flags=0;
#ifndef ORA_TESTING
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
#endif
    while(__atomic_test_and_set(&store_lock_word,__ATOMIC_ACQUIRE))__asm__ volatile("pause");
    return flags;
}
static void store_unlock(uint64_t flags) {
    __atomic_clear(&store_lock_word,__ATOMIC_RELEASE);
#ifndef ORA_TESTING
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory","cc");
#else
    (void)flags;
#endif
}
static size_t key_bucket(uint64_t key) {
    key^=key>>30;key*=UINT64_C(0xbf58476d1ce4e5b9);
    key^=key>>27;key*=UINT64_C(0x94d049bb133111eb);
    return (key^(key>>31))%VERSIONED_BUCKETS;
}
static application_store_t *find_store(uint32_t owner) {
    for(application_store_t *store=stores;store;store=store->next)
        if(store->owner==owner)return store;
    return NULL;
}
static application_key_t *find_key(application_store_t *store,uint64_t key) {
    if(store)for(application_key_t *entry=store->keys[key_bucket(key)];entry;entry=entry->next)
        if(entry->key==key)return entry;
    return NULL;
}
static void release_versions(stored_version_t *version) {
    while(version){stored_version_t *next=version->older;ora_release(version->content);kernel_allocator_free(version);version=next;}
}
int versioned_put(uint32_t owner,uint64_t key,const void *data,size_t size,uint32_t *out) {
    if(!data || !size)return -1;
    ora_ref_t content=ora_create(data,size,ORA_CANONICAL);
    if(!content)return -1;
    stored_version_t *version=kernel_allocator_alloc(owner,sizeof(*version));
    if(!version){ora_release(content);return -1;}
    uint64_t flags=store_lock();
    application_store_t *store=find_store(owner);
    int fresh_store=0;
    if(!store) {
        store=kernel_allocator_alloc(owner,sizeof(*store));
        if(store){memset(store,0,sizeof(*store));store->owner=owner;fresh_store=1;}
    }
    application_key_t *entry=find_key(store,key);
    if(store && !entry) {
        entry=kernel_allocator_alloc(owner,sizeof(*entry));
        if(entry){*entry=(application_key_t){.key=key,.next_version=1,.next=store->keys[key_bucket(key)]};store->keys[key_bucket(key)]=entry;}
    }
    if(!entry || !entry->next_version) {
        store_unlock(flags);
        if(fresh_store)kernel_allocator_free(store);
        kernel_allocator_free(version);ora_release(content);return -1;
    }
    version->number=entry->next_version++;
    version->content=content;version->older=entry->current;
    entry->current=version;
    if(fresh_store){store->next=stores;stores=store;}
    if(out)*out=version->number;
    store_unlock(flags);return 0;
}
ora_ref_t versioned_acquire(uint32_t owner,uint64_t key,uint32_t number,versioned_info_t *info) {
    uint64_t flags=store_lock();
    application_key_t *entry=find_key(find_store(owner),key);
    stored_version_t *version=entry?entry->current:NULL;
    if(number)while(version && version->number!=number)version=version->older;
    ora_ref_t content=version?ora_retain(version->content):NULL;
    if(content && info)*info=(versioned_info_t){version->number,ora_size(content),version==entry->current};
    store_unlock(flags);return content;
}
uint32_t versioned_current(uint32_t owner,uint64_t key) {
    uint64_t flags=store_lock();application_key_t *entry=find_key(find_store(owner),key);
    uint32_t number=entry && entry->current?entry->current->number:0;
    store_unlock(flags);return number;
}
size_t versioned_count(uint32_t owner,uint64_t key) {
    uint64_t flags=store_lock();application_key_t *entry=find_key(find_store(owner),key);size_t count=0;
    if(entry)for(stored_version_t *version=entry->current;version;version=version->older)++count;
    store_unlock(flags);return count;
}
int versioned_info_at(uint32_t owner,uint64_t key,size_t index,versioned_info_t *out) {
    if(!out)return -1;
    uint64_t flags=store_lock();application_key_t *entry=find_key(find_store(owner),key);
    stored_version_t *version=entry?entry->current:NULL;
    while(version && index--)version=version->older;
    if(version)*out=(versioned_info_t){version->number,ora_size(version->content),version==entry->current};
    store_unlock(flags);return version?0:-1;
}
int versioned_remove(uint32_t owner,uint64_t key,uint32_t number) {
    uint64_t flags=store_lock();application_key_t *entry=find_key(find_store(owner),key);
    stored_version_t **link=entry?&entry->current:NULL;
    if(number)while(link && *link && (*link)->number!=number)link=&(*link)->older;
    stored_version_t *removed=link?*link:NULL;
    if(removed){*link=removed->older;removed->older=NULL;}
    store_unlock(flags);release_versions(removed);return removed?0:-1;
}
void versioned_remove_all(uint32_t owner,uint64_t key) {
    uint64_t flags=store_lock();application_store_t *store=find_store(owner);
    application_key_t **link=store?&store->keys[key_bucket(key)]:NULL;
    while(link && *link && (*link)->key!=key)link=&(*link)->next;
    application_key_t *removed=link?*link:NULL;
    if(removed)*link=removed->next;
    store_unlock(flags);
    if(removed){release_versions(removed->current);kernel_allocator_free(removed);}
}
void versioned_reap(uint32_t owner) {
    uint64_t flags=store_lock();application_store_t **link=&stores;
    while(*link && (*link)->owner!=owner)link=&(*link)->next;
    application_store_t *removed=*link;if(removed)*link=removed->next;
    store_unlock(flags);
    if(!removed)return;
    for(unsigned i=0;i<VERSIONED_BUCKETS;++i) {
        application_key_t *key=removed->keys[i];
        while(key){application_key_t *next=key->next;release_versions(key->current);kernel_allocator_free(key);key=next;}
    }
    kernel_allocator_free(removed);
}
