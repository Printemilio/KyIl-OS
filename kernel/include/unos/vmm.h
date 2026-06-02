#ifndef UNOS_VMM_H
#define UNOS_VMM_H

#include <stdint.h>

#define VMM_PAGE_PRESENT  0x001ull
#define VMM_PAGE_WRITABLE 0x002ull
#define VMM_PAGE_USER     0x004ull
#define VMM_PAGE_PWT      0x008ull
#define VMM_PAGE_PCD      0x010ull

void vmm_init(void);
void vmm_map_page(void *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(void *pml4, uint64_t virt);
void vmm_map_kernel_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_kernel_page(uint64_t virt);
uint64_t vmm_get_physical(void *pml4, uint64_t virt);
int vmm_set_page_flags(void *pml4, uint64_t virt, uint64_t flags);
void vmm_unmap_range(void *pml4, uint64_t virt, uint64_t length, uint8_t free_physical);
void vmm_destroy_address_space(void *pml4);
void *vmm_new_address_space(void);
uint64_t vmm_address_space_to_cr3(void *pml4);
void *vmm_cr3_to_address_space(uint64_t cr3);
void vmm_sync_kernel_mappings(void *pml4);

#endif
