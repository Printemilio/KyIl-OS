/* Exercise the actual software driver, not a second compositor implementation.
 * Link with -ffunction-sections -fdata-sections -Wl,--gc-sections so unused
 * privileged driver entry points are not linked into this host test. */
#include "../kernel/src/gpu_driver_software.c"
/* Host libc; kernel/stdio.h intentionally has no printf. */
extern int printf(const char *, ...);

static uint32_t expected_blend(uint32_t under,uint32_t over,unsigned alpha) {
    uint32_t out=0;
    for(unsigned shift=0;shift<24;shift+=8)
        out|=(((((over>>shift)&255)*alpha+((under>>shift)&255)*(255-alpha)+127)/255)<<shift);
    return out;
}

int main(void) {
    uint32_t source[10*8],destination[20*16];
    gpu_driver_resource_t src={.cpu_mapping=source,.stride=10};
    gpu_driver_resource_t dst={.cpu_mapping=destination,.stride=20};
    const vex_rect_t clips[]={{0,0,0,0},{4,4,3,5},{-2,-3,7,9},{30,30,2,2},{15,14,10,10}};
    unsigned cases=0;
    for(unsigned scale=1;scale<=2;++scale)
    for(unsigned alpha=128;alpha<=255;alpha+=127)
    for(unsigned c=0;c<sizeof(clips)/sizeof(*clips);++c) {
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<10;++x)
            source[y*10+x]=0xff000000u|(x*23u<<16)|(y*29u<<8)|0x41u;
        for(unsigned i=0;i<320;++i)destination[i]=0x00365971u;
        gpu_driver_composite_req_t r={0};
        r.source=&src;r.destination=&dst;
        r.source_width=8;r.source_height=8;
        r.destination_width=16;r.destination_height=16;
        r.source_rect=(vex_rect_t){1,2,4,4};
        r.destination_rect=(vex_rect_t){2,3,4*(int)scale,4*(int)scale};
        r.clip=clips[c];r.opacity=alpha;
        r.alpha_mode=VEX_GPU_SURFACE_ALPHA_OPAQUE;
        r.filter=VEX_GPU_SURFACE_FILTER_NEAREST;
        r.transform=VEX_GPU_SURFACE_TRANSFORM_IDENTITY;
        if(software_buffer_composite(NULL,&r))return 1;
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<20;++x) {
            uint32_t expected=0x00365971u;
            int inside=x<16 && x>=2 && x<2+4*scale && y>=3 && y<3+4*scale;
            if(r.clip.w>0 && r.clip.h>0)
                inside=inside && (int)x>=r.clip.x && (int)x<r.clip.x+r.clip.w &&
                    (int)y>=r.clip.y && (int)y<r.clip.y+r.clip.h;
            if(inside)expected=expected_blend(expected,
                source[(2+(y-3)/scale)*10+1+(x-2)/scale],alpha);
            /* XRGB has no observable alpha byte. The optimized opaque copy
             * may preserve it while the blending path emits only RGB. */
            const int mismatch=inside?(destination[y*20+x]&0xffffffu)!=expected:
                destination[y*20+x]!=expected;
            if(mismatch) {
                printf("FAIL case=%u pixel=%u,%u expected=%x actual=%x\n",cases,x,y,expected,destination[y*20+x]);
                return 2;
            }
        }
        ++cases;
    }
    printf("PASS: %u compositor clipping cases (copy/scaling/opacity/pitch/offscreen)\n",cases);
    return 0;
}
