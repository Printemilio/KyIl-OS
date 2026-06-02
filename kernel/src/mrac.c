#include <unos/mrac.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/boot_requests.h>
#include <unos/pmm.h>
#include <unos/scheduler.h>
#include <unos/vmm.h>

#define MRAC_INDEX_BUCKET_COUNT 1024ull
#define MRAC_OBJECT_BUCKET_COUNT 1024ull
#define MRAC_HASH_BUCKETS 256u
#define MRAC_ALLOC_ALIGNMENT 64ull
#define MRAC_CHUNK_MIN_PAGES 4ull
#define MRAC_PAGE_SIZE 4096ull
#define MRAC_CHUNK_VIRT_BASE 0xffffffffc0000000ull
#define MRAC_CHUNK_VIRT_LIMIT 0xfffffffff0000000ull
#define MRAC_PTR_LOW48_MASK 0x0000ffffffffffffull
#define FNV1A_OFFSET_BASIS 14695981039346656037ull
#define FNV1A_PRIME 1099511628211ull

typedef uintptr_t MRAC_tagged_ptr_t;

typedef struct MRAC_heap_block MRAC_heap_block_t;
typedef struct MRAC_alloc_chunk MRAC_alloc_chunk_t;
typedef struct MRAC_chunk_page MRAC_chunk_page_t;
typedef struct MRAC_pid_arena MRAC_pid_arena_t;
typedef struct MRAC_object MRAC_object_t;
typedef struct MRAC_collision MRAC_collision_t;
typedef struct MRAC_version_ref MRAC_version_ref_t;
typedef struct MRAC_hash_entry MRAC_hash_entry_t;

struct MRAC_heap_block {
    size_t size;
    uint32_t pid;
    uint8_t free;
    uint8_t reserved[51];
};

_Static_assert(sizeof(MRAC_heap_block_t) == MRAC_ALLOC_ALIGNMENT, "MRAC heap blocks must preserve 64-byte cache-line alignment");

struct MRAC_alloc_chunk {
    uint8_t *base;
    size_t size;
    size_t payload_offset;
    uint64_t page_count;
    MRAC_chunk_page_t *pages;
    MRAC_alloc_chunk_t *next;
};

struct MRAC_chunk_page {
    uint64_t phys;
    MRAC_chunk_page_t *next;
};

struct MRAC_pid_arena {
    uint32_t pid;
    uint32_t free_chunks;
    MRAC_alloc_chunk_t *chunks;
    MRAC_pid_arena_t *next;
};

struct MRAC_object {
    uint64_t content_hash;
    void *data;
    size_t size;
    uint32_t refcount;
    MRAC_tagged_ptr_t next_tagged;
};

struct MRAC_collision {
    MRAC_object_t base;
    uint8_t tag;
};

struct MRAC_version_ref {
    uint32_t version;
    uint32_t borrows;
    uint8_t owner_present;
    uint8_t reserved[3];
    MRAC_object_t *object;
    MRAC_version_ref_t *older;
};

struct MRAC_hash_entry {
    uint64_t address;
    void *value;
    MRAC_hash_entry_t *next;
};

struct MRAC_entry {
    uint32_t pid;
    uint64_t key_hash;
    MRAC_version_ref_t *latest_version;
    MRAC_entry_t *next;
};

static MRAC_entry_t **MRAC_index_table;
static MRAC_object_t **MRAC_object_table;
static MRAC_pid_arena_t *MRAC_arenas;
static MRAC_hash_entry_t *MRAC_block_hash[MRAC_HASH_BUCKETS];
static MRAC_hash_entry_t *MRAC_chunk_hash[MRAC_HASH_BUCKETS];
static MRAC_hash_entry_t *MRAC_hash_entry_freelist;
static uint8_t *MRAC_meta_cursor;
static uint8_t *MRAC_meta_end;
static uint64_t MRAC_chunk_virt_cursor;
static bool MRAC_ready;
static bool MRAC_primary_allocator_ready;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

static uint64_t pages_for_bytes(uint64_t byte_count) {
    return (byte_count + MRAC_PAGE_SIZE - 1ull) / MRAC_PAGE_SIZE;
}

static void *phys_to_hhdm(void *phys) {
    if (phys == NULL || hhdm_request.response == NULL) {
        return NULL;
    }

    return (void *)(hhdm_request.response->offset + (uint64_t)phys);
}

