#include <unos/fs.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/debug_log.h>
#include <unos/iso9660.h>
#include <unos/mrac.h>
#include <unos/disk.h>
#include <unos/heap.h>
#include <unos/pmm.h>
#include <unos/scheduler.h>
#include <unos/spotlight.h>
#include <unos/users.h>
#include <unos/compat.h>

#define FS_PID 0xfffffff0u
#define FS_MAGIC 0x554e4f5346533032ull
#define FS_MAX_ENTRIES 64u
#define FS_MAX_BOOT_MODULES 128u
#define FS_MAX_PATH 128u
#define FS_TABLE_LBA 2048ull
#define FS_TABLE_SECTORS 64u
#define FS_DATA_START_LBA 4096ull
#define FS_SECTOR_SIZE 512ull
#define FNV1A_OFFSET_BASIS 14695981039346656037ull
#define FNV1A_PRIME 1099511628211ull

typedef struct {
    uint64_t magic;
    uint64_t next_free_lba;
    uint32_t entry_count;
    uint32_t reserved;
} fs_header_t;

typedef struct {
    uint64_t hash;
    uint64_t offset_lba;
    uint64_t size;
    uint8_t used;
    uint8_t protected;
    char path[FS_MAX_PATH];
} fs_disk_entry_t;

typedef struct {
    uint8_t used;
    size_t size;
    void *data;
    char path[FS_MAX_PATH];
} fs_boot_module_entry_t;

static fs_header_t fs_header;
static fs_disk_entry_t *fs_entries;
static fs_boot_module_entry_t fs_boot_modules[FS_MAX_BOOT_MODULES];
static uint8_t *fs_table_buffer;
static uint8_t *fs_io_buffer;
static int fs_ready;
static int fs_disk_available;

static uint64_t fnv1a64_string(const char *str) {
    uint64_t hash = FNV1A_OFFSET_BASIS;

    for (size_t i = 0; str[i] != '\0'; ++i) {
        hash ^= (uint8_t)str[i];
        hash *= FNV1A_PRIME;
    }

    return hash;
}

static size_t string_length(const char *str) {
    size_t length = 0;
    while (str[length] != '\0') {
        ++length;
    }
    return length;
}

static int string_equals(const char *left, const char *right) {
    size_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }
    return left[i] == right[i];
}

static int starts_with(const char *str, const char *prefix) {
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (str[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static int home_path_allowed(const char *path) {
    if (!starts_with(path, "/home/")) {
        return 1;
    }

    if (user_is_admin()) {
        return 1;
    }

    const char *username = user_current_username();
    size_t i = 0;
    const char *owner = path + 6u;

    if (owner[0] == '\0') {
        return 1;
    }

    while (username[i] != '\0' && owner[i] != '\0' && owner[i] != '/') {
        if (username[i] != owner[i]) {
            return 0;
        }
        ++i;
    }

    return username[i] == '\0' && (owner[i] == '\0' || owner[i] == '/');
}

static int linux_runtime_path_allowed(const char *path) {
    return starts_with(path, "/lib/")
        || starts_with(path, "/lib64/")
        || starts_with(path, "/usr/lib/")
        || starts_with(path, "/usr/lib64/")
        || starts_with(path, "/usr/NX/lib/");
}

static int userspace_path_allowed(const char *path) {
    if (scheduler_current_user_mode() == 0) {
        return 1;
    }

    if (scheduler_current_abi() == VEX_ABI_LINUX && linux_runtime_path_allowed(path)) {
        return 1;
    }

    if (fs_is_protected(path)) {
        return 0;
    }

    return starts_with(path, "/tmp/") || home_path_allowed(path);
}

static void copy_bytes(void *dst, const void *src, size_t size) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < size; ++i) {
        d[i] = s[i];
    }
}

static void zero_bytes(void *dst, size_t size) {
    uint8_t *d = dst;
    for (size_t i = 0; i < size; ++i) {
        d[i] = 0;
    }
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
        char text[2];
        text[0] = buffer[--length];
        text[1] = '\0';
        debug_log_puts(text);
    }
}

