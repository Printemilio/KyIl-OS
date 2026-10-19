#include <unos/vmm.h>
#include <unos/boot_console.h>
#include <unos/boot_ranges.h>
#include <unos/kernel_mapping_stats.h>
#include <unos/scheduler.h>

#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/boot_requests.h>
#include <unos/pmm.h>
#include <unos/memory_diagnostic.h>
#include <unos/debug_log.h>
#include <unos/smp.h>
#include <unos/kernel_guard.h>
#include <stdio.h>

#define VMM_PAGE_SIZE 4096ull
#define VMM_ENTRIES_PER_TABLE 512ull
#define VMM_ADDR_MASK 0x000ffffffffff000ull
#define VMM_KERNEL_PML4_START 256ull
#define VMM_PAGE_FLAG_MASK (0xfffull | VMM_PAGE_NX)

#define MSR_EFER 0xc0000080u
#define EFER_NXE (1ull << 11u)

extern char __kernel_start[];
extern char __kernel_end[];

static uint64_t *kernel_pml4;
static uint64_t hhdm_offset;
kernel_mapping_stats_t kernel_mapping_stats;

/* Kernel PDPT/PD/PT pages belong to VMM and are shared by every PML4.
 * Register roots, not scheduler tasks or process groups: vfork can borrow a
 * CR3 across groups, and a loader's new root exists before a task owns it.
 * Metadata uses PMM/HHDM directly to avoid recursive heap/MRAC mappings. */
typedef struct vmm_root {
    uint64_t *pml4;
    struct vmm_root *next, *hash_next;
    kernel_gate_t table_lock;
    uint32_t walkers, active;
    uint8_t closing;
    vmm_memory_usage_t usage;
} vmm_root_t;
static vmm_root_t *vmm_roots;
static vmm_root_t *vmm_root_index[256];
static unsigned kernel_mapping_lock;
static volatile uint8_t mapping_test_ready;
static uint64_t mapping_test_address;
static uint64_t mapping_test_reserved_address;

static void mapping_test_thread(void) {
    while (!mapping_test_ready) process_wait_event(&mapping_test_ready);
    int ok = *(volatile uint64_t *)(uintptr_t)mapping_test_address == UINT64_C(0x5a81f743aedd0123);
    process_exit_status(ok ? 0 : 1);
}

static uint64_t kernel_mapping_enter(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    while (__atomic_exchange_n(&kernel_mapping_lock, 1u, __ATOMIC_ACQUIRE))
        smp_relax();
    return flags;
}

