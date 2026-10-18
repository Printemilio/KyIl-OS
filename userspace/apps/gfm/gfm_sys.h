#ifndef GFM_SYS_H
#define GFM_SYS_H

/*
 * Thin platform layer under GFM's file operations.
 *
 * gfm_fs.c only talks to the file system through these calls, so the same
 * logic runs on Vex (gfm_sys_vex.c, native syscalls) and on the host for the
 * unit test (tests in userspace/linux_tests/gfm_fs_test.c). Every call
 * returns 0 / a descriptor >= 0 on success and a negative value on failure.
 */

#include <stddef.h>
#include <stdint.h>

#define GFM_SYS_NAME_MAX 96u

#define GFM_TYPE_UNKNOWN 0u
#define GFM_TYPE_FILE 1u
#define GFM_TYPE_DIRECTORY 2u
#define GFM_TYPE_SYMLINK 3u

typedef struct gfm_sys_dirent {
    char name[GFM_SYS_NAME_MAX];
    uint8_t type;
} gfm_sys_dirent_t;

typedef struct gfm_sys_stat {
    uint8_t type;
    uint64_t size;
    uint32_t mode;      /* permission bits only (0777) */
    uint32_t uid;
} gfm_sys_stat_t;

/* Children of a directory (never "." or ".."). `count` is set even when the
 * directory holds more entries than `capacity`. */
int gfm_sys_list(const char *path, gfm_sys_dirent_t *entries, size_t capacity, size_t *count);
int gfm_sys_stat(const char *path, gfm_sys_stat_t *out);

int gfm_sys_open_read(const char *path);
/* Creates the file and fails when it already exists. */
int gfm_sys_open_create_exclusive(const char *path, uint32_t mode);
long gfm_sys_read(int fd, void *buffer, size_t count);
long gfm_sys_write(int fd, const void *buffer, size_t count);
int gfm_sys_close(int fd);

int gfm_sys_unlink(const char *path);
/* Replaces an existing regular file at `to`, like rename(2). */
int gfm_sys_rename(const char *from, const char *to);
int gfm_sys_mkdir(const char *path, uint32_t mode);
/* Removes a directory and everything below it. */
int gfm_sys_remove_tree(const char *path);
int gfm_sys_chmod(const char *path, uint32_t mode);

#endif
