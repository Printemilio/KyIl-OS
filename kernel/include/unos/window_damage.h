#ifndef UNOS_WINDOW_DAMAGE_H
#define UNOS_WINDOW_DAMAGE_H
#include <unos/vex_abi.h>
/* Kernel compositor policy, not a userspace/driver ABI. Whole-window painters
 * require the overlap closure: repainting one window may touch another that
 * was outside the original damage. Final painting still follows z order. */
static inline int window_damage_intersects(vex_rect_t a,vex_rect_t b) {
    return a.w>0 && a.h>0 && b.w>0 && b.h>0 &&
        (int64_t)a.x<(int64_t)b.x+b.w && (int64_t)b.x<(int64_t)a.x+a.w &&
        (int64_t)a.y<(int64_t)b.y+b.h && (int64_t)b.y<(int64_t)a.y+a.h;
}
static inline uint32_t window_damage_closure(const vex_rect_t *windows,unsigned count,vex_rect_t damage) {
    uint32_t selected=0,previous;
    if(count>32)return 0;
    do {
        previous=selected;
        for(unsigned i=0;i<count;i++) {
            if(selected&(1u<<i))continue;
            int touched=window_damage_intersects(windows[i],damage);
            for(unsigned j=0;!touched && j<count;j++)
                if((selected&(1u<<j)) && window_damage_intersects(windows[i],windows[j]))touched=1;
            if(touched)selected|=1u<<i;
        }
    }while(previous!=selected);
    return selected;
}
#endif
