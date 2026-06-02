#ifndef UNOS_FS_H
#define UNOS_FS_H

#include <stddef.h>

void fs_init(void);
int fs_read(const char *path, void **data, size_t *size);
int fs_write(const char *path, void *data, size_t size);
int fs_register_boot_module(const char *path, void *data, size_t size);
int fs_delete(const char *path);
int fs_delete_prefix(const char *prefix);
int fs_exists(const char *path);
int fs_is_protected(const char *path);
void fs_list(void (*visitor)(const char *path, size_t size));

#endif
