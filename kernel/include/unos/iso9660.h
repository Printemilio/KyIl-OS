#ifndef UNOS_ISO9660_H
#define UNOS_ISO9660_H

#include <stddef.h>

/* Initialise the ISO 9660 reader from the boot CD-ROM.
 * Must be called after disk_init(). */
void iso9660_init(void);

/* Read a file by its VFS-style absolute path (e.g. "/lib/libc.so.6").
 * Allocates *data_out with pmm_alloc_contiguous_pages; never freed (cached).
 * Returns 0 on success, -1 if not found or read error. */
int iso9660_read(const char *path, void **data_out, size_t *size_out);

/* Return 1 if the path exists in the ISO, 0 otherwise. */
int iso9660_exists(const char *path);

#endif
