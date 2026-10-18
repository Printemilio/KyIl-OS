#include <unos/kernel_allocator.h>
#include <unos/kernel_virtual_layout.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/boot_requests.h>
#include <unos/pmm.h>
#include <unos/scheduler.h>
#include <unos/vmm.h>
#include <unos/memory_diagnostic.h>
#include <unos/debug_log.h>
#include <unos/smp.h>
#include <stdio.h>

#define ka_HASH_BUCKETS 256u
#define ka_ALLOC_ALIGNMENT 64ull
#define ka_CHUNK_MIN_PAGES 4ull
#define ka_PAGE_SIZE 4096ull
#define ka_CHUNK_VIRT_BASE VEX_KERNEL_ALLOC_BASE
#define ka_CHUNK_VIRT_LIMIT VEX_KERNEL_ALLOC_LIMIT



typedef struct ka_heap_block ka_heap_block_t;
typedef struct ka_alloc_chunk ka_alloc_chunk_t;
typedef struct ka_chunk_page ka_chunk_page_t;
typedef struct ka_pid_arena ka_pid_arena_t;



typedef struct ka_hash_entry ka_hash_entry_t;

struct ka_heap_block {
    size_t size;
    uint32_t pid;
    uint8_t free;
    uint8_t reserved[51];
};

_Static_assert(sizeof(ka_heap_block_t) == ka_ALLOC_ALIGNMENT, "raw heap blocks must preserve 64-byte cache-line alignment");

struct ka_alloc_chunk {
    uint8_t *base;
    size_t size;
    size_t payload_offset;
    uint64_t page_count;
    ka_chunk_page_t *pages;
    ka_alloc_chunk_t *next;
};

struct ka_chunk_page {
    uint64_t phys;
    ka_chunk_page_t *next;
};

struct ka_pid_arena {
    uint32_t pid;
    uint32_t free_chunks;
    uint64_t resident_pages,peak_pages;
    uint8_t retired;
    ka_alloc_chunk_t *chunks;
    ka_pid_arena_t *next;
};







struct ka_hash_entry {
    uint64_t address;
    void *value;
    ka_hash_entry_t *next;
};



static ka_pid_arena_t *ka_arenas;
static ka_pid_arena_t *ka_arena_freelist;
static ka_hash_entry_t *ka_block_hash[ka_HASH_BUCKETS];
static ka_hash_entry_t *ka_chunk_hash[ka_HASH_BUCKETS];
static ka_hash_entry_t *ka_hash_entry_freelist;
static ka_alloc_chunk_t *ka_chunk_freelist;
static ka_alloc_chunk_t *ka_unused_chunk_descriptors;
static ka_chunk_page_t *ka_page_freelist;
static uint8_t *ka_meta_cursor;
static uint8_t *ka_meta_end;
static uint64_t ka_chunk_virt_cursor;

static bool ka_primary_allocator_ready;
static uint64_t ka_meta_pages;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return value > UINT64_MAX - (alignment - 1ull) ? 0u : (value + alignment - 1ull) & ~(alignment - 1ull);
}

static uint64_t pages_for_bytes(uint64_t byte_count) {
    return (byte_count + ka_PAGE_SIZE - 1ull) / ka_PAGE_SIZE;
}

static void *phys_to_hhdm(void *phys) {
    if (phys == NULL || hhdm_request.response == NULL) {
        return NULL;
    }

    return (void *)(hhdm_request.response->offset + (uint64_t)phys);
}

static void zero_bytes(void *ptr, size_t size) {
    memset(ptr, 0, size);
}

static uint64_t reserve_chunk_virtual_base(uint64_t page_count) {
    const uint64_t size = page_count * ka_PAGE_SIZE;
    const uint64_t base = align_up(ka_chunk_virt_cursor, ka_PAGE_SIZE);
    const uint64_t end = base + size;

    if (page_count == 0u || size / ka_PAGE_SIZE != page_count) {
        return 0u;
    }
    if (end < base || end > ka_CHUNK_VIRT_LIMIT) {
        return 0u;
    }

    ka_chunk_virt_cursor = end;
    return base;
}