static void zero_bytes(void *ptr, size_t size) {
    uint8_t *bytes = ptr;

    for (size_t i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

static uint64_t reserve_chunk_virtual_base(uint64_t page_count) {
    const uint64_t size = page_count * MRAC_PAGE_SIZE;
    const uint64_t base = align_up(MRAC_chunk_virt_cursor, MRAC_PAGE_SIZE);
    const uint64_t end = base + size;

    if (page_count == 0u || size / MRAC_PAGE_SIZE != page_count) {
        return 0u;
    }
    if (end < base || end > MRAC_CHUNK_VIRT_LIMIT) {
        return 0u;
    }

    MRAC_chunk_virt_cursor = end;
    return base;
}

static void release_chunk_backing(MRAC_alloc_chunk_t *chunk) {
    uint64_t virt;

    if (chunk == NULL || chunk->base == NULL) {
        return;
    }

    virt = (uint64_t)chunk->base;
    for (MRAC_chunk_page_t *page = chunk->pages; page != NULL; page = page->next, virt += MRAC_PAGE_SIZE) {
        scheduler_unmap_kernel_page_all(virt);
        vmm_unmap_kernel_page(virt);
        pmm_free_page((void *)page->phys);
    }
}

static void *MRAC_meta_alloc(size_t size) {
    const size_t aligned = (size_t)align_up(size, sizeof(uint64_t));
    void *result;

    if (aligned == 0u) {
        return NULL;
    }

    if (aligned > MRAC_PAGE_SIZE) {
        const uint64_t pages = pages_for_bytes(aligned);
        void *physical = pmm_alloc_contiguous_pages(pages);
        uint8_t *virt = phys_to_hhdm(physical);

        if (virt == NULL) {
            return NULL;
        }

        zero_bytes(virt, (size_t)(pages * MRAC_PAGE_SIZE));
        return virt;
    }

    if (MRAC_meta_cursor == NULL || MRAC_meta_cursor + aligned > MRAC_meta_end) {
        void *page = pmm_alloc_page();
        uint8_t *virt = phys_to_hhdm(page);

        if (virt == NULL) {
            return NULL;
        }

        zero_bytes(virt, MRAC_PAGE_SIZE);
        MRAC_meta_cursor = virt;
        MRAC_meta_end = virt + MRAC_PAGE_SIZE;
    }

    result = MRAC_meta_cursor;
    MRAC_meta_cursor += aligned;
    return result;
}

static uint64_t fnv1a64(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint64_t hash = FNV1A_OFFSET_BASIS;

    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= FNV1A_PRIME;
    }

    return hash;
}

static uint64_t fnv1a64_zero_extended(const void *data, size_t data_size, size_t total_size) {
    const uint8_t *bytes = data;
    uint64_t hash = FNV1A_OFFSET_BASIS;

    for (size_t i = 0; i < data_size; ++i) {
        hash ^= bytes[i];
        hash *= FNV1A_PRIME;
    }
    for (size_t i = data_size; i < total_size; ++i) {
        hash ^= 0u;
        hash *= FNV1A_PRIME;
    }

    return hash;
}

static size_t index_bucket(uint64_t hash) {
    return hash % MRAC_INDEX_BUCKET_COUNT;
}

static size_t object_bucket(uint64_t hash) {
    return hash % MRAC_OBJECT_BUCKET_COUNT;
}

static size_t MRAC_hash_bucket(uint64_t address) {
    return (size_t)((address >> 12u) & (MRAC_HASH_BUCKETS - 1u));
}

static MRAC_hash_entry_t *MRAC_hash_entry_alloc(void) {
    MRAC_hash_entry_t *entry;

    if (MRAC_hash_entry_freelist != NULL) {
        entry = MRAC_hash_entry_freelist;
        MRAC_hash_entry_freelist = entry->next;
        entry->address = 0u;
        entry->value = NULL;
        entry->next = NULL;
        return entry;
    }

    entry = MRAC_meta_alloc(sizeof(MRAC_hash_entry_t));
    if (entry != NULL) {
        zero_bytes(entry, sizeof(*entry));
    }
    return entry;
}

static void MRAC_hash_entry_release(MRAC_hash_entry_t *entry) {
    if (entry == NULL) {
        return;
    }

    entry->address = 0u;
    entry->value = NULL;
    entry->next = MRAC_hash_entry_freelist;
    MRAC_hash_entry_freelist = entry;
}

static void MRAC_hash_insert(MRAC_hash_entry_t **table, uint64_t address, void *value) {
    const size_t bucket = MRAC_hash_bucket(address);
    MRAC_hash_entry_t *entry;

    for (entry = table[bucket]; entry != NULL; entry = entry->next) {
        if (entry->address == address) {
            entry->value = value;
            return;
        }
    }

    entry = MRAC_hash_entry_alloc();
    if (entry == NULL) {
        return;
    }

    entry->address = address;
    entry->value = value;
    entry->next = table[bucket];
    table[bucket] = entry;
}

static void *MRAC_hash_find(MRAC_hash_entry_t **table, uint64_t address) {
    for (MRAC_hash_entry_t *entry = table[MRAC_hash_bucket(address)]; entry != NULL; entry = entry->next) {
        if (entry->address == address) {
            return entry->value;
        }
    }

    return NULL;
}

static void MRAC_hash_remove(MRAC_hash_entry_t **table, uint64_t address) {
    const size_t bucket = MRAC_hash_bucket(address);
    MRAC_hash_entry_t *previous = NULL;
    MRAC_hash_entry_t *entry = table[bucket];

    while (entry != NULL) {
        if (entry->address == address) {
            if (previous == NULL) {
                table[bucket] = entry->next;
            } else {
                previous->next = entry->next;
            }
            MRAC_hash_entry_release(entry);
            return;
        }

        previous = entry;
        entry = entry->next;
    }
}

static void MRAC_chunk_hash_insert(MRAC_alloc_chunk_t *chunk) {
    if (chunk == NULL || chunk->base == NULL) {
        return;
    }

    for (uint64_t page_index = 0u; page_index < chunk->page_count; ++page_index) {
        MRAC_hash_insert(MRAC_chunk_hash, (uint64_t)chunk->base + (page_index * MRAC_PAGE_SIZE), chunk);
    }
}

static void MRAC_chunk_hash_remove(MRAC_alloc_chunk_t *chunk) {
    if (chunk == NULL || chunk->base == NULL) {
        return;
    }

    for (uint64_t page_index = 0u; page_index < chunk->page_count; ++page_index) {
        MRAC_hash_remove(MRAC_chunk_hash, (uint64_t)chunk->base + (page_index * MRAC_PAGE_SIZE));
    }
}

static MRAC_object_t *decode_tagged_ptr(MRAC_tagged_ptr_t tagged) {
    const uint64_t low = (uint64_t)tagged & MRAC_PTR_LOW48_MASK;

    if (low == 0u) {
        return NULL;
    }

    if ((low & (1ull << 47u)) != 0u) {
        return (MRAC_object_t *)(low | 0xffff000000000000ull);
    }

    return (MRAC_object_t *)low;
}

static uint16_t decode_collision_tag(MRAC_tagged_ptr_t tagged) {
    return (uint16_t)((uint64_t)tagged >> 48u);
}

static MRAC_tagged_ptr_t encode_tagged_ptr(MRAC_object_t *next, uint16_t tag) {
    const uint64_t low = ((uint64_t)next) & MRAC_PTR_LOW48_MASK;
    return (MRAC_tagged_ptr_t)(low | ((uint64_t)tag << 48u));
}

static MRAC_object_t *object_next(const MRAC_object_t *object) {
    return object == NULL ? NULL : decode_tagged_ptr(object->next_tagged);
}

static uint16_t object_tag(const MRAC_object_t *object) {
    return object == NULL ? 0u : decode_collision_tag(object->next_tagged);
}

static void object_set_next(MRAC_object_t *object, MRAC_object_t *next) {
    if (object == NULL) {
        return;
    }

    object->next_tagged = encode_tagged_ptr(next, object_tag(object));
}

static size_t chunk_payload_offset(const MRAC_alloc_chunk_t *chunk) {
    if (chunk == NULL || chunk->payload_offset == 0u) {
        return sizeof(MRAC_heap_block_t);
    }
    return chunk->payload_offset;
}

static size_t block_payload_capacity_in_chunk(const MRAC_alloc_chunk_t *chunk, const MRAC_heap_block_t *block) {
    if (block == NULL) {
        return 0u;
    }
    if ((uint8_t *)block == chunk->base) {
        return block->size - chunk_payload_offset(chunk);
    }
    return block->size - sizeof(MRAC_heap_block_t);
}

static int block_is_sane(const MRAC_alloc_chunk_t *chunk, const MRAC_heap_block_t *block) {
    const uint8_t *base = chunk == NULL ? NULL : chunk->base;
    const uint8_t *end = chunk == NULL ? NULL : chunk->base + chunk->size;
    const uint8_t *ptr = (const uint8_t *)block;

    if (chunk == NULL || block == NULL || base == NULL || end == NULL) {
        return 0;
    }
    if (ptr < base || ptr + sizeof(MRAC_heap_block_t) > end) {
        return 0;
    }
    if ((((uintptr_t)ptr) & (MRAC_ALLOC_ALIGNMENT - 1ull)) != 0u) {
        return 0;
    }
    if (block->size < sizeof(MRAC_heap_block_t) || (block->size & (MRAC_ALLOC_ALIGNMENT - 1ull)) != 0u) {
        return 0;
    }
    if (ptr + block->size > end) {
        return 0;
    }

    return 1;
}

static MRAC_heap_block_t *next_block_in_chunk(const MRAC_alloc_chunk_t *chunk, MRAC_heap_block_t *block) {
    if (!block_is_sane(chunk, block)) {
        return NULL;
    }

    uint8_t *next = (uint8_t *)block + block->size;

    if (chunk == NULL || next >= chunk->base + chunk->size) {
        return NULL;
    }

    return (MRAC_heap_block_t *)next;
}

static void split_block(MRAC_heap_block_t *block, size_t requested_payload_size) {
    const size_t required_size = sizeof(MRAC_heap_block_t) + requested_payload_size;

    if (block->size < required_size + sizeof(MRAC_heap_block_t) + MRAC_ALLOC_ALIGNMENT) {
        return;
    }

    {
        MRAC_heap_block_t *remainder = (MRAC_heap_block_t *)((uint8_t *)block + required_size);
        remainder->size = block->size - required_size;
        remainder->pid = block->pid;
        remainder->free = 1u;
        block->size = required_size;
    }
}

static void coalesce_chunk(MRAC_alloc_chunk_t *chunk) {
    MRAC_heap_block_t *block = (MRAC_heap_block_t *)chunk->base;

    while (block != NULL) {
        MRAC_heap_block_t *next = next_block_in_chunk(chunk, block);

        if (next != NULL && block->free != 0u && next->free != 0u) {
            block->size += next->size;
            continue;
        }

        block = next;
    }
}

static int chunk_all_free(MRAC_alloc_chunk_t *chunk) {
    if (chunk == NULL) {
        return 0;
    }

    coalesce_chunk(chunk);
    {
        MRAC_heap_block_t *block = (MRAC_heap_block_t *)chunk->base;
        return block != NULL && block->free != 0u && block->size == chunk->size;
    }
}

static void release_free_chunks(MRAC_pid_arena_t *arena) {
    MRAC_alloc_chunk_t *previous = NULL;
    MRAC_alloc_chunk_t *chunk;

    if (arena == NULL || hhdm_request.response == NULL) {
        return;
    }

    chunk = arena->chunks;
    while (chunk != NULL) {
        MRAC_alloc_chunk_t *next = chunk->next;
        if (chunk_all_free(chunk)) {
            if (previous == NULL) {
                arena->chunks = next;
            } else {
                previous->next = next;
            }
            MRAC_chunk_hash_remove(chunk);
            release_chunk_backing(chunk);
            chunk = next;
            continue;
        }

        previous = chunk;
        chunk = next;
    }
}

static MRAC_pid_arena_t *find_arena_for_chunk(MRAC_alloc_chunk_t *target, MRAC_alloc_chunk_t **previous_out) {
    if (previous_out != NULL) {
        *previous_out = NULL;
    }
    if (target == NULL) {
        return NULL;
    }

    for (MRAC_pid_arena_t *arena = MRAC_arenas; arena != NULL; arena = arena->next) {
        MRAC_alloc_chunk_t *previous = NULL;
        for (MRAC_alloc_chunk_t *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
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

static void release_chunk_from_arena(MRAC_pid_arena_t *arena, MRAC_alloc_chunk_t *chunk, MRAC_alloc_chunk_t *previous) {
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

    MRAC_chunk_hash_remove(chunk);
    release_chunk_backing(chunk);
}

static MRAC_alloc_chunk_t *new_chunk(uint32_t pid, size_t minimum_payload) {
    const uint64_t pages = pages_for_bytes(sizeof(MRAC_heap_block_t) + minimum_payload) > MRAC_CHUNK_MIN_PAGES
        ? pages_for_bytes(sizeof(MRAC_heap_block_t) + minimum_payload)
        : MRAC_CHUNK_MIN_PAGES;
    MRAC_alloc_chunk_t *chunk;
    MRAC_heap_block_t *initial;
    uint64_t virt;
    MRAC_chunk_page_t **tail;

    virt = reserve_chunk_virtual_base(pages);
    if (virt == 0u) {
        return NULL;
    }

    chunk = MRAC_meta_alloc(sizeof(MRAC_alloc_chunk_t));
    if (chunk == NULL) {
        return NULL;
    }

    zero_bytes(chunk, sizeof(*chunk));
    chunk->base = (uint8_t *)virt;
    chunk->size = (size_t)(pages * MRAC_PAGE_SIZE);
    chunk->payload_offset = sizeof(MRAC_heap_block_t);
    chunk->page_count = pages;
    tail = &chunk->pages;

    for (uint64_t page_index = 0u; page_index < pages; ++page_index) {
        void *physical = pmm_alloc_page();
        MRAC_chunk_page_t *page;

        if (physical == NULL) {
            release_chunk_backing(chunk);
            return NULL;
        }

        page = MRAC_meta_alloc(sizeof(MRAC_chunk_page_t));
        if (page == NULL) {
            pmm_free_page(physical);
            release_chunk_backing(chunk);
            return NULL;
        }

        page->phys = (uint64_t)physical;
        page->next = NULL;
        *tail = page;
        tail = &page->next;

        vmm_map_kernel_page(virt + (page_index * MRAC_PAGE_SIZE), (uint64_t)physical, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
        scheduler_map_kernel_page_all(virt + (page_index * MRAC_PAGE_SIZE), (uint64_t)physical, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
        zero_bytes((void *)(virt + (page_index * MRAC_PAGE_SIZE)), MRAC_PAGE_SIZE);
    }

    initial = (MRAC_heap_block_t *)chunk->base;
    initial->size = chunk->size;
    initial->pid = pid;
    initial->free = 1u;
    MRAC_chunk_hash_insert(chunk);
    return chunk;
}

static MRAC_alloc_chunk_t *new_dedicated_chunk(uint32_t pid, size_t payload_size) {
    const uint64_t pages = pages_for_bytes(MRAC_PAGE_SIZE + payload_size);
    MRAC_alloc_chunk_t *chunk;
    MRAC_heap_block_t *initial;
    uint64_t virt;
    MRAC_chunk_page_t **tail;

    virt = reserve_chunk_virtual_base(pages);
    if (virt == 0u) {
        return NULL;
    }

    chunk = MRAC_meta_alloc(sizeof(MRAC_alloc_chunk_t));
    if (chunk == NULL) {
        return NULL;
    }

    zero_bytes(chunk, sizeof(*chunk));
    chunk->base = (uint8_t *)virt;
    chunk->size = (size_t)(pages * MRAC_PAGE_SIZE);
    chunk->payload_offset = MRAC_PAGE_SIZE;
    chunk->page_count = pages;
    tail = &chunk->pages;

    for (uint64_t page_index = 0u; page_index < pages; ++page_index) {
        void *physical = pmm_alloc_page();
        MRAC_chunk_page_t *page;

        if (physical == NULL) {
            release_chunk_backing(chunk);
            return NULL;
        }

        page = MRAC_meta_alloc(sizeof(MRAC_chunk_page_t));
        if (page == NULL) {
            pmm_free_page(physical);
            release_chunk_backing(chunk);
            return NULL;
        }

        page->phys = (uint64_t)physical;
        page->next = NULL;
        *tail = page;
        tail = &page->next;

        vmm_map_kernel_page(virt + (page_index * MRAC_PAGE_SIZE), (uint64_t)physical, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
        scheduler_map_kernel_page_all(virt + (page_index * MRAC_PAGE_SIZE), (uint64_t)physical, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
        zero_bytes((void *)(virt + (page_index * MRAC_PAGE_SIZE)), MRAC_PAGE_SIZE);
    }

    initial = (MRAC_heap_block_t *)chunk->base;
    initial->size = chunk->size;
    initial->pid = pid;
    initial->free = 0u;
    MRAC_chunk_hash_insert(chunk);
    return chunk;
}

static MRAC_pid_arena_t *find_arena(uint32_t pid) {
    for (MRAC_pid_arena_t *arena = MRAC_arenas; arena != NULL; arena = arena->next) {
        if (arena->pid == pid) {
            return arena;
        }
    }

    return NULL;
}

static MRAC_pid_arena_t *ensure_arena(uint32_t pid) {
    MRAC_pid_arena_t *arena = find_arena(pid);

    if (arena != NULL) {
        return arena;
    }

    arena = MRAC_meta_alloc(sizeof(MRAC_pid_arena_t));
    if (arena == NULL) {
        return NULL;
    }

    arena->pid = pid;
    arena->free_chunks = 0u;
    arena->chunks = NULL;
    arena->next = MRAC_arenas;
    MRAC_arenas = arena;
    return arena;
}

static MRAC_alloc_chunk_t *find_chunk_for_address(const void *ptr) {
    const uint8_t *address = ptr;
    MRAC_alloc_chunk_t *chunk;

    if (ptr == NULL) {
        return NULL;
    }

    chunk = MRAC_hash_find(MRAC_chunk_hash, (uint64_t)(uintptr_t)address & ~(MRAC_PAGE_SIZE - 1ull));
    if (chunk != NULL && address >= chunk->base && address < chunk->base + chunk->size) {
        return chunk;
    }

    for (MRAC_pid_arena_t *arena = MRAC_arenas; arena != NULL; arena = arena->next) {
        for (MRAC_alloc_chunk_t *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
            if (address >= chunk->base && address < chunk->base + chunk->size) {
                MRAC_hash_insert(MRAC_chunk_hash, (uint64_t)(uintptr_t)address & ~(MRAC_PAGE_SIZE - 1ull), chunk);
                return chunk;
            }
        }
    }

    return NULL;
}

static MRAC_chunk_page_t *chunk_page_at(MRAC_alloc_chunk_t *chunk, uint64_t page_index) {
    MRAC_chunk_page_t *page;

    if (chunk == NULL || page_index >= chunk->page_count) {
        return NULL;
    }

    page = chunk->pages;
    for (uint64_t index = 0u; page != NULL && index < page_index; ++index) {
        page = page->next;
    }
    return page;
}

static MRAC_heap_block_t *find_free_block(MRAC_pid_arena_t *arena, size_t aligned_size) {
    for (MRAC_alloc_chunk_t *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
        for (MRAC_heap_block_t *block = (MRAC_heap_block_t *)chunk->base;
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

static MRAC_heap_block_t *find_block_header(void *ptr, MRAC_alloc_chunk_t **chunk_out) {
    MRAC_heap_block_t *cached_block = MRAC_hash_find(MRAC_block_hash, (uint64_t)(uintptr_t)ptr);

    if (cached_block != NULL) {
        MRAC_alloc_chunk_t *cached_chunk = find_chunk_for_address(ptr);
        if (cached_chunk != NULL) {
            const size_t cached_payload_offset = (uint8_t *)cached_block == cached_chunk->base ? chunk_payload_offset(cached_chunk) : sizeof(MRAC_heap_block_t);
            if ((uint8_t *)cached_block + cached_payload_offset == (uint8_t *)ptr) {
                if (chunk_out != NULL) {
                    *chunk_out = cached_chunk;
                }
                return cached_block;
            }
        }
    }

    for (MRAC_pid_arena_t *arena = MRAC_arenas; arena != NULL; arena = arena->next) {
        for (MRAC_alloc_chunk_t *chunk = arena->chunks; chunk != NULL; chunk = chunk->next) {
            for (MRAC_heap_block_t *block = (MRAC_heap_block_t *)chunk->base;
                 block != NULL;
                 block = next_block_in_chunk(chunk, block)) {
                if (!block_is_sane(chunk, block)) {
                    break;
                }
                const size_t payload_offset = (uint8_t *)block == chunk->base ? chunk_payload_offset(chunk) : sizeof(MRAC_heap_block_t);
                if ((uint8_t *)block + payload_offset == (uint8_t *)ptr) {
                    MRAC_hash_insert(MRAC_block_hash, (uint64_t)(uintptr_t)ptr, block);
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

static void *MRAC_raw_alloc(uint32_t pid, size_t size) {
    MRAC_pid_arena_t *arena;
    MRAC_heap_block_t *block;
    MRAC_alloc_chunk_t *block_chunk = NULL;
    const size_t aligned = (size_t)align_up(size, MRAC_ALLOC_ALIGNMENT);
    int chunk_was_all_free = 0;

    if (!MRAC_primary_allocator_ready || aligned == 0u) {
        return NULL;
    }

    arena = ensure_arena(pid);
    if (arena == NULL) {
        return NULL;
    }

    if (aligned >= MRAC_PAGE_SIZE) {
        MRAC_alloc_chunk_t *chunk = new_dedicated_chunk(pid, aligned);
        void *payload;

        if (chunk == NULL) {
            return NULL;
        }

        chunk->next = arena->chunks;
        arena->chunks = chunk;
        payload = (uint8_t *)chunk->base + chunk_payload_offset(chunk);
        MRAC_hash_insert(MRAC_block_hash, (uint64_t)(uintptr_t)payload, chunk->base);
        return payload;
    }

    block = find_free_block(arena, aligned);
    if (block == NULL) {
        MRAC_alloc_chunk_t *chunk = new_chunk(pid, aligned);
        if (chunk == NULL) {
            return NULL;
        }

        chunk->next = arena->chunks;
        arena->chunks = chunk;
        ++arena->free_chunks;
        block = (MRAC_heap_block_t *)chunk->base;
        block_chunk = chunk;
        chunk_was_all_free = 1;
    } else {
        block_chunk = find_chunk_for_address((uint8_t *)block + sizeof(MRAC_heap_block_t));
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
        void *payload = (uint8_t *)block + sizeof(MRAC_heap_block_t);
        MRAC_hash_insert(MRAC_block_hash, (uint64_t)(uintptr_t)payload, block);
        return payload;
    }
}

static void MRAC_raw_free(void *ptr) {
    MRAC_alloc_chunk_t *chunk = NULL;
    MRAC_alloc_chunk_t *previous = NULL;
    MRAC_pid_arena_t *arena;
    MRAC_heap_block_t *block;

    if (!MRAC_primary_allocator_ready || ptr == NULL) {
        return;
    }

    block = find_block_header(ptr, &chunk);
    if (block == NULL || block->free != 0u) {
        return;
    }

    MRAC_hash_remove(MRAC_block_hash, (uint64_t)(uintptr_t)ptr);
    block->free = 1u;
    if (chunk != NULL) {
        coalesce_chunk(chunk);
        if (chunk_all_free(chunk)) {
            arena = find_arena_for_chunk(chunk, &previous);
            if (arena != NULL) {
                ++arena->free_chunks;
            }
            if (arena != NULL && arena->free_chunks > 1u) {
                release_chunk_from_arena(arena, chunk, previous);
            }
        }
    }
}

static void *MRAC_raw_realloc(uint32_t pid, void *ptr, size_t size) {
    MRAC_alloc_chunk_t *chunk = NULL;
    MRAC_heap_block_t *block;
    size_t old_size;
    void *new_ptr;

    if (ptr == NULL) {
        return MRAC_raw_alloc(pid, size);
    }

    if (size == 0u) {
        MRAC_raw_free(ptr);
        return NULL;
    }

    block = find_block_header(ptr, &chunk);
    if (block == NULL || block->free != 0u) {
        return NULL;
    }

    old_size = block_payload_capacity_in_chunk(chunk, block);
    if (old_size >= size) {
        split_block(block, (size_t)align_up(size, MRAC_ALLOC_ALIGNMENT));
        return ptr;
    }

    new_ptr = MRAC_raw_alloc(pid, size);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_size);
    MRAC_raw_free(ptr);
    return new_ptr;
}

static void zero_index_table(void) {
    for (size_t i = 0; i < MRAC_INDEX_BUCKET_COUNT; ++i) {
        MRAC_index_table[i] = NULL;
    }
}

static void zero_object_table(void) {
    for (size_t i = 0; i < MRAC_OBJECT_BUCKET_COUNT; ++i) {
        MRAC_object_table[i] = NULL;
    }
}

static bool object_matches(const MRAC_object_t *object, uint64_t content_hash, const void *data, size_t size) {
    return object != NULL &&
           object->content_hash == content_hash &&
           object->size == size &&
           memcmp(object->data, data, size) == 0;
}

static uint16_t next_collision_tag(uint64_t content_hash) {
    uint16_t next_tag = 0u;

    for (MRAC_object_t *object = MRAC_object_table[object_bucket(content_hash)];
         object != NULL;
         object = object_next(object)) {
        if (object->content_hash != content_hash) {
            continue;
        }

        if ((uint16_t)(object_tag(object) + 1u) > next_tag) {
            next_tag = (uint16_t)(object_tag(object) + 1u);
        }
    }

    return next_tag;
}

static MRAC_object_t *find_exact_object(uint64_t content_hash, const void *data, size_t size) {
    for (MRAC_object_t *object = MRAC_object_table[object_bucket(content_hash)];
         object != NULL;
         object = object_next(object)) {
        if (object_matches(object, content_hash, data, size)) {
            return object;
        }
    }

    return NULL;
}

static MRAC_object_t *new_object(uint32_t pid, uint64_t content_hash, const void *data, size_t size) {
    const uint16_t tag = next_collision_tag(content_hash);
    MRAC_object_t *object;
    void *stored_data;

    if (tag == 0u) {
        object = MRAC_raw_alloc(pid, sizeof(MRAC_object_t));
    } else {
        MRAC_collision_t *collision = MRAC_raw_alloc(pid, sizeof(MRAC_collision_t));
        if (collision == NULL) {
            return NULL;
        }
        collision->tag = (uint8_t)(tag > 0xffu ? 0xffu : tag);
        object = &collision->base;
    }

    if (object == NULL) {
        return NULL;
    }

    stored_data = MRAC_raw_alloc(pid, size);
    if (stored_data == NULL) {
        MRAC_raw_free(object);
        return NULL;
    }

    memcpy(stored_data, data, size);

    object->content_hash = content_hash;
    object->data = stored_data;
    object->size = size;
    object->refcount = 0u;
    object->next_tagged = encode_tagged_ptr(MRAC_object_table[object_bucket(content_hash)], tag);
    MRAC_object_table[object_bucket(content_hash)] = object;
    return object;
}

static MRAC_object_t *new_zero_extended_object(uint32_t pid, uint64_t content_hash, const void *data, size_t data_size, size_t total_size) {
    const uint16_t tag = next_collision_tag(content_hash);
    MRAC_object_t *object;
    void *stored_data;

    if (tag == 0u) {
        object = MRAC_raw_alloc(pid, sizeof(MRAC_object_t));
    } else {
        MRAC_collision_t *collision = MRAC_raw_alloc(pid, sizeof(MRAC_collision_t));
        if (collision == NULL) {
            return NULL;
        }
        collision->tag = (uint8_t)(tag > 0xffu ? 0xffu : tag);
        object = &collision->base;
    }

    if (object == NULL) {
        return NULL;
    }

    stored_data = MRAC_raw_alloc(pid, total_size);
    if (stored_data == NULL) {
        MRAC_raw_free(object);
        return NULL;
    }

    if (data_size != 0u) {
        memcpy(stored_data, data, data_size);
    }
    if (total_size > data_size) {
        zero_bytes((uint8_t *)stored_data + data_size, total_size - data_size);
    }

    object->content_hash = content_hash;
    object->data = stored_data;
    object->size = total_size;
    object->refcount = 0u;
    object->next_tagged = encode_tagged_ptr(MRAC_object_table[object_bucket(content_hash)], tag);
    MRAC_object_table[object_bucket(content_hash)] = object;
    return object;
}

static MRAC_object_t *get_or_create_object(uint32_t pid, const void *data, size_t size) {
    const uint64_t content_hash = fnv1a64(data, size);
    MRAC_object_t *object = find_exact_object(content_hash, data, size);

    if (object != NULL) {
        return object;
    }

    return new_object(pid, content_hash, data, size);
}

static MRAC_object_t *get_or_create_zero_extended_object(uint32_t pid, const void *data, size_t data_size, size_t total_size) {
    const uint64_t content_hash = fnv1a64_zero_extended(data, data_size, total_size);

    for (MRAC_object_t *object = MRAC_object_table[object_bucket(content_hash)];
         object != NULL;
         object = object_next(object)) {
        if (object->content_hash != content_hash || object->size != total_size) {
            continue;
        }
        if (data_size != 0u && memcmp(object->data, data, data_size) != 0) {
            continue;
        }

        int zeros_match = 1;
        const uint8_t *zeros = (const uint8_t *)object->data + data_size;
        for (size_t i = data_size; i < total_size; ++i) {
            if (zeros[i - data_size] != 0u) {
                zeros_match = 0;
                break;
            }
        }
        if (zeros_match != 0) {
            return object;
        }
    }

    return new_zero_extended_object(pid, content_hash, data, data_size, total_size);
}

static void object_acquire(MRAC_object_t *object) {
    if (object != NULL && object->refcount != UINT32_MAX) {
        ++object->refcount;
    }
}

static void unlink_object(MRAC_object_t *target) {
    const size_t bucket = object_bucket(target->content_hash);
    MRAC_object_t *previous = NULL;
    MRAC_object_t *object = MRAC_object_table[bucket];

    while (object != NULL) {
        if (object == target) {
            MRAC_object_t *next = object_next(object);
            if (previous == NULL) {
                MRAC_object_table[bucket] = next;
            } else {
                object_set_next(previous, next);
            }
            return;
        }

        previous = object;
        object = object_next(object);
    }
}

static void destroy_object(MRAC_object_t *object) {
    if (object == NULL) {
        return;
    }

    unlink_object(object);
    MRAC_raw_free(object->data);
    MRAC_raw_free(object);
}

static void object_release(MRAC_object_t *object) {
    if (object == NULL || object->refcount == 0u) {
        return;
    }

    --object->refcount;
    if (object->refcount == 0u) {
        destroy_object(object);
    }
}

static MRAC_entry_t *find_entry(uint32_t pid, uint64_t key_hash) {
    for (MRAC_entry_t *entry = MRAC_index_table[index_bucket(key_hash)];
         entry != NULL;
         entry = entry->next) {
        if (entry->pid == pid && entry->key_hash == key_hash) {
            return entry;
        }
    }

    return NULL;
}

static MRAC_entry_t *new_entry(uint32_t pid, uint64_t key_hash) {
    MRAC_entry_t *entry = MRAC_raw_alloc(pid, sizeof(MRAC_entry_t));

    if (entry == NULL) {
        return NULL;
    }

    zero_bytes(entry, sizeof(*entry));
    entry->pid = pid;
    entry->key_hash = key_hash;
    entry->next = MRAC_index_table[index_bucket(key_hash)];
    MRAC_index_table[index_bucket(key_hash)] = entry;
    return entry;
}

static MRAC_entry_t *ensure_entry(uint32_t pid, uint64_t key_hash) {
    MRAC_entry_t *entry = find_entry(pid, key_hash);

    if (entry != NULL) {
        return entry;
    }

    return new_entry(pid, key_hash);
}

static MRAC_version_ref_t *find_version_ref(const MRAC_entry_t *entry, uint32_t version, uint8_t require_owner) {
    if (entry == NULL) {
        return NULL;
    }

    for (MRAC_version_ref_t *ref = entry->latest_version; ref != NULL; ref = ref->older) {
        if (ref->version != version) {
            continue;
        }
        if (require_owner != 0u && ref->owner_present == 0u) {
            return NULL;
        }
        return ref;
    }

    return NULL;
}

static MRAC_version_ref_t *current_version_ref(const MRAC_entry_t *entry) {
    if (entry == NULL) {
        return NULL;
    }

    for (MRAC_version_ref_t *ref = entry->latest_version; ref != NULL; ref = ref->older) {
        if (ref->owner_present != 0u && ref->object != NULL) {
            return ref;
        }
    }

    return NULL;
}

static uint32_t next_entry_version(const MRAC_entry_t *entry) {
    if (entry == NULL || entry->latest_version == NULL) {
        return 1u;
    }

    return entry->latest_version->version == UINT32_MAX
        ? UINT32_MAX
        : entry->latest_version->version + 1u;
}

static MRAC_version_ref_t *new_version_ref(uint32_t pid, MRAC_object_t *object, uint32_t version) {
    MRAC_version_ref_t *ref;

    if (object == NULL) {
        return NULL;
    }

    ref = MRAC_raw_alloc(pid, sizeof(MRAC_version_ref_t));
    if (ref == NULL) {
        return NULL;
    }

    zero_bytes(ref, sizeof(*ref));
    ref->version = version;
    ref->owner_present = 1u;
    ref->object = object;
    object_acquire(object);
    return ref;
}

static void destroy_version_ref(MRAC_version_ref_t *ref) {
    if (ref == NULL) {
        return;
    }

    if (ref->owner_present != 0u && ref->object != NULL) {
        object_release(ref->object);
        ref->owner_present = 0u;
    }

    while (ref->borrows > 0u && ref->object != NULL) {
        object_release(ref->object);
        --ref->borrows;
    }

    MRAC_raw_free(ref);
}

static void release_version_owner(MRAC_version_ref_t *ref) {
    if (ref == NULL || ref->owner_present == 0u || ref->object == NULL) {
        if (ref != NULL) {
            ref->owner_present = 0u;
        }
        return;
    }

    ref->owner_present = 0u;
    object_release(ref->object);
}

static void cleanup_version_refs(MRAC_entry_t *entry) {
    MRAC_version_ref_t *previous = NULL;
    MRAC_version_ref_t *ref;

    if (entry == NULL) {
        return;
    }

    ref = entry->latest_version;
    while (ref != NULL) {
        MRAC_version_ref_t *next = ref->older;
        if (ref->owner_present == 0u && ref->borrows == 0u) {
            if (previous == NULL) {
                entry->latest_version = next;
            } else {
                previous->older = next;
            }
            destroy_version_ref(ref);
            ref = next;
            continue;
        }

        previous = ref;
        ref = next;
    }
}

static void remove_entry(MRAC_entry_t *target) {
    const size_t bucket = index_bucket(target->key_hash);
    MRAC_entry_t *previous = NULL;
    MRAC_entry_t *entry = MRAC_index_table[bucket];

    while (entry != NULL) {
        if (entry == target) {
            MRAC_version_ref_t *ref = entry->latest_version;

            if (previous == NULL) {
                MRAC_index_table[bucket] = entry->next;
            } else {
                previous->next = entry->next;
            }

            while (ref != NULL) {
                MRAC_version_ref_t *next = ref->older;
                destroy_version_ref(ref);
                ref = next;
            }

            MRAC_raw_free(entry);
            return;
        }

        previous = entry;
        entry = entry->next;
    }
}

static void cleanup_entry_if_dead(MRAC_entry_t *entry) {
    if (entry == NULL) {
        return;
    }

    cleanup_version_refs(entry);
    if (entry->latest_version == NULL) {
        remove_entry(entry);
    }
}

static void *upsert_binding(uint32_t pid, uint64_t key_hash, const void *data, size_t size, uint32_t version) {
    MRAC_entry_t *entry;
    MRAC_object_t *object;
    MRAC_version_ref_t *ref;
    uint64_t content_hash;

    if (!MRAC_ready || data == NULL || size == 0u) {
        return NULL;
    }

    content_hash = fnv1a64(data, size);
    entry = ensure_entry(pid, key_hash);
    if (entry == NULL) {
        return NULL;
    }

    ref = current_version_ref(entry);
    if (ref != NULL &&
        ref->object != NULL &&
        object_matches(ref->object, content_hash, data, size)) {
        ref = new_version_ref(pid, ref->object, version);
        if (ref == NULL) {
            return NULL;
        }
        ref->older = entry->latest_version;
        entry->latest_version = ref;
        return ref->object->data;
    }

    object = get_or_create_object(pid, data, size);
    if (object == NULL) {
        return NULL;
    }

    ref = new_version_ref(pid, object, version);
    if (ref == NULL) {
        if (object->refcount == 0u) {
            destroy_object(object);
        }
        return NULL;
    }

    ref->older = entry->latest_version;
    entry->latest_version = ref;
    return object->data;
}

static void *upsert_zero_extended_binding(uint32_t pid, uint64_t key_hash, const void *data, size_t data_size, size_t total_size, uint32_t version) {
    MRAC_entry_t *entry;
    MRAC_object_t *object;
    MRAC_version_ref_t *ref;

    if (!MRAC_ready || data == NULL || data_size > total_size || total_size == 0u) {
        return NULL;
    }

    object = get_or_create_zero_extended_object(pid, data, data_size, total_size);
    if (object == NULL) {
        return NULL;
    }

    entry = ensure_entry(pid, key_hash);
    if (entry == NULL) {
        if (object->refcount == 0u) {
            destroy_object(object);
        }
        return NULL;
    }

    ref = new_version_ref(pid, object, version);
    if (ref == NULL) {
        if (object->refcount == 0u) {
            destroy_object(object);
        }
        return NULL;
    }

    ref->older = entry->latest_version;
    entry->latest_version = ref;
    return object->data;
}

void MRAC_init(void) {
    MRAC_ready = false;
    MRAC_primary_allocator_ready = false;
    MRAC_arenas = NULL;
    MRAC_meta_cursor = NULL;
    MRAC_meta_end = NULL;
    MRAC_chunk_virt_cursor = MRAC_CHUNK_VIRT_BASE;

    if (hhdm_request.response == NULL) {
        return;
    }

    MRAC_primary_allocator_ready = true;
    MRAC_index_table = MRAC_meta_alloc(sizeof(MRAC_entry_t *) * MRAC_INDEX_BUCKET_COUNT);
    MRAC_object_table = MRAC_meta_alloc(sizeof(MRAC_object_t *) * MRAC_OBJECT_BUCKET_COUNT);
    if (MRAC_index_table == NULL || MRAC_object_table == NULL) {
        MRAC_primary_allocator_ready = false;
        return;
    }

    zero_index_table();
    zero_object_table();
    MRAC_ready = true;
}

int MRAC_primary_allocator_active(void) {
    return MRAC_primary_allocator_ready != 0;
}

void *MRAC_alloc(uint32_t pid, size_t size) {
    return MRAC_raw_alloc(pid, size);
}

void MRAC_free(void *ptr) {
    MRAC_raw_free(ptr);
}

void *MRAC_realloc(uint32_t pid, void *ptr, size_t size) {
    return MRAC_raw_realloc(pid, ptr, size);
}

void *MRAC_store(uint32_t pid, void *data, size_t size) {
    MRAC_entry_t *existing;
    uint64_t key_hash;

    if (!MRAC_ready || data == NULL || size == 0u) {
        return NULL;
    }

    key_hash = fnv1a64(data, size);
    existing = find_entry(pid, key_hash);
    return upsert_binding(pid, key_hash, data, size, next_entry_version(existing));
}

void *MRAC_store_with_hash(uint32_t pid, uint64_t hash, void *data, size_t size) {
    MRAC_entry_t *existing;

    if (!MRAC_ready || data == NULL || size == 0u) {
        return NULL;
    }

    existing = find_entry(pid, hash);
    return upsert_binding(pid, hash, data, size, next_entry_version(existing));
}

void *MRAC_store_with_hash_zero_extended(uint32_t pid, uint64_t hash, const void *data, size_t data_size, size_t total_size) {
    MRAC_entry_t *existing;

    if (!MRAC_ready || data == NULL || data_size > total_size || total_size == 0u) {
        return NULL;
    }

    existing = find_entry(pid, hash);
    return upsert_zero_extended_binding(pid, hash, data, data_size, total_size, next_entry_version(existing));
}

int MRAC_can_direct_map(const void *data, size_t size) {
    MRAC_alloc_chunk_t *chunk;
    const uintptr_t aligned_address = (uintptr_t)data;
    const uintptr_t address = (uintptr_t)data;
    const size_t mapped_size = (size_t)align_up(size, MRAC_PAGE_SIZE);
    uintptr_t mapped_end;
    uintptr_t chunk_end;

    if (!MRAC_primary_allocator_ready || data == NULL || size == 0u) {
        return 0;
    }
    if (mapped_size < size) {
        return 0;
    }
    if ((aligned_address & (MRAC_PAGE_SIZE - 1ull)) != 0u) {
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

int MRAC_direct_physical_page(const void *data, size_t offset, uint64_t *phys_out) {
    MRAC_alloc_chunk_t *chunk;
    MRAC_chunk_page_t *page;
    const uint8_t *address = (const uint8_t *)data + offset;
    uint64_t page_index;

    if (phys_out == NULL || offset > (SIZE_MAX - MRAC_PAGE_SIZE) || !MRAC_can_direct_map(data, offset + MRAC_PAGE_SIZE)) {
        return -1;
    }
    if (((uintptr_t)address & (MRAC_PAGE_SIZE - 1ull)) != 0u) {
        return -1;
    }

    chunk = find_chunk_for_address(address);
    if (chunk == NULL) {
        return -1;
    }

    page_index = (uint64_t)(address - chunk->base) / MRAC_PAGE_SIZE;
    page = chunk_page_at(chunk, page_index);
    if (page == NULL) {
        return -1;
    }

    *phys_out = page->phys;
    return 0;
}

void *MRAC_lookup(uint32_t pid, uint64_t hash) {
    MRAC_entry_t *entry;
    MRAC_version_ref_t *ref;

    if (!MRAC_ready) {
        return NULL;
    }

    entry = find_entry(pid, hash);
    ref = current_version_ref(entry);
    if (ref == NULL || ref->object == NULL) {
        return NULL;
    }

    if (ref->borrows != UINT32_MAX) {
        ++ref->borrows;
    }
    object_acquire(ref->object);
    return ref->object->data;
}

void *MRAC_lookup_version(uint32_t pid, uint64_t hash, uint32_t version) {
    MRAC_entry_t *entry;
    MRAC_version_ref_t *ref;

    if (!MRAC_ready) {
        return NULL;
    }

    entry = find_entry(pid, hash);
    ref = find_version_ref(entry, version, 1u);
    if (ref == NULL || ref->object == NULL) {
        return NULL;
    }

    if (ref->borrows != UINT32_MAX) {
        ++ref->borrows;
    }
    object_acquire(ref->object);
    return ref->object->data;
}

void *MRAC_modify(uint32_t pid, uint64_t hash, void *new_data, size_t size) {
    MRAC_entry_t *previous;
    uint64_t new_hash;

    if (!MRAC_ready || new_data == NULL || size == 0u) {
        return NULL;
    }

    new_hash = fnv1a64(new_data, size);
    previous = find_entry(pid, hash);
    if (current_version_ref(previous) == NULL) {
        return NULL;
    }

    return upsert_binding(pid, new_hash, new_data, size, next_entry_version(previous));
}

void *MRAC_modify_with_hash(uint32_t pid, uint64_t hash, void *new_data, size_t size) {
    MRAC_entry_t *previous;

    if (!MRAC_ready || new_data == NULL || size == 0u) {
        return NULL;
    }

    previous = find_entry(pid, hash);
    return upsert_binding(pid, hash, new_data, size, next_entry_version(previous));
}

void MRAC_release(uint32_t pid, uint64_t hash) {
    MRAC_entry_t *entry;
    MRAC_version_ref_t *ref;

    if (!MRAC_ready) {
        return;
    }

    entry = find_entry(pid, hash);
    ref = current_version_ref(entry);
    if (ref == NULL || ref->object == NULL || ref->borrows == 0u) {
        return;
    }

    --ref->borrows;
    object_release(ref->object);
    cleanup_entry_if_dead(entry);
}

void MRAC_release_version(uint32_t pid, uint64_t hash, uint32_t version) {
    MRAC_entry_t *entry;
    MRAC_version_ref_t *ref;

    if (!MRAC_ready) {
        return;
    }

    entry = find_entry(pid, hash);
    ref = find_version_ref(entry, version, 0u);
    if (ref == NULL || ref->object == NULL || ref->borrows == 0u) {
        return;
    }

    --ref->borrows;
    object_release(ref->object);
    cleanup_entry_if_dead(entry);
}

void MRAC_remove(uint32_t pid, uint64_t hash) {
    MRAC_entry_t *entry;

    if (!MRAC_ready) {
        return;
    }

    entry = find_entry(pid, hash);
    if (entry == NULL) {
        return;
    }

    for (MRAC_version_ref_t *ref = entry->latest_version; ref != NULL; ref = ref->older) {
        release_version_owner(ref);
    }
    cleanup_entry_if_dead(entry);
}

int MRAC_remove_version(uint32_t pid, uint64_t hash, uint32_t version) {
    MRAC_entry_t *entry;
    MRAC_version_ref_t *ref;

    if (!MRAC_ready) {
        return -1;
    }

    entry = find_entry(pid, hash);
    ref = find_version_ref(entry, version, 1u);
    if (ref == NULL) {
        return -1;
    }

    release_version_owner(ref);
    cleanup_entry_if_dead(entry);
    return 0;
}

uint32_t MRAC_current_version(uint32_t pid, uint64_t hash) {
    MRAC_entry_t *entry;
    MRAC_version_ref_t *ref;

    if (!MRAC_ready) {
        return 0u;
    }

    entry = find_entry(pid, hash);
    ref = current_version_ref(entry);
    return ref == NULL ? 0u : ref->version;
}

size_t MRAC_version_count(uint32_t pid, uint64_t hash) {
    MRAC_entry_t *entry;
    size_t count = 0u;

    if (!MRAC_ready) {
        return 0u;
    }

    entry = find_entry(pid, hash);
    if (entry == NULL) {
        return 0u;
    }

    for (MRAC_version_ref_t *ref = entry->latest_version; ref != NULL; ref = ref->older) {
        if (ref->owner_present != 0u) {
            ++count;
        }
    }

    return count;
}

int MRAC_version_info_at(uint32_t pid, uint64_t hash, size_t index, MRAC_version_info_t *out) {
    MRAC_entry_t *entry;
    MRAC_version_ref_t *current;
    size_t position = 0u;

    if (!MRAC_ready || out == NULL) {
        return -1;
    }

    entry = find_entry(pid, hash);
    current = current_version_ref(entry);
    if (entry == NULL) {
        return -1;
    }

    for (MRAC_version_ref_t *ref = entry->latest_version; ref != NULL; ref = ref->older) {
        if (ref->owner_present == 0u || ref->object == NULL) {
            continue;
        }

        if (position == index) {
            out->version = ref->version;
            out->size = ref->object->size;
            out->is_current = ref == current ? 1u : 0u;
            return 0;
        }

        ++position;
    }

    return -1;
}

int MRAC_binding_info_at(uint32_t pid, size_t index, MRAC_binding_info_t *out) {
    size_t current = 0u;

    if (!MRAC_ready || out == NULL) {
        return -1;
    }

    for (size_t bucket = 0; bucket < MRAC_INDEX_BUCKET_COUNT; ++bucket) {
        for (MRAC_entry_t *entry = MRAC_index_table[bucket]; entry != NULL; entry = entry->next) {
            MRAC_version_ref_t *ref;

            if (entry->pid != pid) {
                continue;
            }

            ref = current_version_ref(entry);
            if (ref == NULL || ref->object == NULL) {
                continue;
            }

            if (current == index) {
                out->key_hash = entry->key_hash;
                out->current_version = ref->version;
                out->current_size = ref->object->size;
                return 0;
            }

            ++current;
        }
    }

    return -1;
}

void MRAC_reap_pid(uint32_t pid) {
    MRAC_pid_arena_t *arena;

    if (!MRAC_ready) {
        return;
    }

    for (size_t bucket = 0; bucket < MRAC_INDEX_BUCKET_COUNT; ++bucket) {
        MRAC_entry_t *entry = MRAC_index_table[bucket];
        while (entry != NULL) {
            MRAC_entry_t *next = entry->next;
            if (entry->pid == pid) {
                remove_entry(entry);
            }
            entry = next;
        }
    }

    arena = find_arena(pid);
    if (arena != NULL) {
        release_free_chunks(arena);
    }
}
