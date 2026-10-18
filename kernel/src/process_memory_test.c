#include <unos/process_memory.h>
#include <unos/vex_legacy_compat.h>
#include <unos/pmm.h>
#include <unos/vmm.h>
#include <unos/heap.h>
#include <unos/boot_requests.h>
#include <string.h>

int process_memory_inventory_test(void) {
    const uint32_t owner=0xfffffd00u;
    uint64_t ns=0,snapshot=0,shared_a=0,shared_b=0;
    void *pml4=vmm_new_address_space();
    if(!pml4)return -1;
    uint64_t cr3=vmm_address_space_to_cr3(pml4);
    unsigned char *bytes=kmalloc(12288);ora_view_t lease={0};
    ora_stats_t before,after;int result=-1;
    int extra_shared_reference=0;
    if(!bytes)goto done;
    memset(bytes,0x5a,12288);
    if(process_memory_namespace_begin(owner,0x200000,&ns) || process_memory_namespace_commit(owner,ns))goto done;
    if(process_memory_register_region(owner,0x200000,12288,PROT_READ,MAP_PRIVATE,0,bytes) ||
       process_memory_materialize_region(owner,cr3,0x200000))goto done;
    if(process_memory_acquire_content(owner,0x200000,&lease))goto done;
    ora_get_stats(&before);
    if(process_memory_snapshot_namespace(owner,ns,&snapshot) ||
       process_memory_protect_region(owner,0x201000,4096,PROT_NONE) ||
       process_memory_protect_region(owner,0x200000,12288,PROT_READ))goto done;
    /* Protect across all three previous slices; clone immutable views without
       re-ingestion and remove a range spanning their boundaries atomically. */
    for(unsigned cycle=0;cycle<16;++cycle) {
        uint32_t child=owner+1;
        void *child_space=vmm_new_address_space();
        if(!child_space)goto done;
        uint64_t child_cr3=vmm_address_space_to_cr3(child_space);
        int failed=process_memory_clone_namespace(owner,cr3,child,child_cr3);
        if(!failed && vmm_get_physical_any(child_space,0x202000)!=vmm_get_physical_any(pml4,0x202000))failed=1;
        process_memory_namespace_destroy(child,process_memory_current_namespace(child),child_cr3);
        process_memory_reap(child);vmm_destroy_address_space(child_space);
        if(failed)goto done;
    }
    if(process_memory_remove_region(owner,cr3,0x201000,4096))goto done;
    ora_get_stats(&after);
    if(before.fingerprints!=after.fingerprints || memcmp(ora_view_data(&lease),bytes,12288))goto done;
    /* Existing immutable projection must detach before WRITE becomes legal. */
    if(process_memory_protect_region(owner,0x200000,4096,PROT_READ|PROT_WRITE)==0 ||
       process_memory_snapshot_region(owner,cr3,0x200000) ||
       process_memory_protect_region(owner,0x200000,4096,PROT_READ|PROT_WRITE))goto done;
    uint64_t physical=vmm_get_physical_any(pml4,0x200000);
    uint64_t original=0;if(!physical || ora_view_physical_page(&lease,0,&original) || physical==original)goto done;
    uint64_t pages=pmm_free_page_count();ora_get_stats(&before);
    if(process_memory_register_mapping(owner,0x1000000000ull,128ull*1024*1024*1024,
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,PROCESS_MEMORY_FLAG_LAZY|PROCESS_MEMORY_FLAG_VMM_DIRECT))goto done;
    ora_get_stats(&after);
    if(before.fingerprints!=after.fingerprints || pages-pmm_free_page_count()>16)goto done;
    if(process_memory_materialize_page(owner,cr3,0x1000000000ull,4) ||
       process_memory_materialize_page(owner,cr3,0x2ffffff000ull,4))goto done;
    void *child_space=vmm_new_address_space();if(!child_space)goto done;
    uint32_t child=owner+1;uint64_t child_cr3=vmm_address_space_to_cr3(child_space);
    pages=pmm_free_page_count();
    int failed=process_memory_clone_namespace(owner,cr3,child,child_cr3);
    if(!failed && (pages-pmm_free_page_count()>64 ||
        vmm_get_physical_any(child_space,0x1000000000ull)==vmm_get_physical_any(pml4,0x1000000000ull)))failed=1;
    process_memory_namespace_destroy(child,process_memory_current_namespace(child),child_cr3);
    process_memory_reap(child);vmm_destroy_address_space(child_space);
    if(failed)goto done;
    if(process_memory_shared_create(4096,&shared_a) || process_memory_shared_create(4096,&shared_b) ||
       shared_a==shared_b || process_memory_shared_retain(shared_a))goto done;
    extra_shared_reference=1;
    if(process_memory_shared_map(owner,cr3,shared_a,0x400000,4096,0,PROT_READ|PROT_WRITE) ||
       process_memory_shared_map(owner,cr3,shared_b,0x500000,4096,0,PROT_READ|PROT_WRITE))goto done;
    uint64_t pa=vmm_get_physical_any(pml4,0x400000),pb=vmm_get_physical_any(pml4,0x500000);
    if(!pa || !pb || pa==pb)goto done;
    *(uint8_t *)(hhdm_request.response->offset+pa)=0x51;
    if(*(uint8_t *)(hhdm_request.response->offset+pb)!=0)goto done;
    /* Read-only shared initial bytes also require distinct mutable backing
       identities. They must never adopt the immutable ORA source's pages. */
    if(process_memory_register_region(owner,0x600000,4096,PROT_READ,MAP_SHARED,0,bytes) ||
       process_memory_register_region(owner,0x700000,4096,PROT_READ,MAP_SHARED,0,bytes) ||
       process_memory_materialize_region(owner,cr3,0x600000) ||
       process_memory_materialize_region(owner,cr3,0x700000) ||
       process_memory_adopt_shared_mapping(owner,cr3,0x600000) ||
       process_memory_adopt_shared_mapping(owner,cr3,0x700000))goto done;
    process_memory_region_t ra,rb;
    if(process_memory_get_region(owner,0x600000,&ra) || process_memory_get_region(owner,0x700000,&rb) ||
       !ra.shared_id || ra.shared_id==rb.shared_id ||
       vmm_get_physical_any(pml4,0x600000)==vmm_get_physical_any(pml4,0x700000))goto done;
    /* Whole-region rollback/removal spans two independent providers and a
       hole; it must retire each last provider exactly once, not its neighbors. */
    ora_get_stats(&before);
    if(process_memory_remove_region(owner,cr3,0x600000,0x101000) ||
       process_memory_shared_size(ra.shared_id) || process_memory_shared_size(rb.shared_id) ||
       vmm_get_physical_any(pml4,0x600000) || vmm_get_physical_any(pml4,0x700000) ||
       vmm_get_physical_any(pml4,0x400000)!=pa || vmm_get_physical_any(pml4,0x500000)!=pb)goto done;
    ora_get_stats(&after);
    if(after.storages+2!=before.storages || after.fingerprints!=before.fingerprints)goto done;
    process_memory_shared_release(shared_a); /* transfer survives creator's ref */
    extra_shared_reference=0;
    if(process_memory_shared_size(shared_a)!=4096 || process_memory_shared_committed_bytes(shared_a)!=4096)goto done;
    process_memory_shared_release(shared_a);shared_a=0;
    result=0;
done:
    process_memory_namespace_destroy(owner,ns,cr3);process_memory_reap(owner);
    if(shared_a)process_memory_shared_release(shared_a);
    if(extra_shared_reference)process_memory_shared_release(shared_a);
    if(shared_b)process_memory_shared_release(shared_b);
    ora_view_release(&lease);kfree(bytes);vmm_destroy_address_space(pml4);
    return result;
}