static void copy_path(char *dst, const char *src) {
    size_t i = 0;
    while (i + 1u < FS_MAX_PATH && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static uint64_t sectors_for_size(size_t size) {
    return ((uint64_t)size + FS_SECTOR_SIZE - 1ull) / FS_SECTOR_SIZE;
}

static fs_disk_entry_t *find_entry(const char *path) {
    for (uint32_t i = 0; i < FS_MAX_ENTRIES; ++i) {
        if (fs_entries[i].used && string_equals(fs_entries[i].path, path)) {
            return &fs_entries[i];
        }
    }
    return NULL;
}

static fs_disk_entry_t *find_free_entry(void) {
    for (uint32_t i = 0; i < FS_MAX_ENTRIES; ++i) {
        if (!fs_entries[i].used) {
            return &fs_entries[i];
        }
    }
    return NULL;
}

static fs_boot_module_entry_t *find_boot_module_entry(const char *path) {
    for (uint32_t i = 0; i < FS_MAX_BOOT_MODULES; ++i) {
        if (fs_boot_modules[i].used && string_equals(fs_boot_modules[i].path, path)) {
            return &fs_boot_modules[i];
        }
    }
    return NULL;
}

static fs_boot_module_entry_t *find_free_boot_module_entry(void) {
    for (uint32_t i = 0; i < FS_MAX_BOOT_MODULES; ++i) {
        if (!fs_boot_modules[i].used) {
            return &fs_boot_modules[i];
        }
    }
    return NULL;
}

static fs_disk_entry_t *ensure_entry(const char *path, uint64_t hash) {
    fs_disk_entry_t *entry = find_entry(path);
    if (entry != NULL) {
        return entry;
    }

    entry = find_free_entry();
    if (entry == NULL) {
        return NULL;
    }

    zero_bytes(entry, sizeof(*entry));
    entry->used = 1;
    entry->protected = fs_is_protected(path) != 0;
    entry->hash = hash;
    copy_path(entry->path, path);
    ++fs_header.entry_count;
    return entry;
}

static void serialize_table(void) {
    zero_bytes(fs_table_buffer, FS_TABLE_SECTORS * FS_SECTOR_SIZE);
    copy_bytes(fs_table_buffer, &fs_header, sizeof(fs_header));
    copy_bytes(fs_table_buffer + FS_SECTOR_SIZE, fs_entries, sizeof(fs_disk_entry_t) * FS_MAX_ENTRIES);
}

static void deserialize_table(void) {
    copy_bytes(&fs_header, fs_table_buffer, sizeof(fs_header));
    copy_bytes(fs_entries, fs_table_buffer + FS_SECTOR_SIZE, sizeof(fs_disk_entry_t) * FS_MAX_ENTRIES);
}

static void persist_table(void) {
    if (!fs_disk_available) {
        return;
    }

    serialize_table();
    (void)disk_write(FS_TABLE_LBA, FS_TABLE_SECTORS, fs_table_buffer);
}

static void format_empty_table(void) {
    fs_header.magic = FS_MAGIC;
    fs_header.next_free_lba = FS_DATA_START_LBA;
    fs_header.entry_count = 0;
    fs_header.reserved = 0;
    zero_bytes(fs_entries, sizeof(fs_disk_entry_t) * FS_MAX_ENTRIES);
    persist_table();
}

void fs_init(void) {
    fs_ready = 0;
    fs_disk_available = 0;

    fs_entries = kmalloc(sizeof(fs_disk_entry_t) * FS_MAX_ENTRIES);
    fs_table_buffer = kmalloc(FS_TABLE_SECTORS * FS_SECTOR_SIZE);
    fs_io_buffer = kmalloc(64u * FS_SECTOR_SIZE);
    if (fs_entries == NULL || fs_table_buffer == NULL || fs_io_buffer == NULL) {
        return;
    }

    zero_bytes(fs_entries, sizeof(fs_disk_entry_t) * FS_MAX_ENTRIES);
    zero_bytes(fs_boot_modules, sizeof(fs_boot_modules));
    disk_init();
    fs_disk_available = disk_size() > FS_DATA_START_LBA;

    if (fs_disk_available && disk_read(FS_TABLE_LBA, FS_TABLE_SECTORS, fs_table_buffer) == 0) {
        deserialize_table();
        if (fs_header.magic != FS_MAGIC) {
            format_empty_table();
        }
    } else {
        format_empty_table();
    }

    fs_ready = 1;
}

int fs_read(const char *path, void **data, size_t *size) {
    fs_boot_module_entry_t *boot_entry;

    if (!fs_ready || path == NULL || data == NULL || size == NULL || !userspace_path_allowed(path)) {
        return -1;
    }

    boot_entry = find_boot_module_entry(path);
    if (boot_entry != NULL) {
        *data = boot_entry->data;
        *size = boot_entry->size;
        return 0;
    }

    const uint64_t hash = fnv1a64_string(path);
    fs_disk_entry_t *entry = find_entry(path);
    void *cached = MRAC_lookup(FS_PID, hash);
    if (cached != NULL) {
        *data = cached;
        *size = entry == NULL ? 0 : (size_t)entry->size;
        return 0;
    }

    if (entry == NULL || !fs_disk_available) {
        /* Last resort: try the boot CD-ROM via ISO 9660. */
        return iso9660_read(path, data, size);
    }

    const uint64_t sectors = sectors_for_size((size_t)entry->size);
    if (sectors > 64u || disk_read(entry->offset_lba, (uint32_t)sectors, fs_io_buffer) != 0) {
        return -1;
    }

    void *stored = MRAC_store_with_hash(FS_PID, hash, fs_io_buffer, (size_t)entry->size);
    if (stored == NULL) {
        return -1;
    }

    *data = stored;
    *size = (size_t)entry->size;
    return 0;
}

int fs_write(const char *path, void *data, size_t size) {
    if (!fs_ready || path == NULL || data == NULL || size == 0 || string_length(path) >= FS_MAX_PATH) {
        return -1;
    }

    if (!userspace_path_allowed(path) || (fs_is_protected(path) && !user_is_admin()) || !home_path_allowed(path)) {
        return -1;
    }

    const uint64_t hash = fnv1a64_string(path);
    fs_disk_entry_t *entry = ensure_entry(path, hash);
    if (entry == NULL) {
        return -1;
    }

    void *stored = MRAC_lookup(FS_PID, hash);
    if (stored != NULL) {
        MRAC_release(FS_PID, hash);
        stored = MRAC_modify_with_hash(FS_PID, hash, data, size);
    } else {
        stored = MRAC_store_with_hash(FS_PID, hash, data, size);
    }

    if (stored == NULL) {
        return -1;
    }

    const uint64_t sectors = sectors_for_size(size);
    if (fs_disk_available && sectors > 64u) {
        entry->offset_lba = 0;
        entry->size = size;
        spotlight_index_add(path, size);
        return 0;
    }

    entry->offset_lba = fs_header.next_free_lba;
    entry->size = size;
    fs_header.next_free_lba += sectors;

    if (fs_disk_available) {
        if (sectors > 64u) {
            return -1;
        }
        zero_bytes(fs_io_buffer, sectors * FS_SECTOR_SIZE);
        copy_bytes(fs_io_buffer, data, size);
        if (disk_write(entry->offset_lba, (uint32_t)sectors, fs_io_buffer) != 0) {
            return -1;
        }
        persist_table();
    }

    spotlight_index_add(path, size);
    return 0;
}

int fs_register_boot_module(const char *path, void *data, size_t size) {
    fs_boot_module_entry_t *entry;
    void *stored;
    const uint64_t hash = fnv1a64_string(path);
    uint64_t free_pages_before_reclaim;
    uint64_t free_pages_after_reclaim;

    if (!fs_ready || path == NULL || data == NULL || size == 0u || string_length(path) >= FS_MAX_PATH) {
        return -1;
    }

    stored = MRAC_store_with_hash(FS_PID, hash, data, size);
    if (stored == NULL) {
        debug_log_puts("boot module: MRAC store failed ");
        debug_log_puts(path);
        debug_log_puts("\n");
        return -1;
    }

    entry = find_boot_module_entry(path);
    if (entry == NULL) {
        entry = find_free_boot_module_entry();
    }
    if (entry == NULL) {
        debug_log_puts("boot module: no slot ");
        debug_log_puts(path);
        debug_log_puts("\n");
        return -1;
    }

    zero_bytes(entry, sizeof(*entry));
    entry->used = 1u;
    entry->data = stored;
    entry->size = size;
    copy_path(entry->path, path);
    free_pages_before_reclaim = pmm_free_page_count();
    pmm_reclaim_boot_module_range(data, (uint64_t)size);
    free_pages_after_reclaim = pmm_free_page_count();
    debug_log_puts("boot module: registered ");
    debug_log_puts(path);
    debug_log_puts(" reclaimed_pages=");
    debug_put_dec_u64(free_pages_after_reclaim > free_pages_before_reclaim ? free_pages_after_reclaim - free_pages_before_reclaim : 0u);
    debug_log_puts("\n");
    spotlight_index_add(path, size);
    return 0;
}

int fs_delete(const char *path) {
    if (!fs_ready || path == NULL) {
        return -1;
    }

    fs_disk_entry_t *entry = find_entry(path);
    if (entry == NULL) {
        return -1;
    }

    if (!userspace_path_allowed(path) || (entry->protected && !user_is_admin()) || !home_path_allowed(path)) {
        return -1;
    }

    entry->used = 0;
    if (fs_header.entry_count > 0) {
        --fs_header.entry_count;
    }

    MRAC_remove(FS_PID, fnv1a64_string(path));
    persist_table();
    spotlight_index_remove(path);
    return 0;
}

int fs_delete_prefix(const char *prefix) {
    int deleted = 0;

    if (!fs_ready || prefix == NULL || prefix[0] == '\0') {
        return -1;
    }

    for (uint32_t i = 0; i < FS_MAX_ENTRIES; ++i) {
        if (!fs_entries[i].used || !starts_with(fs_entries[i].path, prefix)) {
            continue;
        }

        if (!userspace_path_allowed(fs_entries[i].path) ||
            (fs_entries[i].protected && !user_is_admin()) ||
            !home_path_allowed(fs_entries[i].path)) {
            continue;
        }

        fs_entries[i].used = 0;
        if (fs_header.entry_count > 0) {
            --fs_header.entry_count;
        }
        MRAC_remove(FS_PID, fs_entries[i].hash);
        spotlight_index_remove(fs_entries[i].path);
        ++deleted;
    }

    if (deleted > 0) {
        persist_table();
    }

    return deleted;
}

int fs_exists(const char *path) {
    if (!fs_ready || path == NULL) {
        return 0;
    }

    if (find_boot_module_entry(path) != NULL) {
        return 1;
    }

    const uint64_t hash = fnv1a64_string(path);
    void *cached = MRAC_lookup(FS_PID, hash);
    if (cached != NULL) {
        MRAC_release(FS_PID, hash);
        return 1;
    }

    if (find_entry(path) != NULL) {
        return 1;
    }

    return iso9660_exists(path);
}

int fs_is_protected(const char *path) {
    if (path == NULL) {
        return 0;
    }

    return string_equals(path, "/sys") || starts_with(path, "/sys/");
}

void fs_list(void (*visitor)(const char *path, size_t size)) {
    if (!fs_ready || visitor == NULL) {
        return;
    }

    for (uint32_t i = 0; i < FS_MAX_ENTRIES; ++i) {
        if (fs_entries[i].used) {
            visitor(fs_entries[i].path, (size_t)fs_entries[i].size);
        }
    }
}
