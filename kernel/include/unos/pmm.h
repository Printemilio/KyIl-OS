#ifndef UNOS_PMM_H
#define UNOS_PMM_H

#include <limine.h>

void pmm_init(struct limine_memmap_response *memmap);
void *pmm_alloc_page(void);
void *pmm_alloc_contiguous_pages(uint64_t page_count);
void pmm_free_page(void *addr);
void pmm_free_contiguous_pages(void *addr, uint64_t page_count);
void pmm_reclaim_boot_module_range(void *addr, uint64_t size);
uint64_t pmm_total_page_count(void);
uint64_t pmm_used_page_count(void);
uint64_t pmm_free_page_count(void);

#endif
