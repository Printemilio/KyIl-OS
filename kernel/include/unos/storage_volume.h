#ifndef KYIL_STORAGE_VOLUME_H
#define KYIL_STORAGE_VOLUME_H
#include <stdint.h>
typedef int (*volume_read_t)(void *,uint64_t,uint32_t,void *);
typedef struct {uint64_t base,sectors;uint32_t partition;} storage_volume_t;
/* Raw legacy VexFS or one explicitly tagged MBR non-FS-data (0xDA) volume.
 * Never adopts a foreign partition based merely on a byte pattern. */
int storage_volume_discover(volume_read_t,void *,uint64_t,storage_volume_t *);
int storage_volume_partition(volume_read_t,void *,uint64_t,unsigned,storage_volume_t *);
#endif
