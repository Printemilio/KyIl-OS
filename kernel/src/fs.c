#include <unos/fs.h>
#include <unos/boot_archive.h>
#include <stdio.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/debug_log.h>
#include <unos/iso9660.h>
#include <unos/ora.h>
#include <unos/kernel_guard.h>
#include <unos/disk.h>
#include <unos/storage_volume.h>
#include <unos/heap.h>
#include <unos/pmm.h>
#include <unos/security.h>
#include <unos/scheduler.h>
#include <unos/spotlight.h>
#include <unos/users.h>
#include <unos/compat.h>
#include <unos/fs_root.h>

static kernel_gate_t fs_gate=KERNEL_GATE_INIT;
kernel_guard_t fs_namespace_begin(void) {return kernel_guard_enter(&fs_gate);}
void fs_namespace_end(kernel_guard_t *guard) {kernel_guard_leave(guard);}

/* VexFS on-disk layout (version 5).
 *
 *   sector FS_TABLE_LBA   fs_header_t: where the entry table lives, how many
 *                         slots it has, the data allocation cursor
 *   table extent          entry_capacity slots of FS_ENTRY_SECTORS sectors,
 *                         allocated in the data area like file contents
 *   data area             one contiguous extent per regular file
 *
 * The table has no fixed size: when it is full, a table twice as large is
 * written to a new extent and the header is switched to it with a single
 * sector write. Changing one entry rewrites only that entry's sectors.
 *
 * Version 4 (a fixed table of 64 entries with 128-byte paths stored right
 * after the header) is migrated in place at mount time.
 *
 * Paths under /tmp live in memory only (FS_ENTRY_FLAG_MEMORY): a tmpfs that
 * starts empty at every boot and never touches the disk. */
#define FS_MAGIC_V4 0x554e4f5346533034ull /* "UNOSFS04" */
#define FS_MAGIC 0x554e4f5346533035ull    /* "UNOSFS05" */
#define FS_ISO_CACHE_MAX_BYTES (64ull * 1024ull * 1024ull)
#define FS_MAX_PATH 512u
#define FS_INITIAL_ENTRIES 256u
#define FS_TABLE_LBA 2048ull
#define FS_IO_SECTORS 64u
#define FS_DATA_START_LBA 4096ull
#define FS_SECTOR_SIZE 512ull
#define FS_ENTRY_SECTORS 3u
#define FS_ENTRY_BYTES (FS_ENTRY_SECTORS * FS_SECTOR_SIZE)
#define FS_ENTRY_FLAG_MEMORY 0x01u
#define FNV1A_OFFSET_BASIS 14695981039346656037ull
#define FNV1A_PRIME 1099511628211ull

#define FS_V4_MAX_ENTRIES 64u
#define FS_V4_MAX_PATH 128u
#define FS_V4_TABLE_SECTORS 64u

typedef struct {
    uint64_t magic;
    uint64_t next_free_lba;
    uint32_t entry_count;
    uint32_t entry_capacity;
    uint64_t table_lba;
    uint64_t reserved[4];
} fs_header_t;

/* Times are indexed like vex_file_times_t: access, modify, change. */
typedef struct {
    uint64_t hash;
    uint64_t offset_lba;
    uint64_t size;
    uint8_t used;
    uint8_t protected;
    uint8_t type;
    uint8_t flags;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint32_t time_valid;
    uint32_t time_nanoseconds[3];
    int64_t time_seconds[3];
    char path[FS_MAX_PATH];
    char link_target[FS_MAX_PATH];
    fs_acl_entry_t acl[FS_ACL_MAX_ENTRIES];
} fs_disk_entry_t;

/* The version 4 records, read only to migrate an existing disk. */
typedef struct {
    uint64_t magic;
    uint64_t next_free_lba;
    uint32_t entry_count;
    uint32_t reserved;
} fs_v4_header_t;

typedef struct {
    uint64_t hash;
    uint64_t offset_lba;
    uint64_t size;
    uint8_t used;
    uint8_t protected;
    uint8_t type;
    uint8_t reserved0;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    char path[FS_V4_MAX_PATH];
    char link_target[FS_V4_MAX_PATH];
    fs_acl_entry_t acl[FS_ACL_MAX_ENTRIES];
} fs_v4_entry_t;

typedef struct fs_boot_module_entry {
    uint8_t used;
    uint8_t iso_cache;
    uint8_t reserved[6];
    size_t size;
    size_t offset; /* immutable ORA content view, zero for standalone files */
    ora_ref_t content;
    uint64_t identity;
    char path[FS_MAX_PATH];
    struct fs_boot_module_entry *next;
} fs_boot_module_entry_t;

_Static_assert(sizeof(fs_disk_entry_t) <= FS_ENTRY_BYTES, "VexFS entry exceeds its slot");
_Static_assert(sizeof(fs_header_t) <= FS_SECTOR_SIZE, "VexFS header exceeds a sector");
/* scripts/make-vexfs-v4-image.py writes this layout for the migration test. */
_Static_assert(sizeof(fs_v4_entry_t) == 328u, "version 4 entry layout is frozen");
_Static_assert(sizeof(fs_v4_header_t) + sizeof(fs_v4_entry_t) * FS_V4_MAX_ENTRIES <=
               FS_V4_TABLE_SECTORS * FS_SECTOR_SIZE, "version 4 table layout");

static fs_header_t fs_header;
static fs_disk_entry_t *fs_entries;
static ora_ref_t *fs_cached_contents;
static uint64_t *fs_entry_identities;
static uint64_t fs_next_identity=1;
static uint32_t fs_entry_capacity;
static fs_boot_module_entry_t *fs_boot_modules;
static uint64_t fs_cached_iso_bytes;
static uint8_t *fs_table_buffer;
static uint8_t *fs_io_buffer;
static int fs_ready;
static int fs_disk_available;
static uint32_t fs_runtime_write_failure_logs;
static storage_volume_t fs_volume;
static int volume_raw_read(void *ctx,uint64_t lba,uint32_t count,void *data){(void)ctx;return disk_read(lba,count,data);}
static uint64_t fs_disk_size(void){return fs_volume.sectors;}
static int fs_disk_read(uint64_t lba,uint32_t count,void *data) {
    if(lba>=fs_volume.sectors || count>fs_volume.sectors-lba)return -1;
    return disk_read(fs_volume.base+lba,count,data);
}
static int fs_disk_write(uint64_t lba,uint32_t count,void *data) {
    if(lba>=fs_volume.sectors || count>fs_volume.sectors-lba)return -1;
    return disk_write(fs_volume.base+lba,count,data);
}

static fs_disk_entry_t *find_entry(const char *path);
static int persist_table(void);
static int persist_header(void);
static int persist_entry(const fs_disk_entry_t *entry);
static int grow_entries(uint32_t capacity);
static void debug_put_dec_u64(uint64_t value);

/* The memory-only namespace (tmpfs). */
static int path_is_memory(const char *normalized) {
    const char *prefix = "/tmp";
    size_t i = 0u;
    while (prefix[i] != '\0') {
        if (normalized[i] != prefix[i]) return 0;
        ++i;
    }
    return normalized[i] == '\0' || normalized[i] == '/';
}

static int entry_is_memory(const fs_disk_entry_t *entry) {
    return (entry->flags & FS_ENTRY_FLAG_MEMORY) != 0u;
}

/* Set the access/modify/change times selected by `which`
   (VEX_FILE_ACCESS_TIME | VEX_FILE_MODIFY_TIME | VEX_FILE_CHANGE_TIME). */
static void entry_touch(fs_disk_entry_t *entry, uint32_t which) {
    uint64_t now;
    if (vex_realtime_ns(&now) != 0) return;
    for (uint32_t i = 0u; i < 3u; ++i) {
        if ((which & (1u << i)) == 0u) continue;
        entry->time_seconds[i] = (int64_t)(now / 1000000000ull);
        entry->time_nanoseconds[i] = (uint32_t)(now % 1000000000ull);
        entry->time_valid |= 1u << i;
    }
}

/* Ladybird's POSIX directory layer stores directory metadata in a marker
 * entry.  Keep that representation private to the VFS, but make access(2)
 * and other permission checks see the directory rather than report ENOENT. */
static fs_disk_entry_t *find_directory_marker(const char *path) {
    static const char suffix[] = "/.ladybird_dir";
    char marker[FS_MAX_PATH];
    size_t length = 0u;
    while (path != NULL && path[length] != '\0') ++length;
    if (path == NULL || length == 0u ||
        length + sizeof(suffix) > sizeof(marker)) return NULL;
    for (size_t i = 0u; i <= length; ++i) marker[i] = path[i];
    if (length == 1u && marker[0] == '/') --length;
    for (size_t i = 0u; suffix[i] != '\0'; ++i) marker[length + i] = suffix[i];
    marker[length + sizeof(suffix) - 1u] = '\0';
    return find_entry(marker);
}

