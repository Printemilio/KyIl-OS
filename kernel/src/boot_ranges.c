#include <unos/boot_ranges.h>
int boot_ranges_visit(const struct limine_memmap_response *map,boot_range_visit_t visit,void *context) {
    if(!map || !map->entries || !visit)return -1;
    for(uint64_t i=0;i<map->entry_count;++i) {
        const struct limine_memmap_entry *e=map->entries[i];
        if(!e || e->length>UINT64_MAX-e->base)return -1;
        switch(e->type) {
        case LIMINE_MEMMAP_USABLE: case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: case LIMINE_MEMMAP_FRAMEBUFFER:
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: case LIMINE_MEMMAP_ACPI_NVS:
            if(e->length && visit(e->base,e->length,e->type,context))return -1;
            break;
        default: break; /* reserved holes and bad memory are not RAM */
        }
    }
    return 0;
}