static void release_chunk_backing(ka_alloc_chunk_t *chunk) {
    uint64_t virt;

    if (chunk == NULL || chunk->base == NULL) {
        return;
    }

    virt = (uint64_t)chunk->base;
    /* Every block/hash/borrow reference has gone before this function. Retire
     * the entire shared kernel extent once, before freeing any physical frame,
     * instead of sending an IPI to every CPU for every page of an ORA chunk. */
    if(vmm_unmap_kernel_range(virt, chunk->size)!=0) {
        debug_log_puts("allocator: invalid kernel extent retirement; backing NOT recycled\n");
        return;
    }
    for (ka_chunk_page_t *page = chunk->pages; page != NULL; virt += ka_PAGE_SIZE) {
        ka_chunk_page_t *next = page->next;
        pmm_free_page((void *)page->phys);
        /* All chunk/block hash references are already gone at each caller.
           The metadata allocator is a permanent bootstrap arena: recycle dead
           descriptors instead of leaking fresh arena bytes on every free. */
        page->phys = 0u;
        page->next = ka_page_freelist;
        ka_page_freelist = page;
        page = next;
    }
    /* Retain the now-unmapped virtual extent with its descriptor. Kernel VAs
       must be reusable too: releasing physical pages alone exhausts this
       bounded window while the PMM still has memory. No live borrow/block
       remains when an arena unlinks an all-free chunk. */
    chunk->pages = NULL;
    chunk->payload_offset = 0u;
    /* Physical ownership is already gone. Coalesce only free virtual ranges,
       retaining descriptors so variable-sized content ingestion cannot exhaust
       VA while physical memory remains available. */
    ka_alloc_chunk_t **link = &ka_chunk_freelist;
    while (*link != NULL) {
        ka_alloc_chunk_t *other = *link;
        if (other->base + other->size == chunk->base ||
            chunk->base + chunk->size == other->base) {
            *link = other->next;
            if (other->base < chunk->base) chunk->base = other->base;
            chunk->size += other->size;
            chunk->page_count += other->page_count;
            other->next = ka_unused_chunk_descriptors;
            ka_unused_chunk_descriptors = other;
            link = &ka_chunk_freelist;
        } else link = &other->next;
    }
    chunk->next = ka_chunk_freelist;
    ka_chunk_freelist = chunk;
}

static void *ka_meta_alloc(size_t size) {
    const size_t aligned = (size_t)align_up(size, sizeof(uint64_t));
    void *result;

    if (aligned == 0u) {
        return NULL;
    }

    if (aligned > ka_PAGE_SIZE) {
        const uint64_t pages = pages_for_bytes(aligned);
        void *physical = pmm_alloc_contiguous_pages(pages);
        uint8_t *virt = phys_to_hhdm(physical);

        if (virt == NULL) {
            return NULL;
        }

        zero_bytes(virt, (size_t)(pages * ka_PAGE_SIZE));
        ka_meta_pages += pages;
        return virt;
    }

    if (ka_meta_cursor == NULL || ka_meta_cursor + aligned > ka_meta_end) {
        void *page = pmm_alloc_page();
        uint8_t *virt = phys_to_hhdm(page);

        if (virt == NULL) {
            return NULL;
        }

        zero_bytes(virt, ka_PAGE_SIZE);
        ++ka_meta_pages;
        ka_meta_cursor = virt;
        ka_meta_end = virt + ka_PAGE_SIZE;
    }

    result = ka_meta_cursor;
    ka_meta_cursor += aligned;
    return result;
}

static ka_alloc_chunk_t *ka_chunk_descriptor_alloc(uint64_t pages) {
    ka_alloc_chunk_t **link = &ka_chunk_freelist;
    ka_alloc_chunk_t *chunk;
    uint64_t virt;
    while (*link != NULL && (*link)->page_count < pages) link = &(*link)->next;
    chunk = *link;
    if (chunk != NULL) {
        if (chunk->page_count > pages) {
            ka_alloc_chunk_t *tail = ka_unused_chunk_descriptors;
            if (tail) ka_unused_chunk_descriptors = tail->next;
            else tail = ka_meta_alloc(sizeof(*tail));
            if (!tail) return NULL; /* Keep the original range on OOM. */
            zero_bytes(tail, sizeof(*tail));
            tail->base = chunk->base + pages * ka_PAGE_SIZE;
            tail->page_count = chunk->page_count - pages;
            tail->size = (size_t)(tail->page_count * ka_PAGE_SIZE);
            tail->next = chunk->next;
            chunk->next = tail;
        }
        *link = chunk->next;
        virt = (uint64_t)chunk->base;
    } else {
        chunk = ka_unused_chunk_descriptors;
        if (chunk) ka_unused_chunk_descriptors = chunk->next;
        else chunk = ka_meta_alloc(sizeof(*chunk));
        if (chunk == NULL) return NULL;
        virt = reserve_chunk_virtual_base(pages);
        if (virt == 0u) return NULL;
    }
    zero_bytes(chunk, sizeof(*chunk));
    chunk->base = (uint8_t *)virt;
    chunk->size = (size_t)(pages * ka_PAGE_SIZE);
    chunk->page_count = pages;
    return chunk;
}

