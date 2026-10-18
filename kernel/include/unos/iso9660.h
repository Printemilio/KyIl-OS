#ifndef UNOS_ISO9660_H
#define UNOS_ISO9660_H

#include <stddef.h>
#include <stdint.h>
#include <unos/file_times.h>
int iso9660_get_times(const char *path, vex_file_times_t *times);

/* Initialise the ISO 9660 reader from the boot CD-ROM.
 * Must be called after disk_init(). */
void iso9660_init(void);

/* Read a file by its VFS-style absolute path (e.g. "/lib/libc.so.6").
 * Allocates *data_out with pmm_alloc_contiguous_pages; never freed (cached).
 * Returns 0 on success, -1 if not found or read error. */
int iso9660_read(const char *path, void **data_out, size_t *size_out);

/* Return 1 if the path exists in the ISO (file or directory), 0 otherwise. */
int iso9660_exists(const char *path);

/* Return 1 if the path is a directory on the ISO, 0 otherwise. */
int iso9660_is_directory(const char *path);

/* Immediate children only, using their Rock Ridge names when available.
 * Callback storage is borrowed until callback return. -1 denotes I/O or
 * malformed metadata, 0 an absent ISO directory or successful enumeration. */
typedef int (*iso9660_directory_visitor_t)(const char *name, size_t size,
                                          int is_directory, void *context);
int iso9660_list_directory(const char *path, iso9660_directory_visitor_t visitor,
                           void *context);

/* Get the disk location of a file without loading it into memory.
 * Returns 0 on success (file found), -1 on failure or if path is a directory. */
int iso9660_get_location(const char *path, uint32_t *lba_out, uint32_t *size_out);

/* Read [count] bytes from a file at [file_offset], using lba/file_size from
 * iso9660_get_location().  Reads from the CD-ROM in 128 KB chunks using an
 * a protected chunk buffer, safe across concurrent SMP readers.
 * Returns bytes read (>= 0), or -1 on error. */
int iso9660_pread(uint32_t lba, uint32_t file_size,
                  size_t file_offset, void *dst, size_t count);

#endif
