#include <unos/process_memory.h>
#include <unos/compat_linux_futex_pi.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unos/boot_requests.h>
#include <unos/debug_log.h>
#include <unos/heap.h>
#include <unos/vex_legacy_compat.h>
#include <unos/ora.h>
#include <unos/kernel_guard.h>
#include <unos/pmm.h>
#include <unos/scheduler.h>
#include <unos/vmm.h>
#include <unos/memory_diagnostic.h>


static kernel_gate_t memory_gate=KERNEL_GATE_INIT;
/* Device IRQs enqueue input/I/O and update scheduler/mailbox state only;
   PM mutations run in threads or synchronous faults. Keep mappings pinned
   atomically without blocking unrelated IRQs for a large copy/materialization. */
kernel_guard_t process_memory_begin_update(void) {return kernel_guard_enter_irq_safe(&memory_gate);}
void process_memory_end_update(kernel_guard_t *guard) {kernel_guard_leave(guard);}
typedef kernel_guard_t pmem_transaction_t;
static inline pmem_transaction_t pmem_transaction_begin(void) {return process_memory_begin_update();}
static inline void pmem_transaction_end(pmem_transaction_t *guard) {kernel_guard_leave(guard);}

#define PROCESS_MEMORY_ROOT_MAGIC           0x4f52415f4e53524full
/* Zero selects the process's active inventory, never a content/storage key. */
#define PMEM_ACTIVE_NAMESPACE              0u
#define PROCESS_MEMORY_PAGE_SIZE            4096ull

typedef struct process_memory_namespace_root {
    uint64_t magic;
    uint64_t namespace_id;
    uint64_t alloc_hint;
    uint32_t region_count;
    uint32_t reserved0;
} process_memory_namespace_root_t;


#define PMEM_REGION_BUCKETS 256u
typedef struct pmem_region_node {
    process_memory_region_t region;
    uint8_t present;
    struct pmem_region_node *next;
} pmem_region_node_t;
typedef struct pmem_namespace {
    uint32_t owner;
    uint64_t identity;
    uint8_t active;
    uint64_t private_charge,peak_private_charge;
    process_memory_namespace_root_t *root;
    pmem_region_node_t *regions[PMEM_REGION_BUCKETS];
    /* Address-ordered index over `regions`, see region_index_ready(). */
    uint64_t generation;                   /* bumped by every membership change */
    pmem_region_node_t **index;
    uint32_t index_count, index_capacity;
    uint64_t index_generation;
    const process_memory_namespace_root_t *index_root;
    uint32_t index_root_count;
    uint8_t index_ok;
    struct pmem_namespace *next;
} pmem_namespace_t;
static pmem_namespace_t *memory_namespaces;
static uint64_t private_region_charge(const process_memory_region_t *r){
    if(r->shared_id || (r->kind_flags&(PROCESS_MEMORY_FLAG_RESERVATION|PROCESS_MEMORY_FLAG_SHARED|PROCESS_MEMORY_FLAG_ORA_DIRECT|PROCESS_MEMORY_FLAG_JIT|PROCESS_MEMORY_FLAG_SURFACE)))return 0;
    return (r->length+4095ull)&~4095ull;
}
static void private_charge_change(pmem_namespace_t *space,uint64_t before,uint64_t after){
    space->private_charge=space->private_charge-before+after;
    if(space->private_charge>space->peak_private_charge)space->peak_private_charge=space->private_charge;
    if(space->active)pmm_commit_delta(0,(int64_t)after-(int64_t)before);
}
static pmem_namespace_t *find_inventory_namespace(uint32_t owner,uint64_t identity) {
    for(pmem_namespace_t *space=memory_namespaces;space;space=space->next)
        if(space->owner==owner && (identity==PMEM_ACTIVE_NAMESPACE ? space->active : space->identity==identity))return space;
    return NULL;
}
static size_t region_bucket(uint64_t address) {return ((address>>12)^(address>>20))%PMEM_REGION_BUCKETS;}
static pmem_region_node_t *find_inventory_region(pmem_namespace_t *space,uint64_t address) {
    if(space)for(pmem_region_node_t *node=space->regions[region_bucket(address)];node;node=node->next)
        if(node->region.addr==address)return node;
    return NULL;
}
static void *store_region_meta(uint32_t owner,uint64_t namespace_id,uint64_t address,const void *data) {
    pmem_namespace_t *space=find_inventory_namespace(owner,namespace_id);
    if(!space || !data)return NULL;
    const process_memory_region_t *incoming=data;
    ora_view_t held={0};
    if(incoming->content.object && ora_view_acquire(incoming->content.object,incoming->content.offset,incoming->content.length,&held)!=0)return NULL;
    pmem_region_node_t *node=find_inventory_region(space,address);
    ++space->generation;
    if(!node) {
        node=kmalloc(sizeof(*node));if(!node){ora_view_release(&held);return NULL;}
        memset(node,0,sizeof(*node));node->next=space->regions[region_bucket(address)];space->regions[region_bucket(address)]=node;
    }
    private_charge_change(space,node->present?private_region_charge(&node->region):0,private_region_charge(incoming));
    if(node->present)ora_view_release(&node->region.content);
    memcpy(&node->region,data,sizeof(node->region));node->region.content=held;node->present=1;
    return &node->region;
}
static void remove_inventory_namespace(uint32_t owner,uint64_t identity) {
    pmem_namespace_t **link=&memory_namespaces;
    while(*link && ((*link)->owner!=owner || (*link)->identity!=identity))link=&(*link)->next;
    pmem_namespace_t *space=*link;if(!space)return;*link=space->next;
    if(space->active)pmm_commit_delta(0,-(int64_t)space->private_charge);
    for(unsigned i=0;i<PMEM_REGION_BUCKETS;++i) {
        pmem_region_node_t *node=space->regions[i];
        while(node){pmem_region_node_t *next=node->next;if(node->present)ora_view_release(&node->region.content);kfree(node);node=next;}
    }
    kfree(space->root);kfree(space->index);kfree(space);
}

static uint64_t next_namespace_id = 1u;
static void debug_put_hex64_local(uint64_t value);

typedef struct process_memory_shared_page {
    uint32_t index;
    uint32_t reserved;
    uint64_t phys;
    struct process_memory_shared_page *next;
} process_memory_shared_page_t;

typedef struct process_memory_shared_backing {
    uint64_t id;
    ora_ref_t storage;
    uint32_t page_count;
    uint64_t *phys_pages;
    process_memory_shared_page_t *sparse_pages;
    uint8_t sparse;
    uint8_t contiguous;
    uint8_t reserved0[2];
    uint32_t sparse_committed_pages;
    struct process_memory_shared_backing *next;
} process_memory_shared_backing_t;

static process_memory_shared_backing_t *shared_backings;
static uint64_t next_shared_backing_id = 1u;

typedef struct process_memory_file_page {
    uint64_t index;
    uint64_t phys;
    struct process_memory_file_page *next;
} process_memory_file_page_t;

typedef struct process_memory_file_backing {
    uint64_t id;
    ora_ref_t storage;
    uint32_t page_count;
    uint64_t file_offset;
    uint64_t file_length;
    vex_web_fd_object_t *object;
    uint8_t shared;
    uint8_t reserved[7];
    struct process_memory_file_backing *next;
} process_memory_file_backing_t;

static process_memory_file_backing_t *file_backings;
static uint64_t next_file_backing_id = 1u;
void process_memory_diagnostic(void) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    uint64_t shared=0,pages=0,files=0,file_pages=0;char line[192];
    for(process_memory_shared_backing_t *b=shared_backings;b;b=b->next) {
        ++shared;
        if(!b->sparse)pages+=b->page_count;
        else for(process_memory_shared_page_t *p=b->sparse_pages;p;p=p->next)++pages;
    }
    for(process_memory_file_backing_t *b=file_backings;b;b=b->next) {
        ++files;
        int duplicate=0;
        for(process_memory_file_backing_t *p=file_backings;p!=b;p=p->next)if(p->object==b->object)duplicate=1;
        if(!duplicate)for(process_memory_file_page_t *p=b->object->shared_mapping_pages;p;p=p->next)++file_pages;
    }
    snprintf(line,sizeof(line),"memory-category: backing shared=%u pages=%u file_mappings=%u shared_file_pages=%u\n",
        (unsigned int)shared,(unsigned int)pages,(unsigned int)files,(unsigned int)file_pages);
    debug_log_puts(line);
}

/* A spawn child has its own resource identity but temporarily borrows the
 * parent's address space. Inventory queries must name the memory owner. */
static uint32_t process_memory_owner(const process_t *process) {
    if (!process) return 0;
    if(process->borrowed_memory_group && process->cr3==process->borrowed_memory_group->cr3)
        return process->borrowed_memory_group->process_id;
    if (process->spawn_parent_pid) return process->spawn_parent_pid;
    return process->group ? process->group->process_id : process->pid;
}

static uint64_t align_down_u64(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1ull);
}

static uint64_t align_up_u64(uint64_t value, uint64_t alignment) {
    if (alignment == 0u || value > UINT64_MAX - (alignment - 1ull)) {
        return 0u;
    }
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

static process_memory_shared_backing_t *find_shared_backing(uint64_t id) {
    for (process_memory_shared_backing_t *backing = shared_backings;
         backing != NULL; backing = backing->next) {
        if (backing->id == id) {
            return backing;
        }
    }
    return NULL;
}

static int shared_backing_acquire(uint64_t id) {
    process_memory_shared_backing_t *backing=find_shared_backing(id);
    return backing && ora_retain(backing->storage) ? 0 : -1;
}

static void shared_storage_destroy(void *state) {
    process_memory_shared_backing_t *backing=state;
    process_memory_shared_backing_t **link=&shared_backings;
    while(*link && *link!=backing)link=&(*link)->next;
    if(*link)*link=backing->next;
    if (backing->sparse != 0u) {
        process_memory_shared_page_t *page = backing->sparse_pages;
        while (page != NULL) {
            process_memory_shared_page_t *next = page->next;
            if (page->phys != 0u) {
                pmm_free_page((void *)(uintptr_t)page->phys);
            }
            kfree(page);
            page = next;
        }
    } else if (backing->contiguous != 0u) {
        if (backing->phys_pages != NULL && backing->page_count != 0u &&
            backing->phys_pages[0] != 0u) {
            pmm_free_contiguous_pages(
                (void *)(uintptr_t)backing->phys_pages[0], backing->page_count);
        }
    } else {
        for (uint32_t index = 0u; index < backing->page_count; ++index) {
            if (backing->phys_pages[index] != 0u) {
                pmm_free_page((void *)(uintptr_t)backing->phys_pages[index]);
            }
        }
    }
    kfree(backing->phys_pages);
    kfree(backing);
}
static void shared_backing_release(uint64_t id) {
    process_memory_shared_backing_t *backing=find_shared_backing(id);
    if(backing)ora_release(backing->storage);
}

static int shared_backing_ensure_page(process_memory_shared_backing_t *backing,
                                      uint32_t index, uint64_t *phys_out) {
    void *phys;

    if (backing == NULL || phys_out == NULL || index >= backing->page_count ||
        hhdm_request.response == NULL) {
        return -1;
    }
    if (backing->sparse != 0u) {
        process_memory_shared_page_t *page;
        for (page = backing->sparse_pages; page != NULL; page = page->next) {
            if (page->index == index) {
                *phys_out = page->phys;
                return 0;
            }
        }
        page = kmalloc(sizeof(*page));
        if (page == NULL) {
            return -1;
        }
        phys = pmm_alloc_page();
        if (phys == NULL) {
            kfree(page);
            return -1;
        }
        memset((uint8_t *)(hhdm_request.response->offset + (uint64_t)phys),
               0, PROCESS_MEMORY_PAGE_SIZE);
        page->index = index;
        page->reserved = 0u;
        page->phys = (uint64_t)phys;
        page->next = backing->sparse_pages;
        backing->sparse_pages = page;
        ++backing->sparse_committed_pages;
    } else if (backing->phys_pages[index] == 0u) {
        phys = pmm_alloc_page();
        if (phys == NULL) {
            return -1;
        }
        memset((uint8_t *)(hhdm_request.response->offset + (uint64_t)phys),
               0, PROCESS_MEMORY_PAGE_SIZE);
        backing->phys_pages[index] = (uint64_t)phys;
    }
    if (backing->sparse != 0u) {
        *phys_out = backing->sparse_pages->phys;
    } else {
        *phys_out = backing->phys_pages[index];
    }
    return 0;
}

static process_memory_file_backing_t *find_file_backing(uint64_t id) {
    for (process_memory_file_backing_t *backing = file_backings;
         backing != NULL; backing = backing->next) {
        if (backing->id == id) {
            return backing;
        }
    }
    return NULL;
}

static int file_backing_retain(uint64_t id) {
    process_memory_file_backing_t *backing=find_file_backing(id);
    return backing && ora_retain(backing->storage) ? 0 : -1;
}

static void file_storage_destroy(void *state) {
    process_memory_file_backing_t *backing=state;
    process_memory_file_backing_t **link=&file_backings;
    while(*link && *link!=backing)link=&(*link)->next;if(*link)*link=backing->next;
    if(backing->object)vex_web_fd_release(backing->object);kfree(backing);
}
static void file_backing_release(uint64_t id) {
    process_memory_file_backing_t *backing=find_file_backing(id);if(backing)ora_release(backing->storage);
}

static int file_backing_read_page(const process_memory_file_backing_t *backing,
                                  uint32_t index, void *destination) {
    uint64_t relative;
    size_t want;
    ssize_t got;

    if (backing == NULL || destination == NULL || index >= backing->page_count ||
        backing->object == NULL || hhdm_request.response == NULL) {
        return -1;
    }
    memset(destination, 0, PROCESS_MEMORY_PAGE_SIZE);
    relative = (uint64_t)index * PROCESS_MEMORY_PAGE_SIZE;
    if (relative >= backing->file_length) {
        return 0;
    }
    want = (size_t)(backing->file_length - relative);
    if (want > PROCESS_MEMORY_PAGE_SIZE) {
        want = PROCESS_MEMORY_PAGE_SIZE;
    }
    if (backing->file_offset > UINT64_MAX - relative ||
        backing->file_offset + relative > (uint64_t)INT64_MAX) {
        return -1;
    }
    got = vex_web_fd_pread_object(backing->object, destination, want,
                                  (off_t)(backing->file_offset + relative));
    if (got < 0 || (size_t)got > want) {
        return -1;
    }
    /* Short reads are valid at EOF; the page was already zero-filled. */
    return 0;
}

void process_memory_file_object_release(vex_web_fd_object_t *object) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    if (!object) return;
    process_memory_file_page_t *page = object->shared_mapping_pages;
    object->shared_mapping_pages = NULL;
    while (page) {
        process_memory_file_page_t *next = page->next;
        pmm_free_page((void *)(uintptr_t)page->phys);
        kfree(page);
        page = next;
    }
}

/* Overlay only materialized pages: no physical commitment for untouched bytes.
 * Also keeps read/pread/write coherent with MAP_SHARED through the same object. */
void process_memory_file_object_cache_copy(vex_web_fd_object_t *object,
    uint64_t offset, void *bytes, size_t count, int writing) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    if (!object || !bytes || !count || offset > UINT64_MAX - count) return;
    for (process_memory_file_page_t *page = object->shared_mapping_pages; page; page = page->next) {
        uint64_t start = page->index * PROCESS_MEMORY_PAGE_SIZE;
        uint64_t end = start + PROCESS_MEMORY_PAGE_SIZE;
        uint64_t first = offset > start ? offset : start;
        uint64_t last = offset + count < end ? offset + count : end;
        if (first >= last) continue;
        void *physical = (void *)(hhdm_request.response->offset + page->phys + first - start);
        void *buffer = (uint8_t *)bytes + first - offset;
        if (writing) memcpy(physical, buffer, (size_t)(last - first));
        else memcpy(buffer, physical, (size_t)(last - first));
    }
}

