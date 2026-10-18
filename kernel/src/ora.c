#include <unos/ora.h>
#include <unos/kernel_allocator.h>
#include <string.h>
#ifndef ORA_TESTING
#include <unos/debug_log.h>
#include <unos/smp.h>
#include <stdio.h>
#endif

#define ORA_BUCKETS 1024u
#define ORA_PAGE 4096u
struct ora_object {
    size_t size;
    uint64_t references, fingerprint;
    ora_kind_t kind;
    uint8_t indexed;
    void *data;
    void (*destroy)(void *);
    struct ora_object *next;
};
static ora_ref_t content_index[ORA_BUCKETS];
static uint8_t ora_lock_word;
static ora_stats_t statistics;

static uint64_t ora_lock(void) {
    uint64_t flags=0;
#ifndef ORA_TESTING
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
#endif
    while (__atomic_test_and_set(&ora_lock_word,__ATOMIC_ACQUIRE)) {
#ifndef ORA_TESTING
        smp_relax();
#else
        __asm__ volatile("pause");
#endif
    }
    return flags;
}
static void ora_unlock(uint64_t flags) {
    __atomic_clear(&ora_lock_word,__ATOMIC_RELEASE);
#ifndef ORA_TESTING
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory","cc");
#else
    (void)flags;
#endif
}

