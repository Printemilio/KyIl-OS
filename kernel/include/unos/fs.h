#ifndef UNOS_FS_H
#define UNOS_FS_H

#include <stddef.h>
#include <stdint.h>
#include <unos/ora.h>
#include <unos/file_times.h>
int fs_get_times(const char *path, vex_file_times_t *times);
/* Opaque mount-session file identity, stable across rename/content changes,
 * distinct after deletion/recreation. No content hash/address is an identity. */
uint64_t fs_file_identity(const char *path);
/* fs_set_times and fs_set_mode fail with FS_NOT_OWNER when the caller may
   not change the entry's metadata, and -1 otherwise (no such VexFS entry,
   I/O error). */
#define FS_NOT_OWNER (-2)

/* `follow`: resolve a final symbolic link first. */
int fs_set_times(const char *path, const vex_file_times_t *times, int follow);

#define FS_MODE_DEFAULT_FILE 0644u
#define FS_MODE_DEFAULT_DIR 0755u
#define FS_MODE_DEFAULT_SYMLINK 0777u
#define FS_ACL_MAX_ENTRIES 4u
#define FS_ACL_READ  0x1u
#define FS_ACL_WRITE 0x2u
#define FS_ACL_EXEC  0x4u

typedef enum fs_entry_type {
    FS_ENTRY_REGULAR = 1,
    FS_ENTRY_DIRECTORY = 2,
    FS_ENTRY_SYMLINK = 3
} fs_entry_type_t;

typedef struct fs_metadata {
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint8_t type;
    uint8_t protected_path;
} fs_metadata_t;

typedef struct fs_acl_entry {
    uint32_t uid;
    uint8_t perms;
    uint8_t used;
} fs_acl_entry_t;

/* Native storage accounting used by compatibility views such as Linux
 * statfs.  The counts describe the VexOS filesystem backend itself; they are
 * not a synthetic Unix mount table. */
typedef struct fs_storage_info {
    uint64_t block_size;
    uint64_t total_blocks;
    uint64_t used_blocks;
    uint64_t free_blocks;
    uint64_t file_slots;
    uint64_t used_files;
    uint8_t disk_backed;
} fs_storage_info_t;

void fs_init(void);
int fs_sync(void);
int fs_format_partition(unsigned index); /* explicit SYSTEM_ADMIN/FS_ADMIN operation */
int fs_register_boot_archive(void *data,size_t size);
int fs_ora_self_test(void);
/* view must be zero-initialised; caller owns/release its exact read lease. */
int fs_read_view(const char *path, void **data, size_t *size, ora_view_t *view);
/* Bounds ingestion before disk/ISO materialization, with the same read lease.
 * Metadata/contents are checked under fs_gate. Oversize returns -3. */
int fs_read_view_bounded(const char *path,void **data,size_t *size,ora_view_t *view,size_t maximum);
/* Acquire immutable resident payload bytes; valid while the read lease is
 * held, including across cache replacement or process/creator destruction. */
int fs_read_boot_module_view(const char *path, const void **data, size_t *size, ora_view_t *view);
int fs_write(const char *path, void *data, size_t size);
int fs_register_boot_module(const char *path, void *data, size_t size);
/* Cache a transient read-only payload without taking ownership of its source.
 * The returned bytes are owned by the FS ORA cache. Caller frees its source.
 * Internal ISO ingestion only: FS keeps its gate until acquiring a lease. */
int fs_cache_readonly_payload(const char *path, void *data, size_t size,
                              const void **cached_out);
int fs_delete(const char *path);
int fs_delete_prefix(const char *prefix);
/* Fails when `new_path` exists. */
int fs_move(const char *old_path, const char *new_path);
/* POSIX rename(2): a regular file at `new_path` is replaced atomically. */
int fs_move_replace(const char *old_path, const char *new_path);
/* Atomic in-memory regular-file replacement, optional backup, preserving
 * destination metadata. IO errors roll the original entries back; this is
 * not a promise of power-loss journaling. Caller holds namespace authority. */
int fs_replace_preserve(const char *source,const char *destination,const char *backup);
int fs_mode_mask(const char *path,uint32_t mask,uint32_t value);
int fs_exists(const char *path);
int fs_is_protected(const char *path);
/* Native namespace protection, not a content lock or a Windows share lease.
 * A readable regular-file reference may pin its canonical name (and ancestors
 * against rename/removal). FS_ADMIN/security can still intervene. */
typedef struct fs_name_protection fs_name_protection_t;
fs_name_protection_t *fs_name_protect(const char *path,int *error);
void fs_name_protection_release(fs_name_protection_t *protection);
static inline void fs_name_protection_cleanup(fs_name_protection_t **p) {fs_name_protection_release(*p);}
int fs_namespace_mutation_allowed(const char *path);
int fs_get_metadata(const char *path, fs_metadata_t *out, uint8_t follow_links);
int fs_set_mode(const char *path, uint32_t mode);
int fs_symlink(const char *target, const char *link_path);
int fs_readlink(const char *path, char *buffer, size_t size);
int fs_resolve_path(const char *path, char *buffer, size_t size);
int fs_access_path(const char *path, int require_read, int require_write, int require_exec, uint8_t follow_links);
/* Directory mutation/search permission, distinct from binary EXEC authority. */
int fs_directory_can_modify(const char *path);
int fs_acl_copy(const char *path, fs_acl_entry_t *entries, size_t capacity, size_t *count_out);
int fs_acl_set(const char *path, uint32_t uid, uint8_t perms);
int fs_acl_clear(const char *path, uint32_t uid);
int fs_get_storage_info(fs_storage_info_t *out);
uint64_t fs_cached_bytes(void);
void fs_list(void (*visitor)(const char *path, size_t size));
/* Immediate children from resident/writable entries and the ISO backend.
 * Names can overlap between backends; the resident entry takes precedence.
 * Visitors may return nonzero to abort; errors are propagated. */
typedef int (*fs_directory_visitor_t)(const char *name, size_t size,
                                     uint8_t type, void *context);
int fs_list_directory(const char *path, fs_directory_visitor_t visitor, void *context);
int fs_directory_exists(const char *path);

#endif
