/* Actual RLSW source, compiled with/without specializations. Differential
 * captures include texture mutation, filters/wrap, alpha/additive blending,
 * clipping, gradients and perspective fallback. No Paint-specific primitive. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define RLSW_IMPLEMENTATION
#include "rlsw.h"
static void vertex(float x,float y,float z,float u,float v){swTexCoord2f(u,v);swVertex3f(x,y,z);}
static void draw(unsigned scenario) {
    unsigned char texels[8*8*4];
    memset(texels,255,sizeof(texels));
    if(scenario%3==1)for(unsigned i=0;i<64;i++){texels[i*4]=(i%8)*32;texels[i*4+1]=(i/8)*32;}
    if(scenario%3==2){texels[(3*8+3)*4+3]=0;texels[(4*8+4)*4+3]=128;}
    uint32_t texture;swGenTextures(1,&texture);swBindTexture(texture);
    swTexImage2D(8,8,SW_RGBA,SW_UNSIGNED_BYTE,texels);
    swTexParameteri(SW_TEXTURE_MIN_FILTER,scenario&1?SW_LINEAR:SW_NEAREST);
    swTexParameteri(SW_TEXTURE_MAG_FILTER,scenario&1?SW_LINEAR:SW_NEAREST);
    swTexParameteri(SW_TEXTURE_WRAP_S,scenario&2?SW_REPEAT:SW_CLAMP);
    swTexParameteri(SW_TEXTURE_WRAP_T,scenario&2?SW_REPEAT:SW_CLAMP);
    swEnable(SW_TEXTURE_2D);swEnable(SW_BLEND);
    swBlendFunc(SW_SRC_ALPHA,scenario&4?SW_ONE:SW_ONE_MINUS_SRC_ALPHA);
    swClearColor(.2f,.3f,.4f,1);swClear(SW_COLOR_BUFFER_BIT|SW_DEPTH_BUFFER_BIT);
    if(scenario&8)swScissor(13,9,50,46);else swScissor(0,0,96,72);
    swEnable(SW_SCISSOR_TEST);
    swMatrixMode(SW_PROJECTION);swLoadIdentity();swOrtho(0,96,72,0,-1,1);
    swMatrixMode(SW_MODELVIEW);swLoadIdentity();
    for(unsigned pass=0;pass<3;pass++) {
        if(pass==1){texels[(3*8+3)*4]=0;swTexSubImage2D(0,0,8,8,SW_RGBA,SW_UNSIGNED_BYTE,texels);}
        swBegin(pass==2?SW_QUADS:SW_TRIANGLES);
        swColor4ub(56,32,255,scenario&16?128:255);
        float lo=2.25f/8,hi=4.75f/8;
        if(scenario&32){lo=-.125f;hi=1.125f;}
        vertex(-3.25f,5.75f,0,lo,lo);
        if(scenario&64)swColor4ub(255,32,56,255);
        vertex(89.75f,13.25f,0,hi,lo);
        vertex(55.25f,68.75f,0,hi,hi);
        if(pass==2)vertex(5.25f,55.75f,0,lo,hi);
        swEnd();
    }
    swDisable(SW_TEXTURE_2D);swDisable(SW_BLEND);
    // Constant row color and vertical gradient exercise span/quad fills.
    swBegin(SW_QUADS);swColor4ub(217,217,217,255);
    vertex(17.25f,10.5f,0,0,0);vertex(36.75f,10.5f,0,0,0);
    if(scenario&64)swColor4ub(0,0,0,255);
    vertex(36.75f,46.5f,0,0,0);vertex(17.25f,46.5f,0,0,0);swEnd();
    swDeleteTextures(1,&texture);
}
static int count_color(const unsigned char *p,unsigned r,unsigned g,unsigned b) {
    int count=0;for(unsigned i=0;i<96*72;i++)if(p[i*4]==r && p[i*4+1]==g && p[i*4+2]==b)count++;
    return count;
}
static int semantic_checks(void) {
    unsigned char pixels[96*72*4];
    swDisable(SW_SCISSOR_TEST);swDisable(SW_TEXTURE_2D);swEnable(SW_BLEND);
    swBlendFunc(SW_SRC_ALPHA,SW_ONE);
    swClearColor(32/255.f,48/255.f,64/255.f,1);swClear(SW_COLOR_BUFFER_BIT);
    swBegin(SW_QUADS);swColor4ub(64,16,32,255);
    vertex(4,4,0,0,0);vertex(14,4,0,0,0);vertex(14,14,0,0,0);vertex(4,14,0,0,0);swEnd();
    swReadPixels(0,0,96,72,SW_RGBA,SW_UNSIGNED_BYTE,pixels);
    if(count_color(pixels,96,64,96)<50){
        fprintf(stderr,"additive mismatch: counts=%d sample=%u,%u,%u\n",count_color(pixels,96,64,96),pixels[(63*96+8)*4],pixels[(63*96+8)*4+1],pixels[(63*96+8)*4+2]);return 3;}
    unsigned char data[16]={255,0,0,255,0,255,0,255,255,255,0,255,0,0,255,255};
    uint32_t tex;swGenTextures(1,&tex);swBindTexture(tex);swTexImage2D(2,2,SW_RGBA,SW_UNSIGNED_BYTE,data);
    swTexParameteri(SW_TEXTURE_WRAP_S,SW_CLAMP);swTexParameteri(SW_TEXTURE_WRAP_T,SW_CLAMP);
    swTexParameteri(SW_TEXTURE_MAG_FILTER,SW_NEAREST);swEnable(SW_TEXTURE_2D);swDisable(SW_BLEND);
    swTexParameteri(SW_TEXTURE_MIN_FILTER,SW_NEAREST);
    swClearColor(0,0,0,1);swClear(SW_COLOR_BUFFER_BIT);
    swBegin(SW_QUADS);swColor4ub(255,255,255,255);
    vertex(4,4,0,1,1);vertex(14,4,0,1,1);vertex(14,14,0,1,1);vertex(4,14,0,1,1);swEnd();
    swReadPixels(0,0,96,72,SW_RGBA,SW_UNSIGNED_BYTE,pixels);
    swDeleteTextures(1,&tex);
    /* This build config exports framebuffer bytes as BGRA. */
    if(count_color(pixels,255,0,0)<50){fprintf(stderr,"clamp mismatch\n");return 4;}
    // Updating an opaque texture with transparent data must retain blending.
    unsigned char opaque[16];memset(opaque,255,sizeof(opaque));
    swGenTextures(1,&tex);swBindTexture(tex);swTexImage2D(2,2,SW_RGBA,SW_UNSIGNED_BYTE,opaque);
    swTexParameteri(SW_TEXTURE_MIN_FILTER,SW_NEAREST);swTexParameteri(SW_TEXTURE_MAG_FILTER,SW_NEAREST);
    swTexParameteri(SW_TEXTURE_WRAP_S,SW_CLAMP);swTexParameteri(SW_TEXTURE_WRAP_T,SW_CLAMP);
    unsigned char transparent[4]={255,255,255,0};
    swTexSubImage2D(0,0,1,1,SW_RGBA,SW_UNSIGNED_BYTE,transparent);
    swEnable(SW_BLEND);swBlendFunc(SW_SRC_ALPHA,SW_ONE_MINUS_SRC_ALPHA);
    swClearColor(32/255.f,48/255.f,64/255.f,1);swClear(SW_COLOR_BUFFER_BIT);
    swBegin(SW_QUADS);swColor4ub(255,255,255,255);
    vertex(4,4,0,.25f,.25f);vertex(14,4,0,.25f,.25f);vertex(14,14,0,.25f,.25f);vertex(4,14,0,.25f,.25f);swEnd();
    swReadPixels(0,0,96,72,SW_RGBA,SW_UNSIGNED_BYTE,pixels);
    swDeleteTextures(1,&tex);
    if(count_color(pixels,64,48,32)!=96*72){fprintf(stderr,"subimage alpha mismatch\n");return 5;}
    return 0;
}
int main(int argc,char **argv) {
    if(argc!=2 || !swInit(96,72))return 1;
    FILE *out=fopen(argv[1],"wb");if(!out)return 2;
    unsigned char pixels[96*72*4];
    for(unsigned i=0;i<128;i++){draw(i);swReadPixels(0,0,96,72,SW_RGBA,SW_UNSIGNED_BYTE,pixels);fwrite(pixels,1,sizeof(pixels),out);}
    // Varying clip W must keep the perspective textured fallback.
    for(unsigned i=0;i<8;i++){
        draw(i);swMatrixMode(SW_PROJECTION);swLoadIdentity();swFrustum(-1,1,-1,1,1,10);
        swMatrixMode(SW_MODELVIEW);swLoadIdentity();swEnable(SW_TEXTURE_2D);
        uint8_t white[4]={255,255,255,255};uint32_t tex;swGenTextures(1,&tex);swBindTexture(tex);
        swTexImage2D(1,1,SW_RGBA,SW_UNSIGNED_BYTE,white);
        swTexParameteri(SW_TEXTURE_MIN_FILTER,SW_NEAREST);swTexParameteri(SW_TEXTURE_MAG_FILTER,SW_NEAREST);
        swTexParameteri(SW_TEXTURE_WRAP_S,SW_REPEAT);swTexParameteri(SW_TEXTURE_WRAP_T,SW_REPEAT);
        swBegin(SW_TRIANGLES);swColor4ub(56,32,255,255);
        vertex(-1,-1,-2,.25f,.25f);vertex(1,-1,-3,.75f,.25f);vertex(0,1,-4,.5f,.75f);swEnd();
        swDeleteTextures(1,&tex);swReadPixels(0,0,96,72,SW_RGBA,SW_UNSIGNED_BYTE,pixels);fwrite(pixels,1,sizeof(pixels),out);
    }
    swMatrixMode(SW_PROJECTION);swLoadIdentity();swOrtho(0,96,72,0,-1,1);
    int result=semantic_checks();fclose(out);swClose();return result;
}