static void kernel_mapping_leave(uint64_t flags) {
    __atomic_store_n(&kernel_mapping_lock, 0u, __ATOMIC_RELEASE);
    __asm__ volatile ("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

/* Registry references protect metadata; each root owns its table lock.
 * No page-table walk may outlive the tables it traverses. Shared kernel
 * branches use the separate kernel-mapping lock, never a whole-kernel gate. */
static unsigned root_bucket(void *pml4) { return ((uint64_t)pml4 >> 12u) & 255u; }
static vmm_root_t *root_find(void *pml4) {
    for(vmm_root_t *r=vmm_root_index[root_bucket(pml4)];r;r=r->hash_next)
        if(r->pml4==pml4)return r;
    return NULL;
}
static void destroy_user_tables(uint64_t *pml4,vmm_root_t *root);
/* Caller holds registry lock. An executing CPU and every lock contender are
 * counted before publication. Closing prevents new activation/walk leases. */
static int root_retire(vmm_root_t *root) {
    if(!root || root->closing!=1u || root->active || root->walkers)return 0;
    vmm_root_t **link=&vmm_roots;
    while(*link && *link!=root)link=&(*link)->next;
    if(*link)*link=root->next;
    link=&vmm_root_index[root_bucket(root->pml4)];
    while(*link && *link!=root)link=&(*link)->hash_next;
    if(*link)*link=root->hash_next;
    --kernel_mapping_stats.registered_spaces;
    return 1;
}
static void root_free(vmm_root_t *root) {
    /* No CPU can load this root, no software walk can observe it, and CR3
       switches flush non-global user translations (PCID is not enabled). */
    destroy_user_tables(root->pml4,root);
    pmm_free_page((void *)((uint64_t)root->pml4-hhdm_offset));
    pmm_free_page((void *)((uint64_t)root-hhdm_offset));
}
typedef struct vmm_walk_guard {
    vmm_root_t *root;
    kernel_guard_t table;
    uint64_t flags;
    uint8_t kernel,valid;
} vmm_walk_guard_t;
static vmm_walk_guard_t walk_enter(void *pml4,uint64_t virt) {
    vmm_walk_guard_t g={0};
    if(!pml4)return g;
    g.flags=kernel_mapping_enter();
    if(kernel_pml4 && ((virt >> 39u)&511u)>=VMM_KERNEL_PML4_START) {
        g.kernel=g.valid=1u;return g;
    }
    g.root=root_find(pml4);
    if(g.root && !g.root->closing) {
        ++g.root->walkers;g.valid=1u;
    }
    kernel_mapping_leave(g.flags);
    if(g.valid)g.table=kernel_guard_enter(&g.root->table_lock);
    return g;
}
static void walk_leave(vmm_walk_guard_t *g) {
    if(!g->valid)return;
    if(g->kernel){kernel_mapping_leave(g->flags);return;}
    kernel_guard_leave(&g->table);
    uint64_t flags=kernel_mapping_enter();
    --g->root->walkers;int retired=root_retire(g->root);
    kernel_mapping_leave(flags);
    if(retired)root_free(g->root);
}
#define VMM_WALK(pml4,virt,failed) \
    vmm_walk_guard_t walk_guard __attribute__((cleanup(walk_leave)))=walk_enter(pml4,virt); \
    if(!walk_guard.valid){failed;}

static void account_leaf(vmm_root_t *root,uint64_t old,uint64_t next){
    if(!root)return;
    int was=(old&VMM_ADDR_MASK) && (old&VMM_PAGE_USER),now=(next&VMM_ADDR_MASK) && (next&VMM_PAGE_USER);
    int private_before=was && !(old&VMM_PAGE_SHARED_ACCOUNT),private_now=now && !(next&VMM_PAGE_SHARED_ACCOUNT);
    if(now && !was){root->usage.resident+=4096;++root->usage.materializations;}
    if(was && !now)root->usage.resident-=4096;
    if(private_now && !private_before)root->usage.private_bytes+=4096;
    if(private_before && !private_now)root->usage.private_bytes-=4096;
    if(private_now!=private_before)pmm_commit_delta(private_now?4096:-4096,0);
    if(root->usage.resident>root->usage.peak_resident)root->usage.peak_resident=root->usage.resident;
    if(root->usage.private_bytes>root->usage.peak_private)root->usage.peak_private=root->usage.private_bytes;
}
int vmm_memory_usage(void *pml4,vmm_memory_usage_t *out){VMM_WALK(pml4,0,return -1);if(!walk_guard.root)return -1;*out=walk_guard.root->usage;return 0;}

int vmm_activate_cr3(uint64_t cr3) {
    cr3&=VMM_ADDR_MASK;
    if(!cr3)return -1;
    vex_cpu_t *cpu=smp_current_cpu();
    uint64_t flags=kernel_mapping_enter();
    vmm_root_t *next=root_find((void *)(hhdm_offset+cr3));
    vmm_root_t *previous=cpu?cpu->active_root:NULL;
    if(!next || (next->closing && next!=previous)) {
        kernel_mapping_leave(flags);return -1;
    }
    if(next!=previous)++next->active;
    /* Retain precedes the hardware switch; release follows it. A root cannot
       be retired between the registry lookup and CR3 publication. */
    uint64_t active;__asm__ volatile("mov %%cr3,%0":"=r"(active));
    if((active&VMM_ADDR_MASK)!=cr3)__asm__ volatile("mov %0,%%cr3"::"r"(cr3):"memory");
    if(cpu)cpu->active_root=next;
    int retired=0;
    if(previous && previous!=next){--previous->active;retired=root_retire(previous);}
    kernel_mapping_leave(flags);
    if(retired)root_free(previous);
    return 0;
}

void vmm_kernel_mapping_stats_snapshot(kernel_mapping_stats_t *out, int reset) {
    if (!out) return;
    uint64_t flags = kernel_mapping_enter();
#define SNAPSHOT(field) out->field = reset \
    ? __atomic_exchange_n(&kernel_mapping_stats.field, 0u, __ATOMIC_RELAXED) \
    : __atomic_load_n(&kernel_mapping_stats.field, __ATOMIC_RELAXED)
    SNAPSHOT(map_calls); SNAPSHOT(unmap_calls); SNAPSHOT(task_visits);
    SNAPSHOT(distinct_spaces); SNAPSHOT(duplicate_cr3); SNAPSHOT(sync_calls);
    SNAPSHOT(root_slots_checked); SNAPSHOT(root_stores); SNAPSHOT(root_changes);
    SNAPSHOT(leaf_maps); SNAPSHOT(leaf_unmaps); SNAPSHOT(root_publications);
#undef SNAPSHOT
    out->registered_spaces = kernel_mapping_stats.registered_spaces;
    kernel_mapping_leave(flags);
}

/* Only a newly created top-level kernel branch needs publication. Lower
 * tables remain shared, including after their final leaf is unmapped. */
static void publish_kernel_root(uint64_t index) {
    ++kernel_mapping_stats.root_publications;
    for (vmm_root_t *root = vmm_roots; root; root = root->next) {
        if (root->pml4 == kernel_pml4) continue;
        ++kernel_mapping_stats.distinct_spaces;
        ++kernel_mapping_stats.root_slots_checked;
        if (root->pml4[index] != kernel_pml4[index]) {
            root->pml4[index] = kernel_pml4[index];
            ++kernel_mapping_stats.root_stores;
            ++kernel_mapping_stats.root_changes;
        }
    }
}

static void diagnostic_table(uint64_t *table, unsigned level, uint64_t *tables, uint64_t *leaves) {
    ++*tables;
    for(unsigned i=0;i<512;i++) {
        uint64_t e=table[i];
        if (!(e & VMM_ADDR_MASK)) continue;
        if(level==1) {++*leaves;continue;}
        if(e & 0x80u) {*leaves += level==3 ? 262144u : 512u;continue;}
        diagnostic_table((void *)(hhdm_offset+(e&VMM_ADDR_MASK)),level-1,tables,leaves);
    }
}
void vmm_memory_diagnostic(void *pml4) {
    VMM_WALK(pml4,0u,return);
    uint64_t tables=1,pages=0,ktables=1,kpages=0;char line[160];uint64_t *t=pml4;
    if(!t)return;
    for(unsigned i=0;i<512;i++) if(t[i]&VMM_ADDR_MASK)
        diagnostic_table((void *)(hhdm_offset+(t[i]&VMM_ADDR_MASK)),3,i<256?&tables:&ktables,i<256?&pages:&kpages);
    snprintf(line,sizeof(line),"memory-category: vmm user_pages=%u tables=%u kernel_tables=%u\n",
        (unsigned int)pages,(unsigned int)tables,(unsigned int)ktables);
    debug_log_puts(line);
}

/* Walk allocated table branches, not every VA in a sparse reservation. */
static uint64_t resident_range(uint64_t *table,unsigned level,uint64_t base,uint64_t start,uint64_t end) {
    uint64_t count=0,span=1ull<<(12u+9u*(level-1u));
    for(unsigned i=0;i<512u;i++) {
        uint64_t lo=base+i*span,hi=lo+span,e=table[i];
        if(lo>=end || hi<=start || !(e&VMM_ADDR_MASK))continue;
        if(level==1u || (level<4u && (e&0x80u))) {
            uint64_t a=lo>start?lo:start,b=hi<end?hi:end;
            count+=(b-a+4095u)/4096u;
        }else count+=resident_range((void *)(hhdm_offset+(e&VMM_ADDR_MASK)),level-1u,lo,start,end);
    }
    return count;
}
uint64_t vmm_resident_pages(void *pml4,uint64_t start,uint64_t end) {
    VMM_WALK(pml4,0u,return 0);
    if(!pml4 || start>=end || end>0x800000000000ull)return 0;
    return resident_range(pml4,4,0,start,end);
}

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

static uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32u) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr" :: "c"(msr), "a"((uint32_t)value),
                      "d"((uint32_t)(value >> 32u)) : "memory");
}

static void invalidate_page(uint64_t virt) {
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
    smp_tlb_shootdown(0u,virt & ~4095ull,4096u);
}

static void invalidate_user_tables(void *pml4) {
    uint64_t active;__asm__ volatile("mov %%cr3,%0":"=r"(active));
    /* User tables never contain global entries. Retire the parent link before
       invalidating paging-structure caches and before reusing its frame. */
    __asm__ volatile("mov %0,%%cr3"::"r"(active):"memory");
    smp_tlb_shootdown(virt_to_phys(pml4),0u,0u);
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
        /* A non-present leaf may still retain a physical page (PROT_NONE).
           Treat it as occupied so unmapping another entry cannot free the
           containing table while that reservation is still live. */
        if (table[i] != 0u) {
            return 0;
        }
    }

    return 1;
}

