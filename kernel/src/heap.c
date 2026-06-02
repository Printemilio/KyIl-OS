#include <unos/heap.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/boot_requests.h>
#include <unos/mrac.h>
#include <unos/pmm.h>
#include <unos/scheduler.h>
#include <unos/vmm.h>

#define HEAP_BASE 0xffffffff90000000ull
#define HEAP_INITIAL_SIZE (8ull * 1024ull * 1024ull)
#define HEAP_PAGE_SIZE 4096ull
#define HEAP_ALIGNMENT 16ull
#define PAGE_ADDR_MASK 0x000ffffffffff000ull

struct heap_block {
    size_t size;
    bool free;
    uint8_t reserved[7];
};

_Static_assert(sizeof(struct heap_block) == HEAP_ALIGNMENT, "heap block headers must preserve 16-byte payload alignment");

static uint8_t *heap_start;
static uint8_t *heap_end;
static bool heap_ready;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

static uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PAGE_ADDR_MASK;
}

static void *current_pml4(void) {
    if (hhdm_request.response == NULL) {
        return NULL;
    }

    return (void *)(hhdm_request.response->offset + read_cr3());
}

static size_t block_payload_capacity(const struct heap_block *block) {
    return block->size - sizeof(struct heap_block);
}

static struct heap_block *next_block(struct heap_block *block) {
    uint8_t *next = (uint8_t *)block + block->size;

    if (next >= heap_end) {
        return NULL;
    }

    return (struct heap_block *)next;
}

static struct heap_block *find_block_from_payload(void *ptr) {
    struct heap_block *block = (struct heap_block *)heap_start;

    while (block != NULL) {
        if ((uint8_t *)block + sizeof(struct heap_block) == (uint8_t *)ptr) {
            return block;
        }

        block = next_block(block);
    }

    return NULL;
}

static void coalesce_free_blocks(void) {
    struct heap_block *block = (struct heap_block *)heap_start;

    while (block != NULL) {
        struct heap_block *next = next_block(block);

        if (next != NULL && block->free && next->free) {
            block->size += next->size;
            continue;
        }

        block = next;
    }
}

static void split_block(struct heap_block *block, size_t requested_payload_size) {
    const size_t required_size = sizeof(struct heap_block) + requested_payload_size;

    if (block->size < required_size + sizeof(struct heap_block) + HEAP_ALIGNMENT) {
        return;
    }

    struct heap_block *remainder = (struct heap_block *)((uint8_t *)block + required_size);
    remainder->size = block->size - required_size;
    remainder->free = true;

    block->size = required_size;
}

void heap_init(void) {
    heap_ready = false;

    void *pml4 = current_pml4();
    if (pml4 == NULL) {
        return;
    }

    for (uint64_t offset = 0; offset < HEAP_INITIAL_SIZE; offset += HEAP_PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        if (phys == NULL) {
            return;
        }

        vmm_map_page(
            pml4,
            HEAP_BASE + offset,
            (uint64_t)phys,
            VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE
        );
    }

    heap_start = (uint8_t *)HEAP_BASE;
    heap_end = heap_start + HEAP_INITIAL_SIZE;

    struct heap_block *initial = (struct heap_block *)heap_start;
    initial->size = HEAP_INITIAL_SIZE;
    initial->free = true;

    heap_ready = true;
}

static void *bootstrap_kmalloc(size_t size) {
    if (!heap_ready || size == 0) {
        return NULL;
    }

    const size_t aligned_size = (size_t)align_up(size, HEAP_ALIGNMENT);
    struct heap_block *block = (struct heap_block *)heap_start;

    while (block != NULL) {
        if (block->free && block_payload_capacity(block) >= aligned_size) {
            split_block(block, aligned_size);
            block->free = false;
            return (uint8_t *)block + sizeof(struct heap_block);
        }

        block = next_block(block);
    }

    return NULL;
}

static void bootstrap_kfree(void *ptr) {
    if (!heap_ready || ptr == NULL) {
        return;
    }

    if ((uint8_t *)ptr < heap_start + sizeof(struct heap_block) || (uint8_t *)ptr >= heap_end) {
        return;
    }

    struct heap_block *block = find_block_from_payload(ptr);
    if (block == NULL || block->free) {
        return;
    }

    block->free = true;
    coalesce_free_blocks();
}

static void *bootstrap_krealloc(void *ptr, size_t size) {
    if (!heap_ready) {
        return NULL;
    }

    if (ptr == NULL) {
        return bootstrap_kmalloc(size);
    }

    if (size == 0) {
        bootstrap_kfree(ptr);
        return NULL;
    }

    struct heap_block *block = find_block_from_payload(ptr);
    if (block == NULL || block->free) {
        return NULL;
    }

    const size_t aligned_size = (size_t)align_up(size, HEAP_ALIGNMENT);
    const size_t old_capacity = block_payload_capacity(block);

    if (old_capacity >= aligned_size) {
        split_block(block, aligned_size);
        return ptr;
    }

    void *new_ptr = bootstrap_kmalloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    const size_t copy_size = old_capacity < size ? old_capacity : size;
    uint8_t *dst = new_ptr;
    const uint8_t *src = ptr;

    for (size_t i = 0; i < copy_size; ++i) {
        dst[i] = src[i];
    }

    bootstrap_kfree(ptr);
    return new_ptr;
}

static int pointer_in_bootstrap_heap(const void *ptr) {
    return heap_ready &&
           ptr != NULL &&
           (const uint8_t *)ptr >= heap_start &&
           (const uint8_t *)ptr < heap_end;
}

void *kmalloc(size_t size) {
    if (MRAC_primary_allocator_active()) {
        const uint32_t pid = scheduler_current_pid();
        void *ptr = MRAC_alloc(pid, size);
        if (ptr != NULL) {
            return ptr;
        }
    }

    return bootstrap_kmalloc(size);
}

void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    if (pointer_in_bootstrap_heap(ptr)) {
        bootstrap_kfree(ptr);
        return;
    }

    if (MRAC_primary_allocator_active()) {
        MRAC_free(ptr);
    }
}

void *krealloc(void *ptr, size_t size) {
    const uint32_t pid = scheduler_current_pid();

    if (ptr == NULL) {
        return kmalloc(size);
    }

    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    if (pointer_in_bootstrap_heap(ptr) || !MRAC_primary_allocator_active()) {
        return bootstrap_krealloc(ptr, size);
    }

    return MRAC_realloc(pid, ptr, size);
}
