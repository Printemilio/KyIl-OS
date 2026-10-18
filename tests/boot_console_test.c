#include <unos/boot_console.h>
#include <unos/boot_requests.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
volatile struct limine_framebuffer_request framebuffer_request;
int main(void) {
    unsigned width=96,height=80,pitch=448;
    unsigned char *storage=malloc(pitch*height+32);memset(storage,0xab,pitch*height+32);
    struct limine_framebuffer fb={.address=storage,.width=width,.height=height,.pitch=pitch,.bpp=32,
        .red_mask_size=8,.red_mask_shift=16,.green_mask_size=8,.green_mask_shift=8,.blue_mask_size=8};
    struct limine_framebuffer *frames[]={&fb};
    struct limine_framebuffer_response response={.framebuffer_count=1,.framebuffers=frames};
    framebuffer_request.response=&response;boot_console_init();assert(boot_console_active());
    for(unsigned i=0;i<20;++i)boot_console_stage("PMM/VMM START/END",i&1);
    boot_console_hex(UINT64_MAX);
    for(unsigned y=0;y<height;++y)for(unsigned x=width*4;x<pitch;++x)assert(storage[y*pitch+x]==0xab);
    for(unsigned i=0;i<32;++i)assert(storage[pitch*height+i]==0xab);
    boot_console_finish();assert(!boot_console_active());
    free(storage);puts("boot-console: PASS no services/allocator dependency, pitch/scroll/clipping/guard bytes");
}