static uint64_t fnv1a64_string(const char *str) {
    uint64_t hash = FNV1A_OFFSET_BASIS;

    for (size_t i = 0; str[i] != '\0'; ++i) {
        hash ^= (uint8_t)str[i];
        hash *= FNV1A_PRIME;
    }

    return hash;
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

static int is_browser_runtime_path(const char *path) {
    return path != NULL &&
        (starts_with(path, "/tmp/c/") || starts_with(path, "/tmp/d/") ||
         starts_with(path, "/tmp/s/"));
}

static void fs_log_runtime_write_failure(const char *reason, const char *path,
                                         uint64_t detail) {
    if (!is_browser_runtime_path(path) || fs_runtime_write_failure_logs >= 32u)
        return;
    ++fs_runtime_write_failure_logs;
    debug_log_puts("fs: runtime write failure reason=");
    debug_log_puts(reason);
    debug_log_puts(" path=");
    debug_log_puts(path);
    debug_log_puts(" detail=");
    debug_put_dec_u64(detail);
    debug_log_puts(" entries=");
    debug_put_dec_u64(fs_header.entry_count);
    debug_log_puts("/ ");
    debug_put_dec_u64(fs_entry_capacity);
    debug_log_puts("\n");
}

static int path_prefix_matches(const char *path, const char *prefix) {
    size_t i = 0u;

    if (path == NULL || prefix == NULL) {
        return 0;
    }
    if (prefix[0] == '/' && prefix[1] == '\0') {
        return path[0] == '/';
    }

    while (prefix[i] != '\0') {
        if (path[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }

    return path[i] == '\0' || path[i] == '/';
}

static int home_path_allowed(const char *path) {
    char owner_name[32];
    size_t owner_length = 0u;
    uint32_t owner_uid = 0u;

    if (!starts_with(path, "/home/")) {
        return 1;
    }

    if ((fs_root_credential_caps(scheduler_current_capabilities()) & VEX_CAP_FS_ADMIN)) {
        return 1;
    }

    const char *owner = path + 6u;

    if (owner[0] == '\0') {
        return 1;
    }

    while (owner[owner_length] != '\0' && owner[owner_length] != '/' && owner_length + 1u < sizeof(owner_name)) {
        owner_name[owner_length] = owner[owner_length];
        ++owner_length;
    }
    owner_name[owner_length] = '\0';

    if (owner_length == 0u) {
        return 1;
    }

    if (user_lookup_uid(owner_name, &owner_uid) == 0) {
        return owner_uid == fs_root_credential_uid(user_current_uid());
    }

    if (string_equals(owner_name, "root")) {
        return fs_root_credential_uid(user_current_uid()) == 0u;
    }

    {
        const char *username = user_name_for_uid(fs_root_credential_uid(user_current_uid()));
        size_t i = 0u;

        while (username[i] != '\0' && owner_name[i] != '\0') {
            if (username[i] != owner_name[i]) {
                return 0;
            }
            ++i;
        }

        return username[i] == '\0' && owner_name[i] == '\0';
    }
}

static int linux_runtime_path_allowed(const char *path) {
    return starts_with(path, "/lib/")
        || starts_with(path, "/lib64/")
        || starts_with(path, "/usr/lib/")
        || starts_with(path, "/usr/lib64/")
        || starts_with(path, "/usr/NX/lib/");
}

static int userspace_path_allowed(const char *path) {
    if(!fs_root_scope_allows(path,0))return 0;
    if (scheduler_current_user_mode() == 0) {
        return 1;
    }

    if (scheduler_current_abi() == VEX_ABI_LINUX && linux_runtime_path_allowed(path)) {
        return 1;
    }

    if (fs_is_protected(path)) {
        if ((fs_root_credential_caps(scheduler_current_capabilities()) & VEX_CAP_FS_ADMIN) != 0u) {
            return 1;
        }
        return 0;
    }

    return path_prefix_matches(path, "/tmp") || home_path_allowed(path);
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

static uint32_t sanitize_mode(uint32_t mode, uint8_t type) {
    const uint32_t masked = mode & 0777u;

    if (masked != 0u) {
        return masked;
    }
    if (type == FS_ENTRY_DIRECTORY) {
        return FS_MODE_DEFAULT_DIR;
    }
    if (type == FS_ENTRY_SYMLINK) {
        return FS_MODE_DEFAULT_SYMLINK;
    }
    return FS_MODE_DEFAULT_FILE;
}

static void init_entry_metadata(fs_disk_entry_t *entry, const char *path, uint64_t hash, uint8_t type) {
    if (entry == NULL) {
        return;
    }

    zero_bytes(entry, sizeof(*entry));
    fs_entry_identities[entry-fs_entries]=0;
    entry->used = 1u;
    entry->protected = fs_is_protected(path) != 0;
    entry->type = type;
    entry->hash = hash;
    entry->uid = fs_root_credential_uid(user_current_uid());
    entry->gid = fs_root_credential_uid(user_current_uid());
    entry->mode = sanitize_mode(0u, type);
    copy_path(entry->path, path);
    entry->link_target[0] = '\0';
}

static void append_dec_u32(char *dst, size_t capacity, size_t *length, uint32_t value) {
    char digits[11];
    size_t count = 0u;

    if (*length + 1u >= capacity) {
        return;
    }
    if (value == 0u) {
        dst[(*length)++] = '0';
        dst[*length] = '\0';
        return;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u && *length + 1u < capacity) {
        dst[(*length)++] = digits[--count];
    }
    dst[*length] = '\0';
}

static void build_honeypot_path(char out[FS_MAX_PATH], uint32_t pid, const char *source_path) {
    const uint64_t path_hash = fnv1a64_string(source_path);
    size_t length = 0u;

    copy_path(out, "/tmp/.honeypot/");
    while (out[length] != '\0') {
        ++length;
    }
    out[length++] = 'p';
    out[length] = '\0';
    append_dec_u32(out, FS_MAX_PATH, &length, pid);
    if (length + 2u < FS_MAX_PATH) {
        out[length++] = '-';
        out[length] = '\0';
    }
    append_dec_u32(out, FS_MAX_PATH, &length, (uint32_t)(path_hash & 0xffffffffu));
}

static int mode_allows(uint32_t mode, uint32_t owner_uid, uint32_t uid, int read, int write, int exec) {
    uint32_t shift = owner_uid == uid ? 6u : 0u;
    uint32_t allowed = (mode >> shift) & 7u;

    if (read != 0 && (allowed & 4u) == 0u) {
        return 0;
    }
    if (write != 0 && (allowed & 2u) == 0u) {
        return 0;
    }
    if (exec != 0 && (allowed & 1u) == 0u) {
        return 0;
    }
    return 1;
}

static uint8_t acl_entry_allows(const fs_acl_entry_t *entry, int read, int write, int exec) {
    if (entry == NULL || entry->used == 0u) {
        return 0u;
    }
    if (read != 0 && (entry->perms & FS_ACL_READ) == 0u) {
        return 0u;
    }
    if (write != 0 && (entry->perms & FS_ACL_WRITE) == 0u) {
        return 0u;
    }
    if (exec != 0 && (entry->perms & FS_ACL_EXEC) == 0u) {
        return 0u;
    }
    return 1u;
}

static int normalize_absolute_path(const char *path, char out[FS_MAX_PATH]) {
    char working[FS_MAX_PATH];
    size_t read_index = 0u;
    size_t write_index = 0u;

    if (path == NULL || out == NULL || path[0] != '/') {
        return 0;
    }

    while (path[read_index] != '\0' && read_index + 1u < sizeof(working)) {
        working[read_index] = path[read_index];
        ++read_index;
    }
    if (path[read_index] != '\0') {
        return 0;
    }
    working[read_index] = '\0';
    read_index = 0u;

    while (working[read_index] != '\0' && write_index + 1u < sizeof(working)) {
        if (working[read_index] == '/') {
            if (write_index == 0u || working[write_index - 1u] != '/') {
                working[write_index++] = '/';
            }
            ++read_index;
            continue;
        }

        if (working[read_index] == '.' &&
            (working[read_index + 1u] == '/' || working[read_index + 1u] == '\0')) {
            read_index += working[read_index + 1u] == '/' ? 2u : 1u;
            continue;
        }

        if (working[read_index] == '.' &&
            working[read_index + 1u] == '.' &&
            (working[read_index + 2u] == '/' || working[read_index + 2u] == '\0')) {
            if (write_index > 1u) {
                --write_index;
                while (write_index > 0u && working[write_index - 1u] != '/') {
                    --write_index;
                }
            }
            read_index += working[read_index + 2u] == '/' ? 3u : 2u;
            continue;
        }

        working[write_index++] = working[read_index++];
    }

    if (working[read_index] != '\0') {
        return 0;
    }
    if (write_index == 0u) {
        working[write_index++] = '/';
    }
    if (write_index > 1u && working[write_index - 1u] == '/') {
        --write_index;
    }

    working[write_index] = '\0';
    copy_path(out, working);
    return 1;
}

static int resolve_normalized_path_internal(const char *path, char out[FS_MAX_PATH], uint8_t follow_links) {
    char current[FS_MAX_PATH];

    if (path == NULL || out == NULL) {
        return 0;
    }

    copy_path(current, path);
    for (uint32_t depth = 0u; depth < 8u; ++depth) {
        fs_disk_entry_t *entry = find_entry(current);

        if (entry == NULL || follow_links == 0u || entry->type != FS_ENTRY_SYMLINK) {
            copy_path(out, current);
            return 1;
        }
        if (!normalize_absolute_path(entry->link_target, current)) {
            return 0;
        }
    }

    return 0;
}

static int entry_allows_access(const fs_disk_entry_t *entry, int require_read, int require_write, int require_exec) {
    const uint32_t uid = fs_root_credential_uid(user_current_uid());

    if (entry == NULL) {
        return 0;
    }
    if (scheduler_current_user_mode() == 0u || (fs_root_credential_caps(scheduler_current_capabilities()) & VEX_CAP_FS_ADMIN)) {
        return 1;
    }
    for (size_t i = 0u; i < FS_ACL_MAX_ENTRIES; ++i) {
        if (entry->acl[i].used != 0u && entry->acl[i].uid == uid) {
            return acl_entry_allows(&entry->acl[i], require_read, require_write, require_exec);
        }
    }
    return mode_allows(entry->mode, entry->uid, uid, require_read, require_write, require_exec);
}

static int sectors_for_size_checked(size_t size, uint64_t *out) {
    const uint64_t value = (uint64_t)size;

    if (out == NULL || value > UINT64_MAX - (FS_SECTOR_SIZE - 1ull)) {
        return -1;
    }
    *out = (value + FS_SECTOR_SIZE - 1ull) / FS_SECTOR_SIZE;
    return 0;
}

static uint64_t sectors_for_size(size_t size) {
    uint64_t sectors = 0u;
    return sectors_for_size_checked(size, &sectors) == 0 ? sectors : 0u;
}

static fs_disk_entry_t *find_entry(const char *path) {
    const uint64_t hash = fnv1a64_string(path);
    for (uint32_t i = 0; i < fs_entry_capacity; ++i) {
        if (fs_entries[i].used && fs_entries[i].hash == hash &&
            string_equals(fs_entries[i].path, path)) {
            return &fs_entries[i];
        }
    }
    return NULL;
}

static fs_disk_entry_t *find_free_entry(void) {
    for (uint32_t i = 0; i < fs_entry_capacity; ++i) {
        if (!fs_entries[i].used) {
            return &fs_entries[i];
        }
    }
    /* Every slot is used: double the table rather than fail. */
    {
        const uint32_t old_capacity = fs_entry_capacity;
        if (old_capacity > UINT32_MAX / 2u || grow_entries(old_capacity * 2u) != 0) {
            return NULL;
        }
        return &fs_entries[old_capacity];
    }
}

/* Find the lowest free extent of `sectors` sectors in the data area. The
 * occupied extents are derived from the entry table (and the table's own
 * extent) instead of a second free-list format on disk. `exclude` is an
 * entry whose current extent may be reused; the caller keeps it occupied
 * while the replacement is written, so a failed write loses nothing. */
static uint64_t table_sectors(uint32_t capacity) {
    return (uint64_t)capacity * FS_ENTRY_SECTORS;
}

static int extents_overlap(uint64_t a, uint64_t a_sectors, uint64_t b, uint64_t b_sectors) {
    return a < b + b_sectors && b < a + a_sectors;
}

static int find_data_extent(const fs_disk_entry_t *exclude, uint64_t sectors,
                            uint64_t *offset_out, uint64_t *next_free_out) {
    uint64_t candidate = FS_DATA_START_LBA;
    const uint64_t disk_limit = fs_disk_size();

    if (sectors == 0u || offset_out == NULL || next_free_out == NULL ||
        !fs_disk_available || disk_limit < FS_DATA_START_LBA) {
        return -1;
    }

    for (;;) {
        uint64_t next = 0u;
        if (candidate > disk_limit || sectors > disk_limit - candidate) {
            return -1;
        }
        if (fs_header.table_lba != 0u &&
            extents_overlap(candidate, sectors, fs_header.table_lba,
                            table_sectors(fs_header.entry_capacity))) {
            next = fs_header.table_lba + table_sectors(fs_header.entry_capacity);
        }
        for (uint32_t i = 0u; i < fs_entry_capacity; ++i) {
            const fs_disk_entry_t *entry = &fs_entries[i];
            uint64_t entry_sectors;
            if (!entry->used || entry == exclude || entry_is_memory(entry) ||
                entry->offset_lba == 0u || entry->size > (uint64_t)SIZE_MAX ||
                sectors_for_size_checked((size_t)entry->size, &entry_sectors) != 0 ||
                entry_sectors == 0u) {
                continue;
            }
            if (extents_overlap(candidate, sectors, entry->offset_lba, entry_sectors) &&
                entry->offset_lba + entry_sectors > next) {
                next = entry->offset_lba + entry_sectors;
            }
        }
        if (next == 0u) {
            break;
        }
        candidate = next;
    }

    *offset_out = candidate;
    *next_free_out = fs_header.next_free_lba > candidate + sectors
        ? fs_header.next_free_lba : candidate + sectors;
    return 0;
}

static int disk_write_file_data(uint64_t offset_lba, const void *data, size_t size) {
    const uint8_t *source = (const uint8_t *)data;
    uint64_t sectors;
    uint64_t written_sectors = 0u;

    if (data == NULL || sectors_for_size_checked(size, &sectors) != 0 || sectors == 0u) {
        return -1;
    }

    while (written_sectors < sectors) {
        const uint64_t remaining = sectors - written_sectors;
        const uint32_t chunk = remaining > FS_IO_SECTORS ? FS_IO_SECTORS : (uint32_t)remaining;
        const size_t chunk_bytes = (size_t)chunk * (size_t)FS_SECTOR_SIZE;
        size_t source_offset;
        size_t copy_size;

        if (written_sectors > (uint64_t)(SIZE_MAX / (size_t)FS_SECTOR_SIZE) ||
            offset_lba > UINT64_MAX - written_sectors) {
            return -1;
        }
        source_offset = (size_t)written_sectors * (size_t)FS_SECTOR_SIZE;
        copy_size = size > source_offset ? size - source_offset : 0u;
        if (copy_size > chunk_bytes) {
            copy_size = chunk_bytes;
        }
        zero_bytes(fs_io_buffer, chunk_bytes);
        copy_bytes(fs_io_buffer, source + source_offset, copy_size);
        if (fs_disk_write(offset_lba + written_sectors, chunk, fs_io_buffer) != 0) {
            return -1;
        }
        written_sectors += chunk;
    }
    return 0;
}

static int disk_read_file_data(uint64_t offset_lba, void *data, size_t size) {
    uint8_t *destination = (uint8_t *)data;
    uint64_t sectors;
    uint64_t read_sectors = 0u;

    if (data == NULL || sectors_for_size_checked(size, &sectors) != 0 || sectors == 0u) {
        return -1;
    }

    while (read_sectors < sectors) {
        const uint64_t remaining = sectors - read_sectors;
        const uint32_t chunk = remaining > FS_IO_SECTORS ? FS_IO_SECTORS : (uint32_t)remaining;
        const size_t chunk_bytes = (size_t)chunk * (size_t)FS_SECTOR_SIZE;
        size_t destination_offset;
        size_t copy_size;

        if (read_sectors > (uint64_t)(SIZE_MAX / (size_t)FS_SECTOR_SIZE) ||
            offset_lba > UINT64_MAX - read_sectors) {
            return -1;
        }
        destination_offset = (size_t)read_sectors * (size_t)FS_SECTOR_SIZE;
        copy_size = size > destination_offset ? size - destination_offset : 0u;
        if (copy_size > chunk_bytes) {
            copy_size = chunk_bytes;
        }
        if (fs_disk_read(offset_lba + read_sectors, chunk, fs_io_buffer) != 0) {
            return -1;
        }
        copy_bytes(destination + destination_offset, fs_io_buffer, copy_size);
        read_sectors += chunk;
    }
    return 0;
}

static fs_boot_module_entry_t *find_boot_module_entry(const char *path) {
    for (fs_boot_module_entry_t *entry = fs_boot_modules;
         entry != NULL; entry = entry->next) {
        if (entry->used && string_equals(entry->path, path)) return entry;
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

    init_entry_metadata(entry, path, hash, FS_ENTRY_REGULAR);
    ++fs_header.entry_count;
    return entry;
}

/* A new entry's namespace and birth time. */
static void entry_born(fs_disk_entry_t *entry, const char *normalized) {
    if (path_is_memory(normalized)) entry->flags |= FS_ENTRY_FLAG_MEMORY;
    entry_touch(entry, VEX_FILE_ACCESS_TIME | VEX_FILE_MODIFY_TIME | VEX_FILE_CHANGE_TIME);
}

static int write_entry_data_unchecked(const char *normalized, void *data, size_t size) {
    const uint64_t hash = fnv1a64_string(normalized);
    fs_disk_entry_t *existing = find_entry(normalized);
    fs_disk_entry_t *entry;
    fs_disk_entry_t old_entry;
    fs_header_t old_header;
    uint64_t sectors = 0u;
    uint64_t offset_lba = 0u;
    uint64_t next_free_lba = 0u;
    int was_used = existing != NULL && existing->used != 0u;

    if (size != 0u && (data == NULL || sectors_for_size_checked(size, &sectors) != 0 || sectors == 0u)) {
        return -1;
    }
    entry = ensure_entry(normalized, hash);
    if (entry == NULL) {
        static const char wk2_prefix[] = "/dev/shm/WK2SharedMemory.";
        if (normalized != NULL && starts_with(normalized, wk2_prefix)) {
            debug_log_puts("wk2-fs-fail: no-entry-slot path=");
            debug_log_puts(normalized);
            debug_log_puts(" used=");
            debug_put_dec_u64(fs_header.entry_count);
            debug_log_puts(" limit=");
            debug_put_dec_u64(fs_entry_capacity);
            debug_log_puts("\n");
        }
        fs_log_runtime_write_failure("entry-slots", normalized,
                                     fs_header.entry_count);
        return -1;
    }

    old_entry = *entry;
    old_header = fs_header;
    if (!was_used) entry_born(entry, normalized);
    entry->type = FS_ENTRY_REGULAR;
    entry->protected = fs_is_protected(normalized) ? 1u : 0u;
    entry_touch(entry, VEX_FILE_MODIFY_TIME | VEX_FILE_CHANGE_TIME);

    if (fs_disk_available && !entry_is_memory(entry)) {
        if (size == 0u) {
            entry->offset_lba = 0u;
            entry->size = 0u;
        } else if (find_data_extent(NULL, sectors, &offset_lba, &next_free_lba) != 0) {
            static const char wk2_prefix[] = "/dev/shm/WK2SharedMemory.";
            if (normalized != NULL && starts_with(normalized, wk2_prefix)) {
                debug_log_puts("wk2-fs-fail: no-disk-extent path=");
                debug_log_puts(normalized);
                debug_log_puts(" sectors=");
                debug_put_dec_u64(sectors);
                debug_log_puts(" disk=");
                debug_put_dec_u64(fs_disk_size());
                debug_log_puts("\n");
            }
            fs_log_runtime_write_failure("disk-extent", normalized, sectors);
            *entry = old_entry;
            fs_header = old_header;
            if (!was_used) {
                --fs_header.entry_count;
                zero_bytes(entry, sizeof(*entry));
            }
            return -1;
        } else if (disk_write_file_data(offset_lba, data, size) != 0) {
            static const char wk2_prefix[] = "/dev/shm/WK2SharedMemory.";
            if (normalized != NULL && starts_with(normalized, wk2_prefix)) {
                debug_log_puts("wk2-fs-fail: disk-write path=");
                debug_log_puts(normalized);
                debug_log_puts(" lba=");
                debug_put_dec_u64(offset_lba);
                debug_log_puts(" sectors=");
                debug_put_dec_u64(sectors);
                debug_log_puts("\n");
            }
            fs_log_runtime_write_failure("disk-data", normalized, offset_lba);
            *entry = old_entry;
            fs_header = old_header;
            if (!was_used) {
                --fs_header.entry_count;
                zero_bytes(entry, sizeof(*entry));
            }
            return -1;
        }

        if (size != 0u) {
            entry->offset_lba = offset_lba;
            fs_header.next_free_lba = next_free_lba;
        }
        entry->size = size;
        if (persist_entry(entry) != 0 || persist_header() != 0) {
            static const char wk2_prefix[] = "/dev/shm/WK2SharedMemory.";
            if (normalized != NULL && starts_with(normalized, wk2_prefix)) {
                debug_log_puts("wk2-fs-fail: persist-table path=");
                debug_log_puts(normalized);
                debug_log_puts("\n");
            }
            fs_log_runtime_write_failure("disk-metadata", normalized, size);
            *entry = old_entry;
            fs_header = old_header;
            if (!was_used) {
                --fs_header.entry_count;
                zero_bytes(entry, sizeof(*entry));
            }
            return -1;
        }
    } else {
        entry->offset_lba = 0u;
        entry->size = size;
    }

    /* Update the cache only after the durable metadata commit.  A failed
       cache allocation leaves a valid on-disk file which fs_read() can load. */
    ora_ref_t replacement=size ? ora_create(data,size,ORA_CANONICAL) : NULL;
    if (size != 0u && !replacement) {
        static const char wk2_prefix[] = "/dev/shm/WK2SharedMemory.";
        static uint32_t optional_cache_failure_logs;
        if (normalized != NULL && starts_with(normalized, wk2_prefix)) {
            debug_log_puts("wk2-fs-fail: ora-cache path=");
            debug_log_puts(normalized);
            debug_log_puts(" size=");
            debug_put_dec_u64(size);
            debug_log_puts("\n");
        }
        if (!fs_disk_available || entry_is_memory(entry)) {
            /* Without the disk, the memory copy is the file itself. */
            *entry = old_entry;
            fs_header = old_header;
            if (!was_used) {
                --fs_header.entry_count;
                zero_bytes(entry, sizeof(*entry));
            }
            return -1;
        }
        /* The disk data and metadata were already committed above. ORA is
         * only an acceleration cache when durable storage is present, so a
         * cache allocation failure must not turn a successful file write
         * into EIO for callers such as SQLite. fs_read() will load this file
         * from disk on the next access. */
        if (optional_cache_failure_logs < 16u) {
            ++optional_cache_failure_logs;
            debug_log_puts("fs: durable write kept after ORA cache miss path=");
            debug_log_puts(normalized);
            debug_log_puts(" size=");
            debug_put_dec_u64(size);
            debug_log_puts("\n");
        }
        ora_release(fs_cached_contents[entry-fs_entries]);fs_cached_contents[entry-fs_entries]=NULL;
        spotlight_index_add(normalized, size);
        return 0;
    }
    ora_release(fs_cached_contents[entry-fs_entries]);fs_cached_contents[entry-fs_entries]=replacement;
    spotlight_index_add(normalized, size);
    return 0;
}

/* A slot as it is stored: memory-only entries occupy no disk slot. */
static void encode_slot(uint32_t index, uint8_t *out) {
    zero_bytes(out, FS_ENTRY_BYTES);
    if (index < fs_entry_capacity && fs_entries[index].used && !entry_is_memory(&fs_entries[index])) {
        copy_bytes(out, &fs_entries[index], sizeof(fs_disk_entry_t));
    }
}

static int persist_header(void) {
    if (!fs_disk_available) {
        return 0;
    }
    zero_bytes(fs_table_buffer, FS_SECTOR_SIZE);
    copy_bytes(fs_table_buffer, &fs_header, sizeof(fs_header));
    return fs_disk_write(FS_TABLE_LBA, 1u, fs_table_buffer);
}

static int persist_slot(uint32_t index) {
    if (!fs_disk_available || index >= fs_entry_capacity) {
        return 0;
    }
    encode_slot(index, fs_table_buffer);
    return fs_disk_write(fs_header.table_lba + (uint64_t)index * FS_ENTRY_SECTORS,
                      FS_ENTRY_SECTORS, fs_table_buffer);
}

static int persist_entry(const fs_disk_entry_t *entry) {
    if (!fs_disk_available || entry == NULL || entry_is_memory(entry)) {
        return 0;
    }
    const uint32_t index = (uint32_t)(entry - fs_entries);
    encode_slot(index, fs_table_buffer);
    return fs_disk_write(fs_header.table_lba + (uint64_t)index * FS_ENTRY_SECTORS,
                      FS_ENTRY_SECTORS, fs_table_buffer);
}

static int write_table_at(uint64_t lba, uint32_t capacity) {
    const uint32_t per_chunk = FS_IO_SECTORS / FS_ENTRY_SECTORS;
    for (uint32_t first = 0u; first < capacity; first += per_chunk) {
        const uint32_t count = capacity - first < per_chunk ? capacity - first : per_chunk;
        for (uint32_t j = 0u; j < count; ++j) {
            encode_slot(first + j, fs_io_buffer + (size_t)j * FS_ENTRY_BYTES);
        }
        if (fs_disk_write(lba + (uint64_t)first * FS_ENTRY_SECTORS,
                       count * FS_ENTRY_SECTORS, fs_io_buffer) != 0) {
            return -1;
        }
    }
    return 0;
}

static int persist_table(void) {
    if (!fs_disk_available) {
        return 0;
    }
    if (write_table_at(fs_header.table_lba, fs_entry_capacity) != 0) {
        return -1;
    }
    return persist_header();
}

/* Replace the in-memory table by one of `capacity` slots and, on disk,
   write it to a new extent before switching the header to it. */
static int grow_entries(uint32_t capacity) {
    fs_disk_entry_t *entries;
    ora_ref_t *cached;
    uint64_t *identities,*old_identities=fs_entry_identities;
    fs_disk_entry_t *old_entries = fs_entries;
    ora_ref_t *old_cached = fs_cached_contents;
    const uint32_t old_capacity = fs_entry_capacity;
    const fs_header_t old_header = fs_header;

    if (capacity <= old_capacity) {
        return 0;
    }
    entries = kmalloc(sizeof(*entries) * capacity);
    cached = kmalloc(sizeof(*cached) * capacity);
    identities=kmalloc(sizeof(*identities)*capacity);
    if (entries == NULL || cached == NULL || identities==NULL) {
        kfree(entries);
        kfree(cached);
        kfree(identities);
        return -1;
    }
    zero_bytes(entries, sizeof(*entries) * capacity);
    zero_bytes(cached, sizeof(*cached) * capacity);
    zero_bytes(identities,sizeof(*identities)*capacity);
    if (old_entries != NULL) {
        copy_bytes(entries, old_entries, sizeof(*entries) * old_capacity);
        copy_bytes(cached, old_cached, sizeof(*cached) * old_capacity);
        copy_bytes(identities,old_identities,sizeof(*identities)*old_capacity);
    }

    if (fs_disk_available && fs_ready) {
        uint64_t lba = 0u;
        uint64_t next_free = 0u;
        /* The search still sees the current table as occupied. */
        if (find_data_extent(NULL, table_sectors(capacity), &lba, &next_free) != 0) {
            kfree(entries);
            kfree(cached);
            kfree(identities);
            return -1;
        }
        fs_entries = entries;
        fs_cached_contents = cached;
        fs_entry_identities=identities;
        fs_entry_capacity = capacity;
        if (write_table_at(lba, capacity) != 0) {
            goto revert;
        }
        fs_header.table_lba = lba;
        fs_header.entry_capacity = capacity;
        fs_header.next_free_lba = next_free;
        if (persist_header() != 0) {
            goto revert;
        }
    } else {
        fs_entries = entries;
        fs_cached_contents = cached;
        fs_entry_identities=identities;
        fs_entry_capacity = capacity;
        fs_header.entry_capacity = capacity;
    }
    kfree(old_entries);
    kfree(old_cached);
    kfree(old_identities);
    return 0;

revert:
    fs_entries = old_entries;
    fs_cached_contents = old_cached;
    fs_entry_identities=old_identities;
    fs_entry_capacity = old_capacity;
    fs_header = old_header;
    kfree(entries);
    kfree(cached);
    kfree(identities);
    return -1;
}

static void copy_bounded(char *dst, size_t capacity, const char *src, size_t src_capacity) {
    size_t i = 0u;
    while (i + 1u < capacity && i < src_capacity && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

/* Allocate the table extent for a freshly formatted or migrated volume. */
static int place_table(void) {
    uint64_t lba = 0u, next_free = 0u;
    fs_header.table_lba = 0u;
    if (find_data_extent(NULL, table_sectors(fs_entry_capacity), &lba, &next_free) != 0) {
        return -1;
    }
    fs_header.magic = FS_MAGIC;
    fs_header.table_lba = lba;
    fs_header.entry_capacity = fs_entry_capacity;
    fs_header.next_free_lba = next_free;
    return persist_table();
}

static int load_table(void) {
    const uint32_t per_chunk = FS_IO_SECTORS / FS_ENTRY_SECTORS;
    const uint32_t capacity = fs_header.entry_capacity;

    if (capacity == 0u || fs_header.entry_count>capacity ||
        fs_header.table_lba < FS_DATA_START_LBA || fs_header.table_lba>=fs_disk_size() ||
        table_sectors(capacity)>fs_disk_size()-fs_header.table_lba ||
        grow_entries(capacity) != 0) {
        return -1;
    }
    for (uint32_t first = 0u; first < capacity; first += per_chunk) {
        const uint32_t count = capacity - first < per_chunk ? capacity - first : per_chunk;
        if (fs_disk_read(fs_header.table_lba + (uint64_t)first * FS_ENTRY_SECTORS,
                      count * FS_ENTRY_SECTORS, fs_io_buffer) != 0) {
            return -1;
        }
        for (uint32_t j = 0u; j < count; ++j) {
            fs_disk_entry_t *entry = &fs_entries[first + j];
            copy_bytes(entry, fs_io_buffer + (size_t)j * FS_ENTRY_BYTES, sizeof(*entry));
            entry->path[FS_MAX_PATH - 1u] = '\0';
            entry->link_target[FS_MAX_PATH - 1u] = '\0';
            entry->flags &= (uint8_t)~FS_ENTRY_FLAG_MEMORY;
        }
    }
    return 0;
}

/* Version 4 -> 5: the 64 entries move into a growable table; contents stay
   where they are. */
static int migrate_v4_table(void) {
    const fs_v4_header_t *old_header = (const fs_v4_header_t *)fs_table_buffer;
    const fs_v4_entry_t *old_entries =
        (const fs_v4_entry_t *)(fs_table_buffer + FS_SECTOR_SIZE);
    uint32_t migrated = 0u;

    zero_bytes(&fs_header, sizeof(fs_header));
    fs_header.next_free_lba = old_header->next_free_lba;
    zero_bytes(fs_entries, sizeof(fs_disk_entry_t) * fs_entry_capacity);
    for (uint32_t i = 0u; i < FS_V4_MAX_ENTRIES && i < fs_entry_capacity; ++i) {
        const fs_v4_entry_t *old = &old_entries[i];
        fs_disk_entry_t *entry = &fs_entries[migrated];
        if (!old->used) continue;
        entry->used = 1u;
        entry->hash = old->hash;
        entry->offset_lba = old->offset_lba;
        entry->size = old->size;
        entry->protected = old->protected;
        entry->type = old->type;
        entry->uid = old->uid;
        entry->gid = old->gid;
        entry->mode = old->mode;
        copy_bounded(entry->path, sizeof(entry->path), old->path, sizeof(old->path));
        copy_bounded(entry->link_target, sizeof(entry->link_target),
                     old->link_target, sizeof(old->link_target));
        copy_bytes(entry->acl, old->acl, sizeof(entry->acl));
        ++migrated;
    }
    fs_header.entry_count = migrated;
    if (place_table() != 0) {
        return -1;
    }
    debug_log_puts("fs: migrated VexFS version 4 to 5, entries=");
    debug_put_dec_u64(migrated);
    debug_log_puts("\n");
    return 0;
}

/* Files left in the memory namespace by an older system that stored /tmp on
   disk: /tmp starts empty. */
static void purge_memory_namespace(void) {
    int changed = 0;
    for (uint32_t i = 0u; i < fs_entry_capacity; ++i) {
        if (fs_entries[i].used && path_is_memory(fs_entries[i].path)) {
            fs_entries[i].used = 0u;
            if (fs_header.entry_count > 0u) --fs_header.entry_count;
            (void)persist_entry(&fs_entries[i]);
            changed = 1;
        }
    }
    if (changed) (void)persist_header();
}

void fs_init(void) {
    KERNEL_GUARD(fs_gate);
    fs_ready = 0;
    fs_disk_available = 0;

    fs_table_buffer = kmalloc(FS_V4_TABLE_SECTORS * FS_SECTOR_SIZE);
    fs_io_buffer = kmalloc(FS_IO_SECTORS * FS_SECTOR_SIZE);
    if (fs_table_buffer == NULL || fs_io_buffer == NULL ||
        grow_entries(FS_INITIAL_ENTRIES) != 0) {
        return;
    }

    fs_boot_modules = NULL;
    fs_cached_iso_bytes = 0u;
    disk_init();
    fs_disk_available=storage_volume_discover(volume_raw_read,NULL,disk_size(),&fs_volume)==0;
    if(fs_disk_available){debug_log_puts("fs: native volume partition=");debug_put_dec_u64(fs_volume.partition);debug_log_puts(" base=");debug_put_dec_u64(fs_volume.base);debug_log_puts(" sectors=");debug_put_dec_u64(fs_volume.sectors);debug_log_puts("\n");}

    if (fs_disk_available && fs_disk_read(FS_TABLE_LBA, FS_V4_TABLE_SECTORS, fs_table_buffer) == 0) {
        const uint64_t magic = *(const uint64_t *)fs_table_buffer;
        int mounted = -1;
        if (magic == FS_MAGIC) {
            copy_bytes(&fs_header, fs_table_buffer, sizeof(fs_header));
            mounted = load_table();
        } else if (magic == FS_MAGIC_V4) {
            mounted = migrate_v4_table();
        }
        if (mounted != 0) {
            /* Discovery never grants permission to destroy a foreign or
               damaged volume. Formatting requires an explicit operation. */
            debug_log_puts("fs: unrecognized/invalid VexFS, disk left untouched\n");
            fs_disk_available = 0;
        }
    } else if (fs_disk_available) {
        debug_log_puts("fs: volume read failed, disk left untouched\n");
        fs_disk_available = 0;
    }
    if (fs_disk_available) {
        debug_log_puts("fs: VexFS 5 mounted, entries=");
        debug_put_dec_u64(fs_header.entry_count);
        debug_log_puts(" capacity=");
        debug_put_dec_u64(fs_header.entry_capacity);
        debug_log_puts("\n");
    } else {
        /* A failed mount may have read part of an entry table. Do not expose
           those partial disk entries as a successful RAM filesystem. */
        zero_bytes(&fs_header,sizeof(fs_header));
        zero_bytes(fs_entries,sizeof(*fs_entries)*fs_entry_capacity);
        debug_log_puts("fs: no usable disk, VexFS keeps every file in memory\n");
    }

    fs_ready = 1;
    purge_memory_namespace();

    /* /tmp is the shared writable temporary directory: its marker lives in
       the memory namespace like everything below it. */
    if (find_entry("/tmp/.ladybird_dir") == NULL) {
        uint8_t marker = 1u;
        (void)write_entry_data_unchecked("/tmp/.ladybird_dir", &marker, sizeof(marker));
    }
    (void)fs_set_mode("/tmp/.ladybird_dir", 0777u);
}
int fs_sync(void) {
    KERNEL_GUARD(fs_gate);
    if(!fs_ready)return -1;
    /* Metadata/data writes are eager. The remaining durable boundary is the
       controller's cache; volatile backends honestly have no media to flush. */
    return fs_disk_available?disk_flush():0;
}
int fs_format_partition(unsigned index) {
    KERNEL_GUARD(fs_gate);
    if(fs_root_path_pinned("/"))return -1;
    if(!scheduler_current_has_capability(VEX_CAP_SYSTEM_ADMIN|VEX_CAP_FS_ADMIN) ||
       !user_has_capability(VEX_CAP_SYSTEM_ADMIN|VEX_CAP_FS_ADMIN))return -1;
    storage_volume_t target;
    if(storage_volume_partition(volume_raw_read,NULL,disk_size(),index,&target))return -1;
    if(fs_disk_available && fs_volume.base==target.base)return -1;
    unsigned char sector[512];
    if(disk_read(target.base+FS_TABLE_LBA,1,sector))return -1;
    uint64_t magic;memcpy(&magic,sector,8);
    if(magic==FS_MAGIC || magic==FS_MAGIC_V4)return -1; /* never reformat an existing native volume */
    if(!fs_io_buffer)return -1;
    memset(fs_io_buffer,0,FS_IO_SECTORS*FS_SECTOR_SIZE);
    uint64_t table_length=(uint64_t)FS_INITIAL_ENTRIES*FS_ENTRY_SECTORS;
    for(uint64_t i=0;i<table_length;i+=FS_IO_SECTORS) {
        uint32_t count=table_length-i>FS_IO_SECTORS?FS_IO_SECTORS:(uint32_t)(table_length-i);
        if(disk_write(target.base+FS_DATA_START_LBA+i,count,fs_io_buffer))return -1;
    }
    memset(sector,0,sizeof(sector));
    fs_header_t header={.magic=FS_MAGIC,.next_free_lba=FS_DATA_START_LBA+(uint64_t)FS_INITIAL_ENTRIES*FS_ENTRY_SECTORS,
        .entry_capacity=FS_INITIAL_ENTRIES,.table_lba=FS_DATA_START_LBA};
    memcpy(sector,&header,sizeof(header));
    if(disk_write(target.base+FS_TABLE_LBA,1,sector) || disk_flush())return -1;
    return 0; /* mount is intentionally deferred until next boot */
}

int fs_read_view(const char *path,void **data,size_t *size,ora_view_t *view) {
    return fs_read_view_bounded(path,data,size,view,SIZE_MAX);
}
int fs_read_view_bounded(const char *path,void **data,size_t *size,ora_view_t *view,size_t maximum) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH],resolved[FS_MAX_PATH];
    if(!view || !data || !size)return -1;
    ora_view_release(view);*data=NULL;*size=0;
    if(!fs_ready || !path || !normalize_absolute_path(path,normalized) ||
       !resolve_normalized_path_internal(normalized,resolved,1) || !userspace_path_allowed(resolved))return -1;
    if(!fs_root_scope_allows(resolved,FS_ROOT_READ))return -1;
    fs_disk_entry_t *entry=find_entry(resolved);
    if(entry && (entry->type!=FS_ENTRY_REGULAR || !entry_allows_access(entry,1,0,0)))return -1;
    fs_boot_module_entry_t *boot=find_boot_module_entry(resolved);
    if((entry && entry->size>maximum) || (!entry && boot && boot->size>maximum))return -3;
    /* A writable VexFS entry shadows the immutable ISO/resident source at
       the same path. Separate caches must not resurrect the old contents. */
    ora_ref_t content=entry?fs_cached_contents[entry-fs_entries]:(boot?boot->content:NULL);
    if(!content && entry && !entry->size)return 0;
    if(!content) {
        if(!entry || !fs_disk_available) {
            uint32_t location_lba,location_size;
            if(iso9660_get_location(resolved,&location_lba,&location_size)!=0)return -1;
            if(location_size>maximum)return -3;
            void *ignored;size_t ignored_size;
            if(iso9660_read(resolved,&ignored,&ignored_size)!=0)return -1;
            boot=find_boot_module_entry(resolved);if(!boot)return -1;
            content=boot->content;
        } else {
            if(!entry->offset_lba || entry->size>SIZE_MAX || !sectors_for_size(entry->size))return -1;
            void *bytes=kmalloc(entry->size);if(!bytes)return -1;
            if(disk_read_file_data(entry->offset_lba,bytes,entry->size)!=0){kfree(bytes);return -1;}
            content=ora_create(bytes,entry->size,ORA_CANONICAL);kfree(bytes);if(!content)return -1;
            fs_cached_contents[entry-fs_entries]=content;
        }
    }
    size_t offset=boot && content==boot->content ? boot->offset : 0u;
    size_t bytes=boot && content==boot->content ? boot->size : ora_size(content);
    if(bytes>maximum)return -3;
    if(ora_view_acquire(content,offset,bytes,view)!=0)return -1;
    *data=(void *)ora_view_data(view);*size=view->length;return 0;
}

int fs_read_boot_module_view(const char *path,const void **data,size_t *size,ora_view_t *view) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH],resolved[FS_MAX_PATH];
    if(!fs_ready || !path || !data || !size || !view || !normalize_absolute_path(path,normalized) ||
       !resolve_normalized_path_internal(normalized,resolved,1) || !userspace_path_allowed(resolved))return -1;
    if(!fs_root_scope_allows(resolved,FS_ROOT_READ))return -1;
    if(find_entry(resolved))return -1; /* caller uses the writable backend */
    fs_boot_module_entry_t *entry=find_boot_module_entry(resolved);if(!entry)return -1;
    ora_view_release(view);
    if(ora_view_acquire(entry->content,entry->offset,entry->size,view)!=0)return -1;
    *data=ora_view_data(view);*size=view->length;return 0;
}

int fs_write(const char *path, void *data, size_t size) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    char honeypot_path[FS_MAX_PATH];
    fs_disk_entry_t *entry;
    security_mrac_decision_t mrac_decision;

    if (!fs_ready || path == NULL || (data == NULL && size != 0u) ||
        !normalize_absolute_path(path, normalized)) {
        return -1;
    }

    if(fs_root_path_pinned(normalized) || !fs_root_scope_allows(normalized,FS_ROOT_WRITE))return -1;
    entry = find_entry(normalized);
    if(!entry && !fs_root_scope_allows(normalized,FS_ROOT_CREATE))return -1;
    if (!userspace_path_allowed(normalized) ||
        (entry != NULL && !entry_allows_access(entry, 0, 1, 0)) ||
        (entry == NULL && scheduler_current_user_mode() != 0u &&
         !(fs_root_credential_caps(scheduler_current_capabilities()) & VEX_CAP_FS_ADMIN) &&
         fs_is_protected(normalized)) ||
        !home_path_allowed(normalized)) {
        fs_log_runtime_write_failure("permission", normalized, size);
        security_audit_log_event("fs-write", normalized, scheduler_current_pid(), scheduler_current_uid(),
                                 scheduler_current_capabilities(), 0u);
        return -1;
    }

    /* MRAC watches for mass rewriting of existing durable files (what
       ransomware does to user data). Creating files, as compilers and
       archivers do by the hundred, and the volatile /tmp are not signals. */
    const int mrac_watched = !path_is_memory(normalized);
    const int overwrites = mrac_watched && entry != NULL && entry->size != 0u;

    if (mrac_watched && security_mrac_pid_quarantined(scheduler_current_pid())) {
        build_honeypot_path(honeypot_path, scheduler_current_pid(), normalized);
        mrac_decision = security_mrac_record_quarantine_activity(scheduler_current_pid(), scheduler_current_uid(),
                                                                 scheduler_current_capabilities(),
                                                                 "mrac-shadow-write", normalized);
        if (mrac_decision == SECURITY_MRAC_QUARANTINE) {
            return -1;
        }
        return write_entry_data_unchecked(honeypot_path, data, size);
    }

    if (write_entry_data_unchecked(normalized, data, size) != 0) {
        return -1;
    }

    if (!overwrites) {
        return 0;
    }
    mrac_decision = security_mrac_record_write(scheduler_current_pid(), scheduler_current_uid(),
                                               scheduler_current_capabilities(), normalized, size);
    if (mrac_decision == SECURITY_MRAC_QUARANTINE) {
        return -1;
    }
    return 0;
}

static int register_readonly_payload(const char *path, void *data, size_t size,
                                    int reclaim_source, const void **cached_out) {
    char normalized[FS_MAX_PATH];
    fs_boot_module_entry_t *entry;
    void *stored;
    uint64_t free_pages_before_reclaim;
    uint64_t free_pages_after_reclaim;
    int new_entry = 0;

    if (!fs_ready || path == NULL || data == NULL || size == 0u ||
        !normalize_absolute_path(path, normalized)) {
        return -1;
    }

    entry = find_boot_module_entry(normalized);
    if (entry == NULL) {
        /* ISO payload caching is bounded by bytes, not an arbitrary number of
         * paths. This lets the font set and ordinary small assets share the
         * same cache while large/excess reads fall back to ISO streaming. */
        if (!reclaim_source &&
            (size > FS_ISO_CACHE_MAX_BYTES ||
             fs_cached_iso_bytes > FS_ISO_CACHE_MAX_BYTES - (uint64_t)size)) {
            return -1;
        }
        entry = kmalloc(sizeof(*entry));
        if (entry == NULL) return -1;
        zero_bytes(entry, sizeof(*entry));
        new_entry = 1;
    }

    /* Reserve metadata before publishing a ORA reference. A full cache must
     * not leave an orphan binding whose subsequent fs_read reports size 0. */
    ora_ref_t content=ora_create(data,size,ORA_CANONICAL);
    stored=(void *)ora_data(content);
    if (stored == NULL) {
        if (new_entry) kfree(entry);
        debug_log_puts("boot module: ORA ingestion failed ");
        debug_log_puts(normalized);
        debug_log_puts("\n");
        return -1;
    }

    entry->used = 1u;
    if (new_entry) entry->iso_cache = reclaim_source ? 0u : 1u;
    ora_release(entry->content);entry->content=content;
    entry->offset = 0u;
    entry->size = size;
    copy_path(entry->path, normalized);
    if (new_entry) {
        entry->next = fs_boot_modules;
        fs_boot_modules = entry;
        if (entry->iso_cache != 0u) fs_cached_iso_bytes += (uint64_t)size;
    }
    free_pages_before_reclaim = pmm_free_page_count();
    if (reclaim_source) pmm_reclaim_boot_module_range(data, (uint64_t)size);
    free_pages_after_reclaim = pmm_free_page_count();
    debug_log_puts("boot module: registered ");
    debug_log_puts(normalized);
    debug_log_puts(" reclaimed_pages=");
    debug_put_dec_u64(free_pages_after_reclaim > free_pages_before_reclaim ? free_pages_after_reclaim - free_pages_before_reclaim : 0u);
    debug_log_puts("\n");
    spotlight_index_add(normalized, size);
    if (cached_out) *cached_out = stored;
    return 0;
}

int fs_register_boot_module(const char *path, void *data, size_t size) {
    KERNEL_GUARD(fs_gate);
    return register_readonly_payload(path, data, size, 1, NULL);
}

static int archive_prepare_file(const boot_archive_file_t *file,void *context) {
    fs_boot_module_entry_t **head=context;
    if(find_boot_module_entry(file->path))return -1;
    fs_boot_module_entry_t *entry=kmalloc(sizeof(*entry));if(!entry)return -1;
    memset(entry,0,sizeof(*entry));copy_path(entry->path,file->path);
    entry->size=file->size;entry->offset=file->offset;entry->used=1;
    entry->next=*head;*head=entry;return 0;
}
int fs_register_boot_archive(void *data,size_t size) {
    KERNEL_GUARD(fs_gate);
    fs_boot_module_entry_t *entries=NULL;
    if(!fs_ready || boot_archive_visit(data,size,archive_prepare_file,&entries))goto failure;
    /* One immutable object, explicit views. It is not a mutable/shared backing
     * and no file is rehashed/re-ingested merely to retain its known reference. */
    ora_ref_t content=ora_create(data,size,ORA_UNIQUE);
    if(!content)goto failure;
    uint32_t count=0;
    while(entries) {
        fs_boot_module_entry_t *entry=entries;entries=entries->next;
        entry->content=ora_retain(content);entry->next=fs_boot_modules;fs_boot_modules=entry;++count;
    }
    ora_release(content);
    pmm_reclaim_boot_module_range(data,size);
    char line[100];snprintf(line,sizeof(line),"boot archive: %u files, one immutable ORA object, ",count);
    debug_log_puts(line);debug_put_dec_u64(size);debug_log_puts(" bytes\n");return 0;
failure:
    while(entries){fs_boot_module_entry_t *next=entries->next;kfree(entries);entries=next;}
    return -1;
}

int fs_cache_readonly_payload(const char *path, void *data, size_t size,
                              const void **cached_out) {
    KERNEL_GUARD(fs_gate);
    if (!cached_out) return -1;
    return register_readonly_payload(path, data, size, 0, cached_out);
}

/* Boot smoke only. Real FS/cache paths, not a second storage implementation. */
int fs_ora_self_test(void) {
    KERNEL_GUARD(fs_gate);
    const char *path="/home/root/ora-fs-lease-smoke";
    char original[]="resident",replacement[]="writable";
    ora_view_t old={0},current={0},boot={0};
    const void *cached=NULL,*boot_bytes=NULL;void *data=NULL;size_t size=0;
    ora_stats_t initial,before,after;int result=-1;
    if(find_entry(path) && fs_delete(path))return -1;
    ora_get_stats(&initial);
    if(fs_cache_readonly_payload(path,original,sizeof(original),&cached) ||
       fs_read_view(path,&data,&size,&old) || size!=sizeof(original) ||
       memcmp(data,original,size))goto done;
    ora_get_stats(&before);
    if(fs_read_boot_module_view(path,&boot_bytes,&size,&boot))goto done;
    ora_view_release(&boot);ora_get_stats(&after);
    if(before.fingerprints!=after.fingerprints)goto done;
    if(fs_write(path,replacement,sizeof(replacement)) ||
       fs_read_view(path,&data,&size,&current) || size!=sizeof(replacement) ||
       memcmp(data,replacement,size) || memcmp(ora_view_data(&old),original,sizeof(original)) ||
       fs_read_boot_module_view(path,&boot_bytes,&size,&boot)==0)goto done;
    /* Deleting the writable overlay must not invalidate either held read. */
    if(fs_delete(path) || memcmp(ora_view_data(&current),replacement,sizeof(replacement)) ||
       memcmp(ora_view_data(&old),original,sizeof(original)))goto done;
    result=0;
done:
    if(find_entry(path) && fs_delete(path))result=-1;
    fs_boot_module_entry_t **link=&fs_boot_modules;
    while(*link && strcmp((*link)->path,path))link=&(*link)->next;
    if(*link) {
        fs_boot_module_entry_t *entry=*link;*link=entry->next;
        if(entry->iso_cache)fs_cached_iso_bytes-=entry->size;
        ora_release(entry->content);kfree(entry);
    }
    ora_view_release(&old);ora_view_release(&current);ora_view_release(&boot);
    ora_get_stats(&after);
    if(after.objects!=initial.objects || after.bytes!=initial.bytes)result=-1;
    return result;
}

int fs_delete(const char *path) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    fs_disk_entry_t *entry;
    fs_disk_entry_t old_entry;
    fs_header_t old_header;

    if (!fs_ready || path == NULL || !normalize_absolute_path(path, normalized)) {
        return -1;
    }

    if(fs_root_path_pinned(normalized) || !fs_root_scope_allows(normalized,FS_ROOT_REMOVE))return -1;
    entry = find_entry(normalized);
    if (entry == NULL) {
        return -1;
    }

    if (!userspace_path_allowed(normalized) ||
        !entry_allows_access(entry, 0, 1, 0) ||
        !home_path_allowed(normalized)) {
        security_audit_log_event("fs-delete", normalized, scheduler_current_pid(), scheduler_current_uid(),
                                 scheduler_current_capabilities(), 0u);
        return -1;
    }
    if (security_mrac_pid_quarantined(scheduler_current_pid())) {
        (void)security_mrac_record_quarantine_activity(scheduler_current_pid(), scheduler_current_uid(),
                                                       scheduler_current_capabilities(),
                                                       "mrac-shadow-delete", normalized);
        return -1;
    }

    old_entry = *entry;
    old_header = fs_header;
    entry->used = 0;
    if (fs_header.entry_count > 0) {
        --fs_header.entry_count;
    }

    /* persist_entry skips memory entries, so encode the slot by hand. */
    if (!entry_is_memory(&old_entry) && (persist_entry(entry) != 0 || persist_header() != 0)) {
        *entry = old_entry;
        fs_header = old_header;
        return -1;
    }

    ora_release(fs_cached_contents[entry-fs_entries]);fs_cached_contents[entry-fs_entries]=NULL;
    spotlight_index_remove(normalized);
    return 0;
}

int fs_delete_prefix(const char *prefix) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    int deleted = 0;
    int persist_error = 0;

    if (!fs_ready || prefix == NULL || prefix[0] == '\0' ||
        !normalize_absolute_path(prefix, normalized) ||
        string_equals(normalized, "/")) {
        return -1;
    }

    if(fs_root_path_pinned(normalized) || !fs_root_scope_allows(normalized,FS_ROOT_REMOVE))return -1;
    for (uint32_t i = 0; i < fs_entry_capacity; ++i) {
        if (!fs_entries[i].used || !path_prefix_matches(fs_entries[i].path, normalized)) {
            continue;
        }

        if (!userspace_path_allowed(fs_entries[i].path) ||
            !entry_allows_access(&fs_entries[i], 0, 1, 0) ||
            !home_path_allowed(fs_entries[i].path)) {
            continue;
        }

        fs_disk_entry_t old_entry = fs_entries[i];
        fs_header_t old_header = fs_header;

        fs_entries[i].used = 0;
        if (fs_header.entry_count > 0) {
            --fs_header.entry_count;
        }

        if (!entry_is_memory(&old_entry) &&
            (persist_entry(&fs_entries[i]) != 0 || persist_header() != 0)) {
            fs_entries[i] = old_entry;
            fs_header = old_header;
            persist_error = 1;
            break;
        }
        ora_release(fs_cached_contents[i]);fs_cached_contents[i]=NULL;
        spotlight_index_remove(fs_entries[i].path);
        ++deleted;
    }

    return persist_error != 0 ? -1 : deleted;
}

