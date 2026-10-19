#pragma once

#include <stdint.h>

#define VEX_LVGL_UI_MAX_ITEMS 8u

typedef enum vex_lvgl_ui_item_kind {
    VEX_LVGL_UI_ITEM_APP = 0,
    VEX_LVGL_UI_ITEM_FILE = 1,
    VEX_LVGL_UI_ITEM_COMMAND = 2
} vex_lvgl_ui_item_kind_t;

typedef struct vex_lvgl_ui_item {
    const char *title;
    const char *subtitle;
    const char *meta;
    uint8_t kind;
    uint8_t selected;
} vex_lvgl_ui_item_t;

/*
 * Kernel-side LVGL shell.
 *
 * Applications keep their own userspace surfaces.  This layer owns only the
 * desktop shell and window chrome, so moving the UI to LVGL does not move app
 * code back into the kernel.
 */
int vex_lvgl_ui_init(uint32_t screen_width, uint32_t screen_height);
int vex_lvgl_ui_ready(void);
void vex_lvgl_ui_begin_scene(void);
void vex_lvgl_ui_draw_taskbar(const char *date_text, int hovered_item);
/* The focused application's name (left) and the time (centre); applied by
 * the next vex_lvgl_ui_draw_taskbar. */
void vex_lvgl_ui_set_taskbar_info(const char *application, const char *clock);
/* The window drawn with the focused frame (gradient ring, coloured lights). */
void vex_lvgl_ui_set_active_window(uint32_t window_id);
/* Row of the context menu under the pointer, or -1. */
void vex_lvgl_ui_set_context_hover(int item);
void vex_lvgl_ui_draw_window_frame(uint32_t x, uint32_t y, uint32_t width,
                                   uint32_t height, const char *title);
/* Native window identity, independent of the number of partial redraws. */
void vex_lvgl_ui_draw_window_frame_id(uint32_t window_id,uint32_t x,uint32_t y,
                                    uint32_t width,uint32_t height,const char *title);
void vex_lvgl_ui_draw_terminal_content(const char *text);

void vex_lvgl_ui_draw_context_menu(uint32_t x, uint32_t y,
                                   const char *const *labels,
                                   const char *const *hints, uint32_t count);
void vex_lvgl_ui_hide_context_menu(void);
void vex_lvgl_ui_draw_app_switcher(const char *const *labels, uint32_t count,
                                   uint32_t selected, int32_t offset_y);
void vex_lvgl_ui_draw_elevation(uint32_t x, uint32_t y, uint32_t width,
                                uint32_t height, const char *process_name);
void vex_lvgl_ui_draw_spotlight(uint32_t x, uint32_t y, uint32_t width,
                                const char *query,
                                const vex_lvgl_ui_item_t *items,
                                uint32_t count, uint8_t confirmation,
                                uint8_t reveal_percent);
void vex_lvgl_ui_hide_spotlight(void);

#include <unos/browser_state.h>
void vex_lvgl_ui_draw_browser_shell(uint32_t x, uint32_t y, uint32_t width,
                                    const char *address, uint8_t focused,
                                      const vex_browser_state_t *tabs,uint32_t tab_count,
                                      uint32_t selected,uint8_t native_home,uint32_t caret,uint8_t select_all);
void vex_lvgl_ui_draw_browser_home(uint32_t x, uint32_t y, uint32_t width,
                                     uint32_t height,const char *address,uint8_t focused,uint32_t caret,uint8_t select_all);
void vex_lvgl_ui_draw_browser_loading(uint32_t x, uint32_t y, uint32_t width,
                                      uint32_t height, const char *status, uint8_t failed);
void vex_lvgl_ui_draw_browser_results(uint32_t x, uint32_t y, uint32_t width,
                                      uint32_t height, const char *query,
                                      const vex_lvgl_ui_item_t *items,
                                      uint32_t count);

/* Internal scene hooks used by the split LVGL renderer modules. */
struct _lv_obj_t;
/* Paint only this widget subtree into the Vex scene, never its siblings.
 * The caller holds the GUI scene gate and owns the composition order. */
void vex_lvgl_ui_paint_object(struct _lv_obj_t *obj);
void vex_lvgl_overlays_begin_scene(void);
void vex_lvgl_browser_begin_scene(void);
