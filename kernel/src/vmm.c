#include <unos/vmm.h>

#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/boot_requests.h>
#include <unos/pmm.h>

#define VMM_PAGE_SIZE 4096ull
#define VMM_ENTRIES_PER_TABLE 512ull
#define VMM_ADDR_MASK 0x000ffffffffff000ull
#define VMM_KERNEL_PML4_START 256ull

extern char __kernel_start[];
extern char __kernel_end[];

static uint64_t *kernel_pml4;
static uint64_t hhdm_offset;

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1ull);
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

static uint64_t pml4_index(uint64_t virt) {
    return (virt >> 39u) & 0x1ffu;
}

static uint64_t pdpt_index(uint64_t virt) {
    return (virt >> 30u) & 0x1ffu;
}

static uint64_t pd_index(uint64_t virt) {
    return (virt >> 21u) & 0x1ffu;
}

static uint64_t pt_index(uint64_t virt) {
    return (virt >> 12u) & 0x1ffu;
}

static void *phys_to_virt(uint64_t phys) {
    return (void *)(hhdm_offset + phys);
}

static uint64_t virt_to_phys(const void *virt) {
    return (uint64_t)virt - hhdm_offset;
}

static void zero_page(void *page) {
    uint64_t *words = page;

    for (uint64_t i = 0; i < VMM_PAGE_SIZE / sizeof(uint64_t); ++i) {
        words[i] = 0;
    }
}

static void write_cr3(uint64_t phys) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(phys) : "memory");
}

static void invalidate_page(uint64_t virt) {
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

static uint64_t highest_physical_end(const struct limine_memmap_response *memmap) {
    uint64_t highest = 0;

    for (uint64_t i = 0; i < memmap->entry_count; ++i) {
        const struct limine_memmap_entry *entry = memmap->entries[i];
        const uint64_t end = entry->base + entry->length;

        if (end > highest) {
            highest = end;
        }
    }

    return highest;
}

static uint64_t *next_table(uint64_t *table, uint64_t index, uint64_t flags) {
    if ((table[index] & VMM_PAGE_PRESENT) != 0) {
        return phys_to_virt(table[index] & VMM_ADDR_MASK);
    }

    void *new_table_phys = pmm_alloc_page();
    if (new_table_phys == NULL) {
        return NULL;
    }

    uint64_t *new_table = phys_to_virt((uint64_t)new_table_phys);
    zero_page(new_table);

    /* User mappings require the USER bit at every level of the walk. Kernel
       mappings leave it clear on intermediate tables. */
    table[index] = ((uint64_t)new_table_phys) | VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | (flags & VMM_PAGE_USER);
    return new_table;
}

static int table_empty(uint64_t *table) {
    if (table == NULL) {
        return 1;
    }

    for (uint64_t i = 0; i < VMM_ENTRIES_PER_TABLE; ++i) {
        if ((table[i] & VMM_PAGE_PRESENT) != 0) {
            return 0;
        }
    }

    return 1;
}

void vmm_map_page(void *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (pml4 == NULL) {
        return;
    }

    uint64_t *pml4_table = pml4;
    uint64_t *pdpt = next_table(pml4_table, pml4_index(virt), flags);
    if (pdpt == NULL) {
        return;
    }

    uint64_t *pd = next_table(pdpt, pdpt_index(virt), flags);
    if (pd == NULL) {
        return;
    }

    uint64_t *pt = next_table(pd, pd_index(virt), flags);
    if (pt == NULL) {
        return;
    }

    pt[pt_index(virt)] = align_down(phys, VMM_PAGE_SIZE) | (flags & 0xfffull);
    invalidate_page(virt);
}

void vmm_unmap_page(void *pml4, uint64_t virt) {
    if (pml4 == NULL) {
        return;
    }

    uint64_t *pml4_table = pml4;
    const uint64_t pml4i = pml4_index(virt);
    const uint64_t pdpti = pdpt_index(virt);
    const uint64_t pdi = pd_index(virt);
    const uint64_t pti = pt_index(virt);
    const uint64_t pml4e = pml4_table[pml4i];
    if ((pml4e & VMM_PAGE_PRESENT) == 0) {
        return;
    }

    uint64_t *pdpt = phys_to_virt(pml4e & VMM_ADDR_MASK);
    const uint64_t pdpte = pdpt[pdpti];
    if ((pdpte & VMM_PAGE_PRESENT) == 0) {
        return;
    }

    uint64_t *pd = phys_to_virt(pdpte & VMM_ADDR_MASK);
    const uint64_t pde = pd[pdi];
    if ((pde & VMM_PAGE_PRESENT) == 0) {
        return;
    }

    uint64_t *pt = phys_to_virt(pde & VMM_ADDR_MASK);
    pt[pti] = 0;
    if (pml4i < VMM_KERNEL_PML4_START && table_empty(pt)) {
        pd[pdi] = 0;
        pmm_free_page((void *)(pde & VMM_ADDR_MASK));

        if (table_empty(pd)) {
            pdpt[pdpti] = 0;
            pmm_free_page((void *)(pdpte & VMM_ADDR_MASK));

            if (table_empty(pdpt)) {
                pml4_table[pml4i] = 0;
                pmm_free_page((void *)(pml4e & VMM_ADDR_MASK));
            }
        }
    }
    invalidate_page(virt);
}

void vmm_map_kernel_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (kernel_pml4 == NULL) {
        return;
    }

    vmm_map_page(kernel_pml4, virt, phys, flags);
}