/* Give an entry a new path, moving its contents between the disk and the
 * memory namespace when the rename crosses /tmp, and write its slot. On
 * failure the entry is left exactly as it was. */
static int relocate_entry(fs_disk_entry_t *entry, const char *new_path) {
    const fs_disk_entry_t saved = *entry;
    const uint32_t index = (uint32_t)(entry - fs_entries);
    const int to_memory = path_is_memory(new_path);
    const int from_memory = entry_is_memory(entry);

    if (!fs_disk_available) {
        /* Without a disk every entry already lives in memory. */
        entry->flags = to_memory ? (uint8_t)(entry->flags | FS_ENTRY_FLAG_MEMORY)
                                 : (uint8_t)(entry->flags & ~FS_ENTRY_FLAG_MEMORY);
    } else if (from_memory && !to_memory) {
        /* Into durable storage: the memory copy is written to an extent. */
        ora_ref_t content = fs_cached_contents[index];
        entry->flags &= (uint8_t)~FS_ENTRY_FLAG_MEMORY;
        entry->offset_lba = 0u;
        if (entry->type == FS_ENTRY_REGULAR && entry->size != 0u) {
            uint64_t sectors = 0u, lba = 0u, next_free = 0u;
            if (content == NULL || sectors_for_size_checked((size_t)entry->size, &sectors) != 0 ||
                find_data_extent(entry, sectors, &lba, &next_free) != 0 ||
                disk_write_file_data(lba, ora_data(content), (size_t)entry->size) != 0) {
                *entry = saved;
                return -1;
            }
            entry->offset_lba = lba;
            fs_header.next_free_lba = next_free;
        }
    } else if (!from_memory && to_memory) {
        /* Into the memory namespace: the contents must be resident first. */
        if (entry->type == FS_ENTRY_REGULAR && entry->size != 0u &&
            fs_cached_contents[index] == NULL) {
            void *bytes;
            if (entry->size > SIZE_MAX || (bytes = kmalloc((size_t)entry->size)) == NULL) {
                return -1;
            }
            if (disk_read_file_data(entry->offset_lba, bytes, (size_t)entry->size) != 0) {
                kfree(bytes);
                return -1;
            }
            fs_cached_contents[index] = ora_create(bytes, (size_t)entry->size, ORA_CANONICAL);
            kfree(bytes);
            if (fs_cached_contents[index] == NULL) {
                return -1;
            }
        }
        entry->flags |= FS_ENTRY_FLAG_MEMORY;
        entry->offset_lba = 0u;
    }
    entry->hash = fnv1a64_string(new_path);
    entry->protected = fs_is_protected(new_path) ? 1u : 0u;
    copy_path(entry->path, new_path);
    entry_touch(entry, VEX_FILE_CHANGE_TIME);
    /* A slot leaving the disk is written as free; one staying or arriving
       is written with its new path. */
    if ((!from_memory || !to_memory) && persist_slot(index) != 0) {
        *entry = saved;
        return -1;
    }
    return 0;
}

