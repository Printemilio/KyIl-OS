/* Real RLSW framebuffer/cache. Incremental and complete rendering must match.
 * Also exercises clipping fallback, resize, transactional OOM and cleanup. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define RLSW_IMPLEMENTATION
#include "rlsw.h"
static int fail_alloc, allocations;
static void *cache_alloc(size_t bytes){if(fail_alloc)return NULL;void *p=malloc(bytes);if(p)allocations++;return p;}
static void cache_free(void *p){if(p){allocations--;free(p);}}
#define VEX_CANVAS_ALLOC cache_alloc
#define VEX_CANVAS_FREE cache_free
#include "userspace/raylib/rlsw_canvas_cache.inc"
static void rect(float x,float y,float w,float h,unsigned char r,unsigned char g,unsigned char b,unsigned char a){
    swBegin(SW_QUADS);swColor4ub(r,g,b,a);
    swVertex2f(x,y);swVertex2f(x+w,y);swVertex2f(x+w,y+h);swVertex2f(x,y+h);swEnd();
}
static void stamp(int n){rect(4+n%41,5+n%27,9,8,n*13,n*17,n*31,n%3?255:127);}
static void setup(int w,int h){
    swViewport(0,0,w,h);swMatrixMode(SW_PROJECTION);swLoadIdentity();swOrtho(0,w,h,0,-1,1);
    swMatrixMode(SW_MODELVIEW);swLoadIdentity();swDisable(SW_DEPTH_TEST);swDisable(SW_TEXTURE_2D);
    swEnable(SW_BLEND);swBlendFunc(SW_SRC_ALPHA,SW_ONE_MINUS_SRC_ALPHA);
    swDisable(SW_SCISSOR_TEST);swClearColor(.85f,.85f,.85f,1);
}
int main(void){
    assert(swInit(96,72));setup(96,72);
    unsigned char previous[96*72*4],reference[96*72*4],actual[96*72*4];
    void *cache=NULL;int rendered=0;
    for(int frame=0;frame<30;frame++){
        int count=(frame%10)*7;
        swClear(SW_COLOR_BUFFER_BIT);for(int i=0;i<count;i++)stamp(i);
        rect(0,0,12,24,17,23,29,255);swReadPixels(0,0,96,72,SW_RGBA,SW_UNSIGNED_BYTE,reference);
        swClear(SW_COLOR_BUFFER_BIT);
        if(frame%10==0 || !vex_raylib_canvas_restore(cache,0,0,96,72))rendered=0;
        for(int i=rendered;i<count;i++)stamp(i);
        cache=vex_raylib_canvas_capture(cache,0,0,96,72);assert(cache);rendered=count;
        rect(0,0,12,24,17,23,29,255);swReadPixels(0,0,96,72,SW_RGBA,SW_UNSIGNED_BYTE,actual);
        assert(memcmp(actual,reference,sizeof(actual))==0);
    }
    swReadPixels(0,0,96,72,SW_RGBA,SW_UNSIGNED_BYTE,previous);
    swEnable(SW_SCISSOR_TEST);swScissor(5,5,16,16);
    assert(!vex_raylib_canvas_restore(cache,0,0,96,72));
    assert(!vex_raylib_canvas_capture(cache,0,0,96,72));
    swReadPixels(0,0,96,72,SW_RGBA,SW_UNSIGNED_BYTE,actual);assert(!memcmp(actual,previous,sizeof(actual)));
    swDisable(SW_SCISSOR_TEST);
    fail_alloc=1;assert(!vex_raylib_canvas_capture(cache,0,0,48,36));
    assert(vex_raylib_canvas_restore(cache,0,0,96,72));assert(allocations==1);fail_alloc=0;
    void *small=vex_raylib_canvas_capture(cache,7,9,48,36);assert(small);cache=small;
    assert(!vex_raylib_canvas_restore(cache,0,0,96,72));
    assert(vex_raylib_canvas_restore(cache,7,9,48,36));assert(allocations==1);
    assert(!vex_raylib_canvas_capture(cache,-1,0,8,8));
    assert(!vex_raylib_canvas_capture(cache,95,0,2147483647,8));
    swEnable(SW_DEPTH_TEST);assert(!vex_raylib_canvas_restore(cache,7,9,48,36));swDisable(SW_DEPTH_TEST);
    vex_raylib_canvas_drop(cache);assert(allocations==0);
    for(int i=0;i<100;i++){cache=vex_raylib_canvas_capture(NULL,0,0,32,24);assert(cache);vex_raylib_canvas_drop(cache);}
    assert(allocations==0);swClose();puts("PASS: retained canvas equivalence, overlay, clipping, resize, OOM, cleanup");
}
