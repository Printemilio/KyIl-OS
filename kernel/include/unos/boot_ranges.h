#ifndef KYIL_BOOT_RANGES_H
#define KYIL_BOOT_RANGES_H
#include <limine.h>
#include <stdint.h>
typedef int (*boot_range_visit_t)(uint64_t base,uint64_t length,uint64_t type,void *context);
/* RAM and loader-owned ranges only. MMIO must be mapped by its owner. */
int boot_ranges_visit(const struct limine_memmap_response *,boot_range_visit_t,void *);
#endif