/* Rename a directory: every entry at or below `old_dir` (its marker
 * included) moves below `new_dir`, which must not exist yet. Called with
 * fs_gate held. */
static int fs_move_directory(const char *old_dir, const char *new_dir) {
    char target[FS_MAX_PATH];
    size_t old_length = 0u, new_length = 0u;
    int moved = 0;

    while (old_dir[old_length] != '\0') ++old_length;
    while (new_dir[new_length] != '\0') ++new_length;
    if (string_equals(old_dir, "/") || path_prefix_matches(new_dir, old_dir)) {
        return -1; /* a directory cannot move into itself */
    }
    for (uint32_t i = 0u; i < fs_entry_capacity; ++i) {
        if (fs_entries[i].used && path_prefix_matches(fs_entries[i].path, new_dir)) {
            return -1; /* the destination exists */
        }
    }
    if (!userspace_path_allowed(old_dir) || !home_path_allowed(old_dir) ||
        !userspace_path_allowed(new_dir) || !home_path_allowed(new_dir) ||
        (scheduler_current_user_mode() != 0u &&
         !(fs_root_credential_caps(scheduler_current_capabilities()) & VEX_CAP_FS_ADMIN) && fs_is_protected(new_dir))) {
        return -1;
    }
    for (uint32_t i = 0u; i < fs_entry_capacity; ++i) {
        fs_disk_entry_t *entry = &fs_entries[i];
        size_t rest = 0u;
        if (!entry->used || !path_prefix_matches(entry->path, old_dir)) continue;
        if (!entry_allows_access(entry, 0, 1, 0)) return moved ? -1 : -1;
        while (entry->path[old_length + rest] != '\0') ++rest;
        if (new_length + rest + 1u > sizeof(target)) return -1;
        copy_bytes(target, new_dir, new_length);
        copy_bytes(target + new_length, entry->path + old_length, rest + 1u);
        spotlight_index_remove(entry->path);
        if (relocate_entry(entry, target) != 0) {
            return -1;
        }
        spotlight_index_add(entry->path, (size_t)entry->size);
        ++moved;
    }
    if (moved == 0) {
        return -1; /* nothing VexFS owns there (a read-only ISO directory) */
    }
    return persist_header();
}