static void map_page(void *pml4, uint64_t virt, uint64_t phys, uint64_t flags,vmm_root_t *root) {
    if (pml4 == NULL ||
        ((flags & VMM_PAGE_USER) != 0u && pml4_index(virt) >= VMM_KERNEL_PML4_START)) {
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

    uint64_t previous_leaf=pt[pt_index(virt)];
    pt[pt_index(virt)] = align_down(phys, VMM_PAGE_SIZE) | (flags & VMM_PAGE_FLAG_MASK);
    account_leaf(root,previous_leaf,pt[pt_index(virt)]);
    if (pml4_index(virt) >= VMM_KERNEL_PML4_START)
        __atomic_fetch_add(&kernel_mapping_stats.leaf_maps, 1u, __ATOMIC_RELAXED);
    /* x86 does not cache non-present translations. An unused zero leaf has
       no stale translation to revoke on another CPU. Replacements and
       PROT_NONE reservations still require acknowledged invalidation. */
    if(previous_leaf)invalidate_page(virt);
    else __asm__ volatile("invlpg (%0)"::"r"(virt):"memory");
}

void vmm_map_page(void *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (pml4 == NULL ||
        ((flags & VMM_PAGE_USER) && pml4_index(virt) >= VMM_KERNEL_PML4_START)) return;
    if (kernel_pml4 != NULL && pml4_index(virt) >= VMM_KERNEL_PML4_START) {
        uint64_t saved = kernel_mapping_enter();
        uint64_t index = pml4_index(virt), previous = kernel_pml4[index];
        map_page(kernel_pml4, virt, phys, flags,NULL);
        if (kernel_pml4[index] != previous) publish_kernel_root(index);
        kernel_mapping_leave(saved);
    } else {
        VMM_WALK(pml4,virt,return);
        map_page(pml4, virt, phys, flags,walk_guard.root);
    }
}

static void unmap_page(void *pml4, uint64_t virt,vmm_root_t *root) {
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
    account_leaf(root,pt[pti],0);pt[pti] = 0;
    uint8_t retire_pt=0,retire_pd=0,retire_pdpt=0;
    if (pml4i >= VMM_KERNEL_PML4_START)
        __atomic_fetch_add(&kernel_mapping_stats.leaf_unmaps, 1u, __ATOMIC_RELAXED);
    if (pml4i < VMM_KERNEL_PML4_START && table_empty(pt)) {
        pd[pdi] = 0;
        retire_pt=1;

        if (table_empty(pd)) {
            pdpt[pdpti] = 0;
            retire_pd=1;

            if (table_empty(pdpt)) {
                pml4_table[pml4i] = 0;
                retire_pdpt=1;
            }
        }
    }
    if(retire_pt)invalidate_user_tables(pml4);
    else invalidate_page(virt);
    if(retire_pt)pmm_free_page((void *)(pde&VMM_ADDR_MASK));
    if(retire_pd)pmm_free_page((void *)(pdpte&VMM_ADDR_MASK));
    if(retire_pdpt)pmm_free_page((void *)(pml4e&VMM_ADDR_MASK));
}

void vmm_unmap_page(void *pml4, uint64_t virt) {
    if (pml4 == NULL) return;
    if (kernel_pml4 != NULL && pml4_index(virt) >= VMM_KERNEL_PML4_START) {
        uint64_t saved = kernel_mapping_enter();
        unmap_page(kernel_pml4, virt,NULL);
        kernel_mapping_leave(saved);
    } else {
        VMM_WALK(pml4,virt,return);
        unmap_page(pml4, virt,walk_guard.root);
    }
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
    VMM_WALK(pml4,virt,return 0);
    if (pml4 == NULL) {
        return 0;
    }

    uint64_t *pml4_table = walk_guard.kernel ? kernel_pml4 : pml4;
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

uint64_t vmm_get_physical_any(void *pml4, uint64_t virt) {
    VMM_WALK(pml4,virt,return 0);
    if (pml4 == NULL) {
        return 0;
    }

    uint64_t *pml4_table = walk_guard.kernel ? kernel_pml4 : pml4;
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
    if (pte == 0) {
        return 0;
    }
    return (pte & VMM_ADDR_MASK) | (virt & (VMM_PAGE_SIZE - 1ull));
}

uint64_t vmm_get_page_flags(void *pml4, uint64_t virt) {
    VMM_WALK(pml4,virt,return 0);
    if (pml4 == NULL) {
        return 0;
    }

    uint64_t *pml4_table = walk_guard.kernel ? kernel_pml4 : pml4;
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
    /* Frame number zero is valid (notably in a UEFI loader memory map).
       Only an entirely empty leaf is absent; PROT_NONE also retains flags. */
    if (pte == 0) {
        return 0;
    }
    uint64_t effective = pte & VMM_PAGE_FLAG_MASK;
    if ((pml4e & VMM_PAGE_USER) == 0u ||
        (pdpte & VMM_PAGE_USER) == 0u ||
        (pde & VMM_PAGE_USER) == 0u) {
        effective &= ~VMM_PAGE_USER;
    }
    return effective;
}

int vmm_set_page_flags(void *pml4, uint64_t virt, uint64_t flags) {
    VMM_WALK(pml4,virt,return -1);
    if (pml4 == NULL) {
        return -1;
    }

    uint64_t *pml4_table = walk_guard.kernel ? kernel_pml4 : pml4;
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
    if (pte == 0) {
        return -1;
    }

    /* A user address space may never alter the shared kernel half.  Reject a
       USER request there instead of silently converting it into a kernel
       mapping, so callers cannot mistake a successful update for a user map. */
    if (pml4i >= VMM_KERNEL_PML4_START) {
        if ((flags & VMM_PAGE_USER) != 0u) {
            return -1;
        }
    }

    if ((flags & VMM_PAGE_USER) != 0u && pml4i < VMM_KERNEL_PML4_START) {
        pml4_table[pml4i] |= VMM_PAGE_USER;
        pdpt[pdpti] |= VMM_PAGE_USER;
        pd[pdi] |= VMM_PAGE_USER;
    }

    pt[pti] = (pte & (VMM_ADDR_MASK|VMM_PAGE_SHARED_ACCOUNT)) | (flags & VMM_PAGE_FLAG_MASK);
    account_leaf(walk_guard.root,pte,pt[pti]);
    invalidate_page(virt);
    return 0;
}

int vmm_set_page_flags_range(void *pml4, uint64_t virt, uint64_t length, uint64_t flags) {
    VMM_WALK(pml4,0u,return -1);
    uint64_t start;
    uint64_t end;
    uint64_t *pml4_table;

    if (pml4 == NULL || length == 0u || virt > UINT64_MAX - length ||
        ((flags & VMM_PAGE_USER) != 0u && ((virt >> 39u) >= VMM_KERNEL_PML4_START))) {
        return -1;
    }
    start = align_down(virt, VMM_PAGE_SIZE);
    end = align_up(virt + length, VMM_PAGE_SIZE);
    if (end == 0u || end < start) {
        return -1;
    }
    pml4_table = pml4;
    uint64_t changed = 0u;
    for (uint64_t pml4i = 0u; pml4i < VMM_KERNEL_PML4_START; ++pml4i) {
        const uint64_t pml4e = pml4_table[pml4i];
        if ((pml4e & VMM_PAGE_PRESENT) == 0u) continue;
        if (end <= (pml4i << 39u) || start >= ((pml4i + 1u) << 39u)) continue;   /* outside the range */
        uint64_t *pdpt = phys_to_virt(pml4e & VMM_ADDR_MASK);
        for (uint64_t pdpti = 0u; pdpti < VMM_ENTRIES_PER_TABLE; ++pdpti) {
            const uint64_t pdpte = pdpt[pdpti];
            if ((pdpte & VMM_PAGE_PRESENT) == 0u) continue;
            if (end <= ((pml4i << 39u) | (pdpti << 30u)) || start >= ((pml4i << 39u) | (pdpti << 30u)) + (1ull << 30u)) continue;
            uint64_t *pd = phys_to_virt(pdpte & VMM_ADDR_MASK);
            for (uint64_t pdi = 0u; pdi < VMM_ENTRIES_PER_TABLE; ++pdi) {
                const uint64_t pde = pd[pdi];
                if ((pde & VMM_PAGE_PRESENT) == 0u) continue;
                if (end <= ((pml4i << 39u) | (pdpti << 30u) | (pdi << 21u)) || start >= ((pml4i << 39u) | (pdpti << 30u) | (pdi << 21u)) + (1ull << 21u)) continue;
                const uint64_t pt_base = (pml4i << 39u) | (pdpti << 30u) |
                    (pdi << 21u);
                uint64_t *pt = phys_to_virt(pde & VMM_ADDR_MASK);
                for (uint64_t pti = 0u; pti < VMM_ENTRIES_PER_TABLE; ++pti) {
                    const uint64_t page = pt_base | (pti << 12u);
                    const uint64_t pte = pt[pti];
                    if ((pte & VMM_ADDR_MASK) == 0u || page < start || page >= end) continue;
                    if ((flags & VMM_PAGE_USER) != 0u) {
                        pml4_table[pml4i] |= VMM_PAGE_USER;
                        pdpt[pdpti] |= VMM_PAGE_USER;
                        pd[pdi] |= VMM_PAGE_USER;
                    }
                    pt[pti] = (pte & (VMM_ADDR_MASK|VMM_PAGE_SHARED_ACCOUNT)) | (flags & VMM_PAGE_FLAG_MASK);
                    account_leaf(walk_guard.root,pte,pt[pti]);
                    __asm__ volatile ("invlpg (%0)" :: "r"(page) : "memory");
                    ++changed;
                }
            }
        }
    }
    /* Other CPUs may hold the old permissions: invalidate them once for the range
     * (the shootdown flushes everything when the range is long). */
    if (changed != 0u) smp_tlb_shootdown(0u, start, end - start);
    return 0;
}

int vmm_clone_present_pages(void *src_pml4, void *dst_pml4, uint64_t virt, uint64_t length) {
    void *first=(uint64_t)src_pml4<(uint64_t)dst_pml4?src_pml4:dst_pml4;
    void *second=first==src_pml4?dst_pml4:src_pml4;
    vmm_walk_guard_t first_guard __attribute__((cleanup(walk_leave)))=walk_enter(first,0u);
    if(!first_guard.valid)return -1;
    vmm_walk_guard_t second_guard __attribute__((cleanup(walk_leave)))=walk_enter(second,0u);
    if(!second_guard.valid)return -1;

    uint64_t start;
    uint64_t end;

    if (src_pml4 == NULL || dst_pml4 == NULL || length == 0u ||
        virt > UINT64_MAX - length) {
        return -1;
    }
    start = align_down(virt, VMM_PAGE_SIZE);
    end = align_up(virt + length, VMM_PAGE_SIZE);
    if (end == 0u || end < start) return -1;

    {
        uint64_t *src = src_pml4;
        for (uint64_t pml4i = 0u; pml4i < VMM_KERNEL_PML4_START; ++pml4i) {
            const uint64_t pml4e = src[pml4i];
            if ((pml4e & VMM_PAGE_PRESENT) == 0u) continue;
            if (end <= (pml4i << 39u) || start >= ((pml4i + 1u) << 39u)) continue;   /* outside the range */
            uint64_t *pdpt = phys_to_virt(pml4e & VMM_ADDR_MASK);
            for (uint64_t pdpti = 0u; pdpti < VMM_ENTRIES_PER_TABLE; ++pdpti) {
                const uint64_t pdpte = pdpt[pdpti];
                if ((pdpte & VMM_PAGE_PRESENT) == 0u) continue;
                if (end <= ((pml4i << 39u) | (pdpti << 30u)) || start >= ((pml4i << 39u) | (pdpti << 30u)) + (1ull << 30u)) continue;
                uint64_t *pd = phys_to_virt(pdpte & VMM_ADDR_MASK);
                for (uint64_t pdi = 0u; pdi < VMM_ENTRIES_PER_TABLE; ++pdi) {
                    const uint64_t pde = pd[pdi];
                    if ((pde & VMM_PAGE_PRESENT) == 0u) continue;
                    if (end <= ((pml4i << 39u) | (pdpti << 30u) | (pdi << 21u)) || start >= ((pml4i << 39u) | (pdpti << 30u) | (pdi << 21u)) + (1ull << 21u)) continue;
                    const uint64_t pt_base = (pml4i << 39u) | (pdpti << 30u) |
                        (pdi << 21u);
                    uint64_t *pt = phys_to_virt(pde & VMM_ADDR_MASK);
                    for (uint64_t pti = 0u; pti < VMM_ENTRIES_PER_TABLE; ++pti) {
                        const uint64_t page = pt_base | (pti << 12u);
                        const uint64_t pte = pt[pti];
                        const uint64_t source_phys = pte & VMM_ADDR_MASK;
                        void *new_page;
                        if (source_phys == 0u || page < start || page >= end) continue;
                        new_page = pmm_alloc_page();
                        if (new_page == NULL || hhdm_request.response == NULL) return -1;
                        memcpy((uint8_t *)(hhdm_request.response->offset +
                                   (uint64_t)new_page),
                               (const uint8_t *)(hhdm_request.response->offset + source_phys),
                               VMM_PAGE_SIZE);
                        vmm_map_page(dst_pml4, page, (uint64_t)new_page,
                                     pte & (VMM_PAGE_FLAG_MASK&~VMM_PAGE_SHARED_ACCOUNT));
                        if ((vmm_get_physical_any(dst_pml4, page) & VMM_ADDR_MASK) !=
                            (uint64_t)new_page) {
                            pmm_free_page(new_page);
                            return -1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

#define VMM_UNMAP_BATCH 64u
/* One cross-CPU shootdown for a batch of cleared leaves (it used to be one per
 * page, each an IPI round trip); the frames are freed only once every CPU has
 * dropped its translations. */
static void vmm_release_batch(uint64_t *frames, unsigned *count, uint8_t free_physical) {
    if (*count == 0u) return;
    smp_tlb_shootdown(0u, 0u, 0u);
    if (free_physical != 0u) {
        for (unsigned i = 0u; i < *count; ++i) {
            pmm_free_page((void *)(uintptr_t)frames[i]);
        }
    }
    *count = 0u;
}

void vmm_unmap_range(void *pml4, uint64_t virt, uint64_t length, uint8_t free_physical) {
    VMM_WALK(pml4,0u,return);
    if (pml4 == NULL || length == 0u || virt > UINT64_MAX - length) {
        return;
    }

    const uint64_t start = align_down(virt, VMM_PAGE_SIZE);
    const uint64_t end = align_up(virt + length, VMM_PAGE_SIZE);
    if (end == 0u || end < start) {
        return;
    }
    /* Beyond a few pages, walk only the tables the range touches once instead of
     * re-walking from the root (and re-locking) for every page. User half only. */
    if (length > 16ull * VMM_PAGE_SIZE && ((end - 1u) >> 39u) < VMM_KERNEL_PML4_START) {
        uint64_t *pml4_table = pml4;
        for (uint64_t pml4i = 0u; pml4i < VMM_KERNEL_PML4_START; ++pml4i) {
            const uint64_t pml4e = pml4_table[pml4i];
            if ((pml4e & VMM_PAGE_PRESENT) == 0u) continue;
            if (end <= (pml4i << 39u) || start >= ((pml4i + 1u) << 39u)) continue;   /* outside the range */
            uint64_t *pdpt = phys_to_virt(pml4e & VMM_ADDR_MASK);
            for (uint64_t pdpti = 0u; pdpti < VMM_ENTRIES_PER_TABLE; ++pdpti) {
                const uint64_t pdpte = pdpt[pdpti];
                if ((pdpte & VMM_PAGE_PRESENT) == 0u) continue;
                if (end <= ((pml4i << 39u) | (pdpti << 30u)) || start >= ((pml4i << 39u) | (pdpti << 30u)) + (1ull << 30u)) continue;
                uint64_t *pd = phys_to_virt(pdpte & VMM_ADDR_MASK);
                for (uint64_t pdi = 0u; pdi < VMM_ENTRIES_PER_TABLE; ++pdi) {
                    const uint64_t pde = pd[pdi];
                    if ((pde & VMM_PAGE_PRESENT) == 0u) continue;
                    if (end <= ((pml4i << 39u) | (pdpti << 30u) | (pdi << 21u)) || start >= ((pml4i << 39u) | (pdpti << 30u) | (pdi << 21u)) + (1ull << 21u)) continue;
                    const uint64_t pt_base = (pml4i << 39u) | (pdpti << 30u) |
                        (pdi << 21u);
                    uint64_t *pt = phys_to_virt(pde & VMM_ADDR_MASK);
                    uint64_t released[VMM_UNMAP_BATCH];   /* small: this runs on a kernel stack */
                    unsigned released_count = 0u;
                    for (uint64_t pti = 0u; pti < VMM_ENTRIES_PER_TABLE; ++pti) {
                        const uint64_t page = pt_base | (pti << 12u);
                        const uint64_t phys = pt[pti] & VMM_ADDR_MASK;
                        if (phys == 0u || page < start || page >= end) continue;
                        /* Clear the leaf in-place.  Calling vmm_unmap_page()
                         * here could free `pt` (and its parents) while this
                         * sparse traversal still references them. */
                        account_leaf(walk_guard.root,pt[pti],0);pt[pti] = 0u;
                        released[released_count++] = phys;
                        if (released_count == VMM_UNMAP_BATCH) {
                            vmm_release_batch(released, &released_count, free_physical);
                        }
                    }
                    vmm_release_batch(released, &released_count, free_physical);
                    if (table_empty(pt)) {
                        pd[pdi] = 0u;
                        invalidate_user_tables(pml4);
                        pmm_free_page((void *)(pde & VMM_ADDR_MASK));
                    }
                }
                if (table_empty(pd)) {
                    pdpt[pdpti] = 0u;
                    invalidate_user_tables(pml4);
                    pmm_free_page((void *)(pdpte & VMM_ADDR_MASK));
                }
            }
            if (table_empty(pdpt)) {
                pml4_table[pml4i] = 0u;
                invalidate_user_tables(pml4);
                pmm_free_page((void *)(pml4e & VMM_ADDR_MASK));
            }
        }
        return;
    }
    for (uint64_t page = start; page < end; page += VMM_PAGE_SIZE) {
        const uint64_t phys = vmm_get_physical_any(pml4, page);
        if (phys != 0u) {
            vmm_unmap_page(pml4, page);
            if (free_physical != 0u) {
                pmm_free_page((void *)(phys & VMM_ADDR_MASK));
            }
        }
    }
}

static void release_user_tables(uint64_t *pml4, int (*owns_page)(uint64_t, void *), void *context,vmm_root_t *root) {
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
                    const uint64_t address=(pml4i<<39u)|(pdpti<<30u)|(pdi<<21u)|(pti<<12u);
                    account_leaf(root,pte,0);
                    if ((pte & VMM_ADDR_MASK) != 0u && (!owns_page || owns_page(address,context))) {
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

static void destroy_user_tables(uint64_t *pml4,vmm_root_t *root) {
    release_user_tables(pml4,NULL,NULL,root);
}

int vmm_retire_user_mappings(void *pml4, int (*owns_page)(uint64_t,void *),void *context) {
    if(!pml4 || pml4==kernel_pml4 || !owns_page)return -1;
    VMM_WALK(pml4,0u,return -1);
    /* A contending walk has already acquired a registry lease. Do not change
     * its contract; the caller can fall back to the ordinary range retirement. */
    uint64_t flags=kernel_mapping_enter();
    if(walk_guard.root->walkers!=1u || walk_guard.root->closing) {
        kernel_mapping_leave(flags);return -1;
    }
    walk_guard.root->closing=2u; /* closed, explicit destroy still owns root */
    kernel_mapping_leave(flags);
    uint64_t detached[VMM_KERNEL_PML4_START];
    uint64_t *table=pml4;
    for(unsigned i=0;i<VMM_KERNEL_PML4_START;++i){detached[i]=table[i];table[i]=0;}
    /* Paging-structure caches as well as leaves are invalidated before any
     * physical frame is reusable. Late CPU activation/walks are refused; CPUs
     * retaining this CR3 can only execute kernel/idle at final owner release. */
    invalidate_user_tables(pml4);
    release_user_tables(detached,owns_page,context,walk_guard.root);
    return 0;
}

void vmm_destroy_address_space(void *pml4) {
    if (pml4 == NULL || pml4 == kernel_pml4) {
        return;
    }

    uint64_t saved=kernel_mapping_enter();
    vmm_root_t *root=root_find(pml4);
    if(root)root->closing=1u;
    int retired=root_retire(root);
    kernel_mapping_leave(saved);
    if(retired)root_free(root);
}

int vmm_unmap_kernel_range(uint64_t virt,uint64_t length) {
    if(!kernel_pml4 || !length || virt<UINT64_C(0xffff800000000000) ||
       virt>UINT64_MAX-length)return -1;
    const uint64_t start=align_down(virt,VMM_PAGE_SIZE),end=align_up(virt+length,VMM_PAGE_SIZE);
    if(!end || end<=start)return -1;
    VMM_WALK(kernel_pml4,start,return -1);
    /* This operation retires 4-KiB leaves, not HHDM/MMIO huge pages. Validate
     * before modifying anything, so rejection leaves the extent intact. */
    for(uint64_t page=start;page<end;) {
        uint64_t next=(page|((1ull<<21u)-1u))+1u;if(!next || next>end)next=end;
        uint64_t pml4e=kernel_pml4[pml4_index(page)];
        if(pml4e&VMM_PAGE_PRESENT) {
            uint64_t *pdpt=phys_to_virt(pml4e&VMM_ADDR_MASK),pdpte=pdpt[pdpt_index(page)];
            if(pdpte&VMM_PAGE_PRESENT) {
                if(pdpte&0x80u)return -1;
                uint64_t *pd=phys_to_virt(pdpte&VMM_ADDR_MASK),pde=pd[pd_index(page)];
                if((pde&(VMM_PAGE_PRESENT|0x80u))==(VMM_PAGE_PRESENT|0x80u))return -1;
            }
        }
        page=next;
    }
    uint64_t changed=0;
    for(uint64_t page=start;page<end;) {
        uint64_t next=(page|((1ull<<21u)-1u))+1u;
        if(!next || next>end)next=end;
        uint64_t pml4e=kernel_pml4[pml4_index(page)];
        if(!(pml4e&VMM_PAGE_PRESENT)){page=next;continue;}
        uint64_t *pdpt=phys_to_virt(pml4e&VMM_ADDR_MASK),pdpte=pdpt[pdpt_index(page)];
        if(!(pdpte&VMM_PAGE_PRESENT)){page=next;continue;}
        uint64_t *pd=phys_to_virt(pdpte&VMM_ADDR_MASK),pde=pd[pd_index(page)];
        if(!(pde&VMM_PAGE_PRESENT)){page=next;continue;}
        uint64_t *pt=phys_to_virt(pde&VMM_ADDR_MASK);
        for(;page<next;page+=VMM_PAGE_SIZE) {
            uint64_t *pte=&pt[pt_index(page)];
            if(*pte&VMM_ADDR_MASK){*pte=0;++changed;}
        }
    }
    if(!changed)return 0;
    __atomic_fetch_add(&kernel_mapping_stats.leaf_unmaps,changed,__ATOMIC_RELAXED);
    /* Include local invalidation on UP, where the remote-shootdown helper is
     * intentionally a no-op. Global entries require PGE invalidation too. */
    uint64_t cr4,cr3;
    __asm__ volatile("mov %%cr4,%0; mov %%cr3,%1":"=r"(cr4),"=r"(cr3));
    if(cr4&(1ull<<7u)) {
        const uint64_t no_global=cr4&~(1ull<<7u);
        __asm__ volatile("mov %0,%%cr4; mov %1,%%cr4"::"r"(no_global),"r"(cr4):"memory");
    } else __asm__ volatile("mov %0,%%cr3"::"r"(cr3):"memory");
    smp_tlb_shootdown(0u,start,end-start);
    return 0;
}

void *vmm_new_address_space(void) {
    void *pml4_phys = pmm_alloc_page();
    if (pml4_phys == NULL) {
        return NULL;
    }

    void *root_phys = pmm_alloc_page();
    if (root_phys == NULL) {
        pmm_free_page(pml4_phys);
        return NULL;
    }
    vmm_root_t *root = phys_to_virt((uint64_t)root_phys);

    uint64_t *pml4 = phys_to_virt((uint64_t)pml4_phys);
    zero_page(pml4);

    uint64_t saved = kernel_mapping_enter();
    if (kernel_pml4 != NULL) {
        for (uint64_t i = VMM_KERNEL_PML4_START; i < VMM_ENTRIES_PER_TABLE; ++i) {
            pml4[i] = kernel_pml4[i];
        }
    }

    memset(root,0,sizeof(*root));
    root->table_lock=(kernel_gate_t)KERNEL_GATE_INIT;
    root->pml4 = pml4;
    root->hash_next=vmm_root_index[root_bucket(pml4)];
    vmm_root_index[root_bucket(pml4)]=root;
    root->next = vmm_roots;
    vmm_roots = root;
    ++kernel_mapping_stats.registered_spaces;
    kernel_mapping_leave(saved);

    return pml4;
}

void vmm_sync_kernel_mappings(void *pml4) {
    uint64_t *table = pml4;

    if (table == NULL || kernel_pml4 == NULL || table == kernel_pml4) {
        return;
    }

    uint64_t saved = kernel_mapping_enter();
    uint64_t changes = 0u;
    for (uint64_t i = VMM_KERNEL_PML4_START; i < VMM_ENTRIES_PER_TABLE; ++i) {
        if (table[i] != kernel_pml4[i]) {
            ++changes;
            table[i] = kernel_pml4[i];
        }
    }
    __atomic_fetch_add(&kernel_mapping_stats.sync_calls, 1u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&kernel_mapping_stats.root_slots_checked, 256u, __ATOMIC_RELAXED);
    __atomic_fetch_add(&kernel_mapping_stats.root_stores, changes, __ATOMIC_RELAXED);
    __atomic_fetch_add(&kernel_mapping_stats.root_changes, changes, __ATOMIC_RELAXED);
    kernel_mapping_leave(saved);
}

int vmm_kernel_mapping_selftest(void) {
    void *a = NULL, *b = NULL, *c = NULL, *pa = NULL, *pb = NULL;
    uint32_t threads[6] = {0};
    uint64_t registered = kernel_mapping_stats.registered_spaces;
    int result = -1;
    if (!mapping_test_reserved_address) {
        for (uint64_t i = VMM_KERNEL_PML4_START; i < 511u; ++i) {
            if (!kernel_pml4[i]) {
                mapping_test_reserved_address = UINT64_C(0xffff000000000000) | (i << 39u);
                break;
            }
        }
    }
    mapping_test_address = mapping_test_reserved_address;
    if (!mapping_test_address) return -1;
    mapping_test_ready = 0u;
    a = vmm_new_address_space(); b = vmm_new_address_space();
    pa = pmm_alloc_page(); pb = pmm_alloc_page();
    if (!a || !b || !pa || !pb) goto cleanup;
    for (unsigned i = 0; i < 6u; ++i) {
        threads[i] = process_create_in_address_space(mapping_test_thread, 1u,
            vmm_address_space_to_cr3(i < 3u ? a : b));
        if (!threads[i]) goto cleanup;
    }
    uint64_t publications = kernel_mapping_stats.root_publications;
    uint64_t leaf_maps = kernel_mapping_stats.leaf_maps;
    uint64_t root_stores = kernel_mapping_stats.root_stores;
    uint64_t previous = kernel_pml4[pml4_index(mapping_test_address)];
    scheduler_map_kernel_page_all(mapping_test_address, (uint64_t)pa,
        VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_NX);
    if (kernel_mapping_stats.leaf_maps != leaf_maps + 1u ||
        kernel_mapping_stats.root_publications != publications + (previous ? 0u : 1u)) goto cleanup;
    if (!previous && kernel_mapping_stats.root_stores != root_stores +
        kernel_mapping_stats.registered_spaces - 1u) goto cleanup;
    uint64_t published = kernel_mapping_stats.root_publications;
    scheduler_map_kernel_page_all(mapping_test_address + 4096u, (uint64_t)pb,
        VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_NX);
    if (kernel_mapping_stats.root_publications != published ||
        kernel_mapping_stats.leaf_maps != leaf_maps + 2u) goto cleanup;
    if (vmm_get_physical(a, mapping_test_address) != (uint64_t)pa ||
        vmm_get_physical(b, mapping_test_address) != (uint64_t)pa ||
        vmm_get_physical(b, mapping_test_address + 4096u) != (uint64_t)pb ||
        !(vmm_get_page_flags(a, mapping_test_address) & VMM_PAGE_NX) ||
        (vmm_get_page_flags(b, mapping_test_address) & VMM_PAGE_USER) ||
        (((uint64_t *)b)[pml4_index(mapping_test_address)] & VMM_PAGE_USER)) goto cleanup;
    /* Read-only supervisor alias: test PFN zero without touching its bytes.
       UEFI may describe this frame as loader-owned. It is not an empty PTE. */
    const uint64_t zero_alias = mapping_test_address + 2u*VMM_PAGE_SIZE;
    vmm_map_page(a,zero_alias,0,VMM_PAGE_PRESENT|VMM_PAGE_NX);
    if(vmm_get_page_flags(a,zero_alias)!=(VMM_PAGE_PRESENT|VMM_PAGE_NX) ||
       vmm_get_physical(a,zero_alias+37u)!=37u ||
       vmm_set_page_flags(a,zero_alias,VMM_PAGE_NX) ||
       vmm_get_page_flags(a,zero_alias)!=VMM_PAGE_NX ||
       vmm_get_physical_any(a,zero_alias+37u)!=37u ||
       vmm_set_page_flags(a,zero_alias,VMM_PAGE_PRESENT|VMM_PAGE_NX) ||
       vmm_set_page_flags(a,zero_alias,VMM_PAGE_PRESENT|VMM_PAGE_USER)==0 ||
       vmm_get_page_flags(a,zero_alias)!=(VMM_PAGE_PRESENT|VMM_PAGE_NX))goto cleanup;
    vmm_unmap_page(a,zero_alias);
    if(vmm_get_page_flags(a,zero_alias))goto cleanup;
    /* A loader/fork root created after publication inherits the shared branch. */
    c = vmm_new_address_space();
    if (!c || vmm_get_physical(c, mapping_test_address) != (uint64_t)pa) goto cleanup;
    /* A USER request must neither alter the canonical leaf nor propagate USER. */
    vmm_map_page(a, mapping_test_address, (uint64_t)pb, VMM_PAGE_PRESENT | VMM_PAGE_USER);
    if (vmm_get_physical(b, mapping_test_address) != (uint64_t)pa) goto cleanup;
    if(vmm_unmap_kernel_range(mapping_test_address+4096u,4096u))goto cleanup;
    if (vmm_get_physical(a, mapping_test_address + 4096u) ||
        vmm_get_physical(b, mapping_test_address + 4096u) ||
        vmm_get_physical(a,mapping_test_address)!=(uint64_t)pa) goto cleanup;
    *(volatile uint64_t *)(uintptr_t)mapping_test_address = UINT64_C(0x5a81f743aedd0123);
    result = 0;
cleanup:
    if (result == 0) mapping_test_ready = 1u;
    for (unsigned i = 0; i < 6u; ++i) if (threads[i]) {
        int status = -1; uint32_t reaped = 0u;
        if (result) process_kill(threads[i]); else process_wake(threads[i]);
        if (process_wait_pid((int32_t)threads[i], &status, 0u, &reaped) != (int)threads[i] ||
            reaped != threads[i] || status != 0) result = -1;
    }
    if (a) vmm_destroy_address_space(a);
    if (b && result == 0 && vmm_get_physical(b, mapping_test_address) != (uint64_t)pa) result = -1;
    if (b) vmm_destroy_address_space(b);
    if (c && result == 0 && vmm_get_physical(c, mapping_test_address) != (uint64_t)pa) result = -1;
    if (c) vmm_destroy_address_space(c);
    if (mapping_test_address) {
        vmm_unmap_kernel_page(mapping_test_address);
        vmm_unmap_kernel_page(mapping_test_address + 4096u);
        vmm_unmap_kernel_page(mapping_test_address + 2u*VMM_PAGE_SIZE);
    }
    if (pa) pmm_free_page(pa);
    if (pb) pmm_free_page(pb);
    /* Registration/teardown metadata must not accumulate over repeated cycles. */
    uint64_t irq_flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(irq_flags) :: "memory");
    uint64_t used = pmm_used_page_count();
    for (unsigned i = 0u; i < 32u; ++i) {
        void *space = vmm_new_address_space();
        if (!space) { result = -1; break; }
        vmm_destroy_address_space(space);
    }
    if (pmm_used_page_count() != used || kernel_mapping_stats.registered_spaces != registered) result = -1;
    __asm__ volatile ("pushq %0; popfq" :: "r"(irq_flags) : "memory", "cc");
    return result;
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

static int map_range(void *pml4, uint64_t virt, uint64_t phys, uint64_t length, uint64_t flags) {
    if(!length)return 0;
    if(length>UINT64_MAX-virt || virt+length>UINT64_MAX-(VMM_PAGE_SIZE-1u) ||
       length>UINT64_MAX-phys)return -1;
    const uint64_t virt_start = align_down(virt, VMM_PAGE_SIZE);
    const uint64_t phys_start = align_down(phys, VMM_PAGE_SIZE);
    const uint64_t virt_end = align_up(virt + length, VMM_PAGE_SIZE);

    for (uint64_t offset = 0; virt_start + offset < virt_end; offset += VMM_PAGE_SIZE) {
        vmm_map_page(pml4, virt_start + offset, phys_start + offset, flags);
        /* The old void mapper silently ignored table-allocation failures.
           Never install a CR3 containing a partially mapped bootstrap range. */
        if(!(vmm_get_page_flags(pml4,virt_start+offset)&VMM_PAGE_PRESENT) ||
           vmm_get_physical(pml4,virt_start+offset)!=phys_start+offset)return -1;
    }
    return 0;
}

static int map_boot_range(uint64_t base,uint64_t length,uint64_t type,void *context) {
    (void)type;
    boot_console_puts("  HHDM RAM base=");boot_console_hex(base);
    boot_console_puts(" size=");boot_console_hex(length);boot_console_puts("\n");
    if(base>UINT64_MAX-hhdm_offset)return -1;
    return map_range(context,hhdm_offset+base,base,length,
                     VMM_PAGE_PRESENT|VMM_PAGE_WRITABLE|VMM_PAGE_NX);
}

static int map_current_stack(void *pml4) {
    uint64_t rsp;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));

    const uint64_t stack_base = align_down(rsp, VMM_PAGE_SIZE);

    for (uint64_t i = 0; i < 32u; ++i) {
        const uint64_t virt = stack_base - (i * VMM_PAGE_SIZE);
        if(virt<hhdm_offset)return -1;
        const uint64_t phys = virt - hhdm_offset;
        vmm_map_page(pml4, virt, phys, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_NX);
        if(!(vmm_get_page_flags(pml4,virt)&VMM_PAGE_PRESENT) || vmm_get_physical(pml4,virt)!=phys)return -1;
    }
    return 0;
}

void vmm_init(void) {
    char diagnostic[192];
    snprintf(diagnostic,sizeof(diagnostic),"vmm-boot: hhdm=%u map=%u executable=%u PMM=%u/%u\n",hhdm_request.response!=NULL,memmap_request.response!=NULL,executable_address_request.response!=NULL,(unsigned)pmm_used_page_count(),(unsigned)pmm_total_page_count());
    debug_log_puts(diagnostic);
    if (hhdm_request.response == NULL ||
        memmap_request.response == NULL ||
        executable_address_request.response == NULL) {
        boot_console_fail("VMM: missing Limine HHDM/memory-map/executable response");
    }

    hhdm_offset = hhdm_request.response->offset;
    /* Enable hardware NX before any user mappings are created. */
    write_msr(MSR_EFER, read_msr(MSR_EFER) | EFER_NXE);
    kernel_pml4 = vmm_new_address_space();
    if (kernel_pml4 == NULL) {
        boot_console_fail("VMM: no physical page for kernel root");
    }
    boot_console_stage("VMM: sparse RAM / loader / framebuffer HHDM",0);
    if(boot_ranges_visit(memmap_request.response,map_boot_range,kernel_pml4))
        boot_console_fail("VMM: invalid RAM extent or page-table allocation failed");
    /* Framebuffer responses are explicit mappings, even when BIOS labels their
       physical aperture as RESERVED rather than FRAMEBUFFER in the memory map. */
    if(framebuffer_request.response)for(uint64_t i=0;i<framebuffer_request.response->framebuffer_count;++i) {
        const struct limine_framebuffer *fb=framebuffer_request.response->framebuffers[i];
        if(!fb || !fb->address || !fb->pitch || fb->height>UINT64_MAX/fb->pitch ||
           (uint64_t)fb->address<hhdm_offset)
            boot_console_fail("VMM: invalid framebuffer response");
        if(map_range(kernel_pml4,(uint64_t)fb->address,(uint64_t)fb->address-hhdm_offset,
                     fb->pitch*fb->height,VMM_PAGE_PRESENT|VMM_PAGE_WRITABLE|VMM_PAGE_NX))
            boot_console_fail("VMM: framebuffer mapping failed");
    }
    boot_console_stage("VMM: sparse HHDM mapped",1);

    const uint64_t kernel_virtual_base = executable_address_request.response->virtual_base;
    const uint64_t kernel_physical_base = executable_address_request.response->physical_base;
    const uint64_t kernel_offset = (uint64_t)__kernel_start - kernel_virtual_base;
    const uint64_t kernel_length = (uint64_t)__kernel_end - (uint64_t)__kernel_start;

    if(map_range(
        kernel_pml4,
        (uint64_t)__kernel_start,
        kernel_physical_base + kernel_offset,
        kernel_length,
        VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE
    ))boot_console_fail("VMM: kernel executable mapping failed");

    if(map_current_stack(kernel_pml4))boot_console_fail("VMM: bootstrap stack mapping failed");
    boot_console_stage("VMM: switching CR3",0);
    write_cr3(virt_to_phys(kernel_pml4));
    uint64_t live_root; __asm__ volatile("mov %%cr3,%0":"=r"(live_root));
    snprintf(diagnostic,sizeof(diagnostic),"vmm-boot: root=%x live=%x\n",(unsigned)virt_to_phys(kernel_pml4),(unsigned)live_root);
    debug_log_puts(diagnostic);
    boot_console_stage("VMM: CR3 live",1);
}

