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
/* RAM described by the firmware map, excluding holes/MMIO/framebuffers. */
void pmm_memory_snapshot(uint64_t *ram_bytes,uint64_t *free_bytes);
void pmm_memory_usage(uint64_t *,uint64_t *,uint64_t *);
/* Observational backing ledger only; no new admission policy. Unassigned
 * physical allocations are charged until attached to an already charged
 * private mapping. Overcommit may exceed physical capacity, honestly. */
void pmm_commit_delta(int64_t private_mapped_delta,int64_t logical_delta);
int pmm_commit_usage(uint64_t *current,uint64_t *peak);

#endif
