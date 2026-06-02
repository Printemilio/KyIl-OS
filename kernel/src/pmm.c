#include <unos/pmm.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/boot_requests.h>

#define PMM_PAGE_SIZE 4096ull
#define PMM_BITS_PER_BYTE 8ull

static uint8_t *pmm_bitmap;
static uint64_t pmm_bitmap_bytes;
static uint64_t pmm_total_pages;
static uint64_t pmm_bitmap_base_page;
static uint64_t pmm_bitmap_page_count;
static bool pmm_ready;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1ull);
}

static uint64_t bytes_for_pages(uint64_t page_count) {
    return (page_count + PMM_BITS_PER_BYTE - 1ull) / PMM_BITS_PER_BYTE;
}

static uint64_t pages_for_bytes(uint64_t byte_count) {
    return (byte_count + PMM_PAGE_SIZE - 1ull) / PMM_PAGE_SIZE;
}

static void bitmap_fill(uint8_t value) {
    for (uint64_t i = 0; i < pmm_bitmap_bytes; ++i) {
        pmm_bitmap[i] = value;
    }
}

static void bitmap_set(uint64_t page_index) {
    pmm_bitmap[page_index / PMM_BITS_PER_BYTE] |= (uint8_t)(1u << (page_index % PMM_BITS_PER_BYTE));
}

static void bitmap_clear(uint64_t page_index) {
    pmm_bitmap[page_index / PMM_BITS_PER_BYTE] &= (uint8_t)~(1u << (page_index % PMM_BITS_PER_BYTE));
}

static bool bitmap_test(uint64_t page_index) {
    return (pmm_bitmap[page_index / PMM_BITS_PER_BYTE] & (uint8_t)(1u << (page_index % PMM_BITS_PER_BYTE))) != 0u;
}

static uint64_t highest_physical_end(const struct limine_memmap_response *memmap) {
    uint64_t highest = 0;

    for (uint64_t i = 0; i < memmap->entry_count; ++i) {
        const struct limine_memmap_entry *entry = memmap->entries[i];
        const uint64_t end = entry->base + entry->length;

        if (end > highest) {
            highest = end;
        }
    }

    return highest;
}

static bool find_bitmap_region(
    const struct limine_memmap_response *memmap,
    uint64_t required_pages,
    uint64_t *base_page
) {
    for (uint64_t i = 0; i < memmap->entry_count; ++i) {
        const struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        const uint64_t base = align_up(entry->base, PMM_PAGE_SIZE);
        const uint64_t end = align_down(entry->base + entry->length, PMM_PAGE_SIZE);

        if (end <= base) {
            continue;
        }

        const uint64_t available_pages = (end - base) / PMM_PAGE_SIZE;
        if (available_pages >= required_pages) {
            *base_page = base / PMM_PAGE_SIZE;
            return true;
        }
    }

    return false;
}

static void mark_usable_regions_free(const struct limine_memmap_response *memmap) {
    for (uint64_t i = 0; i < memmap->entry_count; ++i) {
        const struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        const uint64_t base = align_up(entry->base, PMM_PAGE_SIZE);
        const uint64_t end = align_down(entry->base + entry->length, PMM_PAGE_SIZE);

        for (uint64_t addr = base; addr < end; addr += PMM_PAGE_SIZE) {
            bitmap_clear(addr / PMM_PAGE_SIZE);
        }
    }
}

static void mark_bitmap_region_used(void) {
    for (uint64_t i = 0; i < pmm_bitmap_page_count; ++i) {
        bitmap_set(pmm_bitmap_base_page + i);
    }
}

