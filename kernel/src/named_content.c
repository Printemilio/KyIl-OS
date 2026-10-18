#include <unos/named_content.h>
#include <unos/kernel_allocator.h>
#include <string.h>
#ifndef ORA_TESTING
#include <unos/smp.h>
#endif

struct named_content_entry {
    ora_ref_t content;
    struct named_content_entry *next;
    char name[];
};
static unsigned bucket(const char *name) {
    uint64_t hash=UINT64_C(14695981039346656037);
    while(*name){hash^=(uint8_t)*name++;hash*=UINT64_C(1099511628211);}
    return hash%128;
}
static uint64_t lock(named_content_map_t *map) {
    uint64_t flags=0;
#ifndef ORA_TESTING
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
#endif
    while(__atomic_test_and_set(&map->lock,__ATOMIC_ACQUIRE)) {
#ifndef ORA_TESTING
        smp_relax();
#else
        __asm__ volatile("pause");
#endif
    }
    return flags;
}
static void unlock(named_content_map_t *map,uint64_t flags) {
    __atomic_clear(&map->lock,__ATOMIC_RELEASE);
#ifndef ORA_TESTING
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory","cc");
#else
    (void)flags;
#endif
}
static named_content_entry_t **find(named_content_map_t *map,const char *name) {
    named_content_entry_t **link=&map->buckets[bucket(name)];
    while(*link && strcmp((*link)->name,name))link=&(*link)->next;
    return link;
}
static named_content_entry_t *new_entry(const char *name) {
    size_t size=strlen(name);
    if(size>SIZE_MAX-sizeof(named_content_entry_t)-1)return NULL;
    named_content_entry_t *entry=kernel_allocator_alloc(0,sizeof(*entry)+size+1);
    if(entry){entry->content=NULL;entry->next=NULL;memcpy(entry->name,name,size+1);}
    return entry;
}
int named_content_bind(named_content_map_t *map,const char *name,ora_ref_t content,enum named_content_mode mode) {
    if(!map || !name || !*name || !content || ora_kind(content)!=ORA_CONTENT)return -1;
    uint64_t flags=lock(map);named_content_entry_t **link=find(map,name),*entry=*link;
    if((entry && mode==NAMED_CREATE) || (!entry && mode==NAMED_REPLACE)){unlock(map,flags);return -1;}
    named_content_entry_t *created=entry?NULL:new_entry(name);
    if(!entry && !created){unlock(map,flags);return -1;}
    ora_ref_t held=ora_retain(content);
    if(!held){kernel_allocator_free(created);unlock(map,flags);return -1;}
    if(!entry){entry=created;*link=entry;}
    ora_ref_t old=entry->content;entry->content=held;
    unlock(map,flags);ora_release(old);return 0;
}
int named_content_put(named_content_map_t *map,const char *name,const void *data,size_t size,enum named_content_mode mode) {
    if(!data && size)return -1;
    ora_ref_t content=ora_create(data,size,ORA_CANONICAL);if(!content)return -1;
    int result=named_content_bind(map,name,content,mode);ora_release(content);return result;
}
ora_ref_t named_content_acquire(named_content_map_t *map,const char *name) {
    if(!map || !name || !*name)return NULL;
    uint64_t flags=lock(map);named_content_entry_t *entry=*find(map,name);
    ora_ref_t result=entry?ora_retain(entry->content):NULL;unlock(map,flags);return result;
}
void named_content_remove(named_content_map_t *map,const char *name) {
    if(!map || !name)return;
    uint64_t flags=lock(map);named_content_entry_t **link=find(map,name),*entry=*link;
    if(entry)*link=entry->next;unlock(map,flags);
    if(entry){ora_release(entry->content);kernel_allocator_free(entry);}
}
int named_content_move(named_content_map_t *map,const char *old,const char *name) {
    if(!map || !old || !name || !*name)return -1;
    if(!strcmp(old,name))return 0;
    uint64_t flags=lock(map);named_content_entry_t **source=find(map,old),*entry=*source;
    if(!entry){unlock(map,flags);return 0;} /* no cache to rename */
    named_content_entry_t **destination=find(map,name);
    if(*destination){unlock(map,flags);return -1;}
    named_content_entry_t *replacement=new_entry(name);
    if(!replacement){unlock(map,flags);return -1;}
    replacement->content=entry->content;*source=entry->next;
    /* source/destination may be in the same bucket: recompute after unlink. */
    destination=find(map,name);*destination=replacement;
    unlock(map,flags);kernel_allocator_free(entry);return 0;
}
void named_content_destroy(named_content_map_t *map) {
    if(!map)return;
    uint64_t flags=lock(map);named_content_entry_t *entries=NULL;
    for(unsigned i=0;i<128;++i) {
        named_content_entry_t *entry=map->buckets[i];map->buckets[i]=NULL;
        while(entry){named_content_entry_t *next=entry->next;entry->next=entries;entries=entry;entry=next;}
    }
    unlock(map,flags);
    while(entries){named_content_entry_t *next=entries->next;ora_release(entries->content);kernel_allocator_free(entries);entries=next;}
}