void vmm_unmap_kernel_page(uint64_t virt) {
    if (kernel_pml4 == NULL) {
        return;
    }

    vmm_unmap_page(kernel_pml4, virt);
}

uint64_t vmm_get_physical(void *pml4, uint64_t virt) {
    if (pml4 == NULL) {
        return 0;
    }

    uint64_t *pml4_table = pml4;
    const uint64_t pml4e = pml4_table[pml4_index(virt)];
    if ((pml4e & VMM_PAGE_PRESENT) == 0) {
        return 0;
    }

    uint64_t *pdpt = phys_to_virt(pml4e & VMM_ADDR_MASK);
    const uint64_t pdpte = pdpt[pdpt_index(virt)];
    if ((pdpte & VMM_PAGE_PRESENT) == 0) {
        return 0;
    }

    uint64_t *pd = phys_to_virt(pdpte & VMM_ADDR_MASK);
    const uint64_t pde = pd[pd_index(virt)];
    if ((pde & VMM_PAGE_PRESENT) == 0) {
        return 0;
    }

    uint64_t *pt = phys_to_virt(pde & VMM_ADDR_MASK);
    const uint64_t pte = pt[pt_index(virt)];
    if ((pte & VMM_PAGE_PRESENT) == 0) {
        return 0;
    }

    return (pte & VMM_ADDR_MASK) | (virt & (VMM_PAGE_SIZE - 1ull));
}

int vmm_set_page_flags(void *pml4, uint64_t virt, uint64_t flags) {
    if (pml4 == NULL) {
        return -1;
    }

    uint64_t *pml4_table = pml4;
    const uint64_t pml4i = pml4_index(virt);
    const uint64_t pdpti = pdpt_index(virt);
    const uint64_t pdi = pd_index(virt);
    const uint64_t pti = pt_index(virt);
    const uint64_t pml4e = pml4_table[pml4i];
    if ((pml4e & VMM_PAGE_PRESENT) == 0) {
        return -1;
    }

    uint64_t *pdpt = phys_to_virt(pml4e & VMM_ADDR_MASK);
    const uint64_t pdpte = pdpt[pdpti];
    if ((pdpte & VMM_PAGE_PRESENT) == 0) {
        return -1;
    }

    uint64_t *pd = phys_to_virt(pdpte & VMM_ADDR_MASK);
    const uint64_t pde = pd[pdi];
    if ((pde & VMM_PAGE_PRESENT) == 0) {
        return -1;
    }

    uint64_t *pt = phys_to_virt(pde & VMM_ADDR_MASK);
    const uint64_t pte = pt[pti];
    if ((pte & VMM_PAGE_PRESENT) == 0) {
        return -1;
    }

    if ((flags & VMM_PAGE_USER) != 0u) {
        pml4_table[pml4i] |= VMM_PAGE_USER;
        pdpt[pdpti] |= VMM_PAGE_USER;
        pd[pdi] |= VMM_PAGE_USER;
    }

    pt[pti] = (pte & VMM_ADDR_MASK) | (flags & 0xfffull);
    invalidate_page(virt);
    return 0;
}

void vmm_unmap_range(void *pml4, uint64_t virt, uint64_t length, uint8_t free_physical) {
    if (pml4 == NULL || length == 0u) {
        return;
    }

    const uint64_t start = align_down(virt, VMM_PAGE_SIZE);
    const uint64_t end = align_up(virt + length, VMM_PAGE_SIZE);
    for (uint64_t page = start; page < end; page += VMM_PAGE_SIZE) {
        const uint64_t phys = vmm_get_physical(pml4, page);
        if (phys != 0u) {
            vmm_unmap_page(pml4, page);
            if (free_physical != 0u) {
                pmm_free_page((void *)(phys & VMM_ADDR_MASK));
            }
        }
    }
}