/* Rename one entry. With `replace`, an existing regular file at the new path is
 * removed in the same transaction (POSIX rename semantics): the new name never
 * disappears and the table is persisted once, so a failure restores both. */
static int fs_move_internal(const char *old_path, const char *new_path, int replace) {
    KERNEL_GUARD(fs_gate);
    char old_normalized[FS_MAX_PATH];
    char new_normalized[FS_MAX_PATH];
    fs_disk_entry_t *entry;
    fs_disk_entry_t *replaced = NULL;
    fs_disk_entry_t replaced_entry = {0};
    fs_header_t old_header;
    uint64_t new_hash;
    fs_disk_entry_t old_entry;

    if (!fs_ready || old_path == NULL || new_path == NULL ||
        !normalize_absolute_path(old_path, old_normalized) ||
        !normalize_absolute_path(new_path, new_normalized)) {
        return -1;
    }
    if (string_equals(old_normalized, new_normalized)) {
        return 0;
    }
    if(fs_root_path_pinned(old_normalized) || fs_root_path_pinned(new_normalized) ||
       !fs_root_scope_allows(old_normalized,FS_ROOT_RENAME) || !fs_root_scope_allows(new_normalized,FS_ROOT_RENAME))return -1;
    if (find_boot_module_entry(old_normalized) != NULL || find_boot_module_entry(new_normalized) != NULL) {
        return -1;
    }

    entry = find_entry(old_normalized);
    if (entry == NULL) {
        return fs_move_directory(old_normalized, new_normalized);
    }
    replaced = find_entry(new_normalized);
    if (replaced != NULL &&
        (!replace || replaced->type != FS_ENTRY_REGULAR || entry->type != FS_ENTRY_REGULAR)) {
        return -1;
    }
    if (!userspace_path_allowed(old_normalized) ||
        !entry_allows_access(entry, 0, 1, 0) ||
        !home_path_allowed(old_normalized) ||
        !userspace_path_allowed(new_normalized) ||
        !home_path_allowed(new_normalized) ||
        (replaced != NULL && !entry_allows_access(replaced, 0, 1, 0))) {
        security_audit_log_event("fs-move", new_normalized, scheduler_current_pid(), scheduler_current_uid(),
                                 scheduler_current_capabilities(), 0u);
        return -1;
    }
    if (security_mrac_pid_quarantined(scheduler_current_pid())) {
        (void)security_mrac_record_quarantine_activity(scheduler_current_pid(), scheduler_current_uid(),
                                                       scheduler_current_capabilities(),
                                                       "mrac-shadow-move", new_normalized);
        return -1;
    }
    if (scheduler_current_user_mode() != 0u &&
        !(fs_root_credential_caps(scheduler_current_capabilities()) & VEX_CAP_FS_ADMIN) &&
        fs_is_protected(new_normalized)) {
        return -1;
    }

    new_hash = fnv1a64_string(new_normalized);

    old_entry = *entry;
    old_header = fs_header;
    if (replaced != NULL) {
        replaced_entry = *replaced;
        replaced->used = 0;
        if (fs_header.entry_count > 0) {
            --fs_header.entry_count;
        }
    }
    spotlight_index_remove(old_normalized);
    (void)new_hash;
    if ((replaced != NULL && !entry_is_memory(&replaced_entry) &&
         persist_slot((uint32_t)(replaced - fs_entries)) != 0) ||
        relocate_entry(entry, new_normalized) != 0 || persist_header() != 0) {
        *entry = old_entry;
        if (replaced != NULL) {
            *replaced = replaced_entry;
            if (!entry_is_memory(replaced)) (void)persist_slot((uint32_t)(replaced - fs_entries));
        }
        fs_header = old_header;
        (void)persist_slot((uint32_t)(entry - fs_entries));
        (void)persist_header();
        spotlight_index_add(old_normalized, (size_t)entry->size);
        return -1;
    }
    if (replaced != NULL) {
        /* The old file's content is released only once the rename is durable. */
        ora_release(fs_cached_contents[replaced - fs_entries]);
        fs_cached_contents[replaced - fs_entries] = NULL;
        spotlight_index_remove(new_normalized);
    }
    spotlight_index_add(new_normalized, (size_t)entry->size);
    return 0;
}