static ka_chunk_page_t *ka_page_descriptor_alloc(void) {
    ka_chunk_page_t *page = ka_page_freelist;
    if (page != NULL) ka_page_freelist = page->next;
    else page = ka_meta_alloc(sizeof(*page));
    if (page != NULL) zero_bytes(page, sizeof(*page));
    return page;
}









static size_t ka_hash_bucket(uint64_t address) {
    return (size_t)((address >> 12u) & (ka_HASH_BUCKETS - 1u));
}

static ka_hash_entry_t *ka_hash_entry_alloc(void) {
    ka_hash_entry_t *entry;

    if (ka_hash_entry_freelist != NULL) {
        entry = ka_hash_entry_freelist;
        ka_hash_entry_freelist = entry->next;
        entry->address = 0u;
        entry->value = NULL;
        entry->next = NULL;
        return entry;
    }

    entry = ka_meta_alloc(sizeof(ka_hash_entry_t));
    if (entry != NULL) {
        zero_bytes(entry, sizeof(*entry));
    }
    return entry;
}

static void ka_hash_entry_release(ka_hash_entry_t *entry) {
    if (entry == NULL) {
        return;
    }

    entry->address = 0u;
    entry->value = NULL;
    entry->next = ka_hash_entry_freelist;
    ka_hash_entry_freelist = entry;
}

static void ka_hash_insert(ka_hash_entry_t **table, uint64_t address, void *value) {
    const size_t bucket = ka_hash_bucket(address);
    ka_hash_entry_t *entry;

    for (entry = table[bucket]; entry != NULL; entry = entry->next) {
        if (entry->address == address) {
            entry->value = value;
            return;
        }
    }

    entry = ka_hash_entry_alloc();
    if (entry == NULL) {
        return;
    }

    entry->address = address;
    entry->value = value;
    entry->next = table[bucket];
    table[bucket] = entry;
}

static void *ka_hash_find(ka_hash_entry_t **table, uint64_t address) {
    for (ka_hash_entry_t *entry = table[ka_hash_bucket(address)]; entry != NULL; entry = entry->next) {
        if (entry->address == address) {
            return entry->value;
        }
    }

    return NULL;
}

static void ka_hash_remove(ka_hash_entry_t **table, uint64_t address) {
    const size_t bucket = ka_hash_bucket(address);
    ka_hash_entry_t *previous = NULL;
    ka_hash_entry_t *entry = table[bucket];

    while (entry != NULL) {
        if (entry->address == address) {
            if (previous == NULL) {
                table[bucket] = entry->next;
            } else {
                previous->next = entry->next;
            }
            ka_hash_entry_release(entry);
            return;
        }

        previous = entry;
        entry = entry->next;
    }
}

static void ka_chunk_hash_insert(ka_alloc_chunk_t *chunk) {
    if (chunk == NULL || chunk->base == NULL) {
        return;
    }

    for (uint64_t page_index = 0u; page_index < chunk->page_count; ++page_index) {
        ka_hash_insert(ka_chunk_hash, (uint64_t)chunk->base + (page_index * ka_PAGE_SIZE), chunk);
    }
}

static void ka_chunk_hash_remove(ka_alloc_chunk_t *chunk) {
    if (chunk == NULL || chunk->base == NULL) {
        return;
    }

    for (uint64_t page_index = 0u; page_index < chunk->page_count; ++page_index) {
        ka_hash_remove(ka_chunk_hash, (uint64_t)chunk->base + (page_index * ka_PAGE_SIZE));
    }
}













static size_t chunk_payload_offset(const ka_alloc_chunk_t *chunk) {
    if (chunk == NULL || chunk->payload_offset == 0u) {
        return sizeof(ka_heap_block_t);
    }
    return chunk->payload_offset;
}

static size_t block_payload_capacity_in_chunk(const ka_alloc_chunk_t *chunk, const ka_heap_block_t *block) {
    if (block == NULL) {
        return 0u;
    }
    if ((uint8_t *)block == chunk->base) {
        return block->size - chunk_payload_offset(chunk);
    }
    return block->size - sizeof(ka_heap_block_t);
}

static int block_is_sane(const ka_alloc_chunk_t *chunk, const ka_heap_block_t *block) {
    const uint8_t *base = chunk == NULL ? NULL : chunk->base;
    const uint8_t *end = chunk == NULL ? NULL : chunk->base + chunk->size;
    const uint8_t *ptr = (const uint8_t *)block;

    if (chunk == NULL || block == NULL || base == NULL || end == NULL) {
        return 0;
    }
    if (ptr < base || ptr + sizeof(ka_heap_block_t) > end) {
        return 0;
    }
    if ((((uintptr_t)ptr) & (ka_ALLOC_ALIGNMENT - 1ull)) != 0u) {
        return 0;
    }
    if (block->size < sizeof(ka_heap_block_t) || (block->size & (ka_ALLOC_ALIGNMENT - 1ull)) != 0u) {
        return 0;
    }
    if (ptr + block->size > end) {
        return 0;
    }

    return 1;
}

