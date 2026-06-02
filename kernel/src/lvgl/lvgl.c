#include "lvgl.h"

#include <unos/heap.h>

static lv_tick_t lv_kernel_tick;
static lv_flush_cb_t lv_flush_cb;
static void *lv_flush_user;
static uint32_t lv_display_width;
static uint32_t lv_display_height;
static uint8_t lv_demo_enabled;
static lv_tick_t lv_last_demo_tick;
static lv_color_t lv_demo_buffer[220u * 28u];

static void fill_rect_local(lv_color_t *buffer, uint32_t stride, uint32_t x, uint32_t y, uint32_t w, uint32_t h, lv_color_t color) {
    for (uint32_t py = 0; py < h; ++py) {
        for (uint32_t px = 0; px < w; ++px) {
            buffer[(y + py) * stride + (x + px)] = color;
        }
    }
}

void lv_init(void) {
    lv_kernel_tick = 0;
    lv_flush_cb = 0;
    lv_flush_user = 0;
    lv_display_width = 0u;
    lv_display_height = 0u;
    lv_demo_enabled = 0u;
    lv_last_demo_tick = 0u;
}

void lv_tick_inc(uint32_t ms) {
    lv_kernel_tick += ms;
}

lv_tick_t lv_tick_get(void) {
    return lv_kernel_tick;
}

void lv_timer_handler(void) {
    lv_area_t area;
    uint32_t panel_width;
    uint32_t panel_height;
    uint32_t panel_x;
    uint32_t panel_y;
    lv_color_t accent;

    if (lv_flush_cb == 0 || !lv_demo_enabled || lv_display_width == 0u || lv_display_height == 0u) {
        return;
    }
    if (lv_kernel_tick - lv_last_demo_tick < 100u) {
        return;
    }
    lv_last_demo_tick = lv_kernel_tick;

    panel_width = 220u;
    panel_height = 28u;
    if (panel_width > lv_display_width) {
        panel_width = lv_display_width;
    }
    /* Place BELOW the 42-pixel taskbar so we don't overwrite it */
    if (panel_height + 50u > lv_display_height) {
        return;
    }
    panel_x = lv_display_width > panel_width + 20u ? lv_display_width - panel_width - 20u : 0u;
    panel_y = 50u;  /* was 10u — that stomped on the taskbar */
    /* Neon purple pulse accent instead of blue/orange */
    accent = ((lv_kernel_tick / 300u) & 1u) != 0u ? 0xff9050ffu : 0xff6030c8u;

    fill_rect_local(lv_demo_buffer, 220u, 0u, 0u, panel_width, panel_height, 0xff12102au);
    fill_rect_local(lv_demo_buffer, 220u, 8u, 7u, 24u, 14u, accent);
    fill_rect_local(lv_demo_buffer, 220u, 44u, 9u, panel_width > 72u ? panel_width - 60u : 24u, 4u, 0xff1e1640u);
    fill_rect_local(lv_demo_buffer, 220u, 44u, 16u, panel_width > 96u ? panel_width - 92u : 16u, 3u, 0xff6840b0u);

    area.x1 = (int32_t)panel_x;
    area.y1 = (int32_t)panel_y;
    area.x2 = (int32_t)(panel_x + panel_width - 1u);
    area.y2 = (int32_t)(panel_y + panel_height - 1u);
    lv_flush_cb(&area, lv_demo_buffer, lv_flush_user);
}

void lv_display_set_flush_cb(lv_flush_cb_t flush_cb, void *user) {
    lv_flush_cb = flush_cb;
    lv_flush_user = user;
}

void lv_display_set_resolution(uint32_t width, uint32_t height) {
    lv_display_width = width;
    lv_display_height = height;
}

void lv_display_set_demo_enabled(uint8_t enabled) {
    lv_demo_enabled = enabled;
}

void *lv_malloc(size_t size) {
    return kmalloc(size);
}

void lv_free(void *ptr) {
    kfree(ptr);
}