int fs_move(const char *old_path, const char *new_path) {
    return fs_move_internal(old_path, new_path, 0);
}
uint64_t fs_cached_bytes(void){
    KERNEL_GUARD(fs_gate);uint64_t total=fs_cached_iso_bytes;
    for(uint32_t i=0;i<fs_entry_capacity;i++)if(fs_entries[i].used && !entry_is_memory(&fs_entries[i]) && fs_cached_contents[i]){
        int duplicate=0;for(uint32_t j=0;j<i;j++)if(fs_entries[j].used && !entry_is_memory(&fs_entries[j]) && fs_cached_contents[j]==fs_cached_contents[i]){duplicate=1;break;}
        if(!duplicate)total+=ora_size(fs_cached_contents[i]);
    }return total;
}

int fs_move_replace(const char *old_path, const char *new_path) {
    return fs_move_internal(old_path, new_path, 1);
}

int fs_replace_preserve(const char *source,const char *destination,const char *backup) {
    KERNEL_GUARD(fs_gate);
    if(security_mrac_pid_quarantined(scheduler_current_pid()))return -13;
    if(!fs_ready || !source || !destination || !strcmp(source,destination) ||
       (backup && (!strcmp(source,backup) || !strcmp(destination,backup))))return -22;
    const char *names[3]={source,destination,backup};
    fs_disk_entry_t *entries[3]={0};
    for(unsigned i=0;i<3;i++)if(names[i]) {
        if(fs_root_path_pinned(names[i]) || !fs_root_scope_allows(names[i],FS_ROOT_RENAME|FS_ROOT_WRITE))return -13;
        if(find_boot_module_entry(names[i]))return -30;
        entries[i]=find_entry(names[i]);
        if(!entries[i]){if(i<2)return -2;if(fs_directory_exists(names[i]))return -21;continue;}
        if(entries[i]->type!=FS_ENTRY_REGULAR)return -95;
        if(!userspace_path_allowed(names[i]) || !home_path_allowed(names[i]) ||
           !entry_allows_access(entries[i],0,1,0) ||
           (scheduler_current_user_mode() && !(fs_root_credential_caps(scheduler_current_capabilities())&VEX_CAP_FS_ADMIN) && fs_is_protected(names[i])))return -13;
    }
    /* Crossing volatile/durable storage needs extent migration, not just a
       metadata swap. Refuse it before mutation instead of losing backing. */
    if(path_is_memory(source)!=path_is_memory(destination) ||
       (backup && path_is_memory(backup)!=path_is_memory(destination)))return -18;
    struct transaction {fs_header_t header;fs_disk_entry_t saved[3];} *tx=kmalloc(sizeof(*tx));
    if(!tx)return -12;
    tx->header=fs_header;
    for(unsigned i=0;i<3;i++)if(entries[i])tx->saved[i]=*entries[i];
    fs_disk_entry_t *src=entries[0],*dst=entries[1],*old_backup=entries[2];
    src->uid=dst->uid;src->gid=dst->gid;src->mode=dst->mode;memcpy(src->acl,dst->acl,sizeof(src->acl));
    copy_path(src->path,destination);src->hash=fnv1a64_string(destination);src->protected=fs_is_protected(destination)!=0;entry_touch(src,VEX_FILE_CHANGE_TIME);
    if(backup){copy_path(dst->path,backup);dst->hash=fnv1a64_string(backup);dst->protected=fs_is_protected(backup)!=0;entry_touch(dst,VEX_FILE_CHANGE_TIME);}
    else {dst->used=0;--fs_header.entry_count;}
    if(old_backup){old_backup->used=0;--fs_header.entry_count;}
    int error=0;
    for(unsigned i=0;i<3;i++)if(entries[i] && !entry_is_memory(&tx->saved[i]) && persist_slot((uint32_t)(entries[i]-fs_entries)))error=1;
    if(!error && persist_header())error=1;
    if(error){
        for(unsigned i=0;i<3;i++)if(entries[i])*entries[i]=tx->saved[i];fs_header=tx->header;
        /* Report failure even if the device also refuses rollback. Never
           publish success after a partial metadata write. */
        for(unsigned i=0;i<3;i++)if(entries[i] && !entry_is_memory(entries[i]))(void)persist_slot((uint32_t)(entries[i]-fs_entries));
        (void)persist_header();kfree(tx);return -5;
    }
    for(unsigned i=0;i<3;i++)if(names[i])spotlight_index_remove(names[i]);
    spotlight_index_add(destination,(size_t)src->size);
    if(backup)spotlight_index_add(backup,(size_t)dst->size);
    for(unsigned i=1;i<3;i++)if(entries[i] && !entries[i]->used){
        size_t index=(size_t)(entries[i]-fs_entries);ora_release(fs_cached_contents[index]);fs_cached_contents[index]=NULL;fs_entry_identities[index]=0;
    }
    kfree(tx);return 0;
}

