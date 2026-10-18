/* Host implementation of gfm_sys.h, for the GFM file-operation unit test. */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../apps/gfm/gfm_sys.h"

int gfm_sys_list(const char *path, gfm_sys_dirent_t *entries, size_t capacity, size_t *count) {
    DIR *directory = opendir(path);
    struct dirent *item;
    size_t total = 0u;

    if (directory == NULL) {
        return -1;
    }
    while ((item = readdir(directory)) != NULL) {
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) {
            continue;
        }
        if (total < capacity) {
            strncpy(entries[total].name, item->d_name, sizeof(entries[total].name) - 1u);
            entries[total].name[sizeof(entries[total].name) - 1u] = 0;
            entries[total].type = item->d_type == DT_DIR ? GFM_TYPE_DIRECTORY
                                : item->d_type == DT_REG ? GFM_TYPE_FILE : GFM_TYPE_UNKNOWN;
        }
        ++total;
    }
    closedir(directory);
    *count = total;
    return 0;
}

int gfm_sys_stat(const char *path, gfm_sys_stat_t *out) {
    struct stat info;

    if (stat(path, &info) != 0) {
        return -1;
    }
    out->type = S_ISDIR(info.st_mode) ? GFM_TYPE_DIRECTORY : GFM_TYPE_FILE;
    out->size = (uint64_t)info.st_size;
    out->mode = info.st_mode & 0777u;
    out->uid = info.st_uid;
    return 0;
}

int gfm_sys_open_read(const char *path) { return open(path, O_RDONLY); }
int gfm_sys_open_create_exclusive(const char *path, uint32_t mode) {
    return open(path, O_WRONLY | O_CREAT | O_EXCL, mode);
}
long gfm_sys_read(int fd, void *buffer, size_t count) { return (long)read(fd, buffer, count); }
long gfm_sys_write(int fd, const void *buffer, size_t count) { return (long)write(fd, buffer, count); }
int gfm_sys_close(int fd) { return close(fd); }
int gfm_sys_unlink(const char *path) { return unlink(path); }
int gfm_sys_mkdir(const char *path, uint32_t mode) { return mkdir(path, mode); }
int gfm_sys_chmod(const char *path, uint32_t mode) { return chmod(path, mode); }

/* rename(2) already replaces an existing regular file. */
int gfm_sys_rename(const char *from, const char *to) { return rename(from, to); }

int gfm_sys_remove_tree(const char *path) {
    gfm_sys_dirent_t entries[256];
    size_t count = 0u;
    size_t i;

    if (gfm_sys_list(path, entries, 256u, &count) != 0) {
        return -1;
    }
    for (i = 0u; i < count && i < 256u; ++i) {
        char child[1024];

        snprintf(child, sizeof(child), "%s/%s", path, entries[i].name);
        if (entries[i].type == GFM_TYPE_DIRECTORY) {
            if (gfm_sys_remove_tree(child) != 0) {
                return -1;
            }
        } else if (unlink(child) != 0) {
            return -1;
        }
    }
    return rmdir(path);
}
