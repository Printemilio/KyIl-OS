#ifndef VEX_LVGL_H
#define VEX_LVGL_H

#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

#include "vex_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VEX_LVGL_MAX_WIDTH 1920u
#define VEX_LVGL_DRAW_ROWS 32u

typedef enum vex_ui_size_class {
    VEX_UI_SIZE_COMPACT = 0,
    VEX_UI_SIZE_MEDIUM = 1,
    VEX_UI_SIZE_EXPANDED = 2
} vex_ui_size_class_t;

typedef struct vex_ui_layout {
    uint32_t width;
    uint32_t height;
    vex_ui_size_class_t size_class;
    int32_t page_padding;
    int32_t gap;
    int32_t panel_padding;
    int32_t header_height;
    int32_t control_height;
    uint8_t short_height;
    uint8_t show_header;
    uint8_t show_secondary;
} vex_ui_layout_t;

typedef struct vex_lvgl_app vex_lvgl_app_t;
typedef void (*vex_lvgl_layout_cb_t)(vex_lvgl_app_t *app,
                                     const vex_ui_layout_t *layout,
                                     void *user_data);

struct vex_lvgl_app {
    vex_app_window_t window;
    lv_display_t *display;
    lv_indev_t *pointer;
    lv_indev_t *keyboard;
    lv_group_t *keyboard_group;
    uint32_t *pixels;
    size_t pixel_capacity;
    uint32_t width;
    uint32_t height;
    int32_t pointer_x;
    int32_t pointer_y;
    lv_indev_state_t pointer_state;
    uint32_t key;
    lv_indev_state_t key_state;
    vex_rect_t pending_dirty;
    uint8_t pending_dirty_valid;
    vex_ui_layout_t layout;
    vex_lvgl_layout_cb_t layout_cb;
    void *layout_user_data;
};

int vex_lvgl_app_open(vex_lvgl_app_t *app, const char *title,
                      uint32_t width, uint32_t height, uint32_t flags);
void vex_lvgl_app_close(vex_lvgl_app_t *app);
int vex_lvgl_app_handle_event(vex_lvgl_app_t *app, const vex_app_window_event_t *event);
void vex_lvgl_app_render(vex_lvgl_app_t *app);
lv_obj_t *vex_lvgl_screen(vex_lvgl_app_t *app);
void vex_lvgl_app_set_layout_handler(vex_lvgl_app_t *app,
                                     vex_lvgl_layout_cb_t callback,
                                     void *user_data);
void vex_lvgl_app_apply_layout(vex_lvgl_app_t *app);

/* Shared VexOS application primitives and design tokens. */
#define VEX_UI_BG 0x111216u
#define VEX_UI_SURFACE 0x1c1d23u
#define VEX_UI_SURFACE_ALT 0x27272fu
#define VEX_UI_DIVIDER 0x483156u
#define VEX_UI_TEXT 0xf4f4f6u
#define VEX_UI_MUTED 0x9c9ca6u
#define VEX_UI_ACCENT 0xb64cffu

void vex_ui_prepare_screen(lv_obj_t *screen);
void vex_ui_apply_page(lv_obj_t *screen, const vex_ui_layout_t *layout);
void vex_ui_apply_panel(lv_obj_t *panel, const vex_ui_layout_t *layout);
void vex_ui_set_visible(lv_obj_t *obj, uint8_t visible);
lv_obj_t *vex_ui_panel(lv_obj_t *parent);
lv_obj_t *vex_ui_label(lv_obj_t *parent, const char *text,
                       const lv_font_t *font, uint32_t color);
lv_obj_t *vex_ui_button(lv_obj_t *parent, const char *text);

#ifdef __cplusplus
}
#endif

#endif
