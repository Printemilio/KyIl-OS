#ifndef UNOS_VMM_H
#define UNOS_VMM_H

#include <stdint.h>

#define VMM_PAGE_PRESENT  0x001ull
#define VMM_PAGE_WRITABLE 0x002ull
#define VMM_PAGE_USER     0x004ull
#define VMM_PAGE_PWT      0x008ull
#define VMM_PAGE_PCD      0x010ull
#define VMM_PAGE_SHARED_ACCOUNT 0x200ull /* software accounting only, no authority */
#define VMM_PAGE_NX       (1ull << 63u)

/* VexOS uses the lower canonical half for user mappings and PML4 entries
 * 256..511 for the shared kernel half.  Keep these limits in one place so
 * syscall validation and the page-table code cannot silently disagree. */
#define VMM_USER_ADDRESS_LIMIT 0x0000800000000000ull
#define VMM_KERNEL_ADDRESS_BASE VMM_USER_ADDRESS_LIMIT

void vmm_init(void);
/* Atomically retain/load/release the executing root, with IRQ exclusion. */
int vmm_activate_cr3(uint64_t cr3);
void vmm_map_page(void *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(void *pml4, uint64_t virt);
void vmm_map_kernel_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_kernel_page(uint64_t virt);
/* Shared kernel leaves: one acknowledged shootdown for the whole extent.
 * The caller may recycle its physical pages only after this returns. */
int vmm_unmap_kernel_range(uint64_t virt, uint64_t length);
uint64_t vmm_get_physical(void *pml4, uint64_t virt);
uint64_t vmm_get_physical_any(void *pml4, uint64_t virt);
uint64_t vmm_get_page_flags(void *pml4, uint64_t virt);
int vmm_set_page_flags(void *pml4, uint64_t virt, uint64_t flags);
/* Range variants walk only present page-table branches.  They are required
 * for sparse lazy mappings whose VA span may contain billions of pages. */
int vmm_set_page_flags_range(void *pml4, uint64_t virt, uint64_t length, uint64_t flags);
int vmm_clone_present_pages(void *src_pml4, void *dst_pml4, uint64_t virt, uint64_t length);
void vmm_unmap_range(void *pml4, uint64_t virt, uint64_t length, uint8_t free_physical);
void vmm_destroy_address_space(void *pml4);
/* Final owner retirement only, after every thread/borrowed storage reference
 * has stopped. Detach the user half, acknowledge one shootdown, then release
 * owned leaves/tables. Backing-owned leaves remain the backing's responsibility.
 * The closed root stays pinned until vmm_destroy_address_space(). */
int vmm_retire_user_mappings(void *pml4, int (*owns_page)(uint64_t, void *), void *context);
void *vmm_new_address_space(void);
uint64_t vmm_address_space_to_cr3(void *pml4);
void *vmm_cr3_to_address_space(uint64_t cr3);
void vmm_sync_kernel_mappings(void *pml4);
typedef struct vmm_memory_usage {uint64_t resident,peak_resident,private_bytes,peak_private,materializations;} vmm_memory_usage_t;
int vmm_memory_usage(void *,vmm_memory_usage_t *);
/* Explicit diagnostic, invoked by the native shell, never at normal boot. */
int vmm_kernel_mapping_selftest(void);

#endif