/* H is private to storage. These two functions are its replacement boundary. */
static uint64_t fingerprint_begin(void) { return UINT64_C(14695981039346656037); }
static uint64_t fingerprint_byte(uint64_t hash, uint8_t byte) {
    return (hash ^ byte) * UINT64_C(1099511628211);
}
static ora_ref_t allocate_object(size_t length) {
    ora_ref_t object=kernel_allocator_alloc(0u,sizeof(*object));
    if(!object)return NULL;
    memset(object,0,sizeof(*object));
    object->references=1u;
    object->size=length;
    return object;
}
static ora_ref_t publish_content(ora_ref_t candidate, int canonical) {
    const size_t bucket=candidate->fingerprint % ORA_BUCKETS;
    uint64_t flags=ora_lock();
    if(canonical) {
        ++statistics.fingerprints;
        for(ora_ref_t object=content_index[bucket];object;object=object->next) {
            if(object->fingerprint==candidate->fingerprint &&
               object->size==candidate->size &&
               memcmp(object->data,candidate->data,candidate->size)==0) {
                if(object->references==UINT64_MAX) {
                    ora_unlock(flags);
                    kernel_allocator_free(candidate->data);
                    kernel_allocator_free(candidate);
                    return NULL;
                }
                ++object->references;
                ++statistics.deduplicated;
                ora_unlock(flags);
                kernel_allocator_free(candidate->data);
                kernel_allocator_free(candidate);
                return object;
            }
        }
        candidate->indexed=1u;
        candidate->next=content_index[bucket];
        content_index[bucket]=candidate;
    }
    ++statistics.objects; ++statistics.contents;
    statistics.bytes+=candidate->size;
    ora_unlock(flags);
    return candidate;
}
ora_ref_t ora_create_zero_extended(const void *data, size_t data_length,
                                  size_t length, ora_policy_t policy) {
    if(data_length>length || (data_length && !data) ||
       (policy!=ORA_UNIQUE && policy!=ORA_CANONICAL) ||
       length>SIZE_MAX-(ORA_PAGE-1u))return NULL;
    ora_ref_t object=allocate_object(length);
    if(!object)return NULL;
    size_t allocation=length>=ORA_PAGE ? (length+ORA_PAGE-1u)&~(size_t)(ORA_PAGE-1u) : length;
    if(!allocation)allocation=1;
    object->data=kernel_allocator_alloc(0u,allocation);
    if(!object->data){kernel_allocator_free(object);return NULL;}
    object->kind=ORA_CONTENT;
    uint64_t hash=fingerprint_begin();
    const uint8_t *source=data;
    uint8_t *stored=object->data;
    for(size_t i=0;i<data_length;++i) {
        uint8_t byte=__atomic_load_n(&source[i],__ATOMIC_RELAXED);
        stored[i]=byte;
        if(policy==ORA_CANONICAL)hash=fingerprint_byte(hash,byte);
    }
    for(size_t i=data_length;i<length;++i) {
        stored[i]=0;
        if(policy==ORA_CANONICAL)hash=fingerprint_byte(hash,0);
    }
    /* The final exported page contains no unrelated kernel bytes. */
    if(allocation>length)memset(stored+length,0,allocation-length);
    object->fingerprint=hash;
    return publish_content(object,policy==ORA_CANONICAL);
}
ora_ref_t ora_create(const void *data,size_t length,ora_policy_t policy) {
    return ora_create_zero_extended(data,length,length,policy);
}
ora_ref_t ora_storage_create(size_t length,void *state,void (*destroy)(void *)) {
    if(!state || !destroy)return NULL;
    ora_ref_t object=allocate_object(length);
    if(!object)return NULL;
    object->kind=ORA_STORAGE; object->data=state; object->destroy=destroy;
    uint64_t flags=ora_lock();
    ++statistics.objects; ++statistics.storages;
    ora_unlock(flags);
    return object;
}
void *ora_storage_state(ora_ref_t object) {
    return object && object->kind==ORA_STORAGE ? object->data : NULL;
}
ora_ref_t ora_retain(ora_ref_t object) {
    if(!object)return NULL;
    uint64_t flags=ora_lock();
    if(object->references==UINT64_MAX){ora_unlock(flags);return NULL;}
    ++object->references;
    ora_unlock(flags);
    return object;
}
void ora_release(ora_ref_t object) {
    if(!object)return;
    uint64_t flags=ora_lock();
    if(--object->references){ora_unlock(flags);return;}
    if(object->indexed) {
        ora_ref_t *link=&content_index[object->fingerprint%ORA_BUCKETS];
        while(*link && *link!=object)link=&(*link)->next;
        if(*link)*link=object->next;
    }
    --statistics.objects;
    if(object->kind==ORA_CONTENT){--statistics.contents;statistics.bytes-=object->size;}
    else --statistics.storages;
    ora_unlock(flags);
    /* Providers may release other ORA objects; never invoke them under the index lock. */
    if(object->kind==ORA_STORAGE)object->destroy(object->data);
    else kernel_allocator_free(object->data);
    kernel_allocator_free(object);
}
ora_kind_t ora_kind(ora_ref_t object) { return object ? object->kind : ORA_CONTENT; }
size_t ora_size(ora_ref_t object) { return object ? object->size : 0u; }
const void *ora_data(ora_ref_t object) {
    return object && object->kind==ORA_CONTENT ? object->data : NULL;
}
int ora_view_acquire(ora_ref_t object,size_t offset,size_t length,ora_view_t *out) {
    if(!object || !out || out->object || offset>object->size || length>object->size-offset)return -1;
    if(!ora_retain(object))return -1;
    *out=(ora_view_t){object,offset,length};
    return 0;
}
void ora_view_release(ora_view_t *view) {
    if(view){ora_release(view->object);*view=(ora_view_t){0};}
}
const void *ora_view_data(const ora_view_t *view) {
    const uint8_t *data=view ? ora_data(view->object) : NULL;
    return data ? data+view->offset : NULL;
}
int ora_view_physical_page(const ora_view_t *view,size_t offset,uint64_t *out) {
    if(!view || !view->object || !out || !view->length ||
       view->offset>ora_size(view->object) || view->length>ora_size(view->object)-view->offset ||
       offset>=view->length || offset%ORA_PAGE || view->offset%ORA_PAGE ||
       (view->length%ORA_PAGE && view->offset+view->length!=ora_size(view->object)))return -1;
    const uint8_t *data=ora_view_data(view);
    return data ? kernel_allocator_physical_page(data,offset,out) : -1;
}
void ora_get_stats(ora_stats_t *out) {
    if(out){uint64_t flags=ora_lock();*out=statistics;ora_unlock(flags);}
}
void ora_memory_diagnostic(void) {
#ifndef ORA_TESTING
    ora_stats_t stats; char line[192]; ora_get_stats(&stats);
    snprintf(line,sizeof(line),"memory-category: ora objects=%u contents=%u storages=%u bytes=%u hashes=%u dedup=%u\n",
        (unsigned)stats.objects,(unsigned)stats.contents,(unsigned)stats.storages,
        (unsigned)stats.bytes,(unsigned)stats.fingerprints,(unsigned)stats.deduplicated);
    debug_log_puts(line);
#endif
}
#ifdef ORA_TESTING
ora_ref_t ora_test_create_fingerprint(const void *data,size_t length,uint64_t fingerprint) {
    ora_ref_t object=allocate_object(length);
    if(!object)return NULL;
    object->data=kernel_allocator_alloc(0u,length?length:1u);
    if(!object->data){kernel_allocator_free(object);return NULL;}
    memcpy(object->data,data,length);object->kind=ORA_CONTENT;
    object->fingerprint=fingerprint;
    return publish_content(object,1);
}
#endif

uint64_t ora_reference_count(ora_ref_t object) {
    if(!object)return 0;uint64_t flags=ora_lock(),refs=object->references;ora_unlock(flags);return refs;
}
