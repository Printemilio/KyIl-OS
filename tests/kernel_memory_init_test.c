#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/kernel_allocator.h>

/* Run against the actual non-SIMD kernel memset in the VM. The surrounding
   sentinel bytes catch both tail overrun and unaligned wide-store overrun. */
int kernel_memory_init_test(void) {
    uint8_t storage[8256];
    const size_t sizes[] = {0,1,2,7,8,9,15,16,31,63,64,65,4095,4096,4097,8192};
    const int values[] = {0,1,0x80,0xff,0x1234,-1};
    for (size_t offset=0; offset<16; ++offset)
        for (size_t k=0; k<sizeof(sizes)/sizeof(sizes[0]); ++k)
            for (size_t v=0; v<sizeof(values)/sizeof(values[0]); ++v) {
                for (size_t i=0; i<sizeof(storage); ++i) storage[i]=0x5a;
                uint8_t *start=storage+16+offset;
                if (memset(start,values[v],sizes[k])!=start) return -1;
                for (size_t i=0; i<sizeof(storage); ++i) {
                    uint8_t expected=i>=16+offset && i<16+offset+sizes[k] ? (uint8_t)values[v] : 0x5a;
                    if (storage[i]!=expected) return -2;
                }
            }
    for (unsigned iteration=0; iteration<16; ++iteration) {
        uint8_t *p=kernel_allocator_alloc(0,8193);
        if (!p) return -3;
        if ((uintptr_t)p%4096 || !kernel_allocator_can_project(p,8193)) {kernel_allocator_free(p); return -4;}
        for (size_t i=0;i<8193;++i) if (p[i]) {kernel_allocator_free(p);return -5;}
        memset(p,0xa5,8193);
        uint64_t physical;
        if (kernel_allocator_physical_page(p,8192,&physical)!=0 || !physical) {kernel_allocator_free(p);return -6;}
        kernel_allocator_free(p);
    }
    return 0;
}
