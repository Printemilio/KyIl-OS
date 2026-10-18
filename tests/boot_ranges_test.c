#include <unos/boot_ranges.h>
#include <assert.h>
#include <stdio.h>
static uint64_t pages,visits;
static int visit(uint64_t base,uint64_t length,uint64_t type,void *context) {
    (void)base;(void)context;
    assert(type!=LIMINE_MEMMAP_RESERVED && type!=LIMINE_MEMMAP_BAD_MEMORY);
    pages+=(length+4095)/4096;++visits;return 0;
}
int main(void) {
    struct limine_memmap_entry ram={0x100000,0x80000000,LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry hole={0x80000000,UINT64_C(0x10000000000)-0x80000000,LIMINE_MEMMAP_RESERVED};
    struct limine_memmap_entry boot={0x90000,0x10000,LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE};
    struct limine_memmap_entry framebuffer={0xe0000000,0x800000,LIMINE_MEMMAP_FRAMEBUFFER};
    struct limine_memmap_entry bad={0x10000,0x10000,LIMINE_MEMMAP_BAD_MEMORY};
    struct limine_memmap_entry *entries[]={&ram,&hole,&boot,&framebuffer,&bad};
    struct limine_memmap_response map={.entry_count=5,.entries=entries};
    assert(!boot_ranges_visit(&map,visit,NULL));
    assert(visits==3 && pages==(0x80000000u/4096u+16u+0x800000u/4096u));
    hole.base=UINT64_MAX-4;hole.length=8;assert(boot_ranges_visit(&map,visit,NULL)<0);
    assert(boot_ranges_visit(NULL,visit,NULL)<0);
    entries[0]=NULL;assert(boot_ranges_visit(&map,visit,NULL)<0);
    puts("boot-ranges: PASS 2GiB RAM + 1TiB reserved hole, loader/FB, bad pages, overflow/null rejection");
}
