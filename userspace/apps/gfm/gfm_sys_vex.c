/* gfm_sys.h on Vex: the native file syscalls of libvex. No private storage. */

#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>

#include "gfm_sys.h"
#include "vex_runtime.h"

/* Not in the shared compat header; the value is the Linux/Vex O_EXCL. */
#define GFM_O_EXCL 0x0080

int gfm_sys_list(const char *path, gfm_sys_dirent_t *entries, size_t capacity, size_t *count) {
    static vex_directory_entry_t raw[256];
    size_t total = 0u;
    size_t kept;
    size_t i;
    size_t j;

    if (capacity > 256u) {
        capacity = 256u;
    }
    if (vex_directory_list(path, raw, capacity, &total) != 0) {
        return -1;
    }
    kept = total < capacity ? total : capacity;
    for (i = 0u; i < kept; ++i) {
        for (j = 0u; j + 1u < GFM_SYS_NAME_MAX && raw[i].name[j] != '\0'; ++j) {
            entries[i].name[j] = raw[i].name[j];
        }
        entries[i].name[j] = '\0';
        entries[i].type = raw[i].type;
    }
    *count = total;
    return 0;
}

int gfm_sys_stat(const char *path, gfm_sys_stat_t *out) {
    struct stat info;

    if (stat(path, &info) != 0) {
        return -1;
    }
    out->type = (info.st_mode & S_IFMT) == S_IFDIR ? GFM_TYPE_DIRECTORY : GFM_TYPE_FILE;
    out->size = info.st_size;
    out->mode = info.st_mode & 0777u;
    out->uid = info.st_uid;
    return 0;
}

int gfm_sys_open_read(const char *path) {
    return open(path, O_RDONLY);
}

int gfm_sys_open_create_exclusive(const char *path, uint32_t mode) {
    return open(path, O_WRONLY | O_CREAT | GFM_O_EXCL, mode);
}

long gfm_sys_read(int fd, void *buffer, size_t count) {
    return (long)read(fd, buffer, count);
}

long gfm_sys_write(int fd, const void *buffer, size_t count) {
    return (long)write(fd, buffer, count);
}

int gfm_sys_close(int fd) {
    return close(fd);
}

int gfm_sys_unlink(const char *path) {
    return unlink(path) == 0 ? 0 : -1;
}

int gfm_sys_rename(const char *from, const char *to) {
    return rename(from, to) == 0 ? 0 : -1;
}

int gfm_sys_mkdir(const char *path, uint32_t mode) {
    return mkdir(path, mode) == 0 ? 0 : -1;
}

/* rmdir only removes an empty directory: empty it first, depth first, a
 * small batch of entries per level. */
int gfm_sys_remove_tree(const char *path) {
    enum { BATCH = 16 };
    vex_directory_entry_t entries[BATCH];
    char child[512];
    size_t count = 0u;
    int result = 0;

    /* Removing children changes the listing: list again until it is empty. */
    for (;;) {
        size_t i;
        size_t removed = 0u;
        if (vex_directory_list(path, entries, BATCH, &count) != 0) {
            result = -1;
            break;
        }
        if (count == 0u) {
            break;
        }
        if (count > BATCH) {
            count = BATCH;
        }
        for (i = 0u; i < count; ++i) {
            size_t used = 0u;
            const char *p;
            for (p = path; *p != '\0' && used + 1u < sizeof(child); ++p) child[used++] = *p;
            if (used > 0u && child[used - 1u] != '/' && used + 1u < sizeof(child)) child[used++] = '/';
            for (p = entries[i].name; *p != '\0' && used + 1u < sizeof(child); ++p) child[used++] = *p;
            child[used] = '\0';
            if (entries[i].type == VEX_DIRECTORY_ENTRY_DIRECTORY
                    ? gfm_sys_remove_tree(child) == 0
                    : unlink(child) == 0) {
                ++removed;
            }
        }
        if (removed == 0u) {
            result = -1; /* nothing could be removed: stop instead of looping */
            break;
        }
    }
    if (result == 0 && rmdir(path) != 0) {
        result = -1;
    }
    return result;
}

int gfm_sys_chmod(const char *path, uint32_t mode) {
    return chmod(path, mode) == 0 ? 0 : -1;
}