void process_memory_file_object_truncate(vex_web_fd_object_t *object, uint64_t size) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    if (!object) return;
    for (process_memory_file_page_t *page = object->shared_mapping_pages; page; page = page->next) {
        uint64_t start = page->index * PROCESS_MEMORY_PAGE_SIZE;
        if (size >= start && size - start >= PROCESS_MEMORY_PAGE_SIZE) continue;
        size_t first = size > start ? (size_t)(size - start) : 0u;
        memset((uint8_t *)(hhdm_request.response->offset + page->phys) + first, 0,
               PROCESS_MEMORY_PAGE_SIZE - first);
    }
}

static int file_backing_ensure_shared_page(process_memory_file_backing_t *backing,
                                           uint32_t index, uint64_t *phys_out) {
    if (!backing || !backing->shared || !phys_out || index >= backing->page_count ||
        !hhdm_request.response || !backing->object) return -1;
    const uint64_t absolute = backing->file_offset / PROCESS_MEMORY_PAGE_SIZE + index;
    if (absolute > (uint64_t)INT64_MAX / PROCESS_MEMORY_PAGE_SIZE) return -1;
    vex_web_fd_object_t *object = backing->object;
    for (process_memory_file_page_t *page = object->shared_mapping_pages; page; page = page->next) {
        if (page->index == absolute) { *phys_out = page->phys; return 0; }
    }
    process_memory_file_page_t *page = kmalloc(sizeof(*page));
    if (!page) return -1;
    void *phys = pmm_alloc_page();
    if (!phys) { kfree(page); return -1; }
    void *bytes = (void *)(hhdm_request.response->offset + (uint64_t)phys);
    memset(bytes, 0, PROCESS_MEMORY_PAGE_SIZE);
    ssize_t got = vex_web_fd_pread_object(object, bytes, PROCESS_MEMORY_PAGE_SIZE,
                                        (off_t)(absolute * PROCESS_MEMORY_PAGE_SIZE));
    if (got < 0 || (size_t)got > PROCESS_MEMORY_PAGE_SIZE) {
        pmm_free_page(phys); kfree(page); return -1;
    }
    page->index = absolute;
    page->phys = (uint64_t)phys;
    page->next = object->shared_mapping_pages;
    object->shared_mapping_pages = page;
    *phys_out = page->phys;
    return 0;
}
static int file_backing_create(int fd, uint64_t file_offset,
                               size_t length, size_t file_length, int shared,
                               uint64_t *id_out) {
    process_memory_file_backing_t *backing;
    vex_web_fd_object_t *object;
    uint64_t page_count64;

    if (id_out == NULL || length == 0u || file_length > length ||
        (uint64_t)length > UINT64_MAX - (PROCESS_MEMORY_PAGE_SIZE - 1ull) ||
        file_offset > (uint64_t)INT64_MAX ||
        (uint64_t)file_length > (uint64_t)INT64_MAX - file_offset) {
        return -1;
    }
    page_count64 = ((uint64_t)length + PROCESS_MEMORY_PAGE_SIZE - 1ull) /
        PROCESS_MEMORY_PAGE_SIZE;
    if (page_count64 == 0u || page_count64 > UINT32_MAX) {
        return -1;
    }
    object = vex_web_fd_acquire(fd);
    if (object == NULL || object->ops == NULL || object->ops->seek == NULL ||
        object->ops->read == NULL) {
        if (object != NULL) {
            (void)vex_web_fd_release(object);
        }
        return -1;
    }
    if (shared != 0) {
        for (process_memory_file_backing_t *existing = file_backings;
             existing != NULL; existing = existing->next) {
            if (existing->shared != 0u && existing->object == object &&
                existing->file_offset == file_offset &&
                existing->file_length == file_length &&
                existing->page_count == (uint32_t)page_count64 &&
                ora_reference_count(existing->storage) != UINT64_MAX) {
                if(!ora_retain(existing->storage)){vex_web_fd_release(object);return -1;}
                (void)vex_web_fd_release(object);
                *id_out = existing->id;
                return 0;
            }
        }
    }
    backing = kmalloc(sizeof(*backing));
    if (backing == NULL) {
        (void)vex_web_fd_release(object);
        return -1;
    }
    backing->id = next_file_backing_id++;
    if (backing->id == 0u) {
        backing->id = next_file_backing_id++;
    }
    backing->storage = NULL;
    backing->page_count = (uint32_t)page_count64;
    backing->file_offset = file_offset;
    backing->file_length = file_length;
    backing->object = object;
    backing->shared = shared != 0 ? 1u : 0u;
    memset(backing->reserved, 0, sizeof(backing->reserved));

    backing->storage=ora_storage_create(length,backing,file_storage_destroy);
    if(!backing->storage){vex_web_fd_release(object);kfree(backing);return -1;}
    backing->next = file_backings;
    file_backings = backing;
    *id_out = backing->id;
    return 0;
}

static int shared_backing_create_from_mapping(void *pml4,
                                              uint64_t addr,
                                              size_t length,
                                              uint64_t *id_out) {
    process_memory_shared_backing_t *backing;
    uint64_t page_count64;

    if (pml4 == NULL || id_out == NULL || length == 0u ||
        (uint64_t)length > UINT64_MAX - (PROCESS_MEMORY_PAGE_SIZE - 1ull) ||
        addr > UINT64_MAX - (uint64_t)length) {
        return -1;
    }
    page_count64 = ((uint64_t)length + PROCESS_MEMORY_PAGE_SIZE - 1ull) /
        PROCESS_MEMORY_PAGE_SIZE;
    if (page_count64 == 0u || page_count64 > UINT32_MAX ||
        page_count64 > (uint64_t)(SIZE_MAX / sizeof(uint64_t))) {
        return -1;
    }

    backing = kmalloc(sizeof(*backing));
    if (backing == NULL) {
        return -1;
    }
    backing->phys_pages = kmalloc((size_t)page_count64 * sizeof(uint64_t));
    if (backing->phys_pages == NULL) {
        kfree(backing);
        return -1;
    }
    backing->id = next_shared_backing_id++;
    if (backing->id == 0u) {
        backing->id = next_shared_backing_id++;
    }
    backing->storage = NULL;
    backing->page_count = (uint32_t)page_count64;
    backing->sparse_pages = NULL;
    backing->sparse = 0u;
    backing->contiguous = 0u;
    backing->next = shared_backings;

    for (uint32_t index = 0u; index < backing->page_count; ++index) {
        const uint64_t page = align_down_u64(addr + ((uint64_t)index * PROCESS_MEMORY_PAGE_SIZE),
                                             PROCESS_MEMORY_PAGE_SIZE);
        const uint64_t phys = vmm_get_physical_any(pml4, page) & ~(PROCESS_MEMORY_PAGE_SIZE - 1ull);
        if (phys == 0u) {
            kfree(backing->phys_pages);
            kfree(backing);
            return -1;
        }
        backing->phys_pages[index] = phys;
    }

    /* Adopt the physical pages only after the entire source mapping has been
       validated. An unpublished failure still belongs to the source PTEs. */
    backing->storage=ora_storage_create(length,backing,shared_storage_destroy);
    if(!backing->storage){kfree(backing->phys_pages);kfree(backing);return -1;}
    shared_backings = backing;
    *id_out = backing->id;
    return 0;
}

static int shared_backing_create_empty(size_t length, uint64_t *id_out) {
    process_memory_shared_backing_t *backing;
    uint64_t page_count64;

    if (id_out == NULL || length == 0u || (uint64_t)length > UINT64_MAX -
        (PROCESS_MEMORY_PAGE_SIZE - 1ull)) {
        return -1;
    }
    page_count64 = ((uint64_t)length + PROCESS_MEMORY_PAGE_SIZE - 1ull) /
        PROCESS_MEMORY_PAGE_SIZE;
    if (page_count64 == 0u || page_count64 > UINT32_MAX) {
        return -1;
    }
    backing = kmalloc(sizeof(*backing));
    if (backing == NULL) {
        return -1;
    }
    backing->phys_pages = kmalloc((size_t)page_count64 * sizeof(uint64_t));
    if (backing->phys_pages == NULL) {
        kfree(backing);
        return -1;
    }
    memset(backing->phys_pages, 0, (size_t)page_count64 * sizeof(uint64_t));
    backing->id = next_shared_backing_id++;
    if (backing->id == 0u) {
        backing->id = next_shared_backing_id++;
    }
    backing->storage = NULL;
    backing->page_count = (uint32_t)page_count64;
    backing->sparse_pages = NULL;
    backing->sparse = 0u;
    backing->contiguous = 0u;
    backing->storage=ora_storage_create(length,backing,shared_storage_destroy);
    if(!backing->storage){kfree(backing->phys_pages);kfree(backing);return -1;}
    backing->next = shared_backings;
    shared_backings = backing;
    *id_out = backing->id;
    return 0;
}

static int shared_backing_create_lazy(size_t length, uint64_t *id_out) {
    process_memory_shared_backing_t *backing;
    uint64_t page_count64;

    if (id_out == NULL || length == 0u || (uint64_t)length > UINT64_MAX -
        (PROCESS_MEMORY_PAGE_SIZE - 1ull)) {
        return -1;
    }
    page_count64 = ((uint64_t)length + PROCESS_MEMORY_PAGE_SIZE - 1ull) /
        PROCESS_MEMORY_PAGE_SIZE;
    if (page_count64 == 0u || page_count64 > UINT32_MAX ||
        page_count64 > (uint64_t)(SIZE_MAX / sizeof(uint64_t))) {
        return -1;
    }
    backing = kmalloc(sizeof(*backing));
    if (backing == NULL) {
        return -1;
    }
    backing->phys_pages = NULL;
    backing->id = next_shared_backing_id++;
    if (backing->id == 0u) {
        backing->id = next_shared_backing_id++;
    }
    backing->storage = NULL;
    backing->page_count = (uint32_t)page_count64;
    backing->sparse_pages = NULL;
    backing->sparse = 1u;
    backing->contiguous = 0u;
    backing->sparse_committed_pages = 0u;
    backing->storage=ora_storage_create(length,backing,shared_storage_destroy);
    if(!backing->storage){kfree(backing->phys_pages);kfree(backing);return -1;}
    backing->next = shared_backings;
    shared_backings = backing;
    *id_out = backing->id;
    return 0;
}








static uint64_t namespace_root_payload_size(uint32_t region_count) {
    return sizeof(process_memory_namespace_root_t) + ((uint64_t)region_count * sizeof(uint64_t));
}

static void debug_log_putchar_local(char c) {
    char text[2];

    text[0] = c;
    text[1] = '\0';
    debug_log_puts(text);
}


static void debug_put_hex64_local(uint64_t value) {
    static const char digits[] = "0123456789abcdef";

    debug_log_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        debug_log_putchar_local(digits[(value >> shift) & 0xfu]);
    }
}

static uint64_t *root_addresses(process_memory_namespace_root_t *root) {
    return (uint64_t *)(root + 1);
}

static const uint64_t *root_addresses_const(const process_memory_namespace_root_t *root) {
    return (const uint64_t *)(root + 1);
}

static process_memory_namespace_root_t *alloc_root_copy(uint64_t namespace_id, uint64_t alloc_hint, uint32_t region_count) {
    process_memory_namespace_root_t *root = kmalloc((size_t)namespace_root_payload_size(region_count));

    if (root == NULL) {
        return NULL;
    }

    root->magic = PROCESS_MEMORY_ROOT_MAGIC;
    root->namespace_id = namespace_id;
    root->alloc_hint = align_up_u64(alloc_hint, PROCESS_MEMORY_PAGE_SIZE);
    root->region_count = region_count;
    root->reserved0 = 0u;
    if (region_count != 0u) {
        memset(root_addresses(root), 0, (size_t)region_count * sizeof(uint64_t));
    }
    return root;
}

/* A root may be allocated with room to grow; `reserved0` then holds its capacity
 * in addresses. Registering or splitting regions edits such a root in place
 * instead of copying the whole address list for every change. */
static uint32_t root_capacity(const process_memory_namespace_root_t *root) {
    return root->reserved0 > root->region_count ? root->reserved0 : root->region_count;
}

static process_memory_namespace_root_t *grow_root(const process_memory_namespace_root_t *root, uint32_t needed) {
    process_memory_namespace_root_t *grown;
    uint32_t capacity;

    if (root == NULL || needed > UINT32_MAX / 4u) {
        return NULL;
    }
    capacity = needed * 2u + 16u;
    grown = kmalloc((size_t)namespace_root_payload_size(capacity));
    if (grown == NULL) {
        return NULL;
    }
    *grown = *root;
    grown->reserved0 = capacity;
    memcpy(root_addresses(grown), root_addresses_const(root), (size_t)root->region_count * sizeof(uint64_t));
    return grown;
}

static process_memory_namespace_root_t *duplicate_root_copy(const process_memory_namespace_root_t *root, uint32_t new_count) {
    process_memory_namespace_root_t *copy;
    uint32_t copied_count = 0u;

    if (root == NULL) {
        return NULL;
    }

    copy = alloc_root_copy(root->namespace_id, root->alloc_hint, new_count);
    if (copy == NULL) {
        return NULL;
    }

    if (root->region_count < new_count) {
        copied_count = root->region_count;
    } else {
        copied_count = new_count;
    }

    if (copied_count != 0u) {
        memcpy(root_addresses(copy), root_addresses_const(root), (size_t)copied_count * sizeof(uint64_t));
    }
    return copy;
}

static process_memory_namespace_root_t *copy_namespace_root(uint32_t pid, uint64_t identity) {
    pmem_namespace_t *space=find_inventory_namespace(pid,identity);
    return space && space->root ? duplicate_root_copy(space->root,space->root->region_count) : NULL;
}

static process_memory_namespace_root_t *copy_active_root(uint32_t pid) {
    return copy_namespace_root(pid, PMEM_ACTIVE_NAMESPACE);
}

/* Region lookups used to walk every region (one hash probe each) on each page
 * fault, user copy and mprotect, with the whole inventory held under the memory
 * gate. The index keeps the nodes of a namespace ordered by address so those
 * become binary searches. It is rebuilt lazily after a membership change (each
 * change bumps space->generation); registering a region updates it in place.
 * `index_ok` is false when the inventory is inconsistent or regions overlap;
 * callers then use their original linear scan. */
static void region_index_sort(pmem_region_node_t **node, uint32_t count) {
    for (uint32_t gap = count / 2u; gap != 0u; gap /= 2u) {
        for (uint32_t i = gap; i < count; ++i) {
            pmem_region_node_t *held = node[i];
            uint32_t j = i;

            while (j >= gap && node[j - gap]->region.addr > held->region.addr) {
                node[j] = node[j - gap];
                j -= gap;
            }
            node[j] = held;
        }
    }
}

static int region_index_current(const pmem_namespace_t *space) {
    return space->index != NULL && space->root != NULL &&
           space->index_generation == space->generation &&
           space->index_root == space->root &&
           space->index_root_count == space->root->region_count;
}

/* Returns 1 when the index can be trusted. */
static int region_index_ready(pmem_namespace_t *space) {
    uint32_t count = 0u;
    uint8_t ok = 1u;

    if (space == NULL || space->root == NULL) {
        return 0;
    }
    if (region_index_current(space)) {
        return space->index_ok;
    }
    if (space->root->region_count > space->index_capacity) {
        uint32_t capacity = space->root->region_count * 2u + 64u;
        pmem_region_node_t **grown = kmalloc((size_t)capacity * sizeof(*grown));

        if (grown == NULL) {
            return 0;
        }
        kfree(space->index);
        space->index = grown;
        space->index_capacity = capacity;
    }
    for (uint32_t i = 0u; i < space->root->region_count; ++i) {
        pmem_region_node_t *node = find_inventory_region(space, root_addresses(space->root)[i]);

        if (node == NULL || !node->present) {
            ok = 0u;
            continue;
        }
        if (node->region.length == 0u || (uint64_t)node->region.length > UINT64_MAX - node->region.addr) {
            continue;
        }
        space->index[count++] = node;
    }
    region_index_sort(space->index, count);
    for (uint32_t i = 1u; i < count; ++i) {
        if (space->index[i - 1u]->region.addr + (uint64_t)space->index[i - 1u]->region.length >
            space->index[i]->region.addr) {
            ok = 0u;
        }
    }
    space->index_count = count;
    space->index_generation = space->generation;
    space->index_root = space->root;
    space->index_root_count = space->root->region_count;
    space->index_ok = ok;
    return ok;
}

