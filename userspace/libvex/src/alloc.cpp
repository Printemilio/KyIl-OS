#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>
#include <unos/vex_abi.h>

#include "vex_runtime.h"

namespace {

static constexpr size_t kPageSize = 4096u;
static constexpr size_t kAlignment = 16u;
static constexpr size_t kMaxSmallAllocation = 2048u;
static constexpr size_t kClassCount = 8u;
static constexpr size_t kDirectAllocationLimit = 4096u;
static constexpr uint32_t kSlabMagic = 0x534c4142u;
static constexpr uint32_t kDirectMagic = 0x44495243u;

struct DirectAllocation {
    void* ptr;
    void* map_base;
    size_t size;
    size_t map_size;
};

struct SlabHeader {
    uint32_t magic;
    uint16_t class_index;
    uint16_t block_count;
    uint16_t free_count;
    uint16_t first_free_hint;
    SlabHeader* next;
    SlabHeader* prev;
    uint32_t bitmap[8];
};

struct DirectHeader {
    uint32_t magic;
    uint32_t reserved0;
    size_t size;
    size_t map_size;
};

static constexpr size_t kClassSizes[kClassCount] = {
    16u, 32u, 64u, 128u, 256u, 512u, 1024u, 2048u
};

static DirectAllocation g_direct_allocations[kDirectAllocationLimit];
static SlabHeader* g_slab_heads[kClassCount];

static size_t round_up(size_t value, size_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uintptr_t align_down_uintptr(uintptr_t value, uintptr_t alignment) {
    return value & ~(alignment - 1u);
}

static int class_index_for_size(size_t size) {
    for (size_t i = 0; i < kClassCount; ++i) {
        if (size <= kClassSizes[i]) {
            return (int)i;
        }
    }
    return -1;
}

static size_t slab_payload_offset() {
    return round_up(sizeof(SlabHeader), kAlignment);
}

static size_t slab_block_count_for_class(size_t class_size) {
    const size_t usable = kPageSize - slab_payload_offset();
    const size_t count = usable / class_size;
    return count > 0u ? count : 0u;
}

static void remember_direct_allocation(void* ptr, void* map_base, size_t size, size_t map_size) {
    for (size_t i = 0; i < kDirectAllocationLimit; ++i) {
        if (g_direct_allocations[i].ptr == nullptr) {
            g_direct_allocations[i].ptr = ptr;
            g_direct_allocations[i].map_base = map_base;
            g_direct_allocations[i].size = size;
            g_direct_allocations[i].map_size = map_size;
            return;
        }
    }
}

static DirectAllocation* find_direct_allocation(void* ptr) {
    for (size_t i = 0; i < kDirectAllocationLimit; ++i) {
        if (g_direct_allocations[i].ptr == ptr) {
            return &g_direct_allocations[i];
        }
    }
    return nullptr;
}

static void forget_direct_allocation(void* ptr) {
    for (size_t i = 0; i < kDirectAllocationLimit; ++i) {
        if (g_direct_allocations[i].ptr == ptr) {
            g_direct_allocations[i].ptr = nullptr;
            g_direct_allocations[i].map_base = nullptr;
            g_direct_allocations[i].size = 0u;
            g_direct_allocations[i].map_size = 0u;
            return;
        }
    }
}

static void link_slab(SlabHeader* slab) {
    SlabHeader*& head = g_slab_heads[slab->class_index];
    slab->prev = nullptr;
    slab->next = head;
    if (head != nullptr) {
        head->prev = slab;
    }
    head = slab;
}

static void unlink_slab(SlabHeader* slab) {
    SlabHeader*& head = g_slab_heads[slab->class_index];

    if (slab->prev != nullptr) {
        slab->prev->next = slab->next;
    } else {
        head = slab->next;
    }
    if (slab->next != nullptr) {
        slab->next->prev = slab->prev;
    }
    slab->prev = nullptr;
    slab->next = nullptr;
}

static SlabHeader* allocate_slab(size_t class_index) {
    void* page = mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    SlabHeader* slab;
    size_t block_count;

    if (page == MAP_FAILED) {
        return nullptr;
    }

    slab = (SlabHeader*)page;
    block_count = slab_block_count_for_class(kClassSizes[class_index]);
    if (block_count == 0u || block_count > 256u) {
        (void)munmap(page, kPageSize);
        return nullptr;
    }

    slab->magic = kSlabMagic;
    slab->class_index = (uint16_t)class_index;
    slab->block_count = (uint16_t)block_count;
    slab->free_count = (uint16_t)block_count;
    slab->first_free_hint = 0u;
    slab->prev = nullptr;
    slab->next = nullptr;
    for (size_t i = 0; i < 8u; ++i) {
        slab->bitmap[i] = 0u;
    }

    link_slab(slab);
    return slab;
}

static void* slab_block_ptr(SlabHeader* slab, size_t block_index) {
    const size_t class_size = kClassSizes[slab->class_index];
    uint8_t* base = (uint8_t*)slab + slab_payload_offset();
    return base + (block_index * class_size);
}

static int slab_block_index_from_ptr(SlabHeader* slab, void* ptr) {
    uint8_t* block_base = (uint8_t*)slab + slab_payload_offset();
    uintptr_t offset;
    size_t class_size;

    if ((uint8_t*)ptr < block_base || (uint8_t*)ptr >= (uint8_t*)slab + kPageSize) {
        return -1;
    }

    class_size = kClassSizes[slab->class_index];
    offset = (uintptr_t)((uint8_t*)ptr - block_base);
    if ((offset % class_size) != 0u) {
        return -1;
    }

    offset /= class_size;
    if (offset >= slab->block_count) {
        return -1;
    }
    return (int)offset;
}

static int slab_block_is_used(const SlabHeader* slab, size_t block_index) {
    const size_t word = block_index / 32u;
    const uint32_t mask = 1u << (block_index % 32u);
    return (slab->bitmap[word] & mask) != 0u;
}

static void slab_mark_used(SlabHeader* slab, size_t block_index) {
    const size_t word = block_index / 32u;
    const uint32_t mask = 1u << (block_index % 32u);

    slab->bitmap[word] |= mask;
}

static void slab_mark_free(SlabHeader* slab, size_t block_index) {
    const size_t word = block_index / 32u;
    const uint32_t mask = 1u << (block_index % 32u);

    slab->bitmap[word] &= ~mask;
}

static int slab_find_free_block(SlabHeader* slab) {
    const size_t start = slab->first_free_hint < slab->block_count ? slab->first_free_hint : 0u;

    for (size_t i = start; i < slab->block_count; ++i) {
        if (!slab_block_is_used(slab, i)) {
            return (int)i;
        }
    }
    for (size_t i = 0u; i < start; ++i) {
        if (!slab_block_is_used(slab, i)) {
            return (int)i;
        }
    }
    return -1;
}

static void* allocate_small(size_t size) {
    const int class_index = class_index_for_size(size);
    SlabHeader* slab;
    int block_index;

    if (class_index < 0) {
        return nullptr;
    }

    for (slab = g_slab_heads[class_index]; slab != nullptr; slab = slab->next) {
        if (slab->free_count == 0u) {
            continue;
        }
        block_index = slab_find_free_block(slab);
        if (block_index >= 0) {
            slab_mark_used(slab, (size_t)block_index);
            --slab->free_count;
            slab->first_free_hint = (uint16_t)((size_t)block_index + 1u);
            return slab_block_ptr(slab, (size_t)block_index);
        }
    }

    slab = allocate_slab((size_t)class_index);
    if (slab == nullptr) {
        return nullptr;
    }

    slab_mark_used(slab, 0u);
    --slab->free_count;
    slab->first_free_hint = 1u;
    return slab_block_ptr(slab, 0u);
}

static void* allocate_direct(size_t size) {
    const size_t map_size = round_up(sizeof(DirectHeader) + size, kPageSize);
    DirectHeader* header = (DirectHeader*)mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void* ptr;

    if (header == MAP_FAILED) {
        return nullptr;
    }

    header->magic = kDirectMagic;
    header->reserved0 = 0u;
    header->size = size;
    header->map_size = map_size;
    ptr = (uint8_t*)header + round_up(sizeof(DirectHeader), kAlignment);
    remember_direct_allocation(ptr, header, size, map_size);
    return ptr;
}

static size_t allocation_capacity(void* ptr) {
    SlabHeader* slab;
    DirectAllocation* direct;
    DirectHeader* direct_header;

    if (ptr == nullptr) {
        return 0u;
    }

    slab = (SlabHeader*)align_down_uintptr((uintptr_t)ptr, kPageSize);
    if (slab->magic == kSlabMagic) {
        const int block_index = slab_block_index_from_ptr(slab, ptr);
        if (block_index >= 0 && slab_block_is_used(slab, (size_t)block_index)) {
            return kClassSizes[slab->class_index];
        }
    }

    direct = find_direct_allocation(ptr);
    if (direct != nullptr) {
        direct_header = (DirectHeader*)direct->map_base;
        if (direct_header->magic == kDirectMagic) {
            return direct_header->size;
        }
        return direct->size;
    }

    return 0u;
}

}

extern "C" void* malloc(size_t size) {
    if (size == 0u) {
        size = 1u;
    }

    if (size <= kMaxSmallAllocation) {
        return allocate_small(size);
    }

    return allocate_direct(round_up(size, kAlignment));
}

extern "C" void free(void* ptr) {
    SlabHeader* slab;
    DirectAllocation* direct;

    if (ptr == nullptr) {
        return;
    }

    slab = (SlabHeader*)align_down_uintptr((uintptr_t)ptr, kPageSize);
    if (slab->magic == kSlabMagic) {
        const int block_index = slab_block_index_from_ptr(slab, ptr);
        if (block_index < 0 || !slab_block_is_used(slab, (size_t)block_index)) {
            return;
        }

        slab_mark_free(slab, (size_t)block_index);
        ++slab->free_count;
        if ((uint16_t)block_index < slab->first_free_hint) {
            slab->first_free_hint = (uint16_t)block_index;
        }

        if (slab->free_count == slab->block_count) {
            unlink_slab(slab);
            (void)munmap(slab, kPageSize);
        }
        return;
    }

    direct = find_direct_allocation(ptr);
    if (direct != nullptr) {
        void* map_base = direct->map_base;
        size_t map_size = direct->map_size;
        forget_direct_allocation(ptr);
        (void)munmap(map_base, map_size);
    }
}

extern "C" void* calloc(size_t count, size_t size) {
    size_t total;
    unsigned char* ptr;

    if (count != 0u && size > ((size_t)-1) / count) {
        return nullptr;
    }

    total = count * size;
    ptr = (unsigned char*)malloc(total);
    if (ptr == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < total; ++i) {
        ptr[i] = 0;
    }
    return ptr;
}

extern "C" void* realloc(void* ptr, size_t size) {
    void* resized;
    size_t current_capacity;
    size_t copy_size;

    if (ptr == nullptr) {
        return malloc(size);
    }

    if (size == 0u) {
        free(ptr);
        return nullptr;
    }

    current_capacity = allocation_capacity(ptr);
    if (current_capacity != 0u) {
        const int old_class_index = current_capacity <= kMaxSmallAllocation ? class_index_for_size(current_capacity) : -1;
        const int new_class_index = size <= kMaxSmallAllocation ? class_index_for_size(size) : -1;

        if (old_class_index >= 0 && old_class_index == new_class_index) {
            return ptr;
        }
        if (old_class_index < 0 && size <= current_capacity) {
            return ptr;
        }
    }

    resized = malloc(size);
    if (resized == nullptr) {
        return nullptr;
    }

    copy_size = current_capacity < size ? current_capacity : size;
    if (copy_size != 0u) {
        __builtin_memcpy(resized, ptr, copy_size);
    }

    free(ptr);
    return resized;
}

void* operator new(size_t size) {
    void* ptr = malloc(size);
    if (ptr == nullptr) {
        abort();
    }
    return ptr;
}

void* operator new[](size_t size) {
    return operator new(size);
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    free(ptr);
}

extern "C" int posix_memalign(void** memptr, size_t alignment, size_t size) {
    if (memptr == nullptr || alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0u) {
        return EINVAL;
    }
    if (size == 0u) {
        size = 1u;
    }

    size_t request_size = round_up(size + alignment - 1u, kAlignment);
    void* map_base = mmap(nullptr, request_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map_base == MAP_FAILED) {
        return ENOMEM;
    }

    uintptr_t raw = (uintptr_t)map_base;
    void* aligned = (void*)round_up(raw, alignment);
    remember_direct_allocation(aligned, map_base, size, request_size);
    *memptr = aligned;
    return 0;
}

extern "C" size_t malloc_good_size(size_t size) {
    const int class_index = class_index_for_size(size == 0u ? 1u : size);

    if (class_index >= 0) {
        return kClassSizes[class_index];
    }
    return round_up(size, kAlignment);
}
