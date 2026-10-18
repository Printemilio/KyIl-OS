#ifndef KYIL_SURFACE_PIXELS_H
#define KYIL_SURFACE_PIXELS_H
#include <stdint.h>
#include <stddef.h>
/* Native XRGB/ARGB uint32_t rows -> Raylib packed, straight-alpha RGBA8.
 * stride_pixels is the Vex contract. No authority is inferred from bytes. */
static inline int kyil_surface_rgba(unsigned char *destination,size_t capacity,
    const unsigned char *source,size_t source_bytes,uint32_t width,uint32_t height,
    uint32_t stride_pixels,uint32_t format,uint32_t alpha_mode){
    if(!destination || !source || !width || !height || stride_pixels<width ||
       (format!=1 && format!=2) || alpha_mode>2 ||
       height>SIZE_MAX/((size_t)stride_pixels*4u) ||
       source_bytes<(size_t)stride_pixels*height*4u || capacity<(size_t)width*height*4u)return -1;
    for(uint32_t y=0;y<height;y++)for(uint32_t x=0;x<width;x++){
        const unsigned char *s=source+((size_t)y*stride_pixels+x)*4u;
        unsigned char *d=destination+((size_t)y*width+x)*4u;
        d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=(format==1 || alpha_mode==0)?255:s[3];
        if(format==2 && alpha_mode==1){
            if(!d[3])d[0]=d[1]=d[2]=0;
            else if(d[3]<255)for(unsigned c=0;c<3;c++){
                unsigned v=(d[c]*255u+d[3]/2u)/d[3];d[c]=v>255?255:v;
            }
        }
    }return 0;
}
#endif