/* Number of index entries whose address is <= `address` (the entry before it is
 * the only one that can contain it). */
static uint32_t region_index_upper(const pmem_namespace_t *space, uint64_t address) {
    uint32_t low = 0u, high = space->index_count;

    while (low < high) {
        const uint32_t middle = low + (high - low) / 2u;

        if (space->index[middle]->region.addr <= address) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low;
}

/* Keep a current index current after `node` was published into the inventory. */
static void region_index_note_added(pmem_namespace_t *space, uint8_t was_current,
                                    const pmem_region_node_t *node) {
    uint32_t position;

    if (!was_current || !space->index_ok || space->index_count >= space->index_capacity ||
        node == NULL || !node->present || node->region.length == 0u) {
        return;
    }
    position = region_index_upper(space, node->region.addr);
    memmove(&space->index[position + 1u], &space->index[position],
            (size_t)(space->index_count - position) * sizeof(*space->index));
    space->index[position] = (pmem_region_node_t *)node;
    ++space->index_count;
    space->index_generation = space->generation;
    space->index_root = space->root;
    space->index_root_count = space->root->region_count;
}

/* Read-only views of the active inventory. They run under the memory gate, which
 * serializes every mutation, so they need no snapshot: copy_active_root() costs a
 * kmalloc (a mapped, zeroed, TLB-shot-down chunk once the inventory passes ~500
 * regions), and the page-fault path used to pay it on every fault. */
static int active_root_header(uint32_t pid, uint64_t *namespace_id, uint64_t *alloc_hint) {
    pmem_namespace_t *space = find_inventory_namespace(pid, PMEM_ACTIVE_NAMESPACE);

    if (space == NULL || space->root == NULL) {
        return -1;
    }
    if (namespace_id != NULL) {
        *namespace_id = space->root->namespace_id;
    }
    if (alloc_hint != NULL) {
        *alloc_hint = space->root->alloc_hint;
    }
    return 0;
}

/* The region of the active inventory that contains `address` and has every bit of
 * `required_kind` set, or NULL. Valid until the memory gate is released. */
static const process_memory_region_t *inventory_region_containing(pmem_namespace_t *space, uint64_t address,
                                                               uint32_t required_kind) {

    if (space == NULL || space->root == NULL) {
        return NULL;
    }
    if (region_index_ready(space)) {
        const uint32_t after = region_index_upper(space, address);
        const process_memory_region_t *region;

        if (after == 0u) {
            return NULL;
        }
        region = &space->index[after - 1u]->region;
        if (address >= region->addr + (uint64_t)region->length ||
            (region->kind_flags & required_kind) != required_kind) {
            return NULL;
        }
        return region;
    }
    for (uint32_t index = 0u; index < space->root->region_count; ++index) {
        const pmem_region_node_t *node = find_inventory_region(space, root_addresses(space->root)[index]);
        const process_memory_region_t *region;

        if (node == NULL || !node->present) {
            continue;
        }
        region = &node->region;
        if (region->length == 0u || (uint64_t)region->length > UINT64_MAX - region->addr ||
            (region->kind_flags & required_kind) != required_kind) {
            continue;
        }
        if (address >= region->addr && address < region->addr + (uint64_t)region->length) {
            return region;
        }
    }
    return NULL;
}

static const process_memory_region_t *active_region_containing(uint32_t pid,uint64_t address,uint32_t required_kind) {
    return inventory_region_containing(find_inventory_namespace(pid,PMEM_ACTIVE_NAMESPACE),address,required_kind);
}

static int store_root_copy(uint32_t pid,uint64_t identity,const process_memory_namespace_root_t *root) {
    if(!root)return -1;
    if(identity==PMEM_ACTIVE_NAMESPACE) {
        pmem_namespace_t *space=find_inventory_namespace(pid,root->namespace_id);
        if(!space)return -1;
        uint64_t previous=0;
        for(pmem_namespace_t *other=memory_namespaces;other;other=other->next)if(other->owner==pid){
            if(other->active){previous+=other->private_charge;if(other->peak_private_charge>space->peak_private_charge)space->peak_private_charge=other->peak_private_charge;}
            other->active=other==space;
        }
        pmm_commit_delta(0,(int64_t)space->private_charge-(int64_t)previous);
        return 0;
    }
    process_memory_namespace_root_t *copy=duplicate_root_copy(root,root->region_count);
    if(!copy)return -1;
    pmem_namespace_t *space=find_inventory_namespace(pid,identity);
    if(!space) {
        space=kmalloc(sizeof(*space));
        if(!space){kfree(copy);return -1;}
        memset(space,0,sizeof(*space));space->owner=pid;space->identity=identity;
        space->next=memory_namespaces;memory_namespaces=space;
    }
    process_memory_namespace_root_t *old=space->root;space->root=copy;kfree(old);++space->generation;
    /* Removed metadata is held only until publication, allowing rollback to
       reuse it without allocation. It is not retained as version history. */
    for(unsigned i=0;i<PMEM_REGION_BUCKETS;++i) {
        pmem_region_node_t **link=&space->regions[i];
        while(*link) {
            pmem_region_node_t *node=*link;
            if(!node->present){*link=node->next;kfree(node);++space->generation;}
            else link=&node->next;
        }
    }
    return 0;
}

static uint64_t allocate_namespace_id(void) {
    /* No identity reuse after saturation: a stale namespace cannot alias a
       new inventory, even across failed creations. */
    return next_namespace_id ? next_namespace_id++ : 0u;
}

static int ensure_active_root(uint32_t pid) {
    uint64_t namespace_id;
    /* Existence is not an allocation attempt. OOM must not manufacture a
       new empty active namespace in place of a live inventory. */
    if(find_inventory_namespace(pid,PMEM_ACTIVE_NAMESPACE))return 0;

    if (process_memory_namespace_begin(pid, 0u, &namespace_id) != 0) {

        return -1;
    }
    return process_memory_namespace_commit(pid, namespace_id);
}


static uint64_t region_flags_from_prot(int prot) {
    uint64_t flags = VMM_PAGE_USER;

    /* A PROT_NONE region remains a reservation in the namespace, but its PTEs
       must stay non-present when a process is rematerialized (fork/exec or a
       namespace snapshot). */
    if ((prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
        flags |= VMM_PAGE_PRESENT;
    }

    if ((prot & PROT_WRITE) != 0) {
        flags |= VMM_PAGE_WRITABLE;
    }
    if ((prot & PROT_EXEC) == 0) {
        flags |= VMM_PAGE_NX;
    }
    return flags;
}

static int region_can_use_ora_direct(const process_memory_region_t *region) {
    return region != NULL &&
        (region->kind_flags & PROCESS_MEMORY_FLAG_ORA_DIRECT) != 0u &&
        (region->prot & PROT_WRITE) == 0 &&
        (region->addr & (PROCESS_MEMORY_PAGE_SIZE - 1ull)) == 0u &&
        region->content.object != NULL;
}

static int load_region_meta_in_namespace(uint32_t pid,uint64_t namespace_id,uint64_t addr,process_memory_region_t *out) {
    pmem_namespace_t *space=find_inventory_namespace(pid,namespace_id);
    pmem_region_node_t *node=find_inventory_region(space,addr);
    if(!out || !node || !node->present)return -1;
    *out=node->region;return 0;
}

static size_t process_memory_maps_append(char *buffer, size_t capacity,
                                         size_t offset, const char *format, ...) {
    va_list ap;
    int written;

    if (buffer == NULL || format == NULL || offset >= capacity) {
        return offset;
    }
    va_start(ap, format);
    written = vsnprintf(buffer + offset, capacity - offset, format, ap);
    va_end(ap);
    if (written < 0) {
        return offset;
    }
    if ((size_t)written >= capacity - offset) {
        /* A truncated maps view is worse than an explicit failure: callers
         * must never mistake a prefix for the process' complete inventory. */
        return capacity;
    }
    return offset + (size_t)written;
}

size_t process_memory_stack_size(uint32_t pid) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root;
    size_t result = 0u;

    if (pid == 0u) {
        return 0u;
    }
    root = copy_active_root(pid);
    if (root == NULL) {
        return 0u;
    }
    for (uint32_t i = 0u; i < root->region_count; ++i) {
        process_memory_region_t region;
        const uint64_t addr = root_addresses_const(root)[i];
        if (load_region_meta_in_namespace(pid, root->namespace_id, addr, &region) != 0) {
            result = 0u;
            break;
        }
        if ((region.kind_flags & PROCESS_MEMORY_FLAG_STACK) != 0u) {
            result = region.length;
            break;
        }
    }
    kfree(root);
    return result;
}

int process_memory_format_maps(uint32_t pid, char *buffer, size_t capacity,
                               size_t *length_out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root;
    size_t used = 0u;

    if (pid == 0u || buffer == NULL || capacity == 0u || length_out == NULL) {
        return -1;
    }
    *length_out = 0u;
    buffer[0] = '\0';
    root = copy_active_root(pid);
    if (root == NULL) {
        return -1;
    }

    /* Region addresses are stable inventory keys but are not required to be
     * inserted in VA order.  Emit the same real regions without inventing a
     * pathname; anonymous VexOS mappings have no file backing to name. */
    for (uint32_t i = 0u; i < root->region_count; ++i) {
        process_memory_region_t region;
        uint64_t end;
        char perms[5];
        const uint64_t addr = root_addresses_const(root)[i];
        if (load_region_meta_in_namespace(pid, root->namespace_id, addr, &region) != 0 ||
            region.length == 0u || region.addr > UINT64_MAX - (uint64_t)region.length) {
            /* The root and its native region metadata are one inventory.  Omitting a
             * missing entry would fabricate a plausible but incomplete maps
             * view, so expose the inconsistency as an explicit failure. */
            kfree(root);
            *length_out = 0u;
            return -1;
        }
        end = align_up_u64(region.addr + (uint64_t)region.length,
                           PROCESS_MEMORY_PAGE_SIZE);
        if (end == 0u || end <= region.addr) {
            continue;
        }
        perms[0] = (region.prot & PROT_READ) != 0 ? 'r' : '-';
        perms[1] = (region.prot & PROT_WRITE) != 0 ? 'w' : '-';
        perms[2] = (region.prot & PROT_EXEC) != 0 ? 'x' : '-';
        perms[3] = (region.kind_flags & PROCESS_MEMORY_FLAG_SHARED) != 0u ? 's' : 'p';
        perms[4] = '\0';
        used = process_memory_maps_append(buffer, capacity, used,
            "%08x%08x-%08x%08x %s %08x 00:00 0\n",
            (unsigned int)(region.addr >> 32u), (unsigned int)region.addr,
            (unsigned int)(end >> 32u), (unsigned int)end, perms,
            (unsigned int)region.shared_offset);
        if (used >= capacity) {
            kfree(root);
            *length_out = 0u;
            return -2; /* caller may retry with a larger buffer */
        }
    }
    *length_out = used;
    kfree(root);
    return 0;
}

static void update_root_alloc_hint_for_region(process_memory_namespace_root_t *root, const process_memory_region_t *region) {
    uint64_t end;

    if (root == NULL || region == NULL) {
        return;
    }
    if ((region->kind_flags & PROCESS_MEMORY_FLAG_STACK) != 0u) {
        return;
    }

    if ((uint64_t)region->length > UINT64_MAX - region->addr) {
        return;
    }
    end = align_up_u64(region->addr + (uint64_t)region->length, PROCESS_MEMORY_PAGE_SIZE);
    if (end > root->alloc_hint) {
        root->alloc_hint = end;
    }
}

static int materialize_region_to_cr3(uint64_t cr3, const process_memory_region_t *region, const void *data) {
    void *pml4;
    const uint8_t *bytes = data;
    const uint64_t start = align_down_u64(region->addr, PROCESS_MEMORY_PAGE_SIZE);
    const uint64_t region_end = region->addr > UINT64_MAX - (uint64_t)region->length
        ? 0u : region->addr + (uint64_t)region->length;
    const uint64_t end = region_end == 0u ? 0u : align_up_u64(region_end, PROCESS_MEMORY_PAGE_SIZE);
    const uint64_t flags = (uint64_t)region_flags_from_prot(region->prot)|
        ((region->shared_id || region_can_use_ora_direct(region))?VMM_PAGE_SHARED_ACCOUNT:0);

    if (region == NULL || end == 0u) {
        return -1;
    }

    pml4 = vmm_cr3_to_address_space(cr3);
    if (pml4 == NULL) {
        return -1;
    }

    if ((region->kind_flags & PROCESS_MEMORY_FLAG_RESERVATION) != 0u) {
        /* A reservation is represented in the inventory only.  In particular,
         * do not install a non-present PTE that would retain a physical page
         * and later be mistaken for an owned leaf by VMM teardown. */
        return 0;
    }

    if ((region->kind_flags & PROCESS_MEMORY_FLAG_VMM_DIRECT) != 0u &&
        region->shared_id == 0u) {
        /* The caller already materialized the VMM-owned pages. */
        return 0;
    }

    if (region->shared_id != 0u) {
        process_memory_shared_backing_t *shared = find_shared_backing(region->shared_id);
        const uint64_t offset_pages = region->shared_offset / PROCESS_MEMORY_PAGE_SIZE;
        if (shared == NULL || offset_pages + (end - start) / PROCESS_MEMORY_PAGE_SIZE > shared->page_count) {
            return -1;
        }
        for (uint64_t page = start; page < end; page += PROCESS_MEMORY_PAGE_SIZE) {
            uint64_t phys;
            if (shared_backing_ensure_page(shared,
                    (uint32_t)(offset_pages + (page - start) / PROCESS_MEMORY_PAGE_SIZE),
                    &phys) != 0) {
                return -1;
            }
            vmm_map_page(pml4, page, phys, flags);
        }
        return 0;
    }

    if (region_can_use_ora_direct(region)) {
        if (data == NULL) {
            return -1;
        }
        for (uint64_t page = start; page < end; page += PROCESS_MEMORY_PAGE_SIZE) {
            uint64_t phys;
            const size_t offset = (size_t)(page - region->addr);

            if (ora_view_physical_page(&region->content, offset, &phys) != 0) {
                debug_log_puts("pmem: direct backing page failed\n");
                return -1;
            }

            vmm_map_page(pml4, page, phys, flags);
            if ((vmm_get_physical(pml4, page) & ~(PROCESS_MEMORY_PAGE_SIZE - 1ull)) != phys) {
                debug_log_puts("pmem: direct map failed\n");
                return -1;
            }
        }
        return 0;
    }

    if (data == NULL) {
        return -1;
    }

    for (uint64_t page = start; page < end; page += PROCESS_MEMORY_PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        uint8_t *dst;
        uint64_t copy_start;
        uint64_t copy_end;

        if (phys == NULL) {
            debug_log_puts("pmem: pmm_alloc_page failed\n");
            return -1;
        }

        dst = (uint8_t *)(hhdm_request.response->offset + (uint64_t)phys);
        memset(dst, 0, PROCESS_MEMORY_PAGE_SIZE);

        copy_start = page > region->addr ? page : region->addr;
        copy_end = (page + PROCESS_MEMORY_PAGE_SIZE) < region_end
            ? (page + PROCESS_MEMORY_PAGE_SIZE)
            : region_end;
        if (copy_end > copy_start) {
            memcpy(dst + (copy_start - page), bytes + (copy_start - region->addr), (size_t)(copy_end - copy_start));
        }

        vmm_map_page(pml4, page, (uint64_t)phys, flags);
    }

    return 0;
}

int process_memory_materialize_page(uint32_t pid, uint64_t cr3, uint64_t address,
                                    uint64_t page_fault_error) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    void *pml4;
    uint64_t page = align_down_u64(address, PROCESS_MEMORY_PAGE_SIZE);
    uint64_t phys = 0u;
    process_memory_region_t region;
    const process_memory_region_t *found;

    /* We only resolve user, non-present faults.  Present faults are genuine
     * protection violations and must follow the normal process-kill path. */
    if ((page_fault_error & 0x1u) != 0u || (page_fault_error & 0x4u) == 0u ||
        pid == 0u || cr3 == 0u) {
        return -1;
    }
    found = active_region_containing(pid, page, PROCESS_MEMORY_FLAG_LAZY);
    if (found != NULL) {
        region = *found;
    }
    if (found == NULL || (region.prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) == 0 ||
        ((page_fault_error & 0x2u) != 0u && (region.prot & PROT_WRITE) == 0) ||
        ((page_fault_error & 0x10u) != 0u && (region.prot & PROT_EXEC) == 0)) {
        return -1;
    }

    pml4 = vmm_cr3_to_address_space(cr3);
    if (pml4 == NULL || hhdm_request.response == NULL) {
        return -1;
    }
    /* mprotect(PROT_NONE) leaves the owned leaf in place but clears PRESENT.
     * Restore its flags on a later permitted access instead of allocating a
     * second page or treating the stale non-present PTE as a fresh fault. */
    if (vmm_get_physical(pml4, page) == 0u &&
        vmm_get_physical_any(pml4, page) != 0u) {
        const uint64_t existing = vmm_get_physical_any(pml4, page) &
            ~(PROCESS_MEMORY_PAGE_SIZE - 1ull);
        if (vmm_set_page_flags(pml4, page, (uint64_t)region_flags_from_prot(region.prot)) != 0) {
            return -1;
        }
        return (vmm_get_physical(pml4, page) & ~(PROCESS_MEMORY_PAGE_SIZE - 1ull)) == existing
            ? 0 : -1;
    }
    if (vmm_get_physical(pml4, page) != 0u) {
        /* Another CPU can materialize this shared-CR3 leaf after hardware
           records #PF but before this transaction starts. The exception's
           not-present bit describes the past, not the current page table. */
        uint64_t flags=vmm_get_page_flags(pml4,page);
        if((flags&(VMM_PAGE_PRESENT|VMM_PAGE_USER))!=(VMM_PAGE_PRESENT|VMM_PAGE_USER) ||
           ((page_fault_error&2u) && !(flags&VMM_PAGE_WRITABLE)) ||
           ((page_fault_error&16u) && (flags&VMM_PAGE_NX)))return -1;
        __asm__ volatile("invlpg (%0)"::"r"(page):"memory");
        return 0;
    }
    const uint64_t fault_region_page =
        (page - align_down_u64(region.addr, PROCESS_MEMORY_PAGE_SIZE)) /
        PROCESS_MEMORY_PAGE_SIZE;
    if (region.shared_id != 0u) {
        process_memory_shared_backing_t *shared = find_shared_backing(region.shared_id);
        const uint64_t backing_page = region.shared_offset / PROCESS_MEMORY_PAGE_SIZE +
            fault_region_page;
        if (shared == NULL || backing_page > UINT32_MAX ||
            shared_backing_ensure_page(shared, (uint32_t)backing_page, &phys) != 0) {
            return -1;
        }
    } else if (region.file_backing_id != 0u) {
        process_memory_file_backing_t *file = find_file_backing(region.file_backing_id);
        const uint64_t file_page = region.file_offset / PROCESS_MEMORY_PAGE_SIZE +
            fault_region_page;
        if (file == NULL || file_page > UINT32_MAX) {
            return -1;
        }
        if (file->shared != 0u) {
            if (file_backing_ensure_shared_page(file, (uint32_t)file_page,
                                                &phys) != 0) {
                return -1;
            }
        } else {
            void *new_page = pmm_alloc_page();
            if (new_page == NULL || hhdm_request.response == NULL) {
                if (new_page != NULL) {
                    pmm_free_page(new_page);
                }
                return -1;
            }
            if (file_backing_read_page(file, (uint32_t)file_page,
                    (void *)(hhdm_request.response->offset +
                             (uint64_t)new_page)) != 0) {
                pmm_free_page(new_page);
                return -1;
            }
            phys = (uint64_t)new_page;
        }
    } else {
        void *new_page = pmm_alloc_page();
        if (new_page == NULL) {
            return -1;
        }
        phys = (uint64_t)new_page;
        memset((uint8_t *)(hhdm_request.response->offset + phys), 0,
               PROCESS_MEMORY_PAGE_SIZE);
    }
    vmm_map_page(pml4, page, phys, (uint64_t)region_flags_from_prot(region.prot)|
        ((region.shared_id || (region.kind_flags&PROCESS_MEMORY_FLAG_SHARED))?VMM_PAGE_SHARED_ACCOUNT:0));
    if ((vmm_get_physical(pml4, page) & ~(PROCESS_MEMORY_PAGE_SIZE - 1ull)) != phys) {
        int page_owned = region.shared_id == 0u;
        if (region.file_backing_id != 0u) {
            process_memory_file_backing_t *file = find_file_backing(region.file_backing_id);
            if (file != NULL && file->shared != 0u) {
                page_owned = 0;
            }
        }
        if (page_owned) {
            pmm_free_page((void *)(uintptr_t)phys);
        }
        return -1;
    }
    return 0;
}