/* Differential test of the region inventory: random registrations, partial
 * removals and protection changes (which split regions) are mirrored in a
 * page-granular model, and after every step each page must be reported as
 * mapped by process_memory_describe_address() exactly when the model says so.
 * This covers the address-ordered index, the in-place address-list edits and the
 * overlap checks that replaced the linear scans. */
int process_memory_index_test(void) {
    enum { PAGES = 192, STEPS = 600 };
    const uint32_t owner = 0xfffffd10u;
    const uint64_t base = 0x30000000ull, page = 4096ull;
    uint8_t model[PAGES] = {0};
    uint64_t seed = 0x9e3779b97f4a7c15ull, namespace_id = 0;
    void *pml4 = vmm_new_address_space();
    int result = -1;

    if (!pml4) return -1;
    const uint64_t cr3 = vmm_address_space_to_cr3(pml4);
    if (process_memory_namespace_begin(owner, base, &namespace_id) || process_memory_namespace_commit(owner, namespace_id)) goto done;
    for (unsigned step = 0; step < STEPS; ++step) {
        seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
        const unsigned operation = (unsigned)(seed % 8u), first = (unsigned)((seed >> 8) % PAGES);
        unsigned count = 1u + (unsigned)((seed >> 24) % 12u);
        if (first + count > PAGES) count = PAGES - first;
        const uint64_t address = base + first * page, length = count * page;
        unsigned covered = 0, overlapped = 0;
        for (unsigned i = first; i < first + count; ++i) { covered += model[i] != 0; }
        overlapped = covered;
        if (operation < 3) {            /* register: only into free space, else it must be refused */
            const int status = process_memory_register_mapping(owner, address, length, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, PROCESS_MEMORY_FLAG_LAZY | PROCESS_MEMORY_FLAG_VMM_DIRECT);
            if ((status == 0) != (overlapped == 0)) goto done;
            if (status == 0) for (unsigned i = first; i < first + count; ++i) model[i] = 1;
        } else if (operation < 5) {     /* remove: succeeds when it touches anything */
            const int status = process_memory_remove_region(owner, cr3, address, length);
            if ((status == 0) != (overlapped != 0)) goto done;
            if (status == 0) for (unsigned i = first; i < first + count; ++i) model[i] = 0;
        } else {                        /* protect: needs the whole range mapped */
            const int status = process_memory_protect_region(owner, address, length, operation == 5 ? PROT_READ : PROT_READ | PROT_WRITE);
            if ((status == 0) != (covered == count)) goto done;
        }
        for (unsigned i = 0; i < PAGES; ++i) {
            uint64_t region_base = 0, offset = 0;
            uint32_t kind = 0;
            const int found = process_memory_describe_address(owner, base + i * page + 123u, &region_base, &kind, &offset, NULL, 0) == 0;
            if (found != (model[i] != 0)) goto done;
        }
    }
    result = 0;
done:
    process_memory_namespace_destroy(owner, namespace_id, cr3);
    process_memory_reap(owner);
    vmm_destroy_address_space(pml4);
    return result;
}
