#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unos/cursor_overlay.h>

int main(void) {
    enum { W=48,H=40,N=W*H };
    uint32_t scene[N], original[N], screen[N], saved[17*17];
    const uint16_t shape[16]={0x8000,0xc000,0xe000,0xf000,0xf800,0xfc00,0xfe00,0xff00,
        0xf000,0xd800,0x8c00,0x0600,0x0600,0,0,0};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x) {
        for(unsigned i=0;i<N;++i)scene[i]=original[i]=0x101010+i;
        cursor_overlay_begin(scene,W,H,x,y,shape,0xe6ecf8,0x040408,saved);
        memcpy(screen,scene,sizeof(screen));
        cursor_overlay_end(scene,W,H,x,y,saved);
        assert(!memcmp(scene,original,sizeof(scene)));
        assert(screen[y*W+x]==0xe6ecf8);
        /* A new frame changes the underlying pixels. No individual scanout
         * store may replace an opaque cursor pixel with the background. */
        for(unsigned i=0;i<N;++i)scene[i]=original[i]=0x204040+i;
        cursor_overlay_begin(scene,W,H,x,y,shape,0xe6ecf8,0x040408,saved);
        for(unsigned i=0;i<N;++i) {
            if(scene[i]==0xe6ecf8 || scene[i]==0x040408)assert(screen[i]==scene[i]);
            screen[i]=scene[i];
        }
        cursor_overlay_end(scene,W,H,x,y,saved);
        assert(!memcmp(scene,original,sizeof(scene)));
    }
    puts("cursor overlay: clipped edges, fresh backing, scene restoration, no cursor erasure PASS");
}