int process_memory_file_fault_is_bus(process_t *process, uint64_t address) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    const process_memory_region_t *region;
    process_memory_file_backing_t *backing;
    uint64_t page_offset;

    if (process == NULL || process->group == NULL || address == 0u) {
        return 0;
    }
    region = active_region_containing(process_memory_owner(process), address, 0u);
    if (region == NULL || region->file_backing_id == 0u) {
        return 0;
    }
    backing = find_file_backing(region->file_backing_id);
    if (backing == NULL) {
        return 0;
    }
    page_offset = align_down_u64(address, PROCESS_MEMORY_PAGE_SIZE) -
        align_down_u64(region->addr, PROCESS_MEMORY_PAGE_SIZE);
    if (region->file_offset > UINT64_MAX - page_offset) {
        return 1;
    }
    return region->file_offset + page_offset >= backing->file_length;
}





int process_memory_namespace_begin(uint32_t pid, uint64_t alloc_hint, uint64_t *namespace_id_out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root;
    uint64_t namespace_id;

    if (pid == 0u || namespace_id_out == NULL) {
        return -1;
    }

    namespace_id = allocate_namespace_id();
    if(!namespace_id)return -1;
    root = alloc_root_copy(namespace_id, alloc_hint, 0u);
    if (root == NULL) {
        return -1;
    }

    if (store_root_copy(pid, namespace_id, root) != 0) {
        kfree(root);
        return -1;
    }

    *namespace_id_out = namespace_id;
    kfree(root);
    return 0;
}

uint64_t process_memory_current_namespace(uint32_t pid) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();
    pmem_namespace_t *space=find_inventory_namespace(pid,PMEM_ACTIVE_NAMESPACE);
    return space?space->identity:0;
}

static int register_inventory_region(uint32_t,uint64_t,const process_memory_region_t *);
int process_memory_snapshot_namespace(uint32_t pid,uint64_t source_namespace_id,uint64_t *snapshot_namespace_id_out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    if(!source_namespace_id)return -1;
    pmem_namespace_t *source=find_inventory_namespace(pid,source_namespace_id);
    uint64_t identity;
    if(!source || !snapshot_namespace_id_out || process_memory_namespace_begin(pid,source->root->alloc_hint,&identity)!=0)return -1;
    for(uint32_t i=0;i<source->root->region_count;++i) {
        process_memory_region_t region;
        if(load_region_meta_in_namespace(pid,source_namespace_id,root_addresses(source->root)[i],&region)!=0)goto fail;
        if(region.shared_id && process_memory_shared_retain(region.shared_id)!=0)goto fail;
        if(region.file_backing_id && file_backing_retain(region.file_backing_id)!=0){if(region.shared_id)shared_backing_release(region.shared_id);goto fail;}
        if(register_inventory_region(pid,identity,&region)!=0) {
            if(region.shared_id)shared_backing_release(region.shared_id);
            if(region.file_backing_id)file_backing_release(region.file_backing_id);
            goto fail;
        }
    }
    *snapshot_namespace_id_out=identity;return 0;
fail:
    process_memory_namespace_destroy(pid,identity,0);return -1;
}

int process_memory_namespace_commit(uint32_t pid, uint64_t namespace_id) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();
    if(!namespace_id)return -1;
    pmem_namespace_t *space=find_inventory_namespace(pid,namespace_id);
    if(!space || !space->root)return -1;
    return store_root_copy(pid,PMEM_ACTIVE_NAMESPACE,space->root);
}

int process_memory_namespace_destroy(uint32_t pid,uint64_t namespace_id,uint64_t cr3) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    if(!namespace_id)return 0;
    pmem_namespace_t *space=find_inventory_namespace(pid,namespace_id);
    void *pml4=cr3?vmm_cr3_to_address_space(cr3):NULL;
    if(!space)return 0;
#if VEX_DESKTOP_TRACE
    uint64_t unmaps=0, releases=0, mark, longest=0, longest_length=0;
    uint64_t flushes=smp_current_cpu()->tlb_flushes;
    uint32_t regions=space->root->region_count;
#endif
    for(uint32_t i=0;i<space->root->region_count;++i) {
        process_memory_region_t region;
        if(load_region_meta_in_namespace(pid,namespace_id,root_addresses(space->root)[i],&region)!=0)continue;
        process_memory_file_backing_t *file=find_file_backing(region.file_backing_id);
        int owns=region.shared_id==0 && !(region.kind_flags & (PROCESS_MEMORY_FLAG_ORA_DIRECT|PROCESS_MEMORY_FLAG_RESERVATION|PROCESS_MEMORY_FLAG_JIT)) && (!file || !file->shared);
#if VEX_DESKTOP_TRACE
        mark=sched_clock_ns();
#endif
        if(pml4 && !(region.kind_flags & PROCESS_MEMORY_FLAG_RESERVATION))vmm_unmap_range(pml4,region.addr,region.length,owns);
#if VEX_DESKTOP_TRACE
        uint64_t elapsed=sched_clock_ns()-mark;unmaps+=elapsed;
        if(elapsed>longest){longest=elapsed;longest_length=region.length;}
        mark=sched_clock_ns();
#endif
        if(region.shared_id)shared_backing_release(region.shared_id);
        if(region.file_backing_id)file_backing_release(region.file_backing_id);
#if VEX_DESKTOP_TRACE
        releases+=sched_clock_ns()-mark;
#endif
    }
#if VEX_DESKTOP_TRACE
    char line[256];snprintf(line,sizeof(line),"namespace-trace: pid=%u regions=%u unmap-us=%u backing-us=%u max-us=%u max-kib=%u flushes=%u\n",pid,regions,(unsigned)(unmaps/1000),(unsigned)(releases/1000),(unsigned)(longest/1000),(unsigned)(longest_length/1024),(unsigned)(smp_current_cpu()->tlb_flushes-flushes));debug_log_puts(line);
#endif
    remove_inventory_namespace(pid,namespace_id);return 0;
}

static int register_inventory_region(uint32_t pid,uint64_t identity,const process_memory_region_t *region) {
    if(!pid || !identity || !region->addr || !region->length || region->addr>UINT64_MAX-region->length ||
       !align_up_u64(region->addr+region->length,4096))return -1;
    pmem_namespace_t *space=find_inventory_namespace(pid,identity);
    if(!space || !space->root || space->root->region_count==UINT32_MAX)return -1;
    const int indexed=region_index_ready(space);
    if(indexed) {
        const uint32_t before=region_index_upper(space,region->addr+region->length-1u);
        if(before) {
            const process_memory_region_t *last=&space->index[before-1u]->region;
            if(last->addr+(uint64_t)last->length>region->addr)return -1;
        }
    } else for(uint32_t i=0;i<space->root->region_count;++i) {
        pmem_region_node_t *node=find_inventory_region(space,root_addresses(space->root)[i]);
        if(!node || !node->present || (region->addr<node->region.addr+node->region.length &&
           node->region.addr<region->addr+region->length))return -1;
    }
    /* Grow the address list first (the only step that can fail besides the node),
       then publish without allocating: no partially registered region on OOM. */
    process_memory_namespace_root_t *bigger=NULL;
    if(root_capacity(space->root)<=space->root->region_count) {
        bigger=grow_root(space->root,space->root->region_count+1u);
        if(!bigger)return -1;
    }
    if(!store_region_meta(pid,identity,region->addr,region)){kfree(bigger);return -1;}
    if(bigger){kfree(space->root);space->root=bigger;}
    root_addresses(space->root)[space->root->region_count++]=region->addr;
    update_root_alloc_hint_for_region(space->root,region);
    region_index_note_added(space,(uint8_t)indexed,find_inventory_region(space,region->addr));
    return 0;
}
int process_memory_register_region_in_namespace(uint32_t pid, uint64_t namespace_id, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    return process_memory_register_region_in_namespace_partial(pid, namespace_id, addr, length, prot, flags, kind_flags, data, length);
}

int process_memory_register_region_in_namespace_partial(uint32_t pid,uint64_t namespace_id,uint64_t addr,size_t length,int prot,int flags,uint32_t kind_flags,const void *data,size_t data_size) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    ora_ref_t content=NULL;
    if(!(kind_flags & (PROCESS_MEMORY_FLAG_RESERVATION|PROCESS_MEMORY_FLAG_VMM_DIRECT))) {
        if(!data || data_size>length)return -1;
        content=ora_create_zero_extended(data,data_size,length,ORA_CANONICAL);
        if(!content)return -1;
    }
    process_memory_region_t region={0};
    region.magic=PROCESS_MEMORY_REGION_MAGIC;region.addr=addr;region.length=length;
    region.prot=prot;region.flags=flags;region.kind_flags=kind_flags & ~PROCESS_MEMORY_FLAG_ORA_DIRECT;
    if(flags & MAP_SHARED)region.kind_flags|=PROCESS_MEMORY_FLAG_SHARED;
    region.allocation_size=length;region.allocation_base=addr;
    region.content=(ora_view_t){content,0,content?length:0};
    if(content && !(flags & MAP_SHARED) && !(prot & PROT_WRITE) && !(kind_flags & PROCESS_MEMORY_FLAG_NO_DIRECT) && !(addr & 4095) && length>=4096)
        region.kind_flags|=PROCESS_MEMORY_FLAG_ORA_DIRECT;
    int result=register_inventory_region(pid,namespace_id,&region);
    ora_release(content);return result;
}

int process_memory_materialize_namespace(uint32_t pid, uint64_t namespace_id, uint64_t cr3) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root = copy_namespace_root(pid, namespace_id);

    if (root == NULL) {
        debug_log_puts("pmem: root lookup failed\n");
        return -1;
    }

    for (uint32_t index = 0u; index < root->region_count; ++index) {
        process_memory_region_t region;
        void *backing;
        

        if (load_region_meta_in_namespace(pid, namespace_id, root_addresses(root)[index], &region) != 0) {
            debug_log_puts("pmem: region meta load failed\n");
            kfree(root);
            return -1;
        }

        if ((region.kind_flags & (PROCESS_MEMORY_FLAG_RESERVATION | PROCESS_MEMORY_FLAG_LAZY)) != 0u ||
            ((region.kind_flags & PROCESS_MEMORY_FLAG_VMM_DIRECT) != 0u && region.shared_id == 0u)) {
            continue;
        }

        if (region.shared_id != 0u) {
            backing = NULL;
        } else {
            backing = (void *)ora_view_data(&region.content);
            if(!backing) {kfree(root);return -1;}
        }

        if (materialize_region_to_cr3(cr3, &region, backing) != 0) {
            debug_log_puts("pmem: materialize region failed\n");
            kfree(root);
            return -1;
        }
    }

    kfree(root);
    return 0;
}

int process_memory_register_region(uint32_t pid, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    uint64_t namespace_id;
    int result;

    if (ensure_active_root(pid) != 0) {
        return -1;
    }

    if (active_root_header(pid, &namespace_id, NULL) != 0) {
        return -1;
    }

    result = process_memory_register_region_in_namespace(pid, namespace_id, addr, length, prot, flags, kind_flags, data);
    if (result != 0) {
        return -1;
    }
    return process_memory_namespace_commit(pid, namespace_id);
}

