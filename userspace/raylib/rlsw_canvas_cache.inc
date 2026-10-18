/* Retained 2D Canvas pixels for the software Raylib backend. No Paint policy.
 * Included after the actual RLSW implementation, also by the host regression. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "vex_canvas_cache.h"
#ifndef VEX_CANVAS_ALLOC
#define VEX_CANVAS_ALLOC malloc
#define VEX_CANVAS_FREE free
#endif
typedef struct { int width, height; unsigned char pixels[]; } vex_canvas_cache_t;

static int vex_canvas_cache_rectangle(int x, int y, int w, int h)
{
    const sw_texture_t *fb=RLSW.colorBuffer;
    if (!fb || !fb->pixels || fb->format!=SW_FRAMEBUFFER_COLOR_FORMAT ||
        sw_immediate_is_active() || (RLSW.userState & SW_STATE_DEPTH_TEST)) return 0;
    if (x<0 || y<0 || w<=0 || h<=0 || x>fb->width || y>fb->height ||
        w>fb->width-x || h>fb->height-y) return 0;
    if ((RLSW.userState & SW_STATE_SCISSOR_TEST) &&
        (x<RLSW.scMin[0] || y<RLSW.scMin[1] || x+w>RLSW.scMax[0] || y+h>RLSW.scMax[1])) return 0;
    return (size_t)w <= (SIZE_MAX-sizeof(vex_canvas_cache_t))/(size_t)h/SW_FRAMEBUFFER_COLOR_SIZE;
}

void *vex_raylib_canvas_capture(void *old_cache, int x, int y, int w, int h)
{
    if (!vex_canvas_cache_rectangle(x,y,w,h)) return NULL;
    vex_canvas_cache_t *old=old_cache, *next=old;
    size_t row=(size_t)w*SW_FRAMEBUFFER_COLOR_SIZE;
    if (!old || old->width!=w || old->height!=h) {
        next=VEX_CANVAS_ALLOC(sizeof(*next)+row*(size_t)h);
        if (!next) return NULL;
        next->width=w;next->height=h;
    }
    const unsigned char *src=RLSW.colorBuffer->pixels;
    size_t pitch=(size_t)RLSW.colorBuffer->width*SW_FRAMEBUFFER_COLOR_SIZE;
    for(int j=0;j<h;j++)memcpy(next->pixels+(size_t)j*row,
        src+((size_t)y+j)*pitch+(size_t)x*SW_FRAMEBUFFER_COLOR_SIZE,row);
    if (next!=old) VEX_CANVAS_FREE(old);
    return next;
}

int vex_raylib_canvas_restore(void *cache, int x, int y, int w, int h)
{
    const vex_canvas_cache_t *saved=cache;
    if (!saved || !vex_canvas_cache_rectangle(x,y,w,h) || saved->width!=w || saved->height!=h) return 0;
    size_t row=(size_t)w*SW_FRAMEBUFFER_COLOR_SIZE;
    size_t pitch=(size_t)RLSW.colorBuffer->width*SW_FRAMEBUFFER_COLOR_SIZE;
    unsigned char *dst=RLSW.colorBuffer->pixels;
    for(int j=0;j<h;j++)memcpy(dst+((size_t)y+j)*pitch+(size_t)x*SW_FRAMEBUFFER_COLOR_SIZE,
        saved->pixels+(size_t)j*row,row);
    return 1;
}
void vex_raylib_canvas_drop(void *cache) { VEX_CANVAS_FREE(cache); }
