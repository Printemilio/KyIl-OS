#ifndef UNOS_DISK_H
#define UNOS_DISK_H

#include <stdint.h>

void disk_init(void);
const char *disk_type(void);
int disk_read(uint64_t lba, uint32_t count, void *buffer);
int disk_write(uint64_t lba, uint32_t count, void *buffer);
uint64_t disk_size(void);

/* CD-ROM / ATAPI interface (ISO 9660 sector size = 2048 bytes) */
int disk_cdrom_available(void);
int disk_cdrom_read(uint32_t lba, uint32_t count, void *buffer);

#endif