int process_memory_register_mapping(uint32_t pid, uint64_t addr, size_t length,
                                    int prot, int flags, uint32_t kind_flags) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    uint64_t namespace_id;
    int result;

    if (ensure_active_root(pid) != 0) {
        return -1;
    }
    if (active_root_header(pid, &namespace_id, NULL) != 0) {
        return -1;
    }

    result = process_memory_register_region_in_namespace(pid, namespace_id, addr, length,
                                                         prot, flags, kind_flags, NULL);
    if (result != 0) {
        return -1;
    }
    return process_memory_namespace_commit(pid, namespace_id);
}

int process_memory_file_map(uint32_t pid, uint64_t cr3, uint64_t addr,
                            size_t length, int prot, int flags, int fd,
                            uint64_t file_offset, size_t file_length) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root;
    process_memory_region_t region;
    uint64_t file_id = 0u;

    if (pid == 0u || cr3 == 0u || addr == 0u || length == 0u ||
        (addr & (PROCESS_MEMORY_PAGE_SIZE - 1ull)) != 0u ||
        (uint64_t)length > UINT64_MAX - addr ||
        file_length > length ||
        file_backing_create(fd, file_offset, length, file_length,
                            (flags & MAP_SHARED) != 0, &file_id) != 0) {
        return -1;
    }

    if (process_memory_register_mapping(pid, addr, length, prot, flags,
            PROCESS_MEMORY_FLAG_VMM_DIRECT | PROCESS_MEMORY_FLAG_LAZY |
            PROCESS_MEMORY_FLAG_FILE_BACKED) != 0) {

        file_backing_release(file_id);
        return -1;
    }
    root = copy_active_root(pid);
    if (root == NULL || load_region_meta_in_namespace(pid, root->namespace_id,
                                                       addr, &region) != 0) {
        if (root != NULL) {
            kfree(root);
        }
        (void)process_memory_remove_region(pid, cr3, addr, length);
        file_backing_release(file_id);
        return -1;
    }
    region.file_backing_id = file_id;
    region.file_offset = 0u;
    region.content = (ora_view_t){0};
    region.shared_id = 0u;
    region.shared_offset = 0u;
    region.kind_flags |= PROCESS_MEMORY_FLAG_VMM_DIRECT |
        PROCESS_MEMORY_FLAG_LAZY | PROCESS_MEMORY_FLAG_FILE_BACKED;
    if (store_region_meta(pid, root->namespace_id, addr, &region) == NULL) {
        kfree(root);
        (void)process_memory_remove_region(pid, cr3, addr, length);
        file_backing_release(file_id);
        return -1;
    }
    kfree(root);
    return 0;
}

int process_memory_adopt_shared_mapping(uint32_t pid, uint64_t cr3, uint64_t addr) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root;
    process_memory_region_t region;
    void *pml4;
    uint64_t shared_id;
    const uint64_t key_addr = addr;

    if (ensure_active_root(pid) != 0) {
        return -1;
    }
    root = copy_active_root(pid);
    if (root == NULL) {
        return -1;
    }
    if (load_region_meta_in_namespace(pid, root->namespace_id, key_addr, &region) != 0) {
        kfree(root);
        return -1;
    }
    if ((region.kind_flags & PROCESS_MEMORY_FLAG_SHARED) == 0u) {
        kfree(root);
        return 0;
    }
    if (region.shared_id != 0u) {
        kfree(root);
        return 0;
    }
    /* Never adopt immutable ORA physical pages into a mutable page owner.
       Keep ownership with ORA until an independent projection is prepared. */
    if(region.kind_flags & PROCESS_MEMORY_FLAG_ORA_DIRECT) {
        if(process_memory_snapshot_region(pid,cr3,addr) ||
           load_region_meta_in_namespace(pid,root->namespace_id,addr,&region)) {
            kfree(root);return -1;
        }
    }
    pml4 = vmm_cr3_to_address_space(cr3);
    if (pml4 == NULL || shared_backing_create_from_mapping(pml4, region.addr,
                                                            region.length, &shared_id) != 0) {
        kfree(root);
        return -1;
    }
    region.shared_id = shared_id;
    region.shared_offset = 0u;
    region.kind_flags |= PROCESS_MEMORY_FLAG_VMM_DIRECT;
    region.content=(ora_view_t){0};
    region.kind_flags &= ~PROCESS_MEMORY_FLAG_ORA_DIRECT;
    if(!store_region_meta(pid,root->namespace_id,key_addr,&region)) {
        shared_backing_release(shared_id);kfree(root);return -1;
    }
    kfree(root);
    return 0;
}

int process_memory_shared_create(size_t length, uint64_t *id_out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    uint64_t id;
    process_memory_shared_backing_t *backing;

    if (shared_backing_create_empty(length, &id) != 0) {
        return -1;
    }
    backing = find_shared_backing(id);
    if (backing == NULL) {
        return -1;
    }
    /* Native SHM is an explicit object: allocate its pages once at create
     * time, while ordinary anonymous mmap remains lazy. */
    for (uint32_t index = 0u; index < backing->page_count; ++index) {
        uint64_t ignored;
        if (shared_backing_ensure_page(backing, index, &ignored) != 0) {
            shared_backing_release(id);
            return -1;
        }
    }
    *id_out = id;
    return 0;
}

int process_memory_shared_create_contiguous(size_t length, uint64_t *id_out,
                                            void **kernel_mapping_out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_shared_backing_t *backing;
    uint64_t id;
    uint64_t bytes;
    void *physical;

    if (id_out == NULL || kernel_mapping_out == NULL || length == 0u ||
        shared_backing_create_empty(length, &id) != 0) {
        return -1;
    }
    backing = find_shared_backing(id);
    if (backing == NULL || hhdm_request.response == NULL) {
        shared_backing_release(id);
        return -1;
    }
    bytes = (uint64_t)backing->page_count * PROCESS_MEMORY_PAGE_SIZE;
    physical = pmm_alloc_contiguous_pages(backing->page_count);
    if (physical == NULL) {
        shared_backing_release(id);
        return -1;
    }
    backing->contiguous = 1u;
    for (uint32_t index = 0u; index < backing->page_count; ++index) {
        backing->phys_pages[index] = (uint64_t)(uintptr_t)physical +
            (uint64_t)index * PROCESS_MEMORY_PAGE_SIZE;
    }
    memset((void *)(uintptr_t)(hhdm_request.response->offset +
            (uint64_t)(uintptr_t)physical), 0, (size_t)bytes);
    *id_out = id;
    *kernel_mapping_out = (void *)(uintptr_t)(hhdm_request.response->offset +
                                               (uint64_t)(uintptr_t)physical);
    return 0;
}

int process_memory_shared_get_contiguous(uint64_t id, size_t offset,
                                         size_t length, void **mapping_out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_shared_backing_t *backing = find_shared_backing(id);
    uint64_t bytes;

    if (backing == NULL || backing->sparse != 0u ||
        backing->contiguous == 0u || backing->phys_pages == NULL ||
        backing->page_count == 0u || hhdm_request.response == NULL ||
        mapping_out == NULL) {
        return -1;
    }
    bytes = (uint64_t)backing->page_count * PROCESS_MEMORY_PAGE_SIZE;
    if ((uint64_t)offset > bytes || (uint64_t)length > bytes - offset ||
        backing->phys_pages[0] > UINT64_MAX - offset ||
        hhdm_request.response->offset > UINT64_MAX -
            (backing->phys_pages[0] + (uint64_t)offset)) {
        return -1;
    }
    *mapping_out = (void *)(uintptr_t)(hhdm_request.response->offset +
        backing->phys_pages[0] + (uint64_t)offset);
    return 0;
}

int process_memory_shared_create_lazy(size_t length, uint64_t *id_out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    return shared_backing_create_lazy(length, id_out);
}

int process_memory_shared_retain(uint64_t id) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    return shared_backing_acquire(id);
}

void process_memory_shared_release(uint64_t id) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    shared_backing_release(id);
}

size_t process_memory_shared_size(uint64_t id) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_shared_backing_t *backing = find_shared_backing(id);
    if (backing == NULL) {
        return 0u;
    }
    return (size_t)backing->page_count * PROCESS_MEMORY_PAGE_SIZE;
}

static int process_memory_shared_map_internal(uint32_t pid, uint64_t cr3, uint64_t id,
                                              uint64_t addr, size_t length, size_t offset,
                                              int prot, int lazy,
                                              int surface_mapping) {
    process_memory_shared_backing_t *backing = find_shared_backing(id);
    process_memory_namespace_root_t *root;
    process_memory_region_t region;
    uint64_t end_offset;
    int backing_retained = 0;
    const int defer_pages = lazy != 0 ||
        (prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) == 0;

    if (backing == NULL || pid == 0u || cr3 == 0u || addr == 0u || length == 0u ||
        (addr & (PROCESS_MEMORY_PAGE_SIZE - 1ull)) != 0u ||
        (offset & (PROCESS_MEMORY_PAGE_SIZE - 1ull)) != 0u ||
        (uint64_t)length > UINT64_MAX - offset) {
        return -1;
    }
    end_offset = (uint64_t)offset + (uint64_t)length;
    if (end_offset > (uint64_t)backing->page_count * PROCESS_MEMORY_PAGE_SIZE) {
        return -1;
    }
    if (process_memory_shared_retain(id) != 0) {

        return -1;
    }
    backing_retained = 1;
    if (process_memory_register_mapping(pid, addr, length, prot, MAP_SHARED,
                                        PROCESS_MEMORY_FLAG_VMM_DIRECT |
                                        PROCESS_MEMORY_FLAG_SHARED |
                                        (defer_pages != 0 ? PROCESS_MEMORY_FLAG_LAZY : 0u)) != 0) {

        if (backing_retained != 0) {
            process_memory_shared_release(id);
        }
        return -1;
    }
    root = copy_active_root(pid);
    if (root == NULL || load_region_meta_in_namespace(pid, root->namespace_id, addr,
                                                       &region) != 0) {
        if (root != NULL) kfree(root);
        (void)process_memory_remove_region(pid, cr3, addr, length);
        if (backing_retained != 0) {
            process_memory_shared_release(id);
        }
        return -1;
    }
    region.shared_id = id;
    region.shared_offset = offset;
    region.content = (ora_view_t){0};
    region.kind_flags |= PROCESS_MEMORY_FLAG_SHARED | PROCESS_MEMORY_FLAG_VMM_DIRECT;
    if (surface_mapping != 0) {
        region.kind_flags |= PROCESS_MEMORY_FLAG_SURFACE;
        region.reserved0 = (uint32_t)prot &
            (uint32_t)(PROT_READ | PROT_WRITE | PROT_EXEC);
    }
    if (store_region_meta(pid, root->namespace_id, addr, &region) == NULL) {
        kfree(root);
        (void)process_memory_remove_region(pid, cr3, addr, length);
        if (backing_retained != 0) {
            process_memory_shared_release(id);
        }
        return -1;
    }
    kfree(root);
    /* The retained reference is now represented by region.shared_id. */
    backing_retained = 0;
    if (defer_pages == 0 && process_memory_materialize_region(pid, cr3, addr) != 0) {

        (void)process_memory_remove_region(pid, cr3, addr, length);
        return -1;
    }
    return 0;
}

int process_memory_shared_map(uint32_t pid, uint64_t cr3, uint64_t id,
                             uint64_t addr, size_t length, size_t offset, int prot) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    return process_memory_shared_map_internal(pid, cr3, id, addr, length, offset,
                                              prot, 0, 0);
}

int process_memory_shared_map_lazy(uint32_t pid, uint64_t cr3, uint64_t id,
                                   uint64_t addr, size_t length, size_t offset, int prot) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    return process_memory_shared_map_internal(pid, cr3, id, addr, length, offset,
                                              prot, 1, 0);
}

int process_memory_shared_map_surface(uint32_t pid, uint64_t cr3, uint64_t id,
                                      uint64_t addr, size_t length,
                                      size_t offset, int prot) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    if ((prot & PROT_EXEC) != 0 || (prot & (PROT_READ | PROT_WRITE)) == 0) {
        return -1;
    }
    return process_memory_shared_map_internal(pid, cr3, id, addr, length, offset,
                                              prot, 0, 1);
}

int process_memory_mark_jit_region(uint32_t pid, uint64_t addr) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root;
    process_memory_region_t region;

    if (ensure_active_root(pid) != 0) {
        return -1;
    }
    root = copy_active_root(pid);
    if (root == NULL || load_region_meta_in_namespace(pid, root->namespace_id,
                                                       addr, &region) != 0) {
        if (root != NULL) {
            kfree(root);
        }
        return -1;
    }
    region.kind_flags |= PROCESS_MEMORY_FLAG_JIT;
    if (store_region_meta(pid, root->namespace_id, addr, &region) == NULL) {
        kfree(root);
        return -1;
    }
    kfree(root);
    return 0;
}

size_t process_memory_shared_committed_bytes(uint64_t id) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_shared_backing_t *backing = find_shared_backing(id);
    size_t count = 0;
    if (!backing) return 0;
    if (backing->sparse)
        return (size_t)backing->sparse_committed_pages * PROCESS_MEMORY_PAGE_SIZE;
    for (uint32_t i = 0; i < backing->page_count; ++i)
        if (backing->phys_pages && backing->phys_pages[i]) ++count;
    return count * PROCESS_MEMORY_PAGE_SIZE;
}

int process_memory_shared_page_present(uint64_t id, uint32_t index) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_shared_backing_t *backing = find_shared_backing(id);
    if (!backing || index >= backing->page_count) return 0;
    if (!backing->sparse) return backing->phys_pages && backing->phys_pages[index];
    for (process_memory_shared_page_t *page = backing->sparse_pages; page; page = page->next)
        if (page->index == index) return page->phys != 0;
    return 0;
}

int process_memory_jit_discard(uint32_t pid, uint64_t cr3, uint64_t id,
                               size_t offset, size_t length) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_shared_backing_t *backing = find_shared_backing(id);
    process_memory_namespace_root_t *root;
    process_memory_region_t *aliases;
    void *pml4 = vmm_cr3_to_address_space(cr3);
    size_t refs = 0;
    if (!backing || !backing->sparse || !pml4 || !length ||
        offset % PROCESS_MEMORY_PAGE_SIZE || length % PROCESS_MEMORY_PAGE_SIZE ||
        offset > (size_t)backing->page_count * PROCESS_MEMORY_PAGE_SIZE ||
        length > (size_t)backing->page_count * PROCESS_MEMORY_PAGE_SIZE - offset)
        return -1;
    root = copy_active_root(pid);
    if (!root) return -1;
    aliases = kmalloc((size_t)root->region_count * sizeof(*aliases));
    if (!aliases) { kfree(root); return -1; }
    /* Collect/validate all inventory records before touching a PTE: OOM and
       malformed metadata cannot leave a partly discarded physical backing. */
    for (uint32_t i = 0; i < root->region_count; ++i) {
        process_memory_region_t region;
        if (load_region_meta_in_namespace(pid, root->namespace_id,
                root_addresses(root)[i], &region) != 0) {
            kfree(aliases); kfree(root); return -1;
        }
        if (region.shared_id != id) continue;
        if (!(region.kind_flags & PROCESS_MEMORY_FLAG_JIT) ||
            region.shared_offset > (size_t)backing->page_count * PROCESS_MEMORY_PAGE_SIZE ||
            region.length > (size_t)backing->page_count * PROCESS_MEMORY_PAGE_SIZE - region.shared_offset) {
            kfree(aliases); kfree(root); return -1;
        }
        aliases[refs++] = region;
    }
    if (!refs) { kfree(aliases); kfree(root); return -1; }
    process_memory_shared_page_t **link = &backing->sparse_pages;
    while (*link) {
        process_memory_shared_page_t *page = *link;
        const size_t position = (size_t)page->index * PROCESS_MEMORY_PAGE_SIZE;
        if (position < offset || position - offset >= length) {
            link = &page->next; continue;
        }
        for (size_t i = 0; i < refs; ++i) {
            const process_memory_region_t *region = &aliases[i];
            if (position >= region->shared_offset && position - region->shared_offset < region->length)
                vmm_unmap_range(pml4, region->addr + position - region->shared_offset,
                                PROCESS_MEMORY_PAGE_SIZE, 0);
        }
        /* A fork or saved namespace may still legitimately use this page.
           Local decommit is advisory there, as for shared mappings; never
           free a page beneath a foreign PTE or invalidate foreign ownership. */
        if (ora_reference_count(backing->storage) == refs) {
            *link = page->next;
            --backing->sparse_committed_pages;
            pmm_free_page((void *)(uintptr_t)page->phys);
            kfree(page);
        } else link = &page->next;
    }
    kfree(aliases);
    kfree(root);
    return 0;
}