void pmm_init(struct limine_memmap_response *memmap) {
    pmm_ready = false;

    if (memmap == NULL || hhdm_request.response == NULL) {
        return;
    }

    const uint64_t highest = highest_physical_end(memmap);
    pmm_total_pages = pages_for_bytes(highest);
    pmm_bitmap_bytes = bytes_for_pages(pmm_total_pages);
    pmm_bitmap_page_count = pages_for_bytes(pmm_bitmap_bytes);

    if (pmm_total_pages == 0 || pmm_bitmap_page_count == 0) {
        return;
    }

    if (!find_bitmap_region(memmap, pmm_bitmap_page_count, &pmm_bitmap_base_page)) {
        return;
    }

    const uint64_t bitmap_physical = pmm_bitmap_base_page * PMM_PAGE_SIZE;
    pmm_bitmap = (uint8_t *)(hhdm_request.response->offset + bitmap_physical);

    /* Start pessimistic: every physical page is occupied until a usable Limine
       range proves otherwise. This keeps reserved, ACPI, framebuffer, kernel,
       and bootloader-owned regions unavailable by default. */
    bitmap_fill(0xffu);
    mark_usable_regions_free(memmap);
    mark_bitmap_region_used();

    pmm_ready = true;
}

void *pmm_alloc_page(void) {
    return pmm_alloc_contiguous_pages(1u);
}

void *pmm_alloc_contiguous_pages(uint64_t page_count) {
    if (!pmm_ready) {
        return NULL;
    }

    if (page_count == 0u || page_count > pmm_total_pages) {
        return NULL;
    }

    for (uint64_t page = 0; page + page_count <= pmm_total_pages; ++page) {
        uint64_t run = 0;

        while (run < page_count && !bitmap_test(page + run)) {
            ++run;
        }

        if (run == page_count) {
            for (uint64_t i = 0; i < page_count; ++i) {
                bitmap_set(page + i);
            }
            return (void *)(page * PMM_PAGE_SIZE);
        }

        page += run;
    }

    return NULL;
}

void pmm_free_page(void *addr) {
    pmm_free_contiguous_pages(addr, 1u);
}

void pmm_free_contiguous_pages(void *addr, uint64_t page_count) {
    if (!pmm_ready || addr == NULL) {
        return;
    }

    if (page_count == 0u) {
        return;
    }

    const uint64_t physical = (uint64_t)addr;
    if ((physical % PMM_PAGE_SIZE) != 0) {
        return;
    }

    const uint64_t page = physical / PMM_PAGE_SIZE;
    if (page >= pmm_total_pages || page + page_count > pmm_total_pages) {
        return;
    }

    for (uint64_t i = 0; i < page_count; ++i) {
        if (!bitmap_test(page + i)) {
            return;
        }
    }

    for (uint64_t i = 0; i < page_count; ++i) {
        bitmap_clear(page + i);
    }
}

void pmm_reclaim_boot_module_range(void *addr, uint64_t size) {
    uint64_t physical;
    uint64_t start;
    uint64_t end;

    if (!pmm_ready || addr == NULL || size == 0u) {
        return;
    }

    physical = (uint64_t)(uintptr_t)addr;
    if (hhdm_request.response != NULL && physical >= hhdm_request.response->offset) {
        physical -= hhdm_request.response->offset;
    }

    start = align_up(physical, PMM_PAGE_SIZE);
    end = align_down(physical + size, PMM_PAGE_SIZE);
    if (end <= start) {
        return;
    }

    pmm_free_contiguous_pages((void *)start, (end - start) / PMM_PAGE_SIZE);
}

uint64_t pmm_total_page_count(void) {
    return pmm_ready ? pmm_total_pages : 0u;
}

uint64_t pmm_used_page_count(void) {
    uint64_t used = 0u;

    if (!pmm_ready) {
        return 0u;
    }

    for (uint64_t page = 0; page < pmm_total_pages; ++page) {
        if (bitmap_test(page)) {
            ++used;
        }
    }

    return used;
}

uint64_t pmm_free_page_count(void) {
    const uint64_t total = pmm_total_page_count();
    const uint64_t used = pmm_used_page_count();
    return used > total ? 0u : total - used;
}
