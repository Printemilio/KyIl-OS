#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../raylib/vex_surface_pixels.h"
int main(void){
    const unsigned char source[]={0,0,255,0, 255,0,0,0, 99,99,99,99,
                                 0,255,0,0, 3,2,1,0, 88,88,88,88};
    const unsigned char expected[]={255,0,0,255, 0,0,255,255, 0,255,0,255, 1,2,3,255};
    unsigned char pixels[20];memset(pixels,0xa5,sizeof(pixels));
    assert(!kyil_surface_rgba(pixels,16,source,sizeof(source),2,2,3,1,0));
    assert(!memcmp(pixels,expected,16));
    for(unsigned i=16;i<20;i++)assert(pixels[i]==0xa5);
    assert(kyil_surface_rgba(pixels,15,source,sizeof(source),2,2,3,1,0)==-1);
    assert(kyil_surface_rgba(pixels,16,source,23,2,2,3,1,0)==-1);
    assert(kyil_surface_rgba(pixels,16,source,sizeof(source),2,2,1,1,0)==-1);
    assert(kyil_surface_rgba(pixels,16,source,sizeof(source),2,2,3,3,0)==-1);
    assert(kyil_surface_rgba(pixels,16,source,sizeof(source),0,2,3,1,0)==-1);
    assert(kyil_surface_rgba(pixels,16,source,sizeof(source),UINT32_MAX,UINT32_MAX,UINT32_MAX,1,0)==-1);
    const unsigned char premultiplied[]={64,32,16,128, 80,70,60,0};
    const unsigned char straight[]={32,64,128,128, 0,0,0,0};
    assert(!kyil_surface_rgba(pixels,8,premultiplied,8,2,1,2,2,1));
    assert(!memcmp(pixels,straight,8));
    const unsigned char alpha[]={30,20,10,80};
    assert(!kyil_surface_rgba(pixels,4,alpha,4,1,1,1,2,2));
    assert(pixels[0]==10 && pixels[1]==20 && pixels[2]==30 && pixels[3]==80);
    puts("Suchu Surface pixels: stride/padding/format/alpha/bounds PASS");return 0;
}