int process_memory_materialize_region(uint32_t pid, uint64_t cr3, uint64_t addr) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root;
    process_memory_region_t region;
    void *backing;
    int result;
    

    if (ensure_active_root(pid) != 0) {
        return -1;
    }

    root = copy_active_root(pid);
    if (root == NULL) {
        return -1;
    }
    result = load_region_meta_in_namespace(pid, root->namespace_id, addr, &region);
    kfree(root);
    if (result != 0) {
        return -1;
    }

    if ((region.kind_flags & (PROCESS_MEMORY_FLAG_RESERVATION | PROCESS_MEMORY_FLAG_LAZY)) != 0u ||
        ((region.kind_flags & PROCESS_MEMORY_FLAG_VMM_DIRECT) != 0u && region.shared_id == 0u)) {
        return 0;
    }

    if (region.shared_id != 0u) {
        backing = NULL;
    } else {
            backing = (void *)ora_view_data(&region.content);
            if(!backing) {return -1;}
        }

    result = materialize_region_to_cr3(cr3, &region, backing);
    return result;
}

int process_memory_snapshot_region(uint32_t pid,uint64_t cr3,uint64_t addr) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    pmem_namespace_t *space=find_inventory_namespace(pid,PMEM_ACTIVE_NAMESPACE);
    if(!space)return -1;
    pmem_region_node_t *node=NULL;
    for(uint32_t i=0;i<space->root->region_count;++i) {
        pmem_region_node_t *candidate=find_inventory_region(space,root_addresses(space->root)[i]);
        if(candidate && candidate->present && addr>=candidate->region.addr && addr-candidate->region.addr<candidate->region.length){node=candidate;break;}
    }
    if(!node)return -1;
    process_memory_region_t region=node->region;
    if(region.shared_id || region.file_backing_id || (region.kind_flags & (PROCESS_MEMORY_FLAG_RESERVATION|PROCESS_MEMORY_FLAG_LAZY)))return 0;
    void *pml4=vmm_cr3_to_address_space(cr3);
    if(!pml4 || !hhdm_request.response)return -1;
    int direct=!!(region.kind_flags & PROCESS_MEMORY_FLAG_ORA_DIRECT);
    ora_ref_t content=NULL;uint8_t *buffer=NULL;
    if(direct)content=ora_retain(region.content.object); /* unchanged immutable bytes: zero hashes */
    else {
        buffer=kmalloc(region.length);if(!buffer)return -1;
        for(size_t offset=0;offset<region.length;) {
            uint64_t va=region.addr+offset,phys=vmm_get_physical_any(pml4,va);
            if(!phys){kfree(buffer);return -1;}
            size_t count=4096-(va & 4095);if(count>region.length-offset)count=region.length-offset;
            memcpy(buffer+offset,(void *)(hhdm_request.response->offset+phys),count);offset+=count;
        }
        content=ora_create(buffer,region.length,ORA_CANONICAL);kfree(buffer);
    }
    if(!content)return -1;
    uint64_t *pages=NULL;size_t count=align_up_u64(region.length,4096)/4096;
    if(direct) {
        pages=kmalloc(count*sizeof(*pages));if(!pages){ora_release(content);return -1;}
        memset(pages,0,count*sizeof(*pages));
        for(size_t i=0;i<count;++i) {
            pages[i]=(uint64_t)pmm_alloc_page();if(!pages[i]) {
                for(size_t j=0;j<i;++j)pmm_free_page((void *)pages[j]);kfree(pages);ora_release(content);return -1;
            }
            memcpy((void *)(hhdm_request.response->offset+pages[i]),(void *)(hhdm_request.response->offset+vmm_get_physical_any(pml4,region.addr+i*4096)),4096);
        }
    }
    region.kind_flags &= ~(PROCESS_MEMORY_FLAG_ORA_DIRECT|PROCESS_MEMORY_FLAG_VMM_DIRECT);
    region.content=direct ? node->region.content : (ora_view_t){content,0,region.length};
    int result=store_region_meta(pid,space->identity,region.addr,&region)?0:-1;
    if(direct) {
        if(!result) {
            /* All tables already exist for the immutable projection. Publish
               private ownership only after its reference can be retained. */
            for(size_t i=0;i<count;++i)vmm_map_page(pml4,region.addr+i*4096,pages[i],region_flags_from_prot(region.prot));
        } else {
            for(size_t i=0;i<count;++i)pmm_free_page((void *)pages[i]);
        }
    }
    ora_release(content);kfree(pages);return result;
}

int process_memory_snapshot_all(uint32_t pid, uint64_t cr3) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root;

    if (ensure_active_root(pid) != 0) {
        return -1;
    }

    root = copy_active_root(pid);
    if (root == NULL) {
        return -1;
    }

    for (uint32_t index = 0u; index < root->region_count; ++index) {
        if (process_memory_snapshot_region(pid, cr3, root_addresses(root)[index]) != 0) {

            kfree(root);
            return -1;
        }
    }

    kfree(root);
    return 0;
}

int process_memory_materialize_all(uint32_t pid, uint64_t cr3) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    const uint64_t namespace_id = process_memory_current_namespace(pid);

    if (namespace_id == 0u) {
        return 0;
    }
    return process_memory_materialize_namespace(pid, namespace_id, cr3);
}

static int region_owns_pte_pages(const process_memory_region_t *region) {
    process_memory_file_backing_t *file;

    if (region == NULL || region->shared_id != 0u) {
        return 0;
    }
    if (region->file_backing_id != 0u) {
        file = find_file_backing(region->file_backing_id);
        if (file != NULL && file->shared != 0u) {
            return 0;
        }
    }
    return
        (region->kind_flags & (PROCESS_MEMORY_FLAG_ORA_DIRECT |
                               PROCESS_MEMORY_FLAG_RESERVATION |
                               PROCESS_MEMORY_FLAG_JIT)) == 0u;
}

/* A split must read the original backing for every segment.  Updating the
 * left segment in-place first would truncate the ORA content, so a later
 * right-hand segment could read past the new object and fault in the kernel. */




int process_memory_clone_namespace(uint32_t src_pid, uint64_t src_cr3, uint32_t dst_pid, uint64_t dst_cr3) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *src_root;
    uint64_t dst_namespace_id;

    src_root = copy_active_root(src_pid);
    if (src_root == NULL) {
        return -1;
    }

    /* Capture the current contents of every private region before cloning.
     * The inventory owns the region; its immutable snapshot can be stale when user code has
     * have modified the PTEs since the mapping was created.  Shared regions
     * deliberately skip this step so both processes retain the same backing. */
    for (uint32_t index = 0u; index < src_root->region_count; ++index) {
        process_memory_region_t region;
        const uint64_t addr = root_addresses(src_root)[index];
        if (load_region_meta_in_namespace(src_pid, src_root->namespace_id, addr, &region) != 0) {
            kfree(src_root);
            return -1;
        }
        if (region.shared_id == 0u &&
            (region.kind_flags & PROCESS_MEMORY_FLAG_LAZY) == 0u &&
            (region.kind_flags & (PROCESS_MEMORY_FLAG_RESERVATION|PROCESS_MEMORY_FLAG_ORA_DIRECT)) == 0u &&
            process_memory_snapshot_region(src_pid, src_cr3, addr) != 0) {

            kfree(src_root);
            return -1;
        }
    }

    if (process_memory_namespace_begin(dst_pid, src_root->alloc_hint, &dst_namespace_id) != 0) {

        kfree(src_root);
        return -1;
    }

    for (uint32_t index = 0u; index < src_root->region_count; ++index) {
        process_memory_region_t region;
        const uint64_t addr = root_addresses(src_root)[index];

        if (load_region_meta_in_namespace(src_pid, src_root->namespace_id, addr, &region) != 0) {
            (void)process_memory_namespace_destroy(dst_pid, dst_namespace_id, dst_cr3);
            kfree(src_root);
            return -1;
        }

        /* Private/direct mappings are copied from the source PTEs into their
         * ORA immutable snapshot before the child is built.  Shared mappings retain the
         * same refcounted physical backing and reservations retain only their
         * address-space inventory entry. */
        if (region.shared_id == 0u &&
            (region.kind_flags & PROCESS_MEMORY_FLAG_LAZY) == 0u &&
            (region.kind_flags & PROCESS_MEMORY_FLAG_RESERVATION) == 0u &&
            ((region.kind_flags & PROCESS_MEMORY_FLAG_VMM_DIRECT) != 0u)) {
            if (process_memory_snapshot_region(src_pid, src_cr3, addr) != 0 ||
                load_region_meta_in_namespace(src_pid, src_root->namespace_id, addr, &region) != 0) {

                (void)process_memory_namespace_destroy(dst_pid, dst_namespace_id, dst_cr3);
                kfree(src_root);
                return -1;
            }
        }

        if (region.shared_id != 0u ||
            (region.kind_flags & PROCESS_MEMORY_FLAG_LAZY) != 0u ||
            (region.kind_flags & PROCESS_MEMORY_FLAG_RESERVATION) != 0u) {
            if (process_memory_register_region_in_namespace_partial(dst_pid, dst_namespace_id, addr,
                    region.length, region.prot, region.flags,
                    region.kind_flags | PROCESS_MEMORY_FLAG_VMM_DIRECT, NULL, 0u) != 0) {

                (void)process_memory_namespace_destroy(dst_pid, dst_namespace_id, dst_cr3);
                kfree(src_root);
                return -1;
            }
            {
                process_memory_region_t child_region;
                if (load_region_meta_in_namespace(dst_pid, dst_namespace_id, addr, &child_region) != 0) {
                    (void)process_memory_namespace_destroy(dst_pid, dst_namespace_id, dst_cr3);
                    kfree(src_root);
                    return -1;
                }
                if(region.shared_id && shared_backing_acquire(region.shared_id)!=0) {
                    process_memory_namespace_destroy(dst_pid,dst_namespace_id,dst_cr3);kfree(src_root);return -1;
                }
                child_region.shared_id = region.shared_id;
                child_region.shared_offset = region.shared_offset;
                child_region.file_backing_id = region.file_backing_id;
                child_region.file_offset = region.file_offset;
                child_region.content = (ora_view_t){0};
                if (region.file_backing_id != 0u &&
                    file_backing_retain(region.file_backing_id) != 0) {
                    if(region.shared_id)shared_backing_release(region.shared_id);
                    (void)process_memory_namespace_destroy(dst_pid, dst_namespace_id, dst_cr3);
                    kfree(src_root);
                    return -1;
                }
                if (store_region_meta(dst_pid, dst_namespace_id, addr, &child_region) == NULL) {
                    if(region.shared_id)shared_backing_release(region.shared_id);
                    if (region.file_backing_id != 0u) {
                        file_backing_release(region.file_backing_id);
                    }
                    (void)process_memory_namespace_destroy(dst_pid, dst_namespace_id, dst_cr3);
                    kfree(src_root);
                    return -1;
                }
                if (region.shared_id != 0u) {
                    /* Child inventory owns the retained mutable reference. */
                } else if (region.file_backing_id != 0u) {
                    if ((region.flags & MAP_SHARED) == 0 &&
                        vmm_clone_present_pages(vmm_cr3_to_address_space(src_cr3),
                            vmm_cr3_to_address_space(dst_cr3), region.addr,
                            region.length) != 0) {
                        (void)process_memory_namespace_destroy(dst_pid, dst_namespace_id, dst_cr3);
                        kfree(src_root);
                        return -1;
                    }
                } else if ((region.kind_flags & PROCESS_MEMORY_FLAG_LAZY) != 0u &&
                           vmm_clone_present_pages(vmm_cr3_to_address_space(src_cr3),
                               vmm_cr3_to_address_space(dst_cr3), region.addr,
                               region.length) != 0) {
                    (void)process_memory_namespace_destroy(dst_pid, dst_namespace_id, dst_cr3);
                    kfree(src_root);
                    return -1;
                }
            }
            continue;
        }

        if(register_inventory_region(dst_pid,dst_namespace_id,&region)!=0) {
            process_memory_namespace_destroy(dst_pid,dst_namespace_id,dst_cr3);kfree(src_root);return -1;
        }

    }

    kfree(src_root);

    if (process_memory_materialize_namespace(dst_pid, dst_namespace_id, dst_cr3) != 0 ||
        process_memory_namespace_commit(dst_pid, dst_namespace_id) != 0) {

        (void)process_memory_namespace_destroy(dst_pid, dst_namespace_id, dst_cr3);
        return -1;
    }

    return 0;
}

int process_memory_validate_surface_mapping(uint32_t pid, uint64_t addr,
                                             size_t length, uint64_t shared_id,
                                             int required_prot) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root;
    int found = 0;
    if (addr == 0u || length == 0u || shared_id == 0u ||
        (addr & 0xfffu) != 0u || (length & 0xfffu) != 0u ||
        (uint64_t)length > UINT64_MAX - addr || ensure_active_root(pid) != 0)
        return -1;
    root = copy_active_root(pid);
    if (root == NULL) return -1;
    for (uint32_t index = 0u; index < root->region_count; ++index) {
        process_memory_region_t candidate;
        const uint64_t candidate_addr = root_addresses(root)[index];
        if (candidate_addr != addr ||
            load_region_meta_in_namespace(pid, root->namespace_id,
                                          candidate_addr, &candidate) != 0)
            continue;
        found = candidate.magic == PROCESS_MEMORY_REGION_MAGIC &&
            candidate.addr == addr && candidate.length == length &&
            candidate.shared_id == shared_id &&
            (candidate.kind_flags & PROCESS_MEMORY_FLAG_SURFACE) != 0u &&
            (candidate.prot & required_prot) == required_prot;
        break;
    }
    kfree(root);
    return found ? 0 : -1;
}

typedef struct region_split_plan {
    pmem_region_node_t *old;
    pmem_region_node_t *parts[3];
    unsigned count;
    uint64_t first,last;
} region_split_plan_t;
static void release_region_node(pmem_region_node_t *node) {
    ora_view_release(&node->region.content);
    if(node->region.shared_id)shared_backing_release(node->region.shared_id);
    if(node->region.file_backing_id)file_backing_release(node->region.file_backing_id);
    kfree(node);
}
static pmem_region_node_t *prepare_region_slice(const process_memory_region_t *original,
                                               uint64_t first,uint64_t last,int prot) {
    pmem_region_node_t *node=kmalloc(sizeof(*node));if(!node)return NULL;
    node->region=*original;node->next=NULL;node->present=1;
    process_memory_region_t *part=&node->region;size_t offset=first-original->addr;
    part->addr=first;part->length=last-first;part->prot=prot;part->content=(ora_view_t){0};
    part->shared_id=0;part->file_backing_id=0;
    if(original->content.object && ora_view_acquire(original->content.object,original->content.offset+offset,part->length,&part->content))goto fail;
    if(original->shared_id) {
        if(shared_backing_acquire(original->shared_id))goto fail;
        part->shared_id=original->shared_id;part->shared_offset=original->shared_offset+offset;
    }
    if(original->file_backing_id) {
        if(file_backing_retain(original->file_backing_id))goto fail;
        part->file_backing_id=original->file_backing_id;part->file_offset=original->file_offset+offset;
    }
    if(part->prot && (part->kind_flags & PROCESS_MEMORY_FLAG_RESERVATION)) {
        part->kind_flags &= ~PROCESS_MEMORY_FLAG_RESERVATION;part->kind_flags|=PROCESS_MEMORY_FLAG_VMM_DIRECT|PROCESS_MEMORY_FLAG_LAZY;
    }
    return node;
fail:release_region_node(node);return NULL;
}
/* All overlapping regions are prepared together, including ranges spanning
 * prior mprotect splits. No publication or PTE change precedes the last
 * allocation. Unmap drops PTE references before releasing any source view. */
