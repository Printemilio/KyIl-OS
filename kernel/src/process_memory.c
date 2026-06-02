#include <unos/process_memory.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/boot_requests.h>
#include <unos/debug_log.h>
#include <unos/heap.h>
#include <unos/vex_legacy_compat.h>
#include <unos/mrac.h>
#include <unos/pmm.h>
#include <unos/scheduler.h>
#include <unos/vmm.h>

#define PROCESS_MEMORY_ROOT_MAGIC           0x4d5241434e53524full
#define PROCESS_MEMORY_ROOT_KEY_TAG         0x726f6f742d6e7300ull
#define PROCESS_MEMORY_DESC_KEY_TAG         0x646573632d6e7300ull
#define PROCESS_MEMORY_REGION_KEY_TAG       0x726567696f6e0000ull
#define PROCESS_MEMORY_BACKING_KEY_TAG      0x6261636b696e0000ull
#define PROCESS_MEMORY_PAGE_SIZE            4096ull

typedef struct process_memory_namespace_root {
    uint64_t magic;
    uint64_t namespace_id;
    uint64_t alloc_hint;
    uint32_t region_count;
    uint32_t reserved0;
} process_memory_namespace_root_t;

static uint64_t next_namespace_id = 1u;

static uint64_t align_down_u64(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1ull);
}