static void destroy_user_tables(uint64_t *pml4) {
    if (pml4 == NULL) {
        return;
    }

    for (uint64_t pml4i = 0; pml4i < VMM_KERNEL_PML4_START; ++pml4i) {
        const uint64_t pml4e = pml4[pml4i];
        if ((pml4e & VMM_PAGE_PRESENT) == 0) {
            continue;
        }

        uint64_t *pdpt = phys_to_virt(pml4e & VMM_ADDR_MASK);
        for (uint64_t pdpti = 0; pdpti < VMM_ENTRIES_PER_TABLE; ++pdpti) {
            const uint64_t pdpte = pdpt[pdpti];
            if ((pdpte & VMM_PAGE_PRESENT) == 0) {
                continue;
            }

            uint64_t *pd = phys_to_virt(pdpte & VMM_ADDR_MASK);
            for (uint64_t pdi = 0; pdi < VMM_ENTRIES_PER_TABLE; ++pdi) {
                const uint64_t pde = pd[pdi];
                if ((pde & VMM_PAGE_PRESENT) == 0) {
                    continue;
                }

                uint64_t *pt = phys_to_virt(pde & VMM_ADDR_MASK);
                for (uint64_t pti = 0; pti < VMM_ENTRIES_PER_TABLE; ++pti) {
                    const uint64_t pte = pt[pti];
                    if ((pte & VMM_PAGE_PRESENT) != 0) {
                        pmm_free_page((void *)(pte & VMM_ADDR_MASK));
                    }
                }
                pmm_free_page((void *)(pde & VMM_ADDR_MASK));
            }
            pmm_free_page((void *)(pdpte & VMM_ADDR_MASK));
        }
        pmm_free_page((void *)(pml4e & VMM_ADDR_MASK));
        pml4[pml4i] = 0;
    }
}

void vmm_destroy_address_space(void *pml4) {
    if (pml4 == NULL || pml4 == kernel_pml4) {
        return;
    }

    destroy_user_tables(pml4);
    pmm_free_page((void *)virt_to_phys(pml4));
}

void *vmm_new_address_space(void) {
    void *pml4_phys = pmm_alloc_page();
    if (pml4_phys == NULL) {
        return NULL;
    }

    uint64_t *pml4 = phys_to_virt((uint64_t)pml4_phys);
    zero_page(pml4);

    if (kernel_pml4 != NULL) {
        for (uint64_t i = VMM_KERNEL_PML4_START; i < VMM_ENTRIES_PER_TABLE; ++i) {
            pml4[i] = kernel_pml4[i];
        }
    }

    return pml4;
}

void vmm_sync_kernel_mappings(void *pml4) {
    uint64_t *table = pml4;

    if (table == NULL || kernel_pml4 == NULL || table == kernel_pml4) {
        return;
    }

    for (uint64_t i = VMM_KERNEL_PML4_START; i < VMM_ENTRIES_PER_TABLE; ++i) {
        table[i] = kernel_pml4[i];
    }
}

uint64_t vmm_address_space_to_cr3(void *pml4) {
    if (pml4 == NULL) {
        return 0;
    }

    return virt_to_phys(pml4);
}

void *vmm_cr3_to_address_space(uint64_t cr3) {
    if (cr3 == 0 || hhdm_request.response == NULL) {
        return NULL;
    }

    return phys_to_virt(cr3 & VMM_ADDR_MASK);
}

static void map_range(void *pml4, uint64_t virt, uint64_t phys, uint64_t length, uint64_t flags) {
    const uint64_t virt_start = align_down(virt, VMM_PAGE_SIZE);
    const uint64_t phys_start = align_down(phys, VMM_PAGE_SIZE);
    const uint64_t virt_end = align_up(virt + length, VMM_PAGE_SIZE);

    for (uint64_t offset = 0; virt_start + offset < virt_end; offset += VMM_PAGE_SIZE) {
        vmm_map_page(pml4, virt_start + offset, phys_start + offset, flags);
    }
}

static void map_current_stack(void *pml4) {
    uint64_t rsp;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));

    const uint64_t stack_base = align_down(rsp, VMM_PAGE_SIZE);

    for (uint64_t i = 0; i < 32u; ++i) {
        const uint64_t virt = stack_base - (i * VMM_PAGE_SIZE);
        const uint64_t phys = virt - hhdm_offset;
        vmm_map_page(pml4, virt, phys, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
    }
}

void vmm_init(void) {
    if (hhdm_request.response == NULL ||
        memmap_request.response == NULL ||
        executable_address_request.response == NULL) {
        return;
    }

    hhdm_offset = hhdm_request.response->offset;
    kernel_pml4 = vmm_new_address_space();
    if (kernel_pml4 == NULL) {
        return;
    }

    const uint64_t highest_phys = highest_physical_end(memmap_request.response);
    map_range(
        kernel_pml4,
        hhdm_offset,
        0,
        highest_phys,
        VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE
    );

    const uint64_t kernel_virtual_base = executable_address_request.response->virtual_base;
    const uint64_t kernel_physical_base = executable_address_request.response->physical_base;
    const uint64_t kernel_offset = (uint64_t)__kernel_start - kernel_virtual_base;
    const uint64_t kernel_length = (uint64_t)__kernel_end - (uint64_t)__kernel_start;

    map_range(
        kernel_pml4,
        (uint64_t)__kernel_start,
        kernel_physical_base + kernel_offset,
        kernel_length,
        VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE
    );

    map_current_stack(kernel_pml4);
    write_cr3(virt_to_phys(kernel_pml4));
}