/* Position of `address` in the root's address list. */
static uint32_t root_position(const process_memory_namespace_root_t *root, uint64_t address) {
    const uint64_t *addresses = root_addresses_const(root);

    for (uint32_t i = 0u; i < root->region_count; ++i) {
        if (addresses[i] == address) {
            return i;
        }
    }
    return UINT32_MAX;
}

#define SPLIT_INLINE_PLANS 8u

/* mode: protection, remove, reserve/decommit, commit. All use the same OOM-safe
 * split publication and owning inventory; no second VM bookkeeping engine. */
static int split_inventory_range(uint32_t pid,uint64_t cr3,uint64_t addr,size_t length,int prot,int mode) {
    const int removing=mode==1;
    if(!addr || !length || length>UINT64_MAX-addr)return -1;
    pmem_namespace_t *space=find_inventory_namespace(pid,PMEM_ACTIVE_NAMESPACE);
    if(!space || !space->root || !space->root->region_count || !region_index_ready(space))return -1;
    const uint64_t end=addr+length;
    /* Regions are disjoint and ordered in the index: the ones that overlap
       [addr,end) are consecutive, starting at most one before the first region
       that begins after addr. */
    uint32_t first=region_index_upper(space,addr);
    if(first)--first;
    uint32_t affected=0;
    int partial=0;
    for(uint32_t i=first;i<space->index_count && space->index[i]->region.addr<end;++i) {
        const process_memory_region_t *region=&space->index[i]->region;
        const uint64_t stop=region->addr+region->length;
        if(addr>=stop)continue;
        ++affected;
        if(addr>region->addr || end<stop)partial=1;
    }
    if(!affected)return -1;
    region_split_plan_t inline_plans[SPLIT_INLINE_PLANS];
    region_split_plan_t *plans=inline_plans;
    if(affected>SPLIT_INLINE_PLANS) {
        plans=kmalloc((size_t)affected*sizeof(*plans));
        if(!plans)return -1;
    }
    memset(plans,0,(size_t)affected*sizeof(*plans));
    {
        uint32_t slot=0;
        for(uint32_t i=first;i<space->index_count && space->index[i]->region.addr<end;++i) {
            pmem_region_node_t *node=space->index[i];
            if(addr<node->region.addr+node->region.length)plans[slot++].old=node;
        }
    }
    /* Whole-region removal is also the rollback path of a failed mapping.
       It must be possible under OOM: compact the existing root in place,
       remove PTEs, then drop the already-owned views/provider references. */
    if(removing && !partial) {
        ++space->generation;
        uint64_t released_charge=0;for(uint32_t i=0;i<affected;i++)released_charge+=private_region_charge(&plans[i].old->region);
        private_charge_change(space,released_charge,0);
        for(uint32_t i=0;i<affected;++i) {
            pmem_region_node_t *node=plans[i].old;
            const uint64_t address=node->region.addr;
            const uint32_t position=root_position(space->root,address);
            if(position!=UINT32_MAX)
                root_addresses(space->root)[position]=root_addresses(space->root)[--space->root->region_count];
            pmem_region_node_t **link=&space->regions[region_bucket(address)];
            while(*link!=node)link=&(*link)->next;
            *link=node->next;
            if(cr3) {
                linux_futex_pi_cancel_range(cr3,address,node->region.length);
                vmm_unmap_range(vmm_cr3_to_address_space(cr3),address,node->region.length,region_owns_pte_pages(&node->region));
            }
            release_region_node(node);
        }
        if(plans!=inline_plans)kfree(plans);
        return 0;
    }
    uint64_t new_count=space->root->region_count,peak=new_count;
    for(uint32_t i=0;i<affected;++i) {
        region_split_plan_t *plan=&plans[i];
        process_memory_region_t *region=&plan->old->region;uint64_t stop=region->addr+region->length;
        if(!removing && ((region->kind_flags & PROCESS_MEMORY_FLAG_ORA_DIRECT) && (prot & PROT_WRITE)))goto fail;
        if(!removing && (region->kind_flags & PROCESS_MEMORY_FLAG_SURFACE) && ((uint32_t)prot & ~region->reserved0))goto fail;
        plan->first=addr>region->addr?addr:region->addr;plan->last=end<stop?end:stop;
        uint64_t starts[3],stops[3];int protections[3];unsigned count=0;
        if(plan->first>region->addr){starts[count]=region->addr;stops[count]=plan->first;protections[count++]=region->prot;}
        if(!removing){starts[count]=plan->first;stops[count]=plan->last;protections[count++]=prot;}
        if(plan->last<stop){starts[count]=plan->last;stops[count]=stop;protections[count++]=region->prot;}
        for(unsigned part=0;part<count;++part) {
            pmem_region_node_t *slice=prepare_region_slice(region,starts[part],stops[part],protections[part]);
            if(slice && mode>=2 && starts[part]==plan->first && stops[part]==plan->last){
                slice->region.kind_flags|=PROCESS_MEMORY_FLAG_LAZY|PROCESS_MEMORY_FLAG_VMM_DIRECT;
                if(mode==2)slice->region.kind_flags|=PROCESS_MEMORY_FLAG_RESERVATION;
                else slice->region.kind_flags&=~PROCESS_MEMORY_FLAG_RESERVATION;
            }
            if(!slice)goto fail;plan->parts[plan->count++]=slice;
        }
        new_count=new_count-1+count;
        if(count>1)peak+=count-1;
    }
    if(peak>UINT32_MAX)goto fail;
    if(!removing) {
        /* Page-rounded ELF extents can share a boundary page. Require actual
           coverage of every requested page, rather than filling gaps. */
        uint64_t cursor=addr;
        while(cursor<end) {
            uint64_t covered=cursor;
            for(uint32_t i=0;i<affected;++i) {
                process_memory_region_t *region=&plans[i].old->region;
                uint64_t start=align_down_u64(region->addr,4096),stop=align_up_u64(region->addr+region->length,4096);
                if(start<=cursor && stop>covered)covered=stop;
            }
            if(covered==cursor)goto fail;cursor=covered;
        }
    }
    /* Everything that can fail has: the rest edits the address list in place
       (a larger copy only when it has no room) and relinks the nodes. */
    process_memory_namespace_root_t *grown=NULL;
    if(peak>root_capacity(space->root)) {   /* edits may transiently exceed the final count */
        grown=grow_root(space->root,(uint32_t)peak);
        if(!grown)goto fail;
    }
    process_memory_namespace_root_t *root=grown?grown:space->root;
    for(uint32_t i=0;i<affected;++i) {
        region_split_plan_t *plan=&plans[i];
        const uint32_t position=root_position(root,plan->old->region.addr);
        uint64_t *addresses=root_addresses(root);
        if(plan->count) {
            if(position!=UINT32_MAX)addresses[position]=plan->parts[0]->region.addr;
            else addresses[root->region_count++]=plan->parts[0]->region.addr;
            for(unsigned part=1;part<plan->count;++part)addresses[root->region_count++]=plan->parts[part]->region.addr;
        } else if(position!=UINT32_MAX) {
            addresses[position]=addresses[--root->region_count];
        }
    }
    ++space->generation;
    uint64_t old_charge=0,new_charge=0;
    for(uint32_t i=0;i<affected;++i){old_charge+=private_region_charge(&plans[i].old->region);
        for(unsigned j=0;j<plans[i].count;j++)new_charge+=private_region_charge(&plans[i].parts[j]->region);}
    private_charge_change(space,old_charge,new_charge);
    for(uint32_t i=0;i<affected;++i) {
        pmem_region_node_t **link=&space->regions[region_bucket(plans[i].old->region.addr)];
        while(*link!=plans[i].old)link=&(*link)->next;*link=plans[i].old->next;
        for(unsigned part=0;part<plans[i].count;++part) {
            pmem_region_node_t *node=plans[i].parts[part];size_t bucket=region_bucket(node->region.addr);
            node->next=space->regions[bucket];space->regions[bucket]=node;
        }
    }
    if(grown){kfree(space->root);space->root=grown;}
    if((removing || mode==2) && cr3)linux_futex_pi_cancel_range(cr3,addr,length);
    if((removing || mode==2) && cr3)for(uint32_t i=0;i<affected;++i)
        vmm_unmap_range(vmm_cr3_to_address_space(cr3),plans[i].first,plans[i].last-plans[i].first,region_owns_pte_pages(&plans[i].old->region));
    for(uint32_t i=0;i<affected;++i)release_region_node(plans[i].old);
    if(plans!=inline_plans)kfree(plans);
    return 0;
fail:
    for(uint32_t i=0;i<affected;++i)for(unsigned part=0;part<plans[i].count;++part)release_region_node(plans[i].parts[part]);
    if(plans!=inline_plans)kfree(plans);
    return -1;
}
int process_memory_remove_region(uint32_t pid,uint64_t cr3,uint64_t addr,size_t length) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    return split_inventory_range(pid,cr3,addr,length,0,1);
}

int process_memory_protection_allowed(uint32_t pid, uint64_t addr,
                                     size_t length, int prot) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    process_memory_namespace_root_t *root;
    const uint64_t permissions = (uint64_t)(uint32_t)prot &
        (uint64_t)(PROT_READ | PROT_WRITE | PROT_EXEC);
    uint64_t end;
    int allowed = 1;

    if (pid == 0u || addr == 0u || length == 0u ||
        (uint64_t)length > UINT64_MAX - addr ||
        (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) {
        return 0;
    }
    end = addr + (uint64_t)length;
    /* Hold the authoritative namespace root directly here.  This check runs
     * before page-table permissions change and must not fail open because a
     * temporary root-copy allocation failed. */
    root = find_inventory_namespace(pid, PMEM_ACTIVE_NAMESPACE) ? find_inventory_namespace(pid, PMEM_ACTIVE_NAMESPACE)->root : NULL;
    if (root == NULL) {
        return 1; /* no registered Surface mapping can exist without a root */
    }
    if (root->magic != PROCESS_MEMORY_ROOT_MAGIC) {
        return 0;
    }
    for (uint32_t index = 0u; index < root->region_count; ++index) {
        process_memory_region_t region;
        uint64_t region_end;
        uint64_t region_start_page;
        uint64_t region_end_page;
        if (load_region_meta_in_namespace(pid, root->namespace_id,
                root_addresses(root)[index], &region) != 0) {
            allowed = 0;
            break;
        }
        if ((region.kind_flags & PROCESS_MEMORY_FLAG_SURFACE) == 0u ||
            region.length == 0u ||
            (uint64_t)region.length > UINT64_MAX - region.addr) {
            continue;
        }
        region_end = region.addr + (uint64_t)region.length;
        region_start_page = align_down_u64(region.addr, PROCESS_MEMORY_PAGE_SIZE);
        region_end_page = align_up_u64(region_end, PROCESS_MEMORY_PAGE_SIZE);
        if (addr < region_end_page && end > region_start_page &&
            (permissions & ~(uint64_t)region.reserved0) != 0u) {
            allowed = 0;
            break;
        }
    }
    return allowed;
}

int process_memory_protect_region(uint32_t pid,uint64_t addr,size_t length,int prot) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    if((prot & ~(PROT_READ|PROT_WRITE|PROT_EXEC)) || ((prot & (PROT_WRITE|PROT_EXEC))==(PROT_WRITE|PROT_EXEC)))return -1;
    return split_inventory_range(pid,0,addr,length,prot,0);
}

int process_memory_clear_namespace(uint32_t pid, uint64_t cr3) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    uint64_t current_namespace = process_memory_current_namespace(pid);
    process_memory_namespace_root_t *root;
    uint64_t new_namespace_id;
    uint64_t alloc_hint = 0u;

    if (current_namespace == 0u) {
        return 0;
    }

    root = copy_active_root(pid);
    if (root != NULL) {
        alloc_hint = root->alloc_hint;
        kfree(root);
    }

    if (process_memory_namespace_begin(pid, alloc_hint, &new_namespace_id) != 0 ||
        process_memory_namespace_commit(pid, new_namespace_id) != 0) {

        return -1;
    }

    return process_memory_namespace_destroy(pid, current_namespace, cr3);
}

uint64_t process_memory_next_alloc_hint(uint32_t pid, uint64_t floor) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    uint64_t hint = align_up_u64(floor, PROCESS_MEMORY_PAGE_SIZE);
    uint64_t active_hint;

    if (active_root_header(pid, NULL, &active_hint) == 0 && active_hint > hint) {
        hint = active_hint;
    }

    return hint;
}

static int inventory_range_has_policy(process_t *process,uint64_t address,size_t length,uint32_t policy) {
    if(!process || !length || address>=VMM_USER_ADDRESS_LIMIT || length>VMM_USER_ADDRESS_LIMIT-address)return 0;
    pmem_namespace_t *space=find_inventory_namespace(process_memory_owner(process),PMEM_ACTIVE_NAMESPACE);
    if(!space)return 0;
    uint64_t end=address+length,cursor=address;
    const int indexed=region_index_ready(space);
    while(cursor<end) {
        uint64_t covered=cursor;
        if(indexed) {
            /* Regions are disjoint, so the one starting last at or before the
               cursor is the only candidate; fall through to the scan if it does
               not carry the policy or does not reach the cursor. */
            const uint32_t after=region_index_upper(space,cursor);
            if(after) {
                const process_memory_region_t *region=&space->index[after-1u]->region;
                const uint64_t stop=align_up_u64(region->addr+region->length,4096);
                if((region->kind_flags & policy)==policy && align_down_u64(region->addr,4096)<=cursor && stop>cursor) {
                    cursor=stop;continue;
                }
            }
        }
        for(uint32_t i=0;i<space->root->region_count;++i) {
            pmem_region_node_t *node=find_inventory_region(space,root_addresses(space->root)[i]);if(!node || !node->present)return 0;
            const process_memory_region_t *region=&node->region;
            if((region->kind_flags & policy)!=policy)continue;
            uint64_t start=align_down_u64(region->addr,4096),stop=align_up_u64(region->addr+region->length,4096);
            if(start<=cursor && stop>covered)covered=stop;
        }
        if(covered==cursor)return 0;cursor=covered;
    }
    return 1;
}
int process_memory_region_contains(process_t *process,uint64_t address,size_t length) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    return inventory_range_has_policy(process,address,length,0);
}
int process_memory_region_is_reservation(process_t *process,uint64_t address,size_t length) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    return inventory_range_has_policy(process,address,length,PROCESS_MEMORY_FLAG_RESERVATION);
}
int process_memory_region_is_lazy(process_t *process,uint64_t address,size_t length) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    return inventory_range_has_policy(process,address,length,PROCESS_MEMORY_FLAG_LAZY);
}

int process_memory_range_is_free(process_t *process, uint64_t address, size_t length) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    const uint32_t owner_pid = process_memory_owner(process);
    const uint64_t end = length == 0u || (uint64_t)length > UINT64_MAX - address
        ? 0u : address + (uint64_t)length;

    if (process == NULL || length == 0u || end == 0u || end <= address) {
        return 0;
    }
    pmem_namespace_t *space = find_inventory_namespace(owner_pid, PMEM_ACTIVE_NAMESPACE);
    if (space == NULL || space->root == NULL) {
        return 1;
    }
    if (region_index_ready(space)) {
        /* Disjoint regions: only the last one starting before `end` can reach
           back over `address`. */
        const uint32_t before = region_index_upper(space, end - 1u);

        return before == 0u || space->index[before - 1u]->region.addr +
            (uint64_t)space->index[before - 1u]->region.length <= address;
    }
    for (uint32_t index = 0u; index < space->root->region_count; ++index) {
        const pmem_region_node_t *node = find_inventory_region(space, root_addresses(space->root)[index]);
        const process_memory_region_t *region;

        if (node == NULL || !node->present) {
            continue;
        }
        region = &node->region;
        if (region->length != 0u && (uint64_t)region->length <= UINT64_MAX - region->addr &&
            address < region->addr + (uint64_t)region->length && region->addr < end) {
            return 0;
        }
    }
    return 1;
}