static ka_heap_block_t *next_block_in_chunk(const ka_alloc_chunk_t *chunk, ka_heap_block_t *block) {
    if (!block_is_sane(chunk, block)) {
        return NULL;
    }

    uint8_t *next = (uint8_t *)block + block->size;

    if (chunk == NULL || next >= chunk->base + chunk->size) {
        return NULL;
    }

    return (ka_heap_block_t *)next;
}

static void split_block(ka_heap_block_t *block, size_t requested_payload_size) {
    const size_t required_size = sizeof(ka_heap_block_t) + requested_payload_size;

    if (block->size < required_size + sizeof(ka_heap_block_t) + ka_ALLOC_ALIGNMENT) {
        return;
    }

    {
        ka_heap_block_t *remainder = (ka_heap_block_t *)((uint8_t *)block + required_size);
        remainder->size = block->size - required_size;
        remainder->pid = block->pid;
        remainder->free = 1u;
        block->size = required_size;
    }
}

static void coalesce_chunk(ka_alloc_chunk_t *chunk) {
    ka_heap_block_t *block = (ka_heap_block_t *)chunk->base;

    while (block != NULL) {
        ka_heap_block_t *next = next_block_in_chunk(chunk, block);

        if (next != NULL && block->free != 0u && next->free != 0u) {
            block->size += next->size;
            continue;
        }

        block = next;
    }
}

static int chunk_all_free(ka_alloc_chunk_t *chunk) {
    if (chunk == NULL) {
        return 0;
    }

    coalesce_chunk(chunk);
    {
        ka_heap_block_t *block = (ka_heap_block_t *)chunk->base;
        return block != NULL && block->free != 0u && block->size == chunk->size;
    }
}

static void release_free_chunks(ka_pid_arena_t *arena) {
    ka_alloc_chunk_t *previous = NULL;
    ka_alloc_chunk_t *chunk;

    if (arena == NULL || hhdm_request.response == NULL) {
        return;
    }

    chunk = arena->chunks;
    while (chunk != NULL) {
        ka_alloc_chunk_t *next = chunk->next;
        if (chunk_all_free(chunk)) {
            if (previous == NULL) {
                arena->chunks = next;
            } else {
                previous->next = next;
            }
            ka_chunk_hash_remove(chunk);
            arena->resident_pages-=chunk->page_count;
            release_chunk_backing(chunk);
            chunk = next;
            continue;
        }

        previous = chunk;
        chunk = next;
    }
}

static ka_pid_arena_t *find_arena_for_chunk(ka_alloc_chunk_t *target, ka_alloc_chunk_t **previous_out) {
    if (previous_out != NULL) {
        *previous_out = NULL;
    }
    if (target == NULL) {
        return NULL;
    }

    for (ka_pid_arena_t *arena = ka_arenas; arena != NULL; arena = arena->next) {
        ka_alloc_chunk_t *previous = NULL;
        for (ka_alloc_chunk_t *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
            if (chunk == target) {
                if (previous_out != NULL) {
                    *previous_out = previous;
                }
                return arena;
            }
            previous = chunk;
        }
    }

    return NULL;
}

static void release_chunk_from_arena(ka_pid_arena_t *arena, ka_alloc_chunk_t *chunk, ka_alloc_chunk_t *previous) {
    if (arena == NULL || chunk == NULL) {
        return;
    }
    if (arena->free_chunks != 0u) {
        --arena->free_chunks;
    }

    if (previous == NULL) {
        if (arena->chunks != chunk) {
            return;
        }
        arena->chunks = chunk->next;
    } else {
        if (previous->next != chunk) {
            return;
        }
        previous->next = chunk->next;
    }

    ka_chunk_hash_remove(chunk);
    arena->resident_pages-=chunk->page_count;
    release_chunk_backing(chunk);
}

