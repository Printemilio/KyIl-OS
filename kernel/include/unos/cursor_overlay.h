#ifndef UNOS_CURSOR_OVERLAY_H
#define UNOS_CURSOR_OVERLAY_H
#include <stdint.h>

/* Private synchronous scanout overlay. The scene remains cursor-free after
 * end(); the display receives the cursor as part of the same damage copy,
 * never a background-only frame followed by a second cursor write. */
static inline void cursor_overlay_begin(uint32_t *scene, uint64_t width,
        uint64_t height, uint64_t x, uint64_t y, const uint16_t shape[16],
        uint32_t foreground, uint32_t shadow, uint32_t backing[17*17]) {
    for (uint64_t row=0; row<17; ++row) for (uint64_t col=0; col<17; ++col) {
        uint32_t *pixel = x+col<width && y+row<height ? scene+(y+row)*width+x+col : 0;
        backing[row*17+col]=pixel?*pixel:0;
        if (!pixel) continue;
        if (row<16 && col<16 && (shape[row]&(0x8000u>>col))) *pixel=foreground;
        else if (row && col && (shape[row-1]&(0x8000u>>(col-1)))) *pixel=shadow;
    }
}
static inline void cursor_overlay_end(uint32_t *scene, uint64_t width,
        uint64_t height, uint64_t x, uint64_t y, const uint32_t backing[17*17]) {
    for (uint64_t row=0; row<17 && y+row<height; ++row)
        for (uint64_t col=0; col<17 && x+col<width; ++col)
            scene[(y+row)*width+x+col]=backing[row*17+col];
}
#endif