void process_memory_resident_diagnostic(process_t *process) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    uint32_t pid=process->group?process->group->process_id:process->pid;
    process_memory_namespace_root_t *root=copy_active_root(pid);
    uint64_t counts[6]={0},largest=0,largest_addr=0,largest_size=0;char line[256];
    if(!root)return;
    for(uint32_t i=0;i<root->region_count;i++) {
        process_memory_region_t r;
        if(load_region_meta_in_namespace(pid,root->namespace_id,root_addresses(root)[i],&r)!=0 ||
            !r.length || r.length>UINT64_MAX-r.addr)continue;
        uint64_t n=vmm_resident_pages(vmm_cr3_to_address_space(process->cr3),r.addr,r.addr+r.length);
        unsigned category=(r.kind_flags&PROCESS_MEMORY_FLAG_FILE_BACKED)?0u:
            (r.kind_flags&PROCESS_MEMORY_FLAG_SURFACE)?1u:
            (r.kind_flags&PROCESS_MEMORY_FLAG_SHARED)?2u:
            (r.kind_flags&PROCESS_MEMORY_FLAG_STACK)?3u:
            (r.kind_flags&PROCESS_MEMORY_FLAG_HEAP)?4u:5u;
        counts[category]+=n;
        if(category>=4u && n>largest){largest=n;largest_addr=r.addr;largest_size=r.length;}
    }
    snprintf(line,sizeof(line),"memory-resident: pid=%u regions=%u file=%u surface=%u shared=%u stack=%u heap=%u private=%u largest=%u\n",
        pid,root->region_count,(unsigned)counts[0],(unsigned)counts[1],(unsigned)counts[2],
        (unsigned)counts[3],(unsigned)counts[4],(unsigned)counts[5],(unsigned)largest);
    debug_log_puts(line);debug_log_puts("memory-largest: addr=");debug_put_hex64_local(largest_addr);
    debug_log_puts(" length=");debug_put_hex64_local(largest_size);debug_log_puts("\n");
    kfree(root);
}

int process_memory_get_region(uint32_t owner,uint64_t address,process_memory_region_t *out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    pmem_namespace_t *space=find_inventory_namespace(owner,PMEM_ACTIVE_NAMESPACE);
    return space ? load_region_meta_in_namespace(owner,space->identity,address,out) : -1;
}
int process_memory_commit_usage(uint32_t owner,uint64_t *current,uint64_t *peak){
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    pmem_namespace_t *space=find_inventory_namespace(owner,PMEM_ACTIVE_NAMESPACE);if(!space)return -1;
    *current=space->private_charge;*peak=space->peak_private_charge;return 0;
}
int process_memory_query_view(uint32_t owner,uint64_t address,vex_memory_view_info_t *out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();
    if(!out || address>=VMM_USER_ADDRESS_LIMIT)return -1;
    const uint64_t base=address&~4095ull;
    pmem_namespace_t *space=find_inventory_namespace(owner,PMEM_ACTIVE_NAMESPACE);
    if(!space || !space->root)return -1;
    memset(out,0,sizeof(*out));out->base=base;
    const process_memory_region_t *r=inventory_region_containing(space,base,0);
    if(r) {
        if(r->addr>base || r->length>VMM_USER_ADDRESS_LIMIT-r->addr)return -1;
        out->length=r->addr+r->length-base;
        out->allocation_base=r->allocation_base?r->allocation_base:r->addr;
        out->permissions=(uint32_t)r->prot;out->mapping_flags=(uint32_t)r->flags;
        out->kind=(r->kind_flags&PROCESS_MEMORY_FLAG_RESERVATION)?VEX_MEMORY_VIEW_RESERVED:VEX_MEMORY_VIEW_MAPPED;
        return 0;
    }
    uint64_t next=VMM_USER_ADDRESS_LIMIT;
    if(region_index_ready(space)) {
        uint32_t after=region_index_upper(space,base);
        if(after<space->index_count)next=space->index[after]->region.addr;
    }else {
        for(unsigned i=0;i<space->root->region_count;i++) {
            pmem_region_node_t *node=find_inventory_region(space,root_addresses_const(space->root)[i]);
            if(!node || !node->present)return -1;
            if(node->region.addr>base && node->region.addr<next)next=node->region.addr;
        }
    }
    if(next<=base)return -1;out->length=next-base;out->kind=VEX_MEMORY_VIEW_FREE;return 0;
}

int process_memory_describe_address(uint32_t owner,uint64_t address,uint64_t *base,uint32_t *kind_flags,
                                    uint64_t *file_offset,char *path,size_t path_capacity) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end))) = pmem_transaction_begin();

    const process_memory_region_t *region=active_region_containing(owner,address,0u);
    if(!region)return -1;
    const process_memory_file_backing_t *file=region->file_backing_id?find_file_backing(region->file_backing_id):NULL;
    *base=region->addr;*kind_flags=region->kind_flags;
    *file_offset=file?file->file_offset+region->file_offset:0u;   /* the backing starts at its own offset in the file */
    if(path_capacity)path[0]=0;
    if(file && path_capacity)vex_web_fd_describe(file->object,path,path_capacity);
    return 0;
}

int process_memory_discard_private(uint32_t owner,uint64_t cr3,uint64_t addr,uint64_t length) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    if(!addr || (addr&4095) || !length || (length&4095) || addr>=VMM_USER_ADDRESS_LIMIT || length>VMM_USER_ADDRESS_LIMIT-addr)return -EINVAL;
    uint64_t end=addr+length;
    for(uint64_t at=addr;at<end;) {
        const process_memory_region_t *r=active_region_containing(owner,at,0);
        if(!r || !(r->flags&MAP_ANONYMOUS) || !(r->flags&MAP_PRIVATE) ||
           !(r->kind_flags&PROCESS_MEMORY_FLAG_LAZY) || r->shared_id || r->file_backing_id || r->content.object ||
           (r->kind_flags&(PROCESS_MEMORY_FLAG_JIT|PROCESS_MEMORY_FLAG_SURFACE|PROCESS_MEMORY_FLAG_RESERVATION)) ||
           !(r->prot&PROT_WRITE) || (r->prot&PROT_EXEC))return -EINVAL;
        at=r->addr+r->length;if(at> end)at=end;
    }
    /* Inventory and lazy zero-fill contract remain unchanged. unmap_range
       performs SMP shootdown before releasing each privately owned page. */
    vmm_unmap_range(vmm_cr3_to_address_space(cr3),addr,length,1);return 0;
}
int process_memory_private_state(uint32_t owner,uint64_t cr3,uint64_t addr,uint64_t length,int prot,int commit){
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    if(!addr || (addr&4095) || !length || (length&4095) || addr>=VMM_USER_ADDRESS_LIMIT || length>VMM_USER_ADDRESS_LIMIT-addr ||
       (prot&~(PROT_READ|PROT_WRITE|PROT_EXEC)) || ((prot&(PROT_WRITE|PROT_EXEC))==(PROT_WRITE|PROT_EXEC)) || (!commit && prot))return -EINVAL;
    void *root=vmm_cr3_to_address_space(cr3);if(!root)return -EINVAL;
    uint64_t end=addr+length;
    for(uint64_t at=addr;at<end;){
        const process_memory_region_t *r=active_region_containing(owner,at,0);
        if(!r || (r->flags&(MAP_PRIVATE|MAP_ANONYMOUS))!=(MAP_PRIVATE|MAP_ANONYMOUS) ||
           r->shared_id || r->file_backing_id || r->content.object ||
           (r->kind_flags&~(PROCESS_MEMORY_FLAG_RESERVATION|PROCESS_MEMORY_FLAG_LAZY|PROCESS_MEMORY_FLAG_VMM_DIRECT)))return -EINVAL;
        at=r->addr+r->length;if(at>end)at=end;
    }
    if(split_inventory_range(owner,cr3,addr,length,prot,commit?3:2))return -ENOMEM;
    if(commit){uint64_t flags=VMM_PAGE_USER;if(prot)flags|=VMM_PAGE_PRESENT;
        if(prot&PROT_WRITE)flags|=VMM_PAGE_WRITABLE;if(!(prot&PROT_EXEC))flags|=VMM_PAGE_NX;
        /* Validated root/range: leaf flag changes allocate nothing. */
        return vmm_set_page_flags_range(root,addr,length,flags);
    }return 0;
}
uint64_t process_memory_available_va(uint32_t owner,uint64_t begin,uint64_t end) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    uint64_t free=end-begin;pmem_namespace_t *space=find_inventory_namespace(owner,PMEM_ACTIVE_NAMESPACE);
    if(!space)return free;
    for(uint32_t i=0;i<space->root->region_count;i++) {
        pmem_region_node_t *node=find_inventory_region(space,root_addresses(space->root)[i]);if(!node || !node->present)continue;
        uint64_t start=node->region.addr,stop=start+node->region.length;
        if(start<begin)start=begin;if(stop>end)stop=end;
        if(start<stop)free-=stop-start;
    }return free;
}

int process_memory_page_info(uint32_t owner,uint64_t cr3,uint64_t addr,vex_memory_page_info_t *out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    if(addr>=VMM_USER_ADDRESS_LIMIT || !out)return -EINVAL;
    memset(out,0,sizeof(*out));const process_memory_region_t *r=active_region_containing(owner,addr,0);
    if(!r)return 0;
    uint64_t flags=vmm_get_page_flags(vmm_cr3_to_address_space(cr3),addr);
    out->present=(flags&(VMM_PAGE_PRESENT|VMM_PAGE_USER))==(VMM_PAGE_PRESENT|VMM_PAGE_USER);
    out->permissions=(uint32_t)r->prot;
    /* Count real active inventory aliases to the same shared/file page,
       independently of creator PID, object references and retained snapshots. */
    if(out->present) {
        uint64_t phys=vmm_get_physical_any(vmm_cr3_to_address_space(cr3),addr&~4095ull);
        for(pmem_namespace_t *space=memory_namespaces;space;space=space->next)if(space->active) {
            for(unsigned b=0;b<PMEM_REGION_BUCKETS;b++)for(pmem_region_node_t *n=space->regions[b];n;n=n->next)if(n->present) {
                process_memory_region_t *q=&n->region;uint64_t offset=0;int same=0;
                if(q==r){same=1;offset=addr-r->addr;}
                else if(r->shared_id && q->shared_id==r->shared_id) {
                    uint64_t pos=r->shared_offset+(addr-r->addr);
                    if(pos>=q->shared_offset && pos-q->shared_offset<q->length){same=1;offset=pos-q->shared_offset;}
                } else if(r->file_backing_id && q->file_backing_id==r->file_backing_id &&
                          (r->flags&MAP_SHARED) && (q->flags&MAP_SHARED)) {
                    uint64_t pos=r->file_offset+(addr-r->addr);
                    if(pos>=q->file_offset && pos-q->file_offset<q->length){same=1;offset=pos-q->file_offset;}
                } else if(r->content.object && q->content.object==r->content.object &&
                          (r->kind_flags&PROCESS_MEMORY_FLAG_ORA_DIRECT) && (q->kind_flags&PROCESS_MEMORY_FLAG_ORA_DIRECT)) {
                    uint64_t pos=r->content.offset+(addr-r->addr);
                    if(pos>=q->content.offset && pos-q->content.offset<q->length){same=1;offset=pos-q->content.offset;}
                }
                if(same) {
                    /* Different Process roots may not currently map a lazy
                       page. Resolve their live CR3 through a scheduler lease. */
                    SCHEDULER_TASK_REF(ref);process_t *p=scheduler_task_acquire(&ref,space->owner,1);
                    if(p) {
                        void *root=vmm_cr3_to_address_space(p->cr3);uint64_t va=(q->addr+offset)&~4095ull;
                        if((vmm_get_page_flags(root,va)&(VMM_PAGE_USER|VMM_PAGE_PRESENT))==(VMM_PAGE_USER|VMM_PAGE_PRESENT) && vmm_get_physical_any(root,va)==phys)++out->mapping_refs;
                    }
                }
            }
        }
        if(!out->mapping_refs)out->mapping_refs=1;
    }
    out->shared=r->shared_id || (r->flags&MAP_SHARED) || (r->kind_flags&PROCESS_MEMORY_FLAG_ORA_DIRECT);return 0;
}

int process_memory_acquire_content(uint32_t pid,uint64_t address,ora_view_t *out) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    pmem_namespace_t *space=find_inventory_namespace(pid,PMEM_ACTIVE_NAMESPACE);
    pmem_region_node_t *node=find_inventory_region(space,address);
    return node && node->present && node->region.content.object ? ora_view_acquire(node->region.content.object,node->region.content.offset,node->region.content.length,out) : -1;
}
int process_memory_set_allocation_size(uint32_t pid,uint64_t address,size_t size) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    pmem_region_node_t *node=find_inventory_region(find_inventory_namespace(pid,PMEM_ACTIVE_NAMESPACE),address);
    if(!node || !node->present || !(node->region.kind_flags & PROCESS_MEMORY_FLAG_HEAP) || size>node->region.length)return -1;
    node->region.allocation_size=size;return 0;
}

void process_memory_reap(uint32_t owner) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    for(;;) {
        pmem_namespace_t *space=memory_namespaces;while(space && space->owner!=owner)space=space->next;
        if(!space)break;process_memory_namespace_destroy(owner,space->identity,0);
    }
}

static int retirement_owns_page(uint64_t address,void *context) {
    const uint32_t owner=*(const uint32_t *)context;
    const process_memory_region_t *region=active_region_containing(owner,address,0u);
    if(region)return region_owns_pte_pages(region);
    /* Unpublished construction namespaces can have materialized shared/ORA
     * leaves. Snapshots overlap active ranges, whose ownership wins above. */
    for(pmem_namespace_t *space=memory_namespaces;space;space=space->next) {
        if(space->owner!=owner || space->active)continue;
        region=inventory_region_containing(space,address,0u);
        if(region)return region_owns_pte_pages(region);
    }
    return 1; /* Uninventoried anonymous leaves: original VMM retirement owner. */
}

void process_memory_reap_mappings(uint32_t owner,uint64_t cr3) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    /* This entry point is used only by final process-group storage retirement,
     * not exec/munmap/snapshot rollback. No executing or borrowing thread can
     * touch the old user half. Close it once instead of thousands of range
     * shootdowns under the shared inventory gate. OOM requires no allocation. */
#if VEX_DESKTOP_TRACE
    const uint64_t begin=sched_clock_ns(),flushes=smp_current_cpu()->tlb_flushes;
#endif
    if(cr3 && vmm_retire_user_mappings(vmm_cr3_to_address_space(cr3),retirement_owns_page,&owner)==0) {
        cr3=0u;
#if VEX_DESKTOP_TRACE
        char line[128];snprintf(line,sizeof(line),"retirement-batch: pid=%u us=%u flushes=%u\n",owner,(unsigned)((sched_clock_ns()-begin)/1000),(unsigned)(smp_current_cpu()->tlb_flushes-flushes));debug_log_puts(line);
#endif
    }
    pmem_namespace_t *active=find_inventory_namespace(owner,PMEM_ACTIVE_NAMESPACE);
    if(active)process_memory_namespace_destroy(owner,active->identity,cr3);
    for(;;) {
        pmem_namespace_t *space=memory_namespaces;
        while(space && space->owner!=owner)space=space->next;
        if(!space)break;
        /* A construction can materialize leaves before committing its inventory.
           Snapshot inventories can overlap the active range: those leaves were
           already unmapped above, so VMM cannot release their pages twice. */
        process_memory_namespace_destroy(owner,space->identity,cr3);
    }
}

int process_memory_prepare_write(uint32_t pid,uint64_t cr3,uint64_t addr,size_t length) {
    pmem_transaction_t transaction __attribute__((cleanup(pmem_transaction_end)))=pmem_transaction_begin();
    if(!length || length>UINT64_MAX-addr)return -1;
    pmem_namespace_t *space=find_inventory_namespace(pid,PMEM_ACTIVE_NAMESPACE);if(!space)return -1;
    for(uint32_t i=0;i<space->root->region_count;++i) {
        pmem_region_node_t *node=find_inventory_region(space,root_addresses(space->root)[i]);if(!node || !node->present)return -1;
        if(node->region.addr<addr+length && addr<node->region.addr+node->region.length &&
           (node->region.kind_flags & PROCESS_MEMORY_FLAG_ORA_DIRECT) &&
           process_memory_snapshot_region(pid,cr3,node->region.addr))return -1;
    }
    return 0;
}