static ka_alloc_chunk_t *new_chunk(uint32_t pid, size_t minimum_payload) {
    const uint64_t pages = pages_for_bytes(sizeof(ka_heap_block_t) + minimum_payload) > ka_CHUNK_MIN_PAGES
        ? pages_for_bytes(sizeof(ka_heap_block_t) + minimum_payload)
        : ka_CHUNK_MIN_PAGES;
    ka_alloc_chunk_t *chunk;
    ka_heap_block_t *initial;
    uint64_t virt;
    ka_chunk_page_t **tail;

    chunk = ka_chunk_descriptor_alloc(pages);
    if (chunk == NULL) {
        return NULL;
    }

    virt = (uint64_t)chunk->base;
    chunk->payload_offset = sizeof(ka_heap_block_t);
    chunk->page_count = pages;
    tail = &chunk->pages;

    for (uint64_t page_index = 0u; page_index < pages; ++page_index) {
        void *physical = pmm_alloc_page();
        ka_chunk_page_t *page;

        if (physical == NULL) {
            release_chunk_backing(chunk);
            return NULL;
        }

        page = ka_page_descriptor_alloc();
        if (page == NULL) {
            pmm_free_page(physical);
            release_chunk_backing(chunk);
            return NULL;
        }

        page->phys = (uint64_t)physical;
        page->next = NULL;
        *tail = page;
        tail = &page->next;

        scheduler_map_kernel_page_all(virt + (page_index * ka_PAGE_SIZE), (uint64_t)physical, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
        zero_bytes((void *)(virt + (page_index * ka_PAGE_SIZE)), ka_PAGE_SIZE);
    }

    initial = (ka_heap_block_t *)chunk->base;
    initial->size = chunk->size;
    initial->pid = pid;
    initial->free = 1u;
    ka_chunk_hash_insert(chunk);
    return chunk;
}

static ka_alloc_chunk_t *new_dedicated_chunk(uint32_t pid, size_t payload_size) {
    const uint64_t pages = pages_for_bytes(ka_PAGE_SIZE + payload_size);
    ka_alloc_chunk_t *chunk;
    ka_heap_block_t *initial;
    uint64_t virt;
    ka_chunk_page_t **tail;

    chunk = ka_chunk_descriptor_alloc(pages);
    if (chunk == NULL) {
        return NULL;
    }

    virt = (uint64_t)chunk->base;
    chunk->payload_offset = ka_PAGE_SIZE;
    chunk->page_count = pages;
    tail = &chunk->pages;

    for (uint64_t page_index = 0u; page_index < pages; ++page_index) {
        void *physical = pmm_alloc_page();
        ka_chunk_page_t *page;

        if (physical == NULL) {
            release_chunk_backing(chunk);
            return NULL;
        }

        page = ka_page_descriptor_alloc();
        if (page == NULL) {
            pmm_free_page(physical);
            release_chunk_backing(chunk);
            return NULL;
        }

        page->phys = (uint64_t)physical;
        page->next = NULL;
        *tail = page;
        tail = &page->next;

        scheduler_map_kernel_page_all(virt + (page_index * ka_PAGE_SIZE), (uint64_t)physical, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
        zero_bytes((void *)(virt + (page_index * ka_PAGE_SIZE)), ka_PAGE_SIZE);
    }

    initial = (ka_heap_block_t *)chunk->base;
    initial->size = chunk->size;
    initial->pid = pid;
    initial->free = 0u;
    ka_chunk_hash_insert(chunk);
    return chunk;
}

static ka_pid_arena_t *find_arena(uint32_t pid) {
    for (ka_pid_arena_t *arena = ka_arenas; arena != NULL; arena = arena->next) {
        if (arena->pid == pid) {
            return arena;
        }
    }

    return NULL;
}

static ka_pid_arena_t *ensure_arena(uint32_t pid) {
    ka_pid_arena_t *arena = find_arena(pid);

    if (arena != NULL) {
        return arena;
    }

    arena=ka_arena_freelist;
    if(arena)ka_arena_freelist=arena->next;
    else arena = ka_meta_alloc(sizeof(ka_pid_arena_t));
    if (arena == NULL) {
        return NULL;
    }

    arena->pid = pid;
    arena->free_chunks = 0u;arena->retired=0;
    arena->resident_pages=arena->peak_pages=0;
    arena->chunks = NULL;
    arena->next = ka_arenas;
    ka_arenas = arena;
    return arena;
}

static void recycle_empty_arena(ka_pid_arena_t *arena) {
    if(!arena || arena->chunks)return;
    ka_pid_arena_t **link=&ka_arenas;
    while(*link && *link!=arena)link=&(*link)->next;
    if(*link){*link=arena->next;arena->next=ka_arena_freelist;ka_arena_freelist=arena;}
}

static ka_alloc_chunk_t *find_chunk_for_address(const void *ptr) {
    const uint8_t *address = ptr;
    ka_alloc_chunk_t *chunk;

    if (ptr == NULL) {
        return NULL;
    }

    chunk = ka_hash_find(ka_chunk_hash, (uint64_t)(uintptr_t)address & ~(ka_PAGE_SIZE - 1ull));
    if (chunk != NULL && address >= chunk->base && address < chunk->base + chunk->size) {
        return chunk;
    }

    for (ka_pid_arena_t *arena = ka_arenas; arena != NULL; arena = arena->next) {
        for (ka_alloc_chunk_t *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
            if (address >= chunk->base && address < chunk->base + chunk->size) {
                ka_hash_insert(ka_chunk_hash, (uint64_t)(uintptr_t)address & ~(ka_PAGE_SIZE - 1ull), chunk);
                return chunk;
            }
        }
    }

    return NULL;
}

static ka_chunk_page_t *chunk_page_at(ka_alloc_chunk_t *chunk, uint64_t page_index) {
    ka_chunk_page_t *page;

    if (chunk == NULL || page_index >= chunk->page_count) {
        return NULL;
    }

    page = chunk->pages;
    for (uint64_t index = 0u; page != NULL && index < page_index; ++index) {
        page = page->next;
    }
    return page;
}

static ka_heap_block_t *find_free_block(ka_pid_arena_t *arena, size_t aligned_size) {
    for (ka_alloc_chunk_t *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
        /* Dedicated allocations have a page-aligned payload and a different
           header extent. They cannot enter the 64-byte block splitting path. */
        if(chunk->payload_offset!=sizeof(ka_heap_block_t))continue;
        for (ka_heap_block_t *block = (ka_heap_block_t *)chunk->base;
             block != NULL;
             block = next_block_in_chunk(chunk, block)) {
            if (!block_is_sane(chunk, block)) {
                break;
            }
            if (block->free != 0u && block_payload_capacity_in_chunk(chunk, block) >= aligned_size) {
                return block;
            }
        }
    }

    return NULL;
}

static ka_heap_block_t *find_block_header(void *ptr, ka_alloc_chunk_t **chunk_out) {
    ka_heap_block_t *cached_block = ka_hash_find(ka_block_hash, (uint64_t)(uintptr_t)ptr);

    if (cached_block != NULL) {
        ka_alloc_chunk_t *cached_chunk = find_chunk_for_address(ptr);
        if (cached_chunk != NULL) {
            const size_t cached_payload_offset = (uint8_t *)cached_block == cached_chunk->base ? chunk_payload_offset(cached_chunk) : sizeof(ka_heap_block_t);
            if ((uint8_t *)cached_block + cached_payload_offset == (uint8_t *)ptr) {
                if (chunk_out != NULL) {
                    *chunk_out = cached_chunk;
                }
                return cached_block;
            }
        }
    }

    for (ka_pid_arena_t *arena = ka_arenas; arena != NULL; arena = arena->next) {
        for (ka_alloc_chunk_t *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
            for (ka_heap_block_t *block = (ka_heap_block_t *)chunk->base;
                 block != NULL;
                 block = next_block_in_chunk(chunk, block)) {
                if (!block_is_sane(chunk, block)) {
                    break;
                }
                const size_t payload_offset = (uint8_t *)block == chunk->base ? chunk_payload_offset(chunk) : sizeof(ka_heap_block_t);
                if ((uint8_t *)block + payload_offset == (uint8_t *)ptr) {
                    ka_hash_insert(ka_block_hash, (uint64_t)(uintptr_t)ptr, block);
                    if (chunk_out != NULL) {
                        *chunk_out = chunk;
                    }
                    return block;
                }
            }
        }
    }

    return NULL;
}

static void *ka_raw_alloc(uint32_t pid, size_t size) {
    ka_pid_arena_t *arena;
    ka_heap_block_t *block;
    ka_alloc_chunk_t *block_chunk = NULL;
    const size_t aligned = (size_t)align_up(size, ka_ALLOC_ALIGNMENT);
    int chunk_was_all_free = 0;

    if (!ka_primary_allocator_ready || aligned == 0u) {
        return NULL;
    }

    arena = ensure_arena(pid);
    if (arena == NULL) {
        return NULL;
    }

    if (aligned >= ka_PAGE_SIZE) {
        ka_alloc_chunk_t *chunk = new_dedicated_chunk(pid, aligned);
        void *payload;

        if (chunk == NULL) {
            return NULL;
        }

        chunk->next = arena->chunks;
        arena->chunks = chunk;
        arena->resident_pages+=chunk->page_count;if(arena->resident_pages>arena->peak_pages)arena->peak_pages=arena->resident_pages;
        payload = (uint8_t *)chunk->base + chunk_payload_offset(chunk);
        ka_hash_insert(ka_block_hash, (uint64_t)(uintptr_t)payload, chunk->base);
        return payload;
    }

    block = find_free_block(arena, aligned);
    if (block == NULL) {
        ka_alloc_chunk_t *chunk = new_chunk(pid, aligned);
        if (chunk == NULL) {
            return NULL;
        }

        chunk->next = arena->chunks;
        arena->chunks = chunk;
        arena->resident_pages+=chunk->page_count;if(arena->resident_pages>arena->peak_pages)arena->peak_pages=arena->resident_pages;
        ++arena->free_chunks;
        block = (ka_heap_block_t *)chunk->base;
        block_chunk = chunk;
        chunk_was_all_free = 1;
    } else {
        block_chunk = find_chunk_for_address((uint8_t *)block + sizeof(ka_heap_block_t));
        if (block_chunk != NULL && chunk_all_free(block_chunk)) {
            chunk_was_all_free = 1;
        }
    }

    split_block(block, aligned);
    block->pid = pid;
    block->free = 0u;
    if (chunk_was_all_free && arena->free_chunks != 0u) {
        --arena->free_chunks;
    }
    {
        void *payload = (uint8_t *)block + sizeof(ka_heap_block_t);
        ka_hash_insert(ka_block_hash, (uint64_t)(uintptr_t)payload, block);
        return payload;
    }
}

static void ka_raw_free(void *ptr) {
    ka_alloc_chunk_t *chunk = NULL;
    ka_alloc_chunk_t *previous = NULL;
    ka_pid_arena_t *arena;
    ka_heap_block_t *block;

    if (!ka_primary_allocator_ready || ptr == NULL) {
        return;
    }

    block = find_block_header(ptr, &chunk);
    if (block == NULL || block->free != 0u) {
        return;
    }

    ka_hash_remove(ka_block_hash, (uint64_t)(uintptr_t)ptr);
    block->free = 1u;
    if (chunk != NULL) {
        coalesce_chunk(chunk);
        if (chunk_all_free(chunk)) {
            arena = find_arena_for_chunk(chunk, &previous);
            if (arena != NULL) {
                ++arena->free_chunks;
            }
            if (arena != NULL && (chunk->payload_offset!=sizeof(ka_heap_block_t) ||
                                  arena->retired || arena->free_chunks > 1u)) {
                release_chunk_from_arena(arena, chunk, previous);
                if(arena->retired)recycle_empty_arena(arena);
            }
        }
    }
}

static void *ka_raw_realloc(uint32_t pid, void *ptr, size_t size) {
    ka_alloc_chunk_t *chunk = NULL;
    ka_heap_block_t *block;
    size_t old_size;
    void *new_ptr;

    if (ptr == NULL) {
        return ka_raw_alloc(pid, size);
    }

    if (size == 0u) {
        ka_raw_free(ptr);
        return NULL;
    }

    block = find_block_header(ptr, &chunk);
    if (block == NULL || block->free != 0u) {
        return NULL;
    }

    old_size = block_payload_capacity_in_chunk(chunk, block);
    if (old_size >= size) {
        if(chunk->payload_offset==sizeof(ka_heap_block_t))
            split_block(block, (size_t)align_up(size, ka_ALLOC_ALIGNMENT));
        return ptr;
    }

    new_ptr = ka_raw_alloc(pid, size);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_size);
    ka_raw_free(ptr);
    return new_ptr;
}


static uint8_t allocator_lock_word;
static uint64_t allocator_lock(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    while (__atomic_test_and_set(&allocator_lock_word, __ATOMIC_ACQUIRE))
        smp_relax();
    return flags;
}
static void allocator_unlock(uint64_t flags) {
    __atomic_clear(&allocator_lock_word, __ATOMIC_RELEASE);
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}
void kernel_allocator_init(void) {
    ka_primary_allocator_ready = false;
    ka_arenas = NULL;ka_arena_freelist=NULL;
    ka_meta_cursor = ka_meta_end = NULL;
    ka_chunk_freelist = NULL;
    ka_unused_chunk_descriptors = NULL;
    ka_page_freelist = NULL;
    ka_chunk_virt_cursor = ka_CHUNK_VIRT_BASE;
    ka_primary_allocator_ready = hhdm_request.response != NULL;
}
int kernel_allocator_active(void) { return ka_primary_allocator_ready; }
void *kernel_allocator_alloc(uint32_t owner, size_t size) {
    uint64_t flags = allocator_lock();
    void *ptr = ka_raw_alloc(owner, size);
    static unsigned failures;
    if (!ptr && failures++ < 4u) {
        char line[160];
        snprintf(line, sizeof(line), "kernel-allocator: allocation failed size=%u cursor-low=%x free-pages=%u\n",
            (unsigned)size, (unsigned)ka_chunk_virt_cursor, (unsigned)pmm_free_page_count());
        debug_log_puts(line);
    }
    allocator_unlock(flags);
    return ptr;
}
void kernel_allocator_free(void *ptr) {
    uint64_t flags = allocator_lock();
    ka_raw_free(ptr);
    allocator_unlock(flags);
}
void *kernel_allocator_realloc(uint32_t owner, void *ptr, size_t size) {
    uint64_t flags = allocator_lock();
    void *result = ka_raw_realloc(owner, ptr, size);
    allocator_unlock(flags);
    return result;
}
void kernel_allocator_reap(uint32_t owner) {
    uint64_t flags = allocator_lock();
    ka_pid_arena_t *arena = find_arena(owner);
    if (arena){arena->retired=1;release_free_chunks(arena);recycle_empty_arena(arena);}
    allocator_unlock(flags);
}

static int ka_can_project(const void *data, size_t size) {
    ka_alloc_chunk_t *chunk;
    const uintptr_t aligned_address = (uintptr_t)data;
    const uintptr_t address = (uintptr_t)data;
    const size_t mapped_size = (size_t)align_up(size, ka_PAGE_SIZE);
    uintptr_t mapped_end;
    uintptr_t chunk_end;

    if (!ka_primary_allocator_ready || data == NULL || size == 0u) {
        return 0;
    }
    if (mapped_size < size) {
        return 0;
    }
    if ((aligned_address & (ka_PAGE_SIZE - 1ull)) != 0u) {
        return 0;
    }

    chunk = find_chunk_for_address(data);
    if (chunk == NULL) {
        return 0;
    }
    mapped_end = address + mapped_size;
    chunk_end = (uintptr_t)chunk->base + chunk->size;
    if (mapped_end < address || mapped_end > chunk_end) {
        return 0;
    }
    return 1;
}

static int ka_physical_page(const void *data, size_t offset, uint64_t *phys_out) {
    ka_alloc_chunk_t *chunk;
    ka_chunk_page_t *page;
    const uint8_t *address = (const uint8_t *)data + offset;
    uint64_t page_index;

    if (phys_out == NULL || offset > (SIZE_MAX - ka_PAGE_SIZE) || !ka_can_project(data, offset + ka_PAGE_SIZE)) {
        return -1;
    }
    if (((uintptr_t)address & (ka_PAGE_SIZE - 1ull)) != 0u) {
        return -1;
    }

    chunk = find_chunk_for_address(address);
    if (chunk == NULL) {
        return -1;
    }

    page_index = (uint64_t)(address - chunk->base) / ka_PAGE_SIZE;
    page = chunk_page_at(chunk, page_index);
    if (page == NULL) {
        return -1;
    }

    *phys_out = page->phys;
    return 0;
}

void kernel_allocator_memory_diagnostic(void) {
    uint64_t pages=0,live=0,blocks=0;
    uint64_t flags=allocator_lock();
    for (ka_pid_arena_t *a=ka_arenas;a;a=a->next)
        for (ka_alloc_chunk_t *c=a->chunks;c;c=c->next) {
            pages+=c->page_count;
            for(ka_heap_block_t *b=(void *)c->base;b;b=next_block_in_chunk(c,b)) {
                if(!block_is_sane(c,b))break;
                if(!b->free){live+=b->size;++blocks;}
            }
        }
    uint64_t meta=ka_meta_pages;
    allocator_unlock(flags);
    char line[192];
    snprintf(line,sizeof(line),"memory-category: kernel-allocator pages=%u live=%u blocks=%u meta=%u\n",
        (unsigned)pages,(unsigned)live,(unsigned)blocks,(unsigned)meta);
    debug_log_puts(line);
}
void kernel_allocator_usage(uint32_t owner,uint64_t *total,uint64_t *owned,uint64_t *peak_owned){
    uint64_t flags=allocator_lock();*total=ka_meta_pages*4096;*owned=*peak_owned=0;
    for(ka_pid_arena_t *a=ka_arenas;a;a=a->next){*total+=a->resident_pages*4096;
        if(a->pid==owner){*owned=a->resident_pages*4096;*peak_owned=a->peak_pages*4096;}}
    allocator_unlock(flags);
}

int kernel_allocator_can_project(const void *data,size_t size) {
    uint64_t flags=allocator_lock();int result=ka_can_project(data,size);allocator_unlock(flags);return result;
}
int kernel_allocator_physical_page(const void *data,size_t offset,uint64_t *out) {
    uint64_t flags=allocator_lock();int result=ka_physical_page(data,offset,out);allocator_unlock(flags);return result;
}