int fs_exists(const char *path) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    char resolved[FS_MAX_PATH];

    if (!fs_ready || path == NULL || !normalize_absolute_path(path, normalized)) {
        return 0;
    }

    if(!fs_root_scope_allows(normalized,0))return 0;
    if (find_boot_module_entry(normalized) != NULL) {
        return 1;
    }
    if (!resolve_normalized_path_internal(normalized, resolved, 1u)) {
        return 0;
    }

    if(!fs_root_scope_allows(resolved,0))return 0;
    if (find_entry(normalized) != NULL || find_entry(resolved) != NULL) {
        return 1;
    }

    return iso9660_exists(resolved);
}

int fs_get_storage_info(fs_storage_info_t *out) {
    KERNEL_GUARD(fs_gate);
    uint64_t total = 0u;
    uint64_t used = 0u;

    if (out == NULL || !fs_ready || fs_entries == NULL) {
        return -1;
    }

    zero_bytes(out, sizeof(*out));
    out->block_size = FS_SECTOR_SIZE;
    out->file_slots = fs_entry_capacity;
    out->disk_backed = fs_disk_available != 0;

    for (uint32_t i = 0u; i < fs_entry_capacity; ++i) {
        fs_disk_entry_t *entry = &fs_entries[i];
        uint64_t sectors = 0u;
        if (!entry->used) {
            continue;
        }
        ++out->used_files;
        if (entry->size != 0u &&
            sectors_for_size_checked((size_t)entry->size, &sectors) == 0 &&
            used <= UINT64_MAX - sectors) {
            used += sectors;
        }
    }

    if (fs_disk_available) {
        const uint64_t disk_limit = fs_disk_size();
        total = disk_limit > FS_DATA_START_LBA
            ? disk_limit - FS_DATA_START_LBA : 0u;
        out->total_blocks = total;
        out->used_blocks = used > total ? total : used;
        out->free_blocks = total - out->used_blocks;
    }

    return 0;
}

int fs_is_protected(const char *path) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];

    if (path == NULL) {
        return 0;
    }

    if (!normalize_absolute_path(path, normalized)) {
        return 0;
    }
    return path_prefix_matches(normalized, "/sys");
}

uint64_t fs_file_identity(const char *path) {
    KERNEL_GUARD(fs_gate);char normalized[FS_MAX_PATH],resolved[FS_MAX_PATH];
    if(!path || !normalize_absolute_path(path,normalized) || !resolve_normalized_path_internal(normalized,resolved,1) ||
       !fs_root_scope_allows(resolved,0))return 0;
    fs_disk_entry_t *entry=find_entry(resolved);uint64_t *identity=NULL;
    if(entry)identity=&fs_entry_identities[entry-fs_entries];
    else {fs_boot_module_entry_t *boot=find_boot_module_entry(resolved);if(boot)identity=&boot->identity;}
    if(identity){if(!*identity){if(fs_next_identity>=(1ull<<62))return 0;*identity=fs_next_identity++;}return *identity;}
    uint32_t lba,size;if(!iso9660_get_location(resolved,&lba,&size))return (1ull<<63)|lba;
    return 0;
}

int fs_get_metadata(const char *path, fs_metadata_t *out, uint8_t follow_links) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    char resolved[FS_MAX_PATH];
    fs_disk_entry_t *entry;

    if (path == NULL || out == NULL ||
        !normalize_absolute_path(path, normalized) ||
        !resolve_normalized_path_internal(normalized, resolved, follow_links)) {
        return -1;
    }

    if(!fs_root_scope_allows(follow_links?resolved:normalized,0))return -1;
    entry = find_entry(follow_links != 0u ? resolved : normalized);
    if (entry == NULL) {
        if (iso9660_exists(resolved)) {
            out->uid = 0u;
            out->gid = 0u;
            out->mode = FS_MODE_DEFAULT_FILE;
            out->type = iso9660_is_directory(resolved) ? FS_ENTRY_DIRECTORY : FS_ENTRY_REGULAR;
            out->protected_path = fs_is_protected(resolved) ? 1u : 0u;
            return 0;
        }
        return -1;
    }

    out->uid = entry->uid;
    out->gid = entry->gid;
    out->mode = entry->mode;
    out->type = entry->type;
    out->protected_path = entry->protected;
    return 0;
}

int fs_get_times(const char *path,vex_file_times_t *times) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH],resolved[FS_MAX_PATH];
    if(!times || !normalize_absolute_path(path,normalized) ||
       !resolve_normalized_path_internal(normalized,resolved,1u) ||
       !userspace_path_allowed(resolved))return -1;
    zero_bytes(times,sizeof(*times));
    /* A writable entry has its own dates; it never inherits the ISO's. */
    fs_disk_entry_t *entry=find_entry(resolved);
    if(!entry)entry=find_directory_marker(resolved);
    if(entry) {
        times->valid=entry->time_valid&(VEX_FILE_ACCESS_TIME|VEX_FILE_MODIFY_TIME|VEX_FILE_CHANGE_TIME);
        for(uint32_t i=0u;i<3u;++i) {
            times->seconds[i]=entry->time_seconds[i];
            times->nanoseconds[i]=entry->time_nanoseconds[i];
        }
        return 0;
    }
    return iso9660_get_times(resolved,times);
}

/* utimensat: set the access and/or modification time (a field of `times`
   counts only when its valid bit is set); the change time becomes now. */
int fs_set_times(const char *path, const vex_file_times_t *times, int follow) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH],resolved[FS_MAX_PATH];
    fs_disk_entry_t *entry, old;
    if(!fs_ready || !times || !path || !normalize_absolute_path(path,normalized) ||
       !resolve_normalized_path_internal(normalized,resolved,follow?1u:0u) ||
       !userspace_path_allowed(resolved))return -1;
    if(!fs_root_scope_allows(resolved,FS_ROOT_WRITE))return -1;
    entry=find_entry(resolved);
    if(!entry)entry=find_directory_marker(resolved);
    if(!entry)return -1;
    /* Only the owner (or an administrator) may set arbitrary times. */
    if(scheduler_current_user_mode()!=0u && !(fs_root_credential_caps(scheduler_current_capabilities()) & VEX_CAP_FS_ADMIN) &&
       entry->uid!=fs_root_credential_uid(user_current_uid()))return FS_NOT_OWNER;
    old=*entry;
    for(uint32_t i=0u;i<2u;++i) {
        if(!(times->valid&(1u<<i)))continue;
        entry->time_seconds[i]=times->seconds[i];
        entry->time_nanoseconds[i]=times->nanoseconds[i];
        entry->time_valid|=1u<<i;
    }
    entry_touch(entry,VEX_FILE_CHANGE_TIME);
    if(persist_entry(entry)!=0){*entry=old;return -1;}
    return 0;
}

int fs_set_mode(const char *path, uint32_t mode) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    char resolved[FS_MAX_PATH];
    fs_disk_entry_t *entry;
    fs_disk_entry_t old;

    if (!fs_ready || path == NULL ||
        !normalize_absolute_path(path, normalized) ||
        !resolve_normalized_path_internal(normalized, resolved, 1u)) {
        return -1;
    }

    if(!fs_root_scope_allows(resolved,FS_ROOT_WRITE))return -1;
    entry = find_entry(resolved);
    if (entry == NULL) {
        entry = find_directory_marker(resolved);
    }
    if (entry == NULL) {
        return -1;
    }
    if (scheduler_current_user_mode() != 0u &&
        !(fs_root_credential_caps(scheduler_current_capabilities()) & VEX_CAP_FS_ADMIN) &&
        entry->uid != fs_root_credential_uid(user_current_uid())) {
        return FS_NOT_OWNER;
    }

    old = *entry;
    entry->mode = sanitize_mode(mode, entry->type);
    entry_touch(entry, VEX_FILE_CHANGE_TIME);
    if (persist_entry(entry) != 0) {
        *entry = old;
        return -1;
    }
    return 0;
}

int fs_mode_mask(const char *path,uint32_t mask,uint32_t value) {
    KERNEL_GUARD(fs_gate);fs_metadata_t metadata;
    if((mask&~0777u) || (value&~mask))return -22;
    if(fs_get_metadata(path,&metadata,1))return -2;
    int result=fs_set_mode(path,(metadata.mode&~mask)|value);
    return result==FS_NOT_OWNER?-13:(result?-5:0);
}

int fs_symlink(const char *target, const char *link_path) {
    KERNEL_GUARD(fs_gate);
    char target_normalized[FS_MAX_PATH];
    char normalized[FS_MAX_PATH];
    fs_disk_entry_t *entry;
    fs_header_t old_header;
    uint64_t hash;

    if (!fs_ready || target == NULL || link_path == NULL ||
        target[0] != '/' ||
        !normalize_absolute_path(target, target_normalized) ||
        !normalize_absolute_path(link_path, normalized)) {
        return -1;
    }
    if(fs_root_path_pinned(normalized) || !fs_root_scope_allows(normalized,FS_ROOT_CREATE))return -1;
    if (find_entry(normalized) != NULL || find_boot_module_entry(normalized) != NULL) {
        return -1;
    }
    if (!userspace_path_allowed(normalized) || !home_path_allowed(normalized)) {
        return -1;
    }

    hash = fnv1a64_string(normalized);
    entry = find_free_entry();
    if (entry == NULL) {
        return -1;
    }

    init_entry_metadata(entry, normalized, hash, FS_ENTRY_SYMLINK);
    entry_born(entry, normalized);
    entry->mode = sanitize_mode(FS_MODE_DEFAULT_SYMLINK, FS_ENTRY_SYMLINK);
    copy_path(entry->link_target, target_normalized);
    old_header = fs_header;
    ++fs_header.entry_count;
    if (persist_entry(entry) != 0 || persist_header() != 0) {
        zero_bytes(entry, sizeof(*entry));
        fs_header = old_header;
        return -1;
    }
    return 0;
}

