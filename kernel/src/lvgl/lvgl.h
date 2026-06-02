#ifndef LVGL_H
#define LVGL_H

#include "lv_conf.h"

typedef uint32_t lv_tick_t;
typedef uint32_t lv_color_t;

typedef struct lv_area_t {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
} lv_area_t;

typedef void (*lv_flush_cb_t)(const lv_area_t *area, const lv_color_t *color_p, void *user);

void lv_init(void);
void lv_tick_inc(uint32_t ms);
lv_tick_t lv_tick_get(void);
void lv_timer_handler(void);
void lv_display_set_flush_cb(lv_flush_cb_t flush_cb, void *user);
void lv_display_set_resolution(uint32_t width, uint32_t height);
void lv_display_set_demo_enabled(uint8_t enabled);
void *lv_malloc(size_t size);
void lv_free(void *ptr);

#endif
