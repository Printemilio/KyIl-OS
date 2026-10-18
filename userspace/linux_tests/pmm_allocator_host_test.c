/* Test the actual kernel allocator against independent ownership accounting.
 * Host-only: fake RAM bitmap, no kernel API or userspace ABI is added. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PMM_HOST_TEST 1
#include "../../kernel/src/pmm.c"
volatile struct limine_hhdm_request hhdm_request;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"PMM regression line %u\n",__LINE__); return 1; } } while (0)
int main(void) {
    /* Actual init with frame zero advertised usable by the bootloader. */
    uint8_t *ram=calloc(4,4096);CHECK(ram);
    struct limine_hhdm_response hhdm={.offset=(uintptr_t)ram};
    struct limine_memmap_entry usable={.base=0,.length=4*4096,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *entries[]={&usable};
    struct limine_memmap_response memory={.entry_count=1,.entries=entries};
    hhdm_request.response=&hhdm;pmm_init(&memory);
    CHECK(bitmap_test(0));CHECK((uintptr_t)pmm_alloc_page()==4096);
    pmm_free_page((void *)4096);CHECK(pmm_used_page_count()==1);
    free(ram);hhdm_request.response=NULL;
    uint8_t bitmap[33], owned[257];
    struct { uint64_t page, count; } live[257] = {{0}};
    memset(bitmap,0,sizeof(bitmap)); memset(owned,0,sizeof(owned));
    pmm_bitmap=bitmap;pmm_bitmap_bytes=sizeof(bitmap);pmm_total_pages=257;pmm_ready=true;
    bitmap_set(0);owned[0]=1;
    pmm_used_pages=1;
    /* Free run straddles the search cursor: a truncated second pass loses it. */
    pmm_search_cursor=4;
    void *block=pmm_alloc_contiguous_pages(256);
    CHECK((uintptr_t)block==4096);CHECK(!pmm_alloc_page());
    pmm_free_contiguous_pages(block,256);
    CHECK(!pmm_alloc_contiguous_pages(0));CHECK(!pmm_alloc_contiguous_pages(UINT64_MAX));
    pmm_free_contiguous_pages((void *)(uintptr_t)4096,UINT64_MAX);
    uint32_t rng=0x783aff21;
    for(unsigned step=0;step<100000;++step) {
        rng=rng*1664525u+1013904223u;unsigned slot=(rng>>8)%257;
        if(live[slot].count) {
            pmm_free_contiguous_pages((void *)(uintptr_t)(live[slot].page*4096),live[slot].count);
            for(uint64_t p=live[slot].page;p<live[slot].page+live[slot].count;++p) {CHECK(owned[p]);owned[p]=0;}
            live[slot].count=0;
        } else {
            uint64_t count=1+((rng>>20)%17);void *address=pmm_alloc_contiguous_pages(count);
            if(address) {
                uint64_t page=(uintptr_t)address/4096;
                CHECK(page && page+count<=257 && !((uintptr_t)address%4096));
                for(uint64_t p=page;p<page+count;++p){CHECK(!owned[p]);owned[p]=1;}
                live[slot].page=page;live[slot].count=count;
            } else {
                for(uint64_t page=1;page+count<=257;++page) {
                    uint64_t free=0;while(free<count&&!owned[page+free])++free;
                    CHECK(free!=count);
                }
            }
        }
        for(unsigned p=0;p<257;++p)CHECK(bitmap_test(p)==!!owned[p]);
    }
    for(unsigned i=0;i<257;++i)if(live[i].count)
        pmm_free_contiguous_pages((void *)(uintptr_t)(live[i].page*4096),live[i].count);
    CHECK(pmm_used_page_count()==1);
    puts("PMM: wrap, crossing cursor, exhaustion, fragmentation, 100000 ownership transitions PASS");return 0;
}