int fs_readlink(const char *path, char *buffer, size_t size) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    fs_disk_entry_t *entry;
    size_t i = 0u;

    if (!fs_ready || path == NULL || buffer == NULL || size == 0u ||
        !normalize_absolute_path(path, normalized)) {
        return -1;
    }

    if(!fs_root_scope_allows(normalized,FS_ROOT_READ))return -1;
    entry = find_entry(normalized);
    if (entry == NULL || entry->type != FS_ENTRY_SYMLINK) {
        return -1;
    }
    while (entry->link_target[i] != '\0' && i < size) {
        buffer[i] = entry->link_target[i];
        ++i;
    }
    return (int)i;
}

int fs_resolve_path(const char *path, char *buffer, size_t size) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    char resolved[FS_MAX_PATH];
    size_t i = 0u;

    if (path == NULL || buffer == NULL || size == 0u ||
        !normalize_absolute_path(path, normalized) ||
        !resolve_normalized_path_internal(normalized, resolved, 1u)) {
        return -1;
    }

    while (resolved[i] != '\0' && i + 1u < size) {
        buffer[i] = resolved[i];
        ++i;
    }
    buffer[i] = '\0';
    return 0;
}

/* A directory that exists only because files live below it (no marker entry),
 * such as /home/root when only /home/root/bin/... or /home/root/Downloads/...
 * were created. Listing already reports such directories, so access to them
 * must agree. */
static int has_descendant_entry(const char *directory) {
    for (uint32_t i = 0u; i < fs_entry_capacity; ++i) {
        if (fs_entries[i].used && path_prefix_matches(fs_entries[i].path, directory) &&
            !string_equals(fs_entries[i].path, directory)) {
            return 1;
        }
    }
    for (fs_boot_module_entry_t *module = fs_boot_modules; module != NULL; module = module->next) {
        if (module->used && path_prefix_matches(module->path, directory) &&
            !string_equals(module->path, directory)) {
            return 1;
        }
    }
    return 0;
}

int fs_directory_exists(const char *path) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH],resolved[FS_MAX_PATH];
    if(!path || !normalize_absolute_path(path,normalized) ||
       !resolve_normalized_path_internal(normalized,resolved,1) || !fs_root_scope_allows(resolved,0))return 0;
    fs_disk_entry_t *entry=find_entry(resolved);
    if(entry)return entry->type==FS_ENTRY_DIRECTORY;
    return find_directory_marker(resolved)!=NULL || has_descendant_entry(resolved) || iso9660_is_directory(resolved);
}

int fs_access_path(const char *path, int require_read, int require_write, int require_exec, uint8_t follow_links) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    char resolved[FS_MAX_PATH];
    fs_disk_entry_t *entry;

    if (path == NULL ||
        !normalize_absolute_path(path, normalized) ||
        !resolve_normalized_path_internal(normalized, resolved, follow_links)) {
        debug_log_puts("fs_access: normalize/resolve failed\n");
        return -1;
    }
    if (!userspace_path_allowed(resolved)) {
        debug_log_puts("fs_access: userspace deny ");
        debug_log_puts(resolved);
        debug_log_puts(" user_mode=");
        debug_log_puts(scheduler_current_user_mode() != 0u ? "1" : "0");
        debug_log_puts(" abi=");
        debug_log_puts(scheduler_current_abi() == VEX_ABI_LINUX ? "linux" : "other");
        debug_log_puts("\n");
        return -1;
    }

    if(!fs_root_scope_allows(resolved,(require_read?FS_ROOT_READ:0)|(require_write?FS_ROOT_WRITE:0)|(require_exec?FS_ROOT_EXEC:0)))return -1;
    entry = find_entry(follow_links != 0u ? resolved : normalized);
    if (entry == NULL && follow_links != 0u) {
        entry = find_directory_marker(resolved);
    }
    if (entry == NULL) {
        if (find_boot_module_entry(resolved) != NULL ||
            find_boot_module_entry(normalized) != NULL) {
            return require_write ? -1 : 0;
        }
        if (has_descendant_entry(resolved)) {
            return require_write ? -1 : 0;   /* implicit directory: readable, not writable */
        }
        if (iso9660_exists(resolved)) {
            return 0;
        }
        debug_log_puts("fs_access: no entry, iso miss ");
        debug_log_puts(resolved);
        debug_log_puts("\n");
        return -1;
    }
    if (!entry_allows_access(entry, require_read, require_write, require_exec)) {
        debug_log_puts("fs_access: entry deny ");
        debug_log_puts(resolved);
        debug_log_puts("\n");
        return -1;
    }
    return 0;
}
int fs_directory_can_modify(const char *path){
    KERNEL_GUARD(fs_gate);char normalized[FS_MAX_PATH],resolved[FS_MAX_PATH];
    if(!path || !normalize_absolute_path(path,normalized) || !resolve_normalized_path_internal(normalized,resolved,1) ||
       !userspace_path_allowed(resolved) || !fs_root_scope_allows(resolved,FS_ROOT_WRITE|FS_ROOT_RENAME))return 0;
    fs_disk_entry_t *entry=find_entry(resolved);if(!entry)entry=find_directory_marker(resolved);
    return entry && entry_allows_access(entry,0,1,1);
}

int fs_acl_copy(const char *path, fs_acl_entry_t *entries, size_t capacity, size_t *count_out) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    char resolved[FS_MAX_PATH];
    fs_disk_entry_t *entry;
    size_t count = 0u;

    if (path == NULL || entries == NULL || count_out == NULL ||
        !normalize_absolute_path(path, normalized) ||
        !resolve_normalized_path_internal(normalized, resolved, 1u)) {
        return -1;
    }

    entry = find_entry(resolved);
    if (entry == NULL) {
        return -1;
    }

    for (size_t i = 0u; i < FS_ACL_MAX_ENTRIES && count < capacity; ++i) {
        if (entry->acl[i].used == 0u) {
            continue;
        }
        entries[count++] = entry->acl[i];
    }
    *count_out = count;
    return 0;
}

int fs_acl_set(const char *path, uint32_t uid, uint8_t perms) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    char resolved[FS_MAX_PATH];
    fs_disk_entry_t *entry;
    size_t free_slot = FS_ACL_MAX_ENTRIES;

    if (!fs_ready || path == NULL ||
        !normalize_absolute_path(path, normalized) ||
        !resolve_normalized_path_internal(normalized, resolved, 1u)) {
        return -1;
    }

    if(!fs_root_scope_allows(resolved,FS_ROOT_WRITE))return -1;
    entry = find_entry(resolved);
    if (entry == NULL) {
        return -1;
    }
    if (scheduler_current_user_mode() != 0u &&
        !(fs_root_credential_caps(scheduler_current_capabilities()) & VEX_CAP_FS_ADMIN) &&
        entry->uid != fs_root_credential_uid(user_current_uid())) {
        return -1;
    }

    perms &= (FS_ACL_READ | FS_ACL_WRITE | FS_ACL_EXEC);
    if (perms == 0u) {
        return fs_acl_clear(path, uid);
    }

    for (size_t i = 0u; i < FS_ACL_MAX_ENTRIES; ++i) {
        if (entry->acl[i].used != 0u && entry->acl[i].uid == uid) {
            const fs_acl_entry_t old_acl = entry->acl[i];
            entry->acl[i].perms = perms;
            if (persist_entry(entry) != 0) {
                entry->acl[i] = old_acl;
                return -1;
            }
            return 0;
        }
        if (entry->acl[i].used == 0u && free_slot == FS_ACL_MAX_ENTRIES) {
            free_slot = i;
        }
    }
    if (free_slot == FS_ACL_MAX_ENTRIES) {
        return -1;
    }

    entry->acl[free_slot].uid = uid;
    entry->acl[free_slot].perms = perms;
    entry->acl[free_slot].used = 1u;
    if (persist_entry(entry) != 0) {
        entry->acl[free_slot].uid = 0u;
        entry->acl[free_slot].perms = 0u;
        entry->acl[free_slot].used = 0u;
        return -1;
    }
    return 0;
}

int fs_acl_clear(const char *path, uint32_t uid) {
    KERNEL_GUARD(fs_gate);
    char normalized[FS_MAX_PATH];
    char resolved[FS_MAX_PATH];
    fs_disk_entry_t *entry;

    if (!fs_ready || path == NULL ||
        !normalize_absolute_path(path, normalized) ||
        !resolve_normalized_path_internal(normalized, resolved, 1u)) {
        return -1;
    }

    if(!fs_root_scope_allows(resolved,FS_ROOT_WRITE))return -1;
    entry = find_entry(resolved);
    if (entry == NULL) {
        return -1;
    }
    if (scheduler_current_user_mode() != 0u &&
        !(fs_root_credential_caps(scheduler_current_capabilities()) & VEX_CAP_FS_ADMIN) &&
        entry->uid != fs_root_credential_uid(user_current_uid())) {
        return -1;
    }

    for (size_t i = 0u; i < FS_ACL_MAX_ENTRIES; ++i) {
        if (entry->acl[i].used != 0u && entry->acl[i].uid == uid) {
            const fs_acl_entry_t old_acl = entry->acl[i];
            entry->acl[i].uid = 0u;
            entry->acl[i].perms = 0u;
            entry->acl[i].used = 0u;
            if (persist_entry(entry) != 0) {
                entry->acl[i] = old_acl;
                return -1;
            }
            return 0;
        }
    }
    return -1;
}

void fs_list(void (*visitor)(const char *path, size_t size)) {
    KERNEL_GUARD(fs_gate);
    if (!fs_ready || visitor == NULL) {
        return;
    }

    for (uint32_t i = 0; i < fs_entry_capacity; ++i) {
        if (fs_entries[i].used && fs_root_scope_allows(fs_entries[i].path,FS_ROOT_ENUM)) {
            visitor(fs_entries[i].path, (size_t)fs_entries[i].size);
        }
    }
    /* The portable boot archive is a real read-only backend, just like ISO.
       Its ancestors must remain observable when no CD-ROM driver exists. */
    for(fs_boot_module_entry_t *m=fs_boot_modules;m;m=m->next)
        if(m->used && !find_entry(m->path) && fs_root_scope_allows(m->path,FS_ROOT_ENUM))visitor(m->path,m->size);
}

typedef struct fs_directory_scan {
    fs_directory_visitor_t visitor;
    void *context;
} fs_directory_scan_t;

static int fs_iso_directory_child(const char *name, size_t size, int directory, void *context) {
    fs_directory_scan_t *scan = context;
    return scan->visitor(name, size, directory ? FS_ENTRY_DIRECTORY : FS_ENTRY_REGULAR, scan->context);
}

int fs_list_directory(const char *path, fs_directory_visitor_t visitor, void *context) {
    KERNEL_GUARD(fs_gate);
    if (!fs_ready || !path || !visitor || !fs_root_scope_allows(path,FS_ROOT_ENUM)) return -1;
    size_t prefix = 0u;
    while (path[prefix]) ++prefix;
    const int root = prefix == 1u && path[0] == '/';
    for (uint32_t i = 0; i < fs_entry_capacity; ++i) {
        if (!fs_entries[i].used || !path_prefix_matches(fs_entries[i].path, path)) continue;
        const char *child = fs_entries[i].path + prefix;
        if (!root) {
            if (*child != '/') continue;
            ++child;
        }
        size_t length = 0u;
        char name[FS_MAX_PATH];
        while (child[length] && child[length] != '/' && length + 1u < sizeof(name)) {
            name[length] = child[length];
            ++length;
        }
        if (!length) continue;
        name[length] = '\0';
        const uint8_t type = child[length] == '/' ? FS_ENTRY_DIRECTORY : fs_entries[i].type;
        int result = visitor(name, (size_t)fs_entries[i].size, type, context);
        if (result) return result;
    }
    for(fs_boot_module_entry_t *m=fs_boot_modules;m;m=m->next) {
        if(!m->used || find_entry(m->path) || !path_prefix_matches(m->path,path))continue;
        const char *child=m->path+prefix;
        if(!root){if(*child!='/')continue;++child;}
        char name[FS_MAX_PATH];size_t length=0;
        while(child[length] && child[length]!='/' && length+1<sizeof(name)) {
            name[length]=child[length];++length;
        }
        if(!length)continue;
        name[length]=0;
        int result=visitor(name,m->size,child[length]=='/'?FS_ENTRY_DIRECTORY:FS_ENTRY_REGULAR,context);
        if(result)return result;
    }
    fs_directory_scan_t scan = {visitor, context};
    return iso9660_is_directory(path) ? iso9660_list_directory(path,fs_iso_directory_child,&scan) : 0;
}