static uint64_t align_up_u64(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

static uint64_t hash_mix_u64(uint64_t seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
    return seed;
}

static uint64_t namespace_desc_key(uint64_t namespace_id) {
    return hash_mix_u64(PROCESS_MEMORY_DESC_KEY_TAG, namespace_id);
}

static uint64_t namespace_region_key(uint64_t namespace_id, uint64_t addr) {
    return hash_mix_u64(hash_mix_u64(PROCESS_MEMORY_REGION_KEY_TAG, namespace_id), addr);
}

static uint64_t namespace_backing_key(uint64_t namespace_id, uint64_t addr) {
    return hash_mix_u64(hash_mix_u64(PROCESS_MEMORY_BACKING_KEY_TAG, namespace_id), addr);
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

static void debug_put_dec_u64(uint64_t value) {
    char buffer[32];
    size_t length = 0u;

    if (value == 0u) {
        debug_log_puts("0");
        return;
    }

    while (value != 0u && length < sizeof(buffer)) {
        buffer[length++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (length != 0u) {
        debug_log_putchar_local(buffer[--length]);
    }
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

static process_memory_namespace_root_t *copy_root_from_key(uint32_t pid, uint64_t key_hash) {
    process_memory_namespace_root_t *root;
    process_memory_namespace_root_t *copy;

    root = MRAC_lookup(pid, key_hash);
    if (root == NULL) {
        return NULL;
    }
    if (root->magic != PROCESS_MEMORY_ROOT_MAGIC) {
        MRAC_release(pid, key_hash);
        return NULL;
    }

    copy = alloc_root_copy(root->namespace_id, root->alloc_hint, root->region_count);
    if (copy != NULL && root->region_count != 0u) {
        memcpy(root_addresses(copy), root_addresses_const(root), (size_t)root->region_count * sizeof(uint64_t));
    }

    MRAC_release(pid, key_hash);
    return copy;
}

static process_memory_namespace_root_t *copy_active_root(uint32_t pid) {
    return copy_root_from_key(pid, PROCESS_MEMORY_ROOT_KEY_TAG);
}

static int store_root_copy(uint32_t pid, uint64_t key_hash, const process_memory_namespace_root_t *root) {
    if (root == NULL) {
        return -1;
    }

    return MRAC_store_with_hash(pid, key_hash, (void *)root, (size_t)namespace_root_payload_size(root->region_count)) == NULL ? -1 : 0;
}

static uint64_t allocate_namespace_id(void) {
    if (next_namespace_id == 0u) {
        ++next_namespace_id;
    }
    return next_namespace_id++;
}

static int ensure_active_root(uint32_t pid) {
    process_memory_namespace_root_t *root;
    uint64_t namespace_id;

    root = copy_active_root(pid);
    if (root != NULL) {
        kfree(root);
        return 0;
    }

    if (process_memory_namespace_begin(pid, 0u, &namespace_id) != 0) {
        return -1;
    }
    return process_memory_namespace_commit(pid, namespace_id);
}

static int root_index_of_addr(const process_memory_namespace_root_t *root, uint64_t addr, uint32_t *index_out) {
    if (root == NULL) {
        return -1;
    }

    for (uint32_t index = 0u; index < root->region_count; ++index) {
        if (root_addresses_const(root)[index] == addr) {
            if (index_out != NULL) {
                *index_out = index;
            }
            return 0;
        }
    }

    return -1;
}

static int region_flags_from_prot(int prot) {
    uint64_t flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;

    if ((prot & PROT_WRITE) != 0) {
        flags |= VMM_PAGE_WRITABLE;
    }
    return (int)flags;
}

static int region_can_use_mrac_direct(const process_memory_region_t *region) {
    return region != NULL &&
        (region->kind_flags & PROCESS_MEMORY_FLAG_MRAC_DIRECT) != 0u &&
        (region->prot & PROT_WRITE) == 0 &&
        (region->addr & (PROCESS_MEMORY_PAGE_SIZE - 1ull)) == 0u &&
        region->direct_source != 0u;
}

static int load_region_meta_in_namespace(uint32_t pid, uint64_t namespace_id, uint64_t addr, process_memory_region_t *out) {
    process_memory_region_t *region;
    const uint64_t key = namespace_region_key(namespace_id, addr);

    if (out == NULL) {
        return -1;
    }

    region = MRAC_lookup(pid, key);
    if (region == NULL) {
        return -1;
    }
    if (region->magic != PROCESS_MEMORY_REGION_MAGIC) {
        MRAC_release(pid, key);
        return -1;
    }

    memcpy(out, region, sizeof(*out));
    MRAC_release(pid, key);
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

    end = align_up_u64(region->addr + region->length, PROCESS_MEMORY_PAGE_SIZE);
    if (end > root->alloc_hint) {
        root->alloc_hint = end;
    }
}

static int materialize_region_to_cr3(uint64_t cr3, const process_memory_region_t *region, const void *data) {
    void *pml4;
    const uint8_t *bytes = data;
    const uint64_t start = align_down_u64(region->addr, PROCESS_MEMORY_PAGE_SIZE);
    const uint64_t end = align_up_u64(region->addr + region->length, PROCESS_MEMORY_PAGE_SIZE);
    const uint64_t flags = (uint64_t)region_flags_from_prot(region->prot);

    if (region == NULL || data == NULL) {
        return -1;
    }

    pml4 = vmm_cr3_to_address_space(cr3);
    if (pml4 == NULL) {
        return -1;
    }

    if (region_can_use_mrac_direct(region)) {
        for (uint64_t page = start; page < end; page += PROCESS_MEMORY_PAGE_SIZE) {
            uint64_t phys;
            const size_t offset = (size_t)(page - region->addr);

            if (MRAC_direct_physical_page(data, offset, &phys) != 0) {
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
        copy_end = (page + PROCESS_MEMORY_PAGE_SIZE) < (region->addr + region->length)
            ? (page + PROCESS_MEMORY_PAGE_SIZE)
            : (region->addr + region->length);
        if (copy_end > copy_start) {
            memcpy(dst + (copy_start - page), bytes + (copy_start - region->addr), (size_t)(copy_end - copy_start));
        }

        vmm_map_page(pml4, page, (uint64_t)phys, flags);
    }

    return 0;
}

uint64_t process_memory_region_key(uint32_t pid, uint64_t addr) {
    process_memory_namespace_root_t *root = copy_active_root(pid);
    uint64_t key = 0u;

    if (root != NULL) {
        key = namespace_region_key(root->namespace_id, addr);
        kfree(root);
    }
    return key;
}

uint64_t process_memory_backing_key(uint32_t pid, uint64_t addr) {
    process_memory_namespace_root_t *root = copy_active_root(pid);
    uint64_t key = 0u;

    if (root != NULL) {
        key = namespace_backing_key(root->namespace_id, addr);
        kfree(root);
    }
    return key;
}

int process_memory_namespace_begin(uint32_t pid, uint64_t alloc_hint, uint64_t *namespace_id_out) {
    process_memory_namespace_root_t *root;
    uint64_t namespace_id;

    if (pid == 0u || namespace_id_out == NULL) {
        return -1;
    }

    namespace_id = allocate_namespace_id();
    root = alloc_root_copy(namespace_id, alloc_hint, 0u);
    if (root == NULL) {
        return -1;
    }

    if (store_root_copy(pid, namespace_desc_key(namespace_id), root) != 0) {
        kfree(root);
        return -1;
    }

    *namespace_id_out = namespace_id;
    kfree(root);
    return 0;
}

uint64_t process_memory_current_namespace(uint32_t pid) {
    process_memory_namespace_root_t *root = copy_active_root(pid);
    uint64_t namespace_id = 0u;

    if (root != NULL) {
        namespace_id = root->namespace_id;
        kfree(root);
    }
    return namespace_id;
}

int process_memory_snapshot_namespace(uint32_t pid, uint64_t source_namespace_id, uint64_t *snapshot_namespace_id_out) {
    process_memory_namespace_root_t *source_root;
    uint64_t snapshot_namespace_id;

    if (pid == 0u || source_namespace_id == 0u || snapshot_namespace_id_out == NULL) {
        return -1;
    }

    source_root = copy_root_from_key(pid, namespace_desc_key(source_namespace_id));
    if (source_root == NULL) {
        return -1;
    }

    if (process_memory_namespace_begin(pid, source_root->alloc_hint, &snapshot_namespace_id) != 0) {
        kfree(source_root);
        return -1;
    }

    for (uint32_t index = 0u; index < source_root->region_count; ++index) {
        process_memory_region_t region;
        void *backing;
        const uint64_t addr = root_addresses(source_root)[index];

        if (load_region_meta_in_namespace(pid, source_namespace_id, addr, &region) != 0) {
            (void)process_memory_namespace_destroy(pid, snapshot_namespace_id, 0u);
            kfree(source_root);
            return -1;
        }

        backing = MRAC_lookup(pid, region.backing_key);
        if (backing == NULL) {
            (void)process_memory_namespace_destroy(pid, snapshot_namespace_id, 0u);
            kfree(source_root);
            return -1;
        }

        if (process_memory_register_region_in_namespace(pid, snapshot_namespace_id, addr, region.length, region.prot,
                region.flags, region.kind_flags, backing) != 0) {
            MRAC_release(pid, region.backing_key);
            (void)process_memory_namespace_destroy(pid, snapshot_namespace_id, 0u);
            kfree(source_root);
            return -1;
        }
        MRAC_release(pid, region.backing_key);
    }

    kfree(source_root);
    *snapshot_namespace_id_out = snapshot_namespace_id;
    return 0;
}

int process_memory_namespace_commit(uint32_t pid, uint64_t namespace_id) {
    process_memory_namespace_root_t *root = copy_root_from_key(pid, namespace_desc_key(namespace_id));
    int result;

    if (root == NULL) {
        return -1;
    }

    result = store_root_copy(pid, PROCESS_MEMORY_ROOT_KEY_TAG, root);
    kfree(root);
    return result;
}

int process_memory_namespace_destroy(uint32_t pid, uint64_t namespace_id, uint64_t cr3) {
    process_memory_namespace_root_t *root = copy_root_from_key(pid, namespace_desc_key(namespace_id));
    void *pml4 = cr3 == 0u ? NULL : vmm_cr3_to_address_space(cr3);

    if (root == NULL) {
        return 0;
    }

    for (uint32_t index = 0u; index < root->region_count; ++index) {
        process_memory_region_t region;
        uint64_t addr = root_addresses(root)[index];

        if (load_region_meta_in_namespace(pid, namespace_id, addr, &region) == 0 && pml4 != NULL) {
            vmm_unmap_range(pml4, addr, region.length, (region.kind_flags & PROCESS_MEMORY_FLAG_MRAC_DIRECT) != 0u ? 0u : 1u);
        }

        MRAC_remove(pid, namespace_region_key(namespace_id, addr));
        MRAC_remove(pid, namespace_backing_key(namespace_id, addr));
    }

    MRAC_remove(pid, namespace_desc_key(namespace_id));
    kfree(root);
    return 0;
}

int process_memory_register_region_in_namespace(uint32_t pid, uint64_t namespace_id, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data) {
    return process_memory_register_region_in_namespace_partial(pid, namespace_id, addr, length, prot, flags, kind_flags, data, length);
}

int process_memory_register_region_in_namespace_partial(uint32_t pid, uint64_t namespace_id, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data, size_t data_size) {
    process_memory_namespace_root_t *root;
    process_memory_namespace_root_t *updated_root;
    process_memory_region_t region;
    void *stored_backing;
    uint32_t region_index = 0u;
    int replacing = 0;

    if (pid == 0u || namespace_id == 0u || addr == 0u || length == 0u || data == NULL || data_size > length) {
        return -1;
    }

    root = copy_root_from_key(pid, namespace_desc_key(namespace_id));
    if (root == NULL) {
        return -1;
    }

    if (root_index_of_addr(root, addr, &region_index) == 0) {
        replacing = 1;
        updated_root = duplicate_root_copy(root, root->region_count);
    } else {
        updated_root = duplicate_root_copy(root, root->region_count + 1u);
    }
    if (updated_root == NULL) {
        kfree(root);
        return -1;
    }

    region.magic = PROCESS_MEMORY_REGION_MAGIC;
    region.addr = addr;
    region.length = length;
    region.prot = prot;
    region.flags = flags;
    region.kind_flags = kind_flags & ~PROCESS_MEMORY_FLAG_MRAC_DIRECT;
    region.reserved0 = 0u;
    region.backing_key = namespace_backing_key(namespace_id, addr);
    region.direct_source = 0u;

    if ((prot & PROT_WRITE) == 0 &&
        (addr & (PROCESS_MEMORY_PAGE_SIZE - 1ull)) == 0u &&
        data_size == length &&
        MRAC_can_direct_map(data, length) != 0) {
        region.kind_flags |= PROCESS_MEMORY_FLAG_MRAC_DIRECT;
        region.direct_source = (uint64_t)(uintptr_t)data;
    } else {
        stored_backing = MRAC_store_with_hash_zero_extended(pid, region.backing_key, data, data_size, length);
        if (stored_backing == NULL) {
            debug_log_puts("pmem: backing store failed len=");
            debug_put_dec_u64(length);
            debug_log_puts(" addr=");
            debug_put_hex64_local(addr);
            debug_log_puts(" free_pages=");
            debug_put_dec_u64(pmm_free_page_count());
            debug_log_puts("\n");
            kfree(updated_root);
            kfree(root);
            return -1;
        }
    }

    if (MRAC_store_with_hash(pid, namespace_region_key(namespace_id, addr), &region, sizeof(region)) == NULL) {
        debug_log_puts("pmem: region meta store failed addr=");
        debug_put_hex64_local(addr);
        debug_log_puts("\n");
        MRAC_remove(pid, region.backing_key);
        kfree(updated_root);
        kfree(root);
        return -1;
    }

    if (replacing == 0) {
        root_addresses(updated_root)[root->region_count] = addr;
    }
    update_root_alloc_hint_for_region(updated_root, &region);

    if (store_root_copy(pid, namespace_desc_key(namespace_id), updated_root) != 0) {
        debug_log_puts("pmem: root store failed count=");
        debug_put_dec_u64(updated_root->region_count);
        debug_log_puts(" addr=");
        debug_put_hex64_local(addr);
        debug_log_puts("\n");
        if (replacing == 0) {
            MRAC_remove(pid, namespace_region_key(namespace_id, addr));
            MRAC_remove(pid, namespace_backing_key(namespace_id, addr));
        }
        kfree(updated_root);
        kfree(root);
        return -1;
    }

    kfree(updated_root);
    kfree(root);
    return 0;
}

int process_memory_materialize_namespace(uint32_t pid, uint64_t namespace_id, uint64_t cr3) {
    process_memory_namespace_root_t *root = copy_root_from_key(pid, namespace_desc_key(namespace_id));

    if (root == NULL) {
        debug_log_puts("pmem: root lookup failed\n");
        return -1;
    }

    for (uint32_t index = 0u; index < root->region_count; ++index) {
        process_memory_region_t region;
        void *backing;
        int release_backing = 0;

        if (load_region_meta_in_namespace(pid, namespace_id, root_addresses(root)[index], &region) != 0) {
            debug_log_puts("pmem: region meta load failed\n");
            kfree(root);
            return -1;
        }

        if (region_can_use_mrac_direct(&region)) {
            backing = (void *)(uintptr_t)region.direct_source;
        } else {
            backing = MRAC_lookup(pid, region.backing_key);
            if (backing == NULL) {
                debug_log_puts("pmem: backing lookup failed\n");
                kfree(root);
                return -1;
            }
            release_backing = 1;
        }
        if (materialize_region_to_cr3(cr3, &region, backing) != 0) {
            debug_log_puts("pmem: materialize region failed\n");
            if (release_backing != 0) {
                MRAC_release(pid, region.backing_key);
            }
            kfree(root);
            return -1;
        }
        if (release_backing != 0) {
            MRAC_release(pid, region.backing_key);
        }
    }

    kfree(root);
    return 0;
}

int process_memory_register_region(uint32_t pid, uint64_t addr, size_t length, int prot, int flags, uint32_t kind_flags, const void *data) {
    process_memory_namespace_root_t *root;
    uint64_t namespace_id;
    int result;

    if (ensure_active_root(pid) != 0) {
        return -1;
    }

    root = copy_active_root(pid);
    if (root == NULL) {
        return -1;
    }
    namespace_id = root->namespace_id;
    kfree(root);

    result = process_memory_register_region_in_namespace(pid, namespace_id, addr, length, prot, flags, kind_flags, data);
    if (result != 0) {
        return -1;
    }
    return process_memory_namespace_commit(pid, namespace_id);
}

int process_memory_materialize_region(uint32_t pid, uint64_t cr3, uint64_t addr) {
    process_memory_namespace_root_t *root;
    process_memory_region_t region;
    void *backing;
    int result;
    int release_backing = 0;

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

    if (region_can_use_mrac_direct(&region)) {
        backing = (void *)(uintptr_t)region.direct_source;
    } else {
        backing = MRAC_lookup(pid, region.backing_key);
        if (backing == NULL) {
            return -1;
        }
        release_backing = 1;
    }

    result = materialize_region_to_cr3(cr3, &region, backing);
    if (release_backing != 0) {
        MRAC_release(pid, region.backing_key);
    }
    return result;
}

int process_memory_snapshot_region(uint32_t pid, uint64_t cr3, uint64_t addr) {
    process_memory_namespace_root_t *root;
    process_memory_region_t region;
    uint8_t *buffer;
    void *pml4;
    uint64_t region_key;

    if (ensure_active_root(pid) != 0) {
        return -1;
    }

    root = copy_active_root(pid);
    if (root == NULL) {
        return -1;
    }
    if (load_region_meta_in_namespace(pid, root->namespace_id, addr, &region) != 0) {
        kfree(root);
        return -1;
    }
    region_key = namespace_region_key(root->namespace_id, addr);
    kfree(root);

    pml4 = vmm_cr3_to_address_space(cr3);
    if (pml4 == NULL) {
        return -1;
    }

    buffer = kmalloc(region.length);
    if (buffer == NULL) {
        return -1;
    }

    for (size_t offset = 0u; offset < region.length; ++offset) {
        const uint64_t phys = vmm_get_physical(pml4, region.addr + offset);
        if (phys == 0u) {
            kfree(buffer);
            return -1;
        }
        buffer[offset] = *(uint8_t *)(hhdm_request.response->offset + phys);
    }

    region.kind_flags &= ~PROCESS_MEMORY_FLAG_MRAC_DIRECT;
    region.direct_source = 0u;
    if ((MRAC_modify_with_hash(pid, region.backing_key, buffer, region.length) == NULL &&
            MRAC_store_with_hash(pid, region.backing_key, buffer, region.length) == NULL) ||
        MRAC_modify_with_hash(pid, region_key, &region, sizeof(region)) == NULL) {
        kfree(buffer);
        return -1;
    }

    kfree(buffer);
    return 0;
}

int process_memory_snapshot_all(uint32_t pid, uint64_t cr3) {
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
    const uint64_t namespace_id = process_memory_current_namespace(pid);

    if (namespace_id == 0u) {
        return 0;
    }
    return process_memory_materialize_namespace(pid, namespace_id, cr3);
}

int process_memory_clone_namespace(uint32_t src_pid, uint64_t src_cr3, uint32_t dst_pid, uint64_t dst_cr3) {
    process_memory_namespace_root_t *src_root;
    uint64_t dst_namespace_id;

    if (process_memory_snapshot_all(src_pid, src_cr3) != 0) {
        return -1;
    }

    src_root = copy_active_root(src_pid);
    if (src_root == NULL) {
        return -1;
    }

    if (process_memory_namespace_begin(dst_pid, src_root->alloc_hint, &dst_namespace_id) != 0) {
        kfree(src_root);
        return -1;
    }

    for (uint32_t index = 0u; index < src_root->region_count; ++index) {
        process_memory_region_t region;
        void *backing;
        int release_backing = 0;
        const uint64_t addr = root_addresses(src_root)[index];

        if (load_region_meta_in_namespace(src_pid, src_root->namespace_id, addr, &region) != 0) {
            kfree(src_root);
            return -1;
        }

        if (region_can_use_mrac_direct(&region)) {
            backing = (void *)(uintptr_t)region.direct_source;
        } else {
            backing = MRAC_lookup(src_pid, region.backing_key);
            if (backing == NULL) {
                kfree(src_root);
                return -1;
            }
            release_backing = 1;
        }

        if (process_memory_register_region_in_namespace_partial(dst_pid, dst_namespace_id, addr, region.length,
                region.prot, region.flags, region.kind_flags, backing, region.length) != 0) {
            if (release_backing != 0) {
                MRAC_release(src_pid, region.backing_key);
            }
            kfree(src_root);
            return -1;
        }
        if (release_backing != 0) {
            MRAC_release(src_pid, region.backing_key);
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

int process_memory_remove_region(uint32_t pid, uint64_t cr3, uint64_t addr, size_t length) {
    process_memory_namespace_root_t *root;
    process_memory_namespace_root_t *updated_root;
    process_memory_region_t region;
    void *pml4 = cr3 == 0u ? NULL : vmm_cr3_to_address_space(cr3);
    uint32_t remove_index;

    if (ensure_active_root(pid) != 0) {
        return -1;
    }

    root = copy_active_root(pid);
    if (root == NULL) {
        return -1;
    }
    if (root_index_of_addr(root, addr, &remove_index) != 0 ||
        load_region_meta_in_namespace(pid, root->namespace_id, addr, &region) != 0) {
        kfree(root);
        return -1;
    }

    updated_root = alloc_root_copy(root->namespace_id, root->alloc_hint, root->region_count - 1u);
    if (updated_root == NULL) {
        kfree(root);
        return -1;
    }

    for (uint32_t src = 0u, dst = 0u; src < root->region_count; ++src) {
        if (src == remove_index) {
            continue;
        }
        root_addresses(updated_root)[dst++] = root_addresses(root)[src];
    }

    if (store_root_copy(pid, namespace_desc_key(root->namespace_id), updated_root) != 0 ||
        process_memory_namespace_commit(pid, root->namespace_id) != 0) {
        kfree(updated_root);
        kfree(root);
        return -1;
    }

    if (pml4 != NULL) {
        vmm_unmap_range(pml4, addr, length == 0u ? region.length : length, (region.kind_flags & PROCESS_MEMORY_FLAG_MRAC_DIRECT) != 0u ? 0u : 1u);
    }
    MRAC_remove(pid, namespace_region_key(root->namespace_id, addr));
    MRAC_remove(pid, namespace_backing_key(root->namespace_id, addr));

    kfree(updated_root);
    kfree(root);
    return 0;
}

int process_memory_protect_region(uint32_t pid, uint64_t addr, size_t length, int prot) {
    process_memory_namespace_root_t *root;
    process_memory_region_t region;
    (void)length;

    if (ensure_active_root(pid) != 0) {
        return -1;
    }

    root = copy_active_root(pid);
    if (root == NULL) {
        return -1;
    }
    if (load_region_meta_in_namespace(pid, root->namespace_id, addr, &region) != 0) {
        kfree(root);
        return -1;
    }

    region.prot = prot;
    if (MRAC_modify_with_hash(pid, namespace_region_key(root->namespace_id, addr), &region, sizeof(region)) == NULL) {
        kfree(root);
        return -1;
    }

    kfree(root);
    return 0;
}

int process_memory_clear_namespace(uint32_t pid, uint64_t cr3) {
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
    process_memory_namespace_root_t *root = copy_active_root(pid);
    uint64_t hint = align_up_u64(floor, PROCESS_MEMORY_PAGE_SIZE);

    if (root != NULL) {
        if (root->alloc_hint > hint) {
            hint = root->alloc_hint;
        }
        kfree(root);
    }

    return hint;
}

int process_memory_region_contains(process_t *process, uint64_t address, size_t length) {
    process_memory_namespace_root_t *root;
    uint64_t end;
    int found = 0;

    if (process == NULL) {
        return 0;
    }
    if (length == 0u) {
        return 1;
    }
    if (length - 1u > UINT64_MAX - address) {
        return 0;
    }

    end = address + (uint64_t)length;
    if (end < address) {
        return 0;
    }

    root = copy_active_root(process->pid);
    if (root == NULL) {
        return 0;
    }

    for (uint32_t index = 0u; index < root->region_count; ++index) {
        process_memory_region_t region;
        uint64_t region_end;
        const uint64_t region_addr = root_addresses_const(root)[index];

        if (load_region_meta_in_namespace(process->pid, root->namespace_id, region_addr, &region) != 0) {
            continue;
        }
        if (region.length - 1u > UINT64_MAX - region.addr) {
            continue;
        }

        region_end = region.addr + (uint64_t)region.length;
        if (address >= region.addr && end <= region_end) {
            found = 1;
            break;
        }
    }

    kfree(root);
    return found;
}
