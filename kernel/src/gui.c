#include <unos/gui.h>
#include <unos/process_memory.h>
static kernel_gate_t gui_scene_gate=KERNEL_GATE_INIT;
gui_state_guard_t gui_state_begin(void) {
    gui_state_guard_t g;
    g.memory=process_memory_begin_update();g.scene=kernel_guard_enter_irq_safe(&gui_scene_gate);return g;
}
void gui_state_end(gui_state_guard_t *g) {
    kernel_guard_leave(&g->scene);process_memory_end_update(&g->memory);
}

#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/boot_requests.h>
#include <unos/app_surface.h>
#include <unos/compositor_surface.h>
#include <unos/debug_log.h>
#include <unos/cursor_overlay.h>
#if VEX_DESKTOP_TRACE
#include <stdio.h>
#endif
#include <unos/elevation.h>
#include <unos/elf.h>
#include <unos/file_manager_ui.h>
#include <unos/profiler.h>
#include <stdio.h>
#include <unos/sched_clock.h>
#include <unos/gpu.h>
#include <unos/heap.h>
#include <unos/irq.h>
#include <unos/keyboard.h>
#include <unos/vex_legacy_compat.h>
#include <unos/mouse.h>
#include <unos/browser_viewport.h>
#include <unos/browser_state.h>
#include <unos/scene_image.h>
#include <unos/rtc.h>
#include <unos/scheduler.h>
#include <unos/shell.h>
#include <unos/spotlight_ui.h>
#include <unos/text_editor_ui.h>
#include <unos/timezone.h>
#include <unos/tty.h>
#include <unos/vex_lvgl_ui.h>
#include <src/core/lv_refr_private.h>
#include <src/core/lv_obj_draw_private.h>
#include <src/misc/lv_area_private.h>
#include <src/display/lv_display_private.h>
#include <src/draw/lv_draw_private.h>
#include <unos/vex_logo.h>
#include "vex_icon_hex.h"
#include "vex_icon_wifi.h"
#include "vex_icon_volume.h"
#include "vex_icon_power.h"
#include "vex_center_logo.h"
#include <unos/fs.h>
#include <unos/power.h>
#include <unos/users.h>
#include <unos/vex_abi.h>
#include "vex_font_inter.h"

#include <lvgl.h>

/* Slim bar: glass at y=13, h=36 (bottom 49) + 5px gap */
#define GUI_TASKBAR_HEIGHT 54ull
#define GUI_BAR_Y 13ull
#define GUI_BAR_H 36ull
/* Vertical centre of the glass bar: 13 + 36/2 = 31 */
#define GUI_BAR_CY 31ull
#define GUI_WINDOW_MARGIN 12ull
#define GUI_TITLE_HEIGHT 45ull
#define GUI_CURSOR_SIZE 17ull
#define GUI_TARGET_FPS 30ull
#define GUI_FRAME_INTERVAL_TICKS 1ull
#define GUI_CLOCK_UPDATE_TICKS 100ull
#define GUI_WINDOW_CONTROL_RADIUS 7ull
#define GUI_CONTEXT_MENU_WIDTH 250ull
#define GUI_CONTEXT_MENU_HEIGHT 282ull
#define GUI_CONTEXT_MENU_ROW_HEIGHT 38ull
#define GUI_CONTEXT_MENU_PAD 8ull
#define GUI_DIRTY_RECT_MAX 96u
#define GUI_LVGL_DRAW_ROWS 64u
#define GUI_BROWSER_SHELL_HEIGHT 67ull
#define GUI_BROWSER_ADDRESS_MAX VEX_BROWSER_URI_SIZE
#define GUI_PAINT_TOOLBAR_HEIGHT 44ull
#define GUI_PAINT_SWATCH_SIZE 20ull
#define GUI_WINDOW_RESIZE_HANDLE 18ull
#define GUI_WINDOW_RESIZE_EDGE 8ull
#define GUI_WINDOW_RESIZE_LEFT 0x01u
#define GUI_WINDOW_RESIZE_RIGHT 0x02u
#define GUI_WINDOW_RESIZE_TOP 0x04u
#define GUI_WINDOW_RESIZE_BOTTOM 0x08u
#define GUI_WINDOW_MIN_W 320ull
#define GUI_WINDOW_MIN_H 180ull
#define GUI_APP_SWITCHER_MAX 5u
#define GUI_APP_SWITCHER_TIMEOUT_TICKS 120ull
#define GUI_APP_SWITCHER_ANIM_TICKS 6ull
#define GUI_OPEN_CONTEXT_SLOTS 8u
#define GUI_OPEN_CONTEXT_PATH_MAX 256u

typedef struct gui_rect {
    uint64_t x;
    uint64_t y;
    uint64_t w;
    uint64_t h;
} gui_rect_t;

typedef enum gui_window_control {
    GUI_WINDOW_CONTROL_NONE = 0,
    GUI_WINDOW_CONTROL_CLOSE,
    GUI_WINDOW_CONTROL_MINIMIZE,
    GUI_WINDOW_CONTROL_MAXIMIZE
} gui_window_control_t;

typedef enum gui_window_drag_target {
    GUI_WINDOW_DRAG_NONE = 0,
    GUI_WINDOW_DRAG_TERMINAL,
    GUI_WINDOW_DRAG_BROWSER,
    GUI_WINDOW_DRAG_APP_SURFACE
} gui_window_drag_target_t;

typedef enum gui_switch_target {
    GUI_SWITCH_TARGET_NONE = 0,
    GUI_SWITCH_TARGET_TERMINAL,
    GUI_SWITCH_TARGET_FILES,
    GUI_SWITCH_TARGET_EDITOR,
    GUI_SWITCH_TARGET_BROWSER,
    GUI_SWITCH_TARGET_PAINT
} gui_switch_target_t;

typedef struct gui_elevation_layout {
    uint64_t panel_x;
    uint64_t panel_y;
    uint64_t panel_w;
    uint64_t panel_h;
    uint64_t approve_x;
    uint64_t approve_y;
    uint64_t approve_w;
    uint64_t approve_h;
    uint64_t deny_x;
    uint64_t deny_y;
    uint64_t deny_w;
    uint64_t deny_h;
} gui_elevation_layout_t;

typedef struct gui_open_context_slot {
    uint32_t pid;
    uint32_t application;
    char path[GUI_OPEN_CONTEXT_PATH_MAX];
} gui_open_context_slot_t;

struct limine_framebuffer *gui_framebuffer;
uint32_t *compositor_scene;
static uint64_t compositor_scene_pixels;
static uint32_t *wallpaper_cache;
static uint64_t wallpaper_cache_width,wallpaper_cache_height;
static void draw_kyil_wordmark(uint64_t x,uint64_t y);
static uint32_t compositor_scene_handle;
static lv_display_t *gui_lvgl_display;
static lv_indev_t *gui_lvgl_pointer;
static uint32_t *gui_lvgl_draw_buffer_a;
static uint32_t *gui_lvgl_draw_buffer_b;
static gui_rect_t dirty_rects[GUI_DIRTY_RECT_MAX];
static uint64_t dirty_rect_count;
static uint64_t cursor_x;
static uint64_t cursor_y;
static uint32_t gui_text_color;
static uint32_t gui_cursor_shadow;
static uint32_t cursor_backing[GUI_CURSOR_SIZE * GUI_CURSOR_SIZE];
static uint64_t cursor_backing_x;
static uint64_t cursor_backing_y;
static uint8_t cursor_backing_valid;
static uint8_t cursor_needs_present;
static uint8_t cursor_buttons;
static uint8_t previous_cursor_buttons;
static uint8_t context_menu_visible;
/* Only the underlayers replaced by this transaction may be alpha-composed
 * again. Outside it, the retained scene already contains the final pixels. */
static const vex_rect_t *gui_paint_clip;
static uint64_t context_menu_x;
static uint64_t context_menu_y;
static uint64_t last_clock_ticks;
static uint8_t last_clock_second;
static volatile uint8_t gui_work_pending = 1u;
static volatile uint32_t gui_loop_tid;
uint8_t gui_compositor_ready;
uint64_t terminal_win_x;
uint64_t terminal_win_y;
uint64_t terminal_win_w;
uint64_t terminal_win_h;
uint8_t terminal_minimized;
uint8_t terminal_maximized;
uint64_t terminal_restore_x;
uint64_t terminal_restore_y;
uint64_t terminal_restore_w;
uint64_t terminal_restore_h;
uint32_t *terminal_snapshot;
uint64_t terminal_snapshot_pixels;
uint8_t terminal_snapshot_valid;
static uint8_t terminal_lvgl_content_dirty;
uint32_t terminal_pid_value;
uint32_t file_manager_pid_value;
uint32_t editor_pid_value;
uint32_t spotlight_pid_value;
uint32_t active_pid;
uint32_t browser_previous_pid;
uint8_t file_manager_opened;
uint8_t editor_opened;
uint8_t browser_previous_paint;
uint8_t editor_has_document;
uint8_t spotlight_visible;
uint8_t browser_active;
uint8_t browser_minimized;
uint8_t browser_native_home_visible;
uint64_t browser_win_x;
uint64_t browser_win_y;
uint64_t browser_win_w;
uint64_t browser_win_h;
static uint8_t browser_window_maximized;
typedef struct browser_presented_slot {
    uint32_t tab_id;
    /* Kept but not shown while a navigation is pending: if the navigation ends
     * without a page (a download), the last frame can be shown again. */
    uint8_t suspended;
    scene_image_t image;
} browser_presented_slot_t;
static browser_presented_slot_t browser_presented[VEX_BROWSER_MAX_TABS];
static uint64_t browser_restore_x;
static uint64_t browser_restore_y;
static uint64_t browser_restore_w;
static uint64_t browser_restore_h;
char browser_address[GUI_BROWSER_ADDRESS_MAX];
uint64_t browser_address_len;
uint8_t browser_address_focused;
uint8_t browser_window_focused;
uint8_t browser_tab_count;
uint8_t browser_results_drawn;
uint8_t browser_loading_visible;
char browser_loading_text[GUI_BROWSER_ADDRESS_MAX];
static uint32_t *blur_buffer;
static uint64_t blur_buffer_pixels;
static gui_open_context_slot_t open_context_slots[GUI_OPEN_CONTEXT_SLOTS];

static scene_image_t *browser_presented_find(uint32_t tab_id, int create) {
    if (!tab_id) return NULL;
    browser_presented_slot_t *free_slot = NULL;
    for (uint32_t i = 0; i < VEX_BROWSER_MAX_TABS; ++i) {
        if (browser_presented[i].tab_id == tab_id) return &browser_presented[i].image;
        if (!browser_presented[i].tab_id && !free_slot) free_slot = &browser_presented[i];
    }
    if (!create || !free_slot) return NULL;
    free_slot->tab_id = tab_id;
    return &free_slot->image;
}
static browser_presented_slot_t *browser_presented_slot_for_image(const scene_image_t *image) {
    for (uint32_t i = 0; i < VEX_BROWSER_MAX_TABS; ++i) {
        if (&browser_presented[i].image == image) return &browser_presented[i];
    }
    return NULL;
}
static int browser_presented_is_live(uint32_t tab_id) {
    const scene_image_t *image = browser_presented_find(tab_id, 0);
    const browser_presented_slot_t *slot = image ? browser_presented_slot_for_image(image) : NULL;
    return slot && !slot->suspended;
}
uint8_t paint_active;
uint8_t paint_minimized;
uint8_t paint_drawing;
uint8_t paint_eraser;
uint8_t paint_previous_browser;
uint32_t paint_previous_pid;
uint32_t *paint_canvas;
uint64_t paint_canvas_width;
uint64_t paint_canvas_height;
uint64_t paint_canvas_pixels;
uint32_t paint_color;
uint64_t paint_brush_radius;
uint64_t paint_last_x;
uint64_t paint_last_y;
static gui_window_drag_target_t dragged_window;
static uint8_t drag_resize_edges;
static uint32_t drag_app_surface_id;
static uint64_t drag_origin_cursor_x;
static uint64_t drag_origin_cursor_y;
static uint64_t drag_origin_win_x;
static uint64_t drag_origin_win_y;
static uint64_t drag_origin_win_w;
static uint64_t drag_origin_win_h;
static uint8_t drag_repaint_active;
static gui_rect_t drag_repaint_bounds[4];
static uint8_t drag_repaint_count;
static gui_window_drag_target_t resize_hover_target;
static uint32_t resize_hover_app_surface_id;
static uint8_t resize_hover_edges;
static uint8_t app_switcher_visible;
static uint8_t app_switcher_count;
static uint8_t app_switcher_selected;
static uint8_t app_switcher_anim_step;
static uint8_t elevation_prompt_visible_cached;
static uint32_t elevation_prompt_request_cached;
static uint64_t elevation_prompt_timer_bucket_cached;
static gui_switch_target_t app_switcher_targets[GUI_APP_SWITCHER_MAX];
static uint64_t app_switcher_deadline_tick;
static uint64_t app_switcher_anim_start_tick;

static int cursor_pixel(uint64_t x, uint64_t y, uint32_t *color);
static void draw_border(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);
void fill_circle(uint64_t cx, uint64_t cy, uint64_t r, uint32_t color);
void fill_rounded_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t r, uint32_t color);
static void draw_rounded_border(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t r, uint32_t color);
static void draw_wallpaper_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
static void blit_rgba_icon(uint64_t x, uint64_t y,
                           const uint8_t *data, uint64_t iw, uint64_t ih);
static void draw_taskbar_brand(void);
static void draw_main_window_frame(const char *title);
static void draw_window_frame_at(uint64_t x, uint64_t y, uint64_t w, uint64_t h, const char *title);
void draw_browser_shell(void);
void draw_browser_native_home(void);
void draw_browser_native_results(void);
void draw_browser_loading_state(void);
void draw_browser_text_clipped(uint64_t x, uint64_t y, const char *text, uint32_t color, uint64_t max_width);
void draw_paint_workspace(void);
void paint_content_rect(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h);
void paint_canvas_rect(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h);
void paint_clear_canvas(void);
int paint_ensure_canvas(uint64_t width, uint64_t height);
void paint_blit_canvas_region(uint64_t local_x, uint64_t local_y, uint64_t width, uint64_t height);
void paint_stamp(uint64_t x, uint64_t y);
void paint_stroke_line(uint64_t x0, uint64_t y0, uint64_t x1, uint64_t y1);
void browser_shell_set_address(const char *address);
void browser_submit_address_to_web(const char *address);
void browser_submit_content_click(uint64_t x, uint64_t y);
void browser_shell_handle_key(char key);
void gui_browser_poll_lifecycle(void);
int gui_browser_lifecycle_pending(void);
void gui_browser_launch(void);
int gui_browser_shortcut(const keyboard_event_t *event);
void gui_browser_cycle_tab(int backwards);
void save_terminal_snapshot(void);
void restore_terminal_snapshot(void);
static gui_window_control_t terminal_control_hit(uint64_t x, uint64_t y);
gui_window_control_t browser_control_hit(uint64_t x, uint64_t y);
gui_window_control_t paint_control_hit(uint64_t x, uint64_t y);
static void set_terminal_minimized(uint8_t minimized);
static void toggle_terminal_maximized(void);
static void toggle_browser_window_maximized(void);
void set_browser_minimized(uint8_t minimized);
void set_paint_minimized(uint8_t minimized);
int browser_handle_shell_click(uint64_t x, uint64_t y);
int paint_handle_click(uint64_t x, uint64_t y);
void paint_handle_hover(uint64_t x, uint64_t y);
void paint_continue_stroke(uint64_t x, uint64_t y);
void paint_stop_stroke(void);
void render_active_app(void);
void switch_to_pid(uint32_t pid);
void send_app_config(uint32_t pid, uint8_t active, uint8_t refresh);
static void send_key_to_active_app(char key);
static const char *window_title_for_pid(uint32_t pid);
static int context_menu_item_at(uint64_t x, uint64_t y);
static void draw_context_menu_icon(uint64_t x, uint64_t y, int item, uint32_t accent);
static void hide_context_menu(void);
void compositor_mark_dirty(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
static void compositor_present(void);
static void compositor_present_cursor(void);
void gui_flush_scene_and_cursor(void);
static void gui_lvgl_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixel_map);
static void gui_lvgl_pointer_read(lv_indev_t *indev, lv_indev_data_t *data);
static void put_pixel_physical(uint64_t x, uint64_t y, uint32_t color);
static uint32_t scene_pixel(uint64_t x, uint64_t y);
static int window_titlebar_hit_at(uint64_t px, uint64_t py, uint64_t x, uint64_t y, uint64_t w);
static uint8_t window_resize_edges_at(uint64_t px, uint64_t py, uint64_t x, uint64_t y, uint64_t w, uint64_t h);
static void clamp_window_bounds(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h);
static void begin_window_drag(gui_window_drag_target_t target, uint8_t resize_edges);
static void update_window_drag(void);
static void end_window_drag(void);

/* Browser-reserved chords are decided only after their non-modifier key is
 * seen. Hold modifier edges briefly so Ctrl+L etc. cannot leak a Control or
 * Shift keydown into the Web document before the native chrome consumes the
 * chord. Unhandled chords are replayed in order, preserving normal Web input. */
#define GUI_BROWSER_DEFERRED_MODIFIER_MAX 8u
#define GUI_BROWSER_KEY_CTRL_LEFT  0x11001du
#define GUI_BROWSER_KEY_CTRL_RIGHT 0x11009du
#define GUI_BROWSER_KEY_SHIFT_LEFT 0x11002au
#define GUI_BROWSER_KEY_SHIFT_RIGHT 0x110036u
#define GUI_BROWSER_KEY_ALT_LEFT   0x110038u
#define GUI_BROWSER_KEY_ALT_RIGHT  0x1100b8u
static keyboard_event_t browser_deferred_modifiers[GUI_BROWSER_DEFERRED_MODIFIER_MAX];
static uint8_t browser_deferred_modifier_count;
static uint8_t browser_deferred_modifier_mask;
static uint8_t browser_physical_modifier_mask;
static uint8_t browser_suppressed_modifier_releases;

static uint8_t browser_modifier_slot(uint32_t key) {
    switch (key) {
        case GUI_BROWSER_KEY_CTRL_LEFT: return 1u << 0;
        case GUI_BROWSER_KEY_CTRL_RIGHT: return 1u << 1;
        case GUI_BROWSER_KEY_SHIFT_LEFT: return 1u << 2;
        case GUI_BROWSER_KEY_SHIFT_RIGHT: return 1u << 3;
        case GUI_BROWSER_KEY_ALT_LEFT: return 1u << 4;
        case GUI_BROWSER_KEY_ALT_RIGHT: return 1u << 5;
        default: return 0u;
    }
}

static void browser_flush_deferred_modifiers(void) {
    for (uint8_t i = 0; i < browser_deferred_modifier_count; ++i) {
        const keyboard_event_t *event = &browser_deferred_modifiers[i];
        browser_viewport_key(event->key, event->hardware, event->pressed, event->modifiers);
    }
    browser_deferred_modifier_count = 0u;
    browser_deferred_modifier_mask = 0u;
}

static void browser_discard_deferred_modifiers(void) {
    browser_deferred_modifier_count = 0u;
    browser_deferred_modifier_mask = 0u;
}

/* Returns nonzero when this modifier event is buffered or consumed. */
static int browser_filter_modifier_event(const keyboard_event_t *event, int viewport_target) {
    const uint8_t slot = browser_modifier_slot(event->key);
    if (!slot) {
        if (browser_deferred_modifier_count) {
            if (viewport_target) browser_flush_deferred_modifiers();
            else browser_discard_deferred_modifiers();
        }
        return 0;
    }
    if (event->pressed) {
        if (!viewport_target) {
            browser_discard_deferred_modifiers();
            return 0;
        }
        if (!(browser_deferred_modifier_mask & slot)) {
            if (browser_deferred_modifier_count == GUI_BROWSER_DEFERRED_MODIFIER_MAX)
                browser_flush_deferred_modifiers();
            browser_deferred_modifiers[browser_deferred_modifier_count++] = *event;
            browser_deferred_modifier_mask |= slot;
        }
        return 1;
    }
    if (browser_suppressed_modifier_releases & slot) {
        browser_suppressed_modifier_releases &= (uint8_t)~slot;
        return 1;
    }
    if (viewport_target && (browser_deferred_modifier_mask & slot)) {
        if (browser_deferred_modifier_count < GUI_BROWSER_DEFERRED_MODIFIER_MAX)
            browser_deferred_modifiers[browser_deferred_modifier_count++] = *event;
        browser_deferred_modifier_mask &= (uint8_t)~slot;
        browser_flush_deferred_modifiers();
        return 1;
    }
    if (!viewport_target) browser_discard_deferred_modifiers();
    return 0;
}
static void draw_resize_hover(void);
static void draw_app_switcher_overlay(void);
static void compute_elevation_layout(gui_elevation_layout_t *layout);
static void draw_elevation_prompt_overlay(void);
static int elevation_button_hit(uint64_t x, uint64_t y);
static void app_switcher_collect(void);
static gui_switch_target_t current_switch_target(void);
static void app_switcher_apply(gui_switch_target_t target);
static void app_switcher_cycle(int reverse);
static void app_switcher_commit(void);
static void close_paint_view(uint8_t keep_open);

static const uint16_t cursor_shape[16] = {
    0x8000, 0xc000, 0xe000, 0xf000,
    0xf800, 0xfc00, 0xfe00, 0xff00,
    0xff80, 0xf800, 0xd800, 0x8c00,
    0x0c00, 0x0600, 0x0600, 0x0000,
};

static uint32_t scale_channel(uint8_t value, uint8_t mask_size) {
    if (mask_size == 0) {
        return 0;
    }

    return ((uint32_t)value * ((1u << mask_size) - 1u)) / 255u;
}

uint32_t pack_color(uint8_t r, uint8_t g, uint8_t b) {
    return (scale_channel(r, gui_framebuffer->red_mask_size) << gui_framebuffer->red_mask_shift)
         | (scale_channel(g, gui_framebuffer->green_mask_size) << gui_framebuffer->green_mask_shift)
         | (scale_channel(b, gui_framebuffer->blue_mask_size) << gui_framebuffer->blue_mask_shift);
}

static uint8_t unpack_channel(uint32_t color, uint8_t shift, uint8_t mask_size) {
    if (mask_size == 0) {
        return 0;
    }

    const uint32_t max_value = (1u << mask_size) - 1u;
    const uint32_t value = (color >> shift) & max_value;
    return (uint8_t)((value * 255u) / max_value);
}

static uint8_t color_r(uint32_t color) {
    return unpack_channel(color, gui_framebuffer->red_mask_shift, gui_framebuffer->red_mask_size);
}

static uint8_t color_g(uint32_t color) {
    return unpack_channel(color, gui_framebuffer->green_mask_shift, gui_framebuffer->green_mask_size);
}

static uint8_t color_b(uint32_t color) {
    return unpack_channel(color, gui_framebuffer->blue_mask_shift, gui_framebuffer->blue_mask_size);
}

static uint32_t mix_color(uint32_t left, uint32_t right, uint64_t step, uint64_t max_step) {
    if (max_step == 0) {
        return left;
    }

    const uint64_t inv = max_step - step;
    const uint8_t r = (uint8_t)((color_r(left) * inv + color_r(right) * step) / max_step);
    const uint8_t g = (uint8_t)((color_g(left) * inv + color_g(right) * step) / max_step);
    const uint8_t b = (uint8_t)((color_b(left) * inv + color_b(right) * step) / max_step);
    return pack_color(r, g, b);
}

static uint32_t dim_color(uint32_t color, uint8_t percent) {
    return pack_color(
        (uint8_t)((uint32_t)color_r(color) * percent / 100u),
        (uint8_t)((uint32_t)color_g(color) * percent / 100u),
        (uint8_t)((uint32_t)color_b(color) * percent / 100u)
    );
}

static uint32_t blend_color(uint32_t under, uint32_t over, uint8_t alpha, uint8_t max_alpha) {
    if (max_alpha == 0 || alpha == 0) {
        return under;
    }
    if (alpha >= max_alpha) {
        return over;
    }

    const uint8_t inv = (uint8_t)(max_alpha - alpha);
    return pack_color(
        (uint8_t)((color_r(under) * inv + color_r(over) * alpha) / max_alpha),
        (uint8_t)((color_g(under) * inv + color_g(over) * alpha) / max_alpha),
        (uint8_t)((color_b(under) * inv + color_b(over) * alpha) / max_alpha)
    );
}

static int rects_touch_or_overlap(const gui_rect_t *left, const gui_rect_t *right) {
    return left->x <= right->x + right->w &&
           left->x + left->w >= right->x &&
           left->y <= right->y + right->h &&
           left->y + left->h >= right->y;
}

static void rect_union(gui_rect_t *dst, const gui_rect_t *src) {
    const uint64_t x0 = dst->x < src->x ? dst->x : src->x;
    const uint64_t y0 = dst->y < src->y ? dst->y : src->y;
    const uint64_t x1 = dst->x + dst->w > src->x + src->w ? dst->x + dst->w : src->x + src->w;
    const uint64_t y1 = dst->y + dst->h > src->y + src->h ? dst->y + dst->h : src->y + src->h;

    dst->x = x0;
    dst->y = y0;
    dst->w = x1 - x0;
    dst->h = y1 - y0;
}

void compositor_mark_dirty(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    if (gui_framebuffer == NULL || w == 0u || h == 0u) {
        return;
    }

    if (x >= gui_framebuffer->width || y >= gui_framebuffer->height) {
        return;
    }

    if (x + w > gui_framebuffer->width) {
        w = gui_framebuffer->width - x;
    }
    if (y + h > gui_framebuffer->height) {
        h = gui_framebuffer->height - y;
    }

    gui_rect_t rect = { x, y, w, h };
    for (uint64_t i = 0; i < dirty_rect_count; ++i) {
        if (rects_touch_or_overlap(&dirty_rects[i], &rect)) {
            rect_union(&dirty_rects[i], &rect);
            return;
        }
    }

    if (dirty_rect_count < GUI_DIRTY_RECT_MAX) {
        dirty_rects[dirty_rect_count++] = rect;
        return;
    }

    dirty_rects[0].x = 0u;
    dirty_rects[0].y = 0u;
    dirty_rects[0].w = gui_framebuffer->width;
    dirty_rects[0].h = gui_framebuffer->height;
    dirty_rect_count = 1u;
}

static void put_pixel_physical(uint64_t x, uint64_t y, uint32_t color) {
    if (gui_framebuffer == NULL || x >= gui_framebuffer->width || y >= gui_framebuffer->height) {
        return;
    }

    uint8_t *row = (uint8_t *)gui_framebuffer->address + y * gui_framebuffer->pitch;
    if (gui_framebuffer->bpp == 32) {
        ((uint32_t *)row)[x] = color;
    }
}

static int scene_clip_bounds(uint64_t *x0,uint64_t *y0,uint64_t *x1,uint64_t *y1) {
    if(gui_paint_clip) {
        const vex_rect_t *c=gui_paint_clip;
        const int64_t right=(int64_t)c->x+c->w,bottom=(int64_t)c->y+c->h;
        if(c->w<=0 || c->h<=0 || right<=0 || bottom<=0)return 0;
        if(c->x>0 && *x0<(uint32_t)c->x)*x0=(uint32_t)c->x;
        if(c->y>0 && *y0<(uint32_t)c->y)*y0=(uint32_t)c->y;
        if(*x1>(uint64_t)right)*x1=(uint64_t)right;
        if(*y1>(uint64_t)bottom)*y1=(uint64_t)bottom;
    }
    return *x0<*x1 && *y0<*y1;
}

void put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (gui_framebuffer == NULL || x >= gui_framebuffer->width || y >= gui_framebuffer->height) {
        return;
    }

    uint64_t x1=x+1,y1=y+1;
    if(!scene_clip_bounds(&x,&y,&x1,&y1))return;
    if (gui_compositor_ready && compositor_scene != NULL) {
        compositor_scene[y * gui_framebuffer->width + x] = color;
    } else {
        put_pixel_physical(x, y, color);
    }
    compositor_mark_dirty(x, y, 1u, 1u);
}

/* Write directly to scene buffer without dirty marking. Caller must mark dirty. */
static inline void scene_write(uint64_t x, uint64_t y, uint32_t color) {
    if (gui_framebuffer == NULL || x >= gui_framebuffer->width || y >= gui_framebuffer->height) {
        return;
    }
    uint64_t x1=x+1,y1=y+1;
    if(!scene_clip_bounds(&x,&y,&x1,&y1))return;
    if (gui_compositor_ready && compositor_scene != NULL) {
        compositor_scene[y * gui_framebuffer->width + x] = color;
    } else {
        put_pixel_physical(x, y, color);
    }
}

static uint32_t scene_pixel(uint64_t x, uint64_t y) {
    if (gui_framebuffer == NULL || x >= gui_framebuffer->width || y >= gui_framebuffer->height) {
        return 0;
    }

    if (gui_compositor_ready && compositor_scene != NULL) {
        return compositor_scene[y * gui_framebuffer->width + x];
    }

    uint8_t *row = (uint8_t *)gui_framebuffer->address + y * gui_framebuffer->pitch;
    if (gui_framebuffer->bpp == 32) {
        return ((uint32_t *)row)[x];
    }
    return 0;
}

static uint32_t get_pixel(uint64_t x, uint64_t y) {
    return scene_pixel(x, y);
}

static void compositor_init_scene(void) {
    uint32_t scene_stride = 0u;

    if (gui_framebuffer == NULL || gui_framebuffer->bpp != 32) {
        return;
    }

    const uint64_t required_pixels = gui_framebuffer->width * gui_framebuffer->height;
    if (required_pixels == 0u) {
        return;
    }

    if (compositor_scene_handle == 0u &&
        gpu_kernel_buffer_create((uint32_t)gui_framebuffer->width,
            (uint32_t)gui_framebuffer->height,
            (uint32_t)gui_framebuffer->width,
            VEX_GPU_MEMORY_CPU_VISIBLE | VEX_GPU_MEMORY_SCANOUT,
            &compositor_scene_handle) == 0) {
        compositor_scene = gpu_kernel_buffer_map(compositor_scene_handle,
            &scene_stride, NULL, NULL);
        if (compositor_scene == NULL ||
            scene_stride != (uint32_t)gui_framebuffer->width) {
            (void)gpu_kernel_buffer_destroy(compositor_scene_handle);
            compositor_scene_handle = 0u;
            compositor_scene = NULL;
        }
    }

    if (compositor_scene == NULL) {
        compositor_scene = kmalloc(sizeof(uint32_t) * required_pixels);
        compositor_scene_handle = 0u;
    }
    compositor_scene_pixels = compositor_scene == NULL
        ? 0u : required_pixels;

    if (compositor_scene == NULL) {
        gui_compositor_ready = 0u;
        return;
    }

    for (uint64_t y = 0; y < gui_framebuffer->height; ++y) {
        const uint8_t *row = (const uint8_t *)gui_framebuffer->address + y * gui_framebuffer->pitch;
        memcpy(compositor_scene + y * gui_framebuffer->width, row, gui_framebuffer->width * sizeof(uint32_t));
    }

    gui_compositor_ready = 1u;
    compositor_mark_dirty(0u, 0u, gui_framebuffer->width, gui_framebuffer->height);
}

int gui_compositor_init_headless(struct limine_framebuffer *framebuffer) {
    if (framebuffer == NULL || framebuffer->bpp != 32) {
        return -1;
    }
    gui_framebuffer = framebuffer;
    compositor_scene = NULL;
    compositor_scene_pixels = 0u;
    compositor_scene_handle = 0u;
    gui_compositor_ready = 0u;
    dirty_rect_count = 0u;
    compositor_init_scene();
    return gui_compositor_ready && compositor_scene_handle != 0u ? 0 : -1;
}

static uint8_t vex_logo_channel(uint32_t x, uint32_t y, uint32_t channel) {
    const uint64_t index = ((uint64_t)y * vex_logo_width + x) * 4u + channel;
    return vex_logo_rgba[index];
}

static void vex_logo_sample_bilinear(uint32_t x_fp, uint32_t y_fp,
                                     uint8_t *red_out, uint8_t *green_out,
                                     uint8_t *blue_out, uint8_t *alpha_out) {
    const uint32_t x0 = x_fp >> 16;
    const uint32_t y0 = y_fp >> 16;
    const uint32_t x1 = x0 + 1u < vex_logo_width ? x0 + 1u : x0;
    const uint32_t y1 = y0 + 1u < vex_logo_height ? y0 + 1u : y0;
    const uint32_t fx = (x_fp >> 8) & 0xffu;
    const uint32_t fy = (y_fp >> 8) & 0xffu;
    const uint32_t inv_fx = 255u - fx;
    const uint32_t inv_fy = 255u - fy;
    const uint32_t w00 = inv_fx * inv_fy;
    const uint32_t w10 = fx * inv_fy;
    const uint32_t w01 = inv_fx * fy;
    const uint32_t w11 = fx * fy;
    const uint32_t r =
        vex_logo_channel(x0, y0, 0u) * w00 +
        vex_logo_channel(x1, y0, 0u) * w10 +
        vex_logo_channel(x0, y1, 0u) * w01 +
        vex_logo_channel(x1, y1, 0u) * w11;
    const uint32_t g =
        vex_logo_channel(x0, y0, 1u) * w00 +
        vex_logo_channel(x1, y0, 1u) * w10 +
        vex_logo_channel(x0, y1, 1u) * w01 +
        vex_logo_channel(x1, y1, 1u) * w11;
    const uint32_t b =
        vex_logo_channel(x0, y0, 2u) * w00 +
        vex_logo_channel(x1, y0, 2u) * w10 +
        vex_logo_channel(x0, y1, 2u) * w01 +
        vex_logo_channel(x1, y1, 2u) * w11;
    const uint32_t a =
        vex_logo_channel(x0, y0, 3u) * w00 +
        vex_logo_channel(x1, y0, 3u) * w10 +
        vex_logo_channel(x0, y1, 3u) * w01 +
        vex_logo_channel(x1, y1, 3u) * w11;

    *red_out = (uint8_t)(r >> 16);
    *green_out = (uint8_t)(g >> 16);
    *blue_out = (uint8_t)(b >> 16);
    *alpha_out = (uint8_t)(a >> 16);
}

void fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    if (gui_framebuffer == NULL || w == 0u || h == 0u) {
        return;
    }
    if (x >= gui_framebuffer->width || y >= gui_framebuffer->height) {
        return;
    }
    uint64_t x1 = x + w > gui_framebuffer->width  ? gui_framebuffer->width  : x + w;
    uint64_t y1 = y + h > gui_framebuffer->height ? gui_framebuffer->height : y + h;
    if(!scene_clip_bounds(&x,&y,&x1,&y1))return;
    const uint64_t cols = x1 - x;

    if (gui_compositor_ready && compositor_scene != NULL) {
        for (uint64_t py = y; py < y1; ++py) {
            uint32_t *row = compositor_scene + py * gui_framebuffer->width + x;
            for (uint64_t i = 0u; i < cols; ++i) {
                row[i] = color;
            }
        }
    } else {
        for (uint64_t py = y; py < y1; ++py) {
            uint32_t *row = (uint32_t *)((uint8_t *)gui_framebuffer->address + py * gui_framebuffer->pitch);
            for (uint64_t i = x; i < x1; ++i) {
                row[i] = color;
            }
        }
    }
    compositor_mark_dirty(x, y, cols, y1 - y);
}

static void draw_wallpaper_rect(uint64_t x0, uint64_t y0, uint64_t w, uint64_t h) {
    profiler_span_t span __attribute__((cleanup(profiler_span_end)))=profiler_span_begin(230);
    if (gui_framebuffer == NULL || w == 0u || h == 0u) {
        return;
    }

    const uint64_t fw = gui_framebuffer->width;
    const uint64_t fh = gui_framebuffer->height;
    if (fw == 0u || fh == 0u) {
        return;
    }

    if(x0>=fw || y0>=fh)return;
    uint64_t x1=w>fw-x0?fw:x0+w,y1=h>fh-y0?fh:y0+h;
    if(!scene_clip_bounds(&x0,&y0,&x1,&y1))return;
    if(wallpaper_cache && (wallpaper_cache_width!=fw || wallpaper_cache_height!=fh)) {
        kfree(wallpaper_cache);wallpaper_cache=NULL;
    }
    if(wallpaper_cache && gui_compositor_ready && compositor_scene) {
        for(uint64_t y=y0;y<y1;y++)memcpy(compositor_scene+y*fw+x0,
            wallpaper_cache+y*fw+x0,(x1-x0)*sizeof(uint32_t));
        compositor_mark_dirty(x0,y0,x1-x0,y1-y0);return;
    }
    uint32_t *new_cache=NULL;
    if(!wallpaper_cache && x0==0 && y0==0 && x1==fw && y1==fh && fw<=SIZE_MAX/fh/sizeof(uint32_t))
        new_cache=kmalloc(fw*fh*sizeof(uint32_t));

    /* 512-entry LUT for the diagonal gradient.
       Colors sampled from the rendered Figma frame (gradient stops are 58%
       alpha over a dark base, so these are the final composited values):
       top-right   (137, 53, 151)
       center      ( 93, 66, 155)   stop at 0.497
       bottom-left ( 47, 88, 156)
       t = ((fw-x)*fw + y*fh) / (fw²+fh²) */
    static uint32_t wlut[512];
    static uint8_t  wlut_ok = 0u;

    if (!wlut_ok) {
        for (uint32_t i = 0u; i < 512u; ++i) {
            const uint64_t t   = (uint64_t)i * 65535u / 511u;
            const uint64_t mid = 32570u;  /* 0.497 × 65535 */
            uint8_t r, g, b;
            if (t <= mid) {
                const uint64_t s = t * 65535u / mid;
                r = (uint8_t)(137u - (uint32_t)(44u * s / 65535u));
                g = (uint8_t)( 53u + (uint32_t)(13u * s / 65535u));
                b = (uint8_t)(151u + (uint32_t)( 4u * s / 65535u));
            } else {
                const uint64_t s = (t - mid) * 65535u / (65535u - mid);
                r = (uint8_t)( 93u - (uint32_t)(46u * s / 65535u));
                g = (uint8_t)( 66u + (uint32_t)(22u * s / 65535u));
                b = (uint8_t)(155u + (uint32_t)( 1u * s / 65535u));
            }
            wlut[i] = pack_color(r, g, b);
        }
        wlut_ok = 1u;
    }

    const uint64_t denom = fw * fw + fh * fh;

    for (uint64_t wy = y0; wy < y1; ++wy) {
        const uint64_t row_c = wy * fh;
        for (uint64_t wx = x0; wx < x1; ++wx) {
            const uint64_t t_num = (fw - wx) * fw + row_c;
            const uint64_t idx   = t_num * 511u / denom;
            uint32_t color=wlut[idx<512u?idx:511u];
            if(new_cache)new_cache[wy*fw+wx]=color;
            scene_write(wx,wy,color);
        }
    }
    if(new_cache){wallpaper_cache=new_cache;wallpaper_cache_width=fw;wallpaper_cache_height=fh;}
    compositor_mark_dirty(x0, y0, x1 - x0, y1 - y0);
}

/* ── Desktop icons (Figma Frame 15): contents of ~/Desktop ── */
#define DESKTOP_ICON_MAX 9u
static struct {
    char name[24];
    char path[56];
    uint8_t is_dir;
} desktop_icons[DESKTOP_ICON_MAX];
static uint64_t desktop_icon_count;

static void desktop_collect_visitor(const char *path, size_t size) {
    /* Match /home/<any user>/Desktop/<entry…> so the icons work no matter
       how the home dir is named (FM hardcodes /home/root/Desktop). */
    static const char head[] = "/home/";
    static const char mid[]  = "/Desktop/";
    char seg[24];
    uint64_t i = 0u, m = 0u, s = 0u;
    uint8_t is_dir = 0u;
    (void)size;

    while (head[i] != '\0') {
        if (path[i] != head[i]) { return; }
        ++i;
    }
    if (path[i] == '\0' || path[i] == '/') { return; }
    while (path[i] != '\0' && path[i] != '/') { ++i; }  /* skip username */
    while (mid[m] != '\0') {
        if (path[i + m] != mid[m]) { return; }
        ++m;
    }
    i += m;
    if (path[i] == '\0') { return; }

    while (path[i + s] != '\0' && path[i + s] != '/' && s < sizeof(seg) - 1u) {
        seg[s] = path[i + s];
        ++s;
    }
    if (path[i + s] == '/') { is_dir = 1u; }
    seg[s] = '\0';
    if (s == 0u) { return; }

    for (uint64_t k = 0u; k < desktop_icon_count; ++k) {
        uint64_t j = 0u;
        while (desktop_icons[k].name[j] != '\0' && desktop_icons[k].name[j] == seg[j]) { ++j; }
        if (desktop_icons[k].name[j] == '\0' && seg[j] == '\0') {
            if (is_dir) { desktop_icons[k].is_dir = 1u; }
            return;
        }
    }
    if (desktop_icon_count >= DESKTOP_ICON_MAX) { return; }
    for (uint64_t j = 0u; j <= s; ++j) {
        desktop_icons[desktop_icon_count].name[j] = seg[j];
    }
    /* Full path of the entry itself: prefix + segment */
    {
        char *dst = desktop_icons[desktop_icon_count].path;
        uint64_t j = 0u;
        while (j < i + s && j < sizeof(desktop_icons[0].path) - 1u) {
            dst[j] = path[j];
            ++j;
        }
        dst[j] = '\0';
    }
    desktop_icons[desktop_icon_count].is_dir = is_dir;
    ++desktop_icon_count;
}

static void desktop_icons_refresh(void) {
    desktop_icon_count = 0u;
    fs_list(desktop_collect_visitor);
}

static int desktop_hover = -1;

static void draw_desktop_icon_cell(uint64_t i) {
    /* Figma Frame 15 grid: origin 52,106 — pitch 109×115, 3 columns,
       gold folders / gray files, label centred under the icon.
       Hover keeps the geometry stable and only adds a quiet plate. */
    const uint32_t gold  = pack_color(201u, 153u, 20u);
    const uint32_t paper = pack_color(217u, 217u, 217u);
    const uint32_t line  = pack_color(120u, 120u, 120u);
    const uint32_t label = pack_color(255u, 255u, 255u);
    const uint64_t col = i % 3u;
    const uint64_t row = i / 3u;
    const uint64_t cx = 52u + col * 109u;
    const uint64_t cy = GUI_TASKBAR_HEIGHT + 38u + row * 115u;
    const int hovered = desktop_hover == (int)i;

    if (i >= desktop_icon_count ||
        cx + 95u >= gui_framebuffer->width || cy + 95u >= gui_framebuffer->height) {
        return;
    }

    if (hovered) {
        fill_rounded_rect(cx > 8u ? cx - 8u : 0u,
                          cy > 6u ? cy - 6u : 0u,
                          97u, 94u, 8u, pack_color(74u, 55u, 104u));
    }

    if (desktop_icons[i].is_dir) {
        fill_rounded_rect(cx, cy, 30u, 12u, 4u, gold);       /* tab */
        fill_rounded_rect(cx, cy + 8u, 81u, 54u, 6u, gold);  /* body */
    } else {
        fill_rounded_rect(cx + 17u, cy, 47u, 62u, 4u, paper);
        fill_rect(cx + 26u, cy + 14u, 29u, 3u, line);
        fill_rect(cx + 26u, cy + 26u, 29u, 3u, line);
        fill_rect(cx + 26u, cy + 38u, 20u, 3u, line);
    }

    {
        const uint64_t lbl_w = gui_font_text_width(desktop_icons[i].name, 13u);
        const uint64_t lbl_x = lbl_w < 81u ? cx + (81u - lbl_w) / 2u
                                           : (cx > (lbl_w - 81u) / 2u ? cx - (lbl_w - 81u) / 2u : cx);
        gui_font_text(lbl_x, cy + 70u, desktop_icons[i].name, label, 13u);
    }
}

static void draw_desktop_icons(void) {
    desktop_icons_refresh();
    for (uint64_t i = 0u; i < desktop_icon_count; ++i) {
        draw_desktop_icon_cell(i);
    }
}

static void draw_desktop_icons_cached(void) {
    for (uint64_t i = 0u; i < desktop_icon_count; ++i) {
        draw_desktop_icon_cell(i);
    }
}

static void drag_repaint_add(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    if (w == 0u || h == 0u || drag_repaint_count >= 4u) {
        return;
    }
    drag_repaint_bounds[drag_repaint_count++] = (gui_rect_t){x, y, w, h};
}

static void draw_wallpaper(void) {
    if (drag_repaint_active != 0u) {
        const uint64_t fw = gui_framebuffer->width;
        const uint64_t fh = gui_framebuffer->height;
        const uint64_t logo_x = fw > VEX_CENTER_LOGO_W ? (fw - VEX_CENTER_LOGO_W) / 2u : 0u;
        const uint64_t logo_y = fh > VEX_CENTER_LOGO_H ? (fh - VEX_CENTER_LOGO_H) / 2u : 0u;
        uint8_t repaint_logo = 0u;

        for (uint8_t i = 0u; i < drag_repaint_count; ++i) {
            const gui_rect_t *damage = &drag_repaint_bounds[i];
            const uint64_t damage_right = damage->x + damage->w;
            const uint64_t damage_bottom = damage->y + damage->h;
            draw_wallpaper_rect(damage->x, damage->y, damage->w, damage->h);
            if (logo_x < damage_right && logo_x + VEX_CENTER_LOGO_W > damage->x &&
                logo_y < damage_bottom && logo_y + VEX_CENTER_LOGO_H > damage->y) {
                repaint_logo = 1u;
            }
        }
        if (repaint_logo != 0u && fw > VEX_CENTER_LOGO_W && fh > VEX_CENTER_LOGO_H) {
            draw_kyil_wordmark(logo_x,logo_y);
        }
        /* The icon inventory is already cached from the full scene render.
         * Do not list the filesystem again on every pointer sample. */
        draw_desktop_icons_cached();
        return;
    }

    draw_wallpaper_rect(0, 0, gui_framebuffer->width, gui_framebuffer->height);
    /* Figma desktop emblem: VEX-OS wordmark centred on the wallpaper. */
    if (gui_framebuffer->width > VEX_CENTER_LOGO_W &&
        gui_framebuffer->height > VEX_CENTER_LOGO_H) {
        draw_kyil_wordmark((gui_framebuffer->width-VEX_CENTER_LOGO_W)/2u,
                          (gui_framebuffer->height-VEX_CENTER_LOGO_H)/2u);
    }
    /* Figma Frame 15: desktop shows the contents of ~/Desktop */
    draw_desktop_icons();
}

/* Blit a Figma-exported RGBA icon at native 1:1 size with alpha blending. */
static void blit_rgba_icon(uint64_t x, uint64_t y,
                           const uint8_t *data, uint64_t iw, uint64_t ih) {
    if (gui_framebuffer == NULL || data == NULL || iw == 0u || ih == 0u) {
        return;
    }
    for (uint64_t dy = 0u; dy < ih && y + dy < gui_framebuffer->height; ++dy) {
        for (uint64_t dx = 0u; dx < iw && x + dx < gui_framebuffer->width; ++dx) {
            const uint64_t base = (dy * iw + dx) * 4u;
            const uint8_t  r    = data[base];
            const uint8_t  g    = data[base + 1u];
            const uint8_t  b    = data[base + 2u];
            const uint8_t  a    = data[base + 3u];
            if (a == 0u) { continue; }
            const uint32_t over = pack_color(r, g, b);
            scene_write(x + dx, y + dy,
                a == 255u ? over
                          : blend_color(scene_pixel(x + dx, y + dy), over, a, 255u));
        }
    }
    compositor_mark_dirty(x, y, iw, ih);
}

/* Public wrapper so other UI modules can blit Figma RGBA assets. */
void gui_blit_rgba(uint64_t x, uint64_t y, const uint8_t *data, uint64_t iw, uint64_t ih) {
    blit_rgba_icon(x, y, data, iw, ih);
}

/* ── Inter proportional font engine (8-bit alpha glyphs) ── */
typedef struct {
    uint8_t cell_h;
    const uint8_t *widths;
    const uint32_t *offsets;
    const uint8_t *data;
} vex_font_face_t;

static const vex_font_face_t vex_font_faces[3] = {
    { INTER13_CELL_H, inter13_widths, inter13_offsets, inter13_data },
    { INTER16_CELL_H, inter16_widths, inter16_offsets, inter16_data },
    { INTER20_CELL_H, inter20_widths, inter20_offsets, inter20_data },
};

static const vex_font_face_t *font_face(uint8_t size) {
    if (size <= 13u) { return &vex_font_faces[0]; }
    if (size <= 16u) { return &vex_font_faces[1]; }
    return &vex_font_faces[2];
}

uint64_t gui_font_text_width(const char *text, uint8_t size) {
    const vex_font_face_t *f = font_face(size);
    uint64_t w = 0u;
    for (uint64_t i = 0u; text != NULL && text[i] != '\0'; ++i) {
        const uint8_t c = (uint8_t)text[i];
        w += (c >= 32u && c < 127u) ? f->widths[c - 32u] : f->widths[0];
    }
    return w;
}

uint8_t gui_font_cell_height(uint8_t size) {
    return font_face(size)->cell_h;
}

void gui_font_text_clipped(uint64_t x, uint64_t y, const char *text,
                           uint32_t color, uint8_t size, uint64_t max_w) {
    const vex_font_face_t *f = font_face(size);
    uint64_t pen = x;

    if (gui_framebuffer == NULL || text == NULL) {
        return;
    }

    for (uint64_t i = 0u; text[i] != '\0'; ++i) {
        const uint8_t c = (uint8_t)text[i];
        const uint8_t gi = (c >= 32u && c < 127u) ? (uint8_t)(c - 32u) : 0u;
        const uint8_t gw = f->widths[gi];
        const uint8_t *bitmap = f->data + f->offsets[gi];

        if (max_w != 0u && pen + gw > x + max_w) {
            break;
        }
        for (uint8_t gy = 0u; gy < f->cell_h; ++gy) {
            const uint64_t py = y + gy;
            if (py >= gui_framebuffer->height) { break; }
            for (uint8_t gx = 0u; gx < gw; ++gx) {
                const uint8_t a = bitmap[(uint64_t)gy * gw + gx];
                if (a == 0u) { continue; }
                const uint64_t px = pen + gx;
                if (px >= gui_framebuffer->width) { break; }
                scene_write(px, py,
                    a == 255u ? color
                              : blend_color(scene_pixel(px, py), color, a, 255u));
            }
        }
        pen += gw;
    }
    compositor_mark_dirty(x, y, pen > x ? pen - x : 0u, f->cell_h);
}

void gui_font_text(uint64_t x, uint64_t y, const char *text, uint32_t color, uint8_t size) {
    gui_font_text_clipped(x, y, text, color, size, 0u);
}
static void draw_kyil_wordmark(uint64_t x,uint64_t y) {
    const vex_font_face_t *f=&vex_font_faces[2];const char *text="KyIlOS";
    uint64_t width=gui_font_text_width(text,20)*3;
    uint64_t pen=x+(VEX_CENTER_LOGO_W>width?(VEX_CENTER_LOGO_W-width)/2:0);
    y+=(VEX_CENTER_LOGO_H>f->cell_h*3?(VEX_CENTER_LOGO_H-f->cell_h*3)/2:0);
    uint64_t start=pen;
    for(unsigned i=0;text[i];i++) {
        unsigned c=(unsigned char)text[i]-32,gw=f->widths[c];const uint8_t *bitmap=f->data+f->offsets[c];
        for(unsigned gy=0;gy<f->cell_h*3;gy++)for(unsigned gx=0;gx<gw*3;gx++) {
            uint8_t alpha=bitmap[(gy/3)*gw+gx/3];if(!alpha)continue;
            uint64_t px=pen+gx,py=y+gy;if(px>=gui_framebuffer->width || py>=gui_framebuffer->height)continue;
            uint32_t color=mix_color(pack_color(156,123,255),pack_color(90,163,250),px-start,width);
            scene_write(px,py,blend_color(scene_pixel(px,py),color,alpha,255));
        }
        pen+=gw*3;
    }
    compositor_mark_dirty(start,y,width,f->cell_h*3);
}

static void draw_taskbar_brand(void) {
    if (gui_framebuffer == NULL) {
        return;
    }
    /* Hexagon brand, vertically centred in the slim bar (25×28) */
    blit_rgba_icon(32u, GUI_BAR_CY - VEX_ICON_HEX_H / 2u,
                   vex_icon_hex, VEX_ICON_HEX_W, VEX_ICON_HEX_H);
}

static void draw_border(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    if (w == 0 || h == 0) {
        return;
    }

    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1u, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1u, y, 1, h, color);
}

void fill_circle(uint64_t cx, uint64_t cy, uint64_t r, uint32_t color) {
    /* 4×4 supersampled edge (17 coverage levels) for smooth outlines */
    const uint64_t x0 = cx > r ? cx - r : 0u;
    const uint64_t y0 = cy > r ? cy - r : 0u;
    const uint64_t x1 = cx + r;
    const uint64_t y1 = cy + r;
    const int64_t r4 = (int64_t)(r * 4u);
    const int64_t r2_scaled = r4 * r4;

    for (uint64_t py = y0; py <= y1; ++py) {
        for (uint64_t px = x0; px <= x1; ++px) {
            /* quick reject/accept on the pixel centre at single resolution */
            const int64_t pdx = (int64_t)px - (int64_t)cx;
            const int64_t pdy = (int64_t)py - (int64_t)cy;
            const int64_t d2 = pdx * pdx + pdy * pdy;
            if (r >= 2u && d2 <= (int64_t)((r - 2u) * (r - 2u))) {
                scene_write(px, py, color);   /* fully inside */
                continue;
            }
            if (d2 > (int64_t)((r + 2u) * (r + 2u))) {
                continue;                      /* fully outside */
            }

            uint8_t coverage = 0u;
            for (uint64_t sy = 0u; sy < 4u; ++sy) {
                for (uint64_t sx = 0u; sx < 4u; ++sx) {
                    const int64_t dx = (int64_t)(px * 4u + sx) - (int64_t)(cx * 4u) - 2;
                    const int64_t dy = (int64_t)(py * 4u + sy) - (int64_t)(cy * 4u) - 2;
                    if (dx * dx + dy * dy <= r2_scaled) {
                        ++coverage;
                    }
                }
            }
            if (coverage == 16u) {
                scene_write(px, py, color);
            } else if (coverage != 0u) {
                scene_write(px, py, blend_color(scene_pixel(px, py), color, coverage, 16u));
            }
        }
    }
    compositor_mark_dirty(x0, y0, x1 - x0 + 2u, y1 - y0 + 2u);
}

void fill_rounded_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t r, uint32_t color) {
    if (w == 0 || h == 0) {
        return;
    }

    if (r * 2u > w) {
        r = w / 2u;
    }
    if (r * 2u > h) {
        r = h / 2u;
    }
    if (r == 0) {
        fill_rect(x, y, w, h, color);
        return;
    }

    /* Interior: plain fills (fast). Corners: 4×4 supersampled coverage. */
    {
        const uint64_t fb_w = gui_framebuffer->width;
        const uint64_t fb_h = gui_framebuffer->height;

        /* middle band (full width, between the corner rows) */
        for (uint64_t py = y + r; py < y + h - r && py < fb_h; ++py) {
            for (uint64_t px = x; px < x + w && px < fb_w; ++px) {
                scene_write(px, py, color);
            }
        }
        /* top and bottom bands minus corners */
        for (uint64_t py = y; py < y + r && py < fb_h; ++py) {
            for (uint64_t px = x + r; px < x + w - r && px < fb_w; ++px) {
                scene_write(px, py, color);
            }
        }
        for (uint64_t py = y + h - r; py < y + h && py < fb_h; ++py) {
            for (uint64_t px = x + r; px < x + w - r && px < fb_w; ++px) {
                scene_write(px, py, color);
            }
        }

        /* four corner boxes, AA against their quarter-circle centre */
        const uint64_t corner_cx[4] = { x + r, x + w - 1u - r, x + r, x + w - 1u - r };
        const uint64_t corner_cy[4] = { y + r, y + r, y + h - 1u - r, y + h - 1u - r };
        const uint64_t box_x[4] = { x, x + w - r, x, x + w - r };
        const uint64_t box_y[4] = { y, y, y + h - r, y + h - r };
        const int64_t r4 = (int64_t)(r * 4u) + 2;   /* +0.5px to mate with edges */
        const int64_t r2_scaled = r4 * r4;

        for (uint64_t c = 0u; c < 4u; ++c) {
            for (uint64_t py = box_y[c]; py < box_y[c] + r && py < fb_h; ++py) {
                for (uint64_t px = box_x[c]; px < box_x[c] + r && px < fb_w; ++px) {
                    uint8_t coverage = 0u;
                    for (uint64_t sy = 0u; sy < 4u; ++sy) {
                        for (uint64_t sx = 0u; sx < 4u; ++sx) {
                            const int64_t dx = (int64_t)(px * 4u + sx) - (int64_t)(corner_cx[c] * 4u) - 2;
                            const int64_t dy = (int64_t)(py * 4u + sy) - (int64_t)(corner_cy[c] * 4u) - 2;
                            if (dx * dx + dy * dy <= r2_scaled) {
                                ++coverage;
                            }
                        }
                    }
                    if (coverage == 16u) {
                        scene_write(px, py, color);
                    } else if (coverage != 0u) {
                        scene_write(px, py, blend_color(scene_pixel(px, py), color, coverage, 16u));
                    }
                }
            }
        }
    }
    compositor_mark_dirty(x, y, w, h);
}

static int rounded_rect_contains(uint64_t px, uint64_t py, uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t r) {
    if (px < x || py < y || px >= x + w || py >= y + h) {
        return 0;
    }
    if (r == 0 || (px >= x + r && px < x + w - r) || (py >= y + r && py < y + h - r)) {
        return 1;
    }

    const uint64_t cx = px < x + r ? x + r : x + w - r - 1u;
    const uint64_t cy = py < y + r ? y + r : y + h - r - 1u;
    const int64_t dx = (int64_t)px - (int64_t)cx;
    const int64_t dy = (int64_t)py - (int64_t)cy;
    return dx * dx + dy * dy <= (int64_t)(r * r);
}

/* Blend `color` at `alpha/255` over the existing scene pixels, with rounded corners.
   Used for the frosted-glass menubar. */
static void blend_rounded_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h,
                                uint64_t r, uint32_t color, uint8_t alpha) {
    if (w == 0u || h == 0u || gui_framebuffer == NULL) {
        return;
    }
    if (r * 2u > w) { r = w / 2u; }
    if (r * 2u > h) { r = h / 2u; }

    for (uint64_t py = y; py < y + h && py < gui_framebuffer->height; ++py) {
        for (uint64_t px = x; px < x + w && px < gui_framebuffer->width; ++px) {
            uint8_t coverage = 0u;
            for (uint64_t sy = 0u; sy < 2u; ++sy) {
                for (uint64_t sx = 0u; sx < 2u; ++sx) {
                    if (rounded_rect_contains(px * 2u + sx, py * 2u + sy,
                                              x * 2u, y * 2u, w * 2u, h * 2u, r * 2u)) {
                        ++coverage;
                    }
                }
            }
            if (coverage != 0u) {
                const uint32_t bg    = scene_pixel(px, py);
                const uint8_t  a_eff = (uint8_t)((uint32_t)alpha * coverage / 4u);
                scene_write(px, py, blend_color(bg, color, a_eff, 255u));
            }
        }
    }
    compositor_mark_dirty(x, y, w, h);
}

static void fill_rounded_rect_horizontal_gradient(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t r, uint32_t left, uint32_t right) {
    if (w == 0 || h == 0) {
        return;
    }

    if (r * 2u > w) {
        r = w / 2u;
    }
    if (r * 2u > h) {
        r = h / 2u;
    }

    const uint64_t max_step = w > 1u ? w - 1u : 1u;
    for (uint64_t py = y; py < y + h && py < gui_framebuffer->height; ++py) {
        for (uint64_t px = x; px < x + w && px < gui_framebuffer->width; ++px) {
            uint8_t coverage = 0;

            for (uint64_t sy = 0; sy < 2u; ++sy) {
                for (uint64_t sx = 0; sx < 2u; ++sx) {
                    const uint64_t sample_x = px * 2u + sx;
                    const uint64_t sample_y = py * 2u + sy;
                    if (rounded_rect_contains(sample_x, sample_y, x * 2u, y * 2u, w * 2u, h * 2u, r * 2u)) {
                        ++coverage;
                    }
                }
            }

            if (coverage != 0u) {
                const uint32_t color = mix_color(left, right, px - x, max_step);
                scene_write(px, py, coverage == 4u ? color : blend_color(scene_pixel(px, py), color, coverage, 4u));
            }
        }
    }
    compositor_mark_dirty(x, y, w, h);
}

static void draw_rounded_border(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t r, uint32_t color) {
    if (w < 2u || h < 2u) {
        return;
    }

    fill_rounded_rect(x, y, w, h, r, color);
    fill_rounded_rect(x + 1u, y + 1u, w - 2u, h - 2u, r > 1u ? r - 1u : 0u, get_pixel(x + 1u, y + 1u));
}

void restore_mouse_cursor_background(void) {
    if (!cursor_backing_valid) {
        return;
    }

    for (uint64_t row = 0; row < GUI_CURSOR_SIZE; ++row) {
        for (uint64_t col = 0; col < GUI_CURSOR_SIZE; ++col) {
            put_pixel_physical(cursor_backing_x + col, cursor_backing_y + row, cursor_backing[row * GUI_CURSOR_SIZE + col]);
        }
    }

    cursor_backing_valid = 0;
    cursor_needs_present = 1u;
}

static uint8_t weekday_for_date(uint16_t year, uint8_t month, uint8_t day) {
    static const uint8_t offsets[12] = { 0u, 3u, 2u, 5u, 0u, 3u, 5u, 1u, 4u, 6u, 2u, 4u };

    if (month < 3u) {
        --year;
    }

    return (uint8_t)((year + year / 4u - year / 100u + year / 400u + offsets[month - 1u] + day) % 7u);
}

static void append_text(char *out, uint64_t capacity, uint64_t *offset, const char *text) {
    while (text != NULL && *text != '\0' && *offset + 1u < capacity) {
        out[*offset] = *text;
        ++*offset;
        ++text;
    }
    if (capacity != 0) {
        out[*offset] = '\0';
    }
}

static void append_2digit(char *out, uint64_t capacity, uint64_t *offset, uint8_t value) {
    if (*offset + 2u >= capacity) {
        return;
    }

    out[(*offset)++] = (char)('0' + (value / 10u));
    out[(*offset)++] = (char)('0' + (value % 10u));
    out[*offset] = '\0';
}

static void format_date(char out[18]) {
    static const char *weekdays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *months[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    const uint8_t day = rtc_day();
    const uint8_t month = rtc_month();
    const uint16_t year = rtc_year();
    uint64_t offset = 0;

    append_text(out, 18u, &offset, weekdays[weekday_for_date(year, month, day)]);
    append_text(out, 18u, &offset, " ");
    append_2digit(out, 18u, &offset, day);
    append_text(out, 18u, &offset, " ");
    append_text(out, 18u, &offset, months[(month == 0u ? 1u : month) - 1u]);
}

static void draw_taskbar_time(void) {
    if (gui_framebuffer == NULL || gui_framebuffer->width <= 160u) {
        return;
    }

    char date_text[18];
    const uint32_t text_col = pack_color(255, 255, 255);
    const uint64_t width    = gui_framebuffer->width;
    /* Inter 16, vertically centred in the glass bar */
    const uint64_t text_y   = GUI_BAR_CY - gui_font_cell_height(16u) / 2u;

    format_date(date_text);

    const uint64_t text_w = gui_font_text_width(date_text, 16u);
    const uint64_t date_x = width > text_w ? (width - text_w) / 2u : 0u;

    gui_font_text(date_x, text_y, date_text, text_col, 16u);
}

/* Hover state for the taskbar chips: 0 hex, 1 battery, 2 volume, 3 wifi, 4 pill */
static int taskbar_hover = -1;

static int taskbar_hover_zone(int idx, uint64_t *zx, uint64_t *zw) {
    const uint64_t W = gui_framebuffer->width;
    switch (idx) {
        case 0: *zx = 28u;       *zw = 33u; return 1;             /* hex brand */
        case 1: *zx = W - 219u;  *zw = 52u; return W > 240u;      /* battery */
        case 2: *zx = W - 155u;  *zw = 30u; return W > 180u;      /* volume */
        case 3: *zx = W - 115u;  *zw = 30u; return W > 140u;      /* wifi */
        case 4: *zx = W - 76u;   *zw = 44u; return W > 100u;      /* notif pill */
        default: return 0;
    }
}

static int taskbar_hover_at(uint64_t x, uint64_t y) {
    if (gui_framebuffer == NULL ||
        y < GUI_BAR_Y + 2u || y >= GUI_BAR_Y + GUI_BAR_H - 2u) {
        return -1;
    }
    for (int i = 0; i < 5; ++i) {
        uint64_t zx, zw;
        if (taskbar_hover_zone(i, &zx, &zw) && x >= zx && x < zx + zw) {
            return i;
        }
    }
    return -1;
}

static void draw_taskbar_system_icons(void) {
    if (gui_framebuffer == NULL || gui_framebuffer->width < 200u) {
        return;
    }

    const uint64_t W  = gui_framebuffer->width;
    const uint32_t ic = pack_color(156u, 156u, 156u);  /* #9C9C9C */

    /* Figma positions (1920px design), all icons at y=27:
       WiFi   x=1809  → offset from right: 1920-1809 = 111
       Volume x=1769  → offset from right: 1920-1769 = 151
       Battery outline x=1848 y=31 w=36 h=14 → right offset: 72
       Battery nub at x=1861+22=1883 → right offset: 37 */

    /* WiFi — real Figma icon 22×22, centred in the slim bar */
    if (W > 111u + VEX_ICON_WIFI_W) {
        blit_rgba_icon(W - 111u, GUI_BAR_CY - VEX_ICON_WIFI_H / 2u,
                       vex_icon_wifi, VEX_ICON_WIFI_W, VEX_ICON_WIFI_H);
    }

    /* Volume — real Figma icon 22×22 */
    if (W > 151u + VEX_ICON_VOLUME_W) {
        blit_rgba_icon(W - 151u, GUI_BAR_CY - VEX_ICON_VOLUME_H / 2u,
                       vex_icon_volume, VEX_ICON_VOLUME_W, VEX_ICON_VOLUME_H);
    }

    /* Notification counter pill — outline 36×14 r=7, dot left, count right */
    if (W > 80u) {
        const uint64_t bx = W - 72u;
        const uint64_t by = GUI_BAR_CY - 7u;
        fill_rounded_rect(bx,      by,      36u, 14u, 7u, ic);
        fill_rounded_rect(bx + 2u, by + 2u, 32u, 10u, 5u, pack_color(20u, 20u, 20u));
        fill_circle(bx + 9u, by + 7u, 4u, ic);                /* dot */
        /* tiny "0": 7×9 ring that fits inside the 14px pill */
        fill_rounded_rect(bx + 21u, by + 3u, 7u, 9u, 3u, ic);
        fill_rounded_rect(bx + 23u, by + 5u, 3u, 5u, 1u, pack_color(20u, 20u, 20u));
    }

    /* Battery — real Figma icon (48×20) */
    if (W > 217u + VEX_ICON_POWER_W) {
        blit_rgba_icon(W - 217u, GUI_BAR_CY - VEX_ICON_POWER_H / 2u,
                       vex_icon_power, VEX_ICON_POWER_W, VEX_ICON_POWER_H);
    }
}

void draw_taskbar_static(void) {
    if (gui_framebuffer == NULL) {
        return;
    }

    const uint64_t W = gui_framebuffer->width;

    /* 1. Gradient wallpaper strip */
    draw_wallpaper_rect(0, 0, W, GUI_TASKBAR_HEIGHT);

    if (vex_lvgl_ui_ready()) {
        char date_text[18];
        format_date(date_text);
        vex_lvgl_ui_draw_taskbar(date_text, taskbar_hover);
        draw_app_switcher_overlay();
        return;
    }

    /* 2. Frosted-glass bar — slim variant: x=20 y=13 w=(W-40) h=36 rx=6
          fill=#141414 fill-opacity=0.48  →  alpha ≈ 122/255 */
    if (W > 40u) {
        blend_rounded_rect(20u, GUI_BAR_Y, W - 40u, GUI_BAR_H, 6u,
                           pack_color(20u, 20u, 20u), 122u);
    }

    /* 2b. Hover highlight: light translucent chip behind the hovered icon */
    if (taskbar_hover >= 0) {
        uint64_t zx, zw;
        if (taskbar_hover_zone(taskbar_hover, &zx, &zw)) {
            blend_rounded_rect(zx > 5u ? zx - 5u : 0u, GUI_BAR_Y + 4u,
                               zw + 10u, GUI_BAR_H - 8u, 6u,
                               pack_color(255u, 255u, 255u), 34u);
        }
    }

    /* 3. Content */
    draw_taskbar_brand();
    draw_taskbar_time();
    draw_taskbar_system_icons();
    draw_app_switcher_overlay();
}

static void draw_main_window_frame(const char *title_text) {
    draw_window_frame_at(terminal_win_x, terminal_win_y, terminal_win_w, terminal_win_h, title_text);
}

static void draw_window_frame_at(uint64_t x, uint64_t y, uint64_t w, uint64_t h, const char *title_text) {
    /* Figma window chrome (Frames 20/21/22):
       solid #141414 card, divider under the 45px title bar,
       traffic lights on the RIGHT (red outermost). */
    const uint32_t body = pack_color(20, 20, 20);    /* #141414 */
    const uint32_t sep  = pack_color(72, 49, 86);    /* divider — dark violet */

    if (gui_framebuffer == NULL || w == 0 || h == 0) {
        return;
    }

    if (vex_lvgl_ui_ready() && x <= UINT32_MAX && y <= UINT32_MAX &&
        w <= UINT32_MAX && h <= UINT32_MAX) {
        vex_lvgl_ui_draw_window_frame((uint32_t)x, (uint32_t)y,
                                      (uint32_t)w, (uint32_t)h, title_text);
        return;
    }

    /* Body */
    fill_rounded_rect(x, y, w, h, 10u, body);
    /* Divider under title bar */
    fill_rect(x + 1u, y + GUI_TITLE_HEIGHT, w - 2u, 1u, sep);

    /* Traffic lights — right side, red outermost (Figma order) */
    if (w > 90u) {
        const uint64_t cy = y + GUI_TITLE_HEIGHT / 2u;
        fill_circle(x + w - 22u, cy, GUI_WINDOW_CONTROL_RADIUS, pack_color(255,   0,   0));
        fill_circle(x + w - 48u, cy, GUI_WINDOW_CONTROL_RADIUS, pack_color(232, 224,   2));
        fill_circle(x + w - 74u, cy, GUI_WINDOW_CONTROL_RADIUS, pack_color( 68, 255,   0));
    }

    /* Title — left-aligned, white, leaves the right free for controls */
    if (title_text != NULL) {
        gui_font_text_clipped(x + 23u, y + (GUI_TITLE_HEIGHT - gui_font_cell_height(16u)) / 2u,
            title_text, pack_color(255, 255, 255), 16u, w > 130u ? w - 130u : 8u);
    }

    compositor_mark_dirty(x > 4u ? x - 4u : 0u, y > 4u ? y - 4u : 0u, w + 8u, h + 8u);
}

void save_terminal_snapshot(void) {
    const uint64_t content_x = terminal_win_x + 8u;
    const uint64_t content_y = terminal_win_y + GUI_TITLE_HEIGHT + 6u;
    const uint64_t content_w = terminal_win_w > 16u ? terminal_win_w - 16u : 0u;
    const uint64_t content_h = terminal_win_h > GUI_TITLE_HEIGHT + 12u
        ? terminal_win_h - GUI_TITLE_HEIGHT - 12u : 0u;

    if (gui_framebuffer == NULL || content_w == 0u || content_h == 0u ||
        vex_lvgl_ui_ready()) {
        return;
    }

    const uint64_t required_pixels = content_w * content_h;
    if (terminal_snapshot == NULL || terminal_snapshot_pixels < required_pixels) {
        uint32_t *new_snapshot = kmalloc(sizeof(uint32_t) * required_pixels);
        if (new_snapshot == NULL) {
            terminal_snapshot_valid = 0u;
            return;
        }
        if (terminal_snapshot != NULL) {
            kfree(terminal_snapshot);
        }
        terminal_snapshot = new_snapshot;
        terminal_snapshot_pixels = required_pixels;
    }

    if (gui_compositor_ready && compositor_scene != NULL) {
        const size_t row_bytes = (size_t)content_w * sizeof(uint32_t);
        for (uint64_t y = 0; y < content_h; ++y) {
            const uint32_t *src = compositor_scene + (content_y + y) * gui_framebuffer->width + content_x;
            uint32_t *dst = terminal_snapshot + y * content_w;
            memcpy(dst, src, row_bytes);
        }
    } else {
        for (uint64_t y = 0; y < content_h; ++y) {
            for (uint64_t x = 0; x < content_w; ++x) {
                terminal_snapshot[y * content_w + x] = get_pixel(content_x + x, content_y + y);
            }
        }
    }

    terminal_snapshot_valid = 1;
}

void gui_terminal_snapshot_refresh(void) {
    GUI_GUARD;
    if (gui_framebuffer == NULL || terminal_minimized) {
        return;
    }

    if (vex_lvgl_ui_ready()) {
        terminal_lvgl_content_dirty = 1u;
        gui_wake();
        return;
    }

    save_terminal_snapshot();
    gui_wake();
}

void gui_terminal_snapshot_refresh_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    GUI_GUARD;
    const uint64_t content_x = terminal_win_x + 8u;
    const uint64_t content_y = terminal_win_y + GUI_TITLE_HEIGHT + 6u;
    const uint64_t content_w = terminal_win_w > 16u ? terminal_win_w - 16u : 0u;
    const uint64_t content_h = terminal_win_h > GUI_TITLE_HEIGHT + 12u
        ? terminal_win_h - GUI_TITLE_HEIGHT - 12u : 0u;

    if (gui_framebuffer == NULL || terminal_minimized || w == 0u || h == 0u ||
        content_w == 0u || content_h == 0u) {
        return;
    }
    if (vex_lvgl_ui_ready()) {
        terminal_lvgl_content_dirty = 1u;
        gui_wake();
        return;
    }
    if (!terminal_snapshot_valid || terminal_snapshot == NULL ||
        terminal_snapshot_pixels < content_w * content_h) {
        save_terminal_snapshot();
        gui_wake();
        return;
    }

    if (x < content_x) {
        const uint64_t clipped = content_x - x;
        if (clipped >= w) {
            return;
        }
        x = content_x;
        w -= clipped;
    }
    if (y < content_y) {
        const uint64_t clipped = content_y - y;
        if (clipped >= h) {
            return;
        }
        y = content_y;
        h -= clipped;
    }
    if (x >= content_x + content_w || y >= content_y + content_h) {
        return;
    }
    if (w > content_x + content_w - x) {
        w = content_x + content_w - x;
    }
    if (h > content_y + content_h - y) {
        h = content_y + content_h - y;
    }

    for (uint64_t row = 0u; row < h; ++row) {
        uint32_t *dst = terminal_snapshot + (y - content_y + row) * content_w + (x - content_x);
        if (gui_compositor_ready && compositor_scene != NULL) {
            const uint32_t *src = compositor_scene + (y + row) * gui_framebuffer->width + x;
            memcpy(dst, src, (size_t)w * sizeof(uint32_t));
        } else {
            for (uint64_t col = 0u; col < w; ++col) {
                dst[col] = get_pixel(x + col, y + row);
            }
        }
    }
    gui_wake();
}

void restore_terminal_snapshot(void) {
    const uint64_t content_x = terminal_win_x + 8u;
    const uint64_t content_y = terminal_win_y + GUI_TITLE_HEIGHT + 6u;
    const uint64_t content_w = terminal_win_w > 16u ? terminal_win_w - 16u : 0u;
    const uint64_t content_h = terminal_win_h > GUI_TITLE_HEIGHT + 12u
        ? terminal_win_h - GUI_TITLE_HEIGHT - 12u : 0u;

    if (vex_lvgl_ui_ready()) {
        return;
    }
    if (gui_framebuffer == NULL || terminal_snapshot == NULL || !terminal_snapshot_valid) {
        draw_main_window_frame("Terminal");
        return;
    }

    if (gui_compositor_ready && compositor_scene != NULL) {
        const size_t row_bytes = (size_t)content_w * sizeof(uint32_t);
        for (uint64_t y = 0; y < content_h; ++y) {
            uint32_t *dst = compositor_scene + (content_y + y) * gui_framebuffer->width + content_x;
            const uint32_t *src = terminal_snapshot + y * content_w;
            memcpy(dst, src, row_bytes);
        }
        compositor_mark_dirty(content_x, content_y, content_w, content_h);
    } else {
        for (uint64_t y = 0; y < content_h; ++y) {
            for (uint64_t x = 0; x < content_w; ++x) {
                put_pixel(content_x + x, content_y + y, terminal_snapshot[y * content_w + x]);
            }
        }
    }
}

int window_control_circle_hit_at(uint64_t x, uint64_t y, uint64_t window_y, uint64_t center_x) {
    const uint64_t center_y = window_y + GUI_TITLE_HEIGHT / 2u;
    const int64_t dx = (int64_t)x - (int64_t)center_x;
    const int64_t dy = (int64_t)y - (int64_t)center_y;
    const uint64_t hit_radius = GUI_WINDOW_CONTROL_RADIUS + 5u;

    return dx * dx + dy * dy <= (int64_t)(hit_radius * hit_radius);
}

static int window_titlebar_hit_at(uint64_t px, uint64_t py, uint64_t x, uint64_t y, uint64_t w) {
    if (py < y || py >= y + GUI_TITLE_HEIGHT || px < x || px >= x + w) {
        return 0;
    }
    if (px < x + 60u) {
        return 0;
    }
    return 1;
}

static uint8_t window_resize_edges_at(uint64_t px, uint64_t py, uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    uint8_t edges = 0u;
    const uint64_t edge = GUI_WINDOW_RESIZE_EDGE;

    if (w < GUI_WINDOW_MIN_W || h < GUI_WINDOW_MIN_H ||
        px < x || py < y || px >= x + w || py >= y + h) {
        return 0u;
    }

    if (px < x + edge) {
        edges |= GUI_WINDOW_RESIZE_LEFT;
    } else if (px >= x + w - edge) {
        edges |= GUI_WINDOW_RESIZE_RIGHT;
    }

    if (py < y + edge) {
        edges |= GUI_WINDOW_RESIZE_TOP;
    } else if (py >= y + h - edge) {
        edges |= GUI_WINDOW_RESIZE_BOTTOM;
    }

    if (edges == 0u &&
        px >= x + w - GUI_WINDOW_RESIZE_HANDLE &&
        py >= y + h - GUI_WINDOW_RESIZE_HANDLE) {
        edges = GUI_WINDOW_RESIZE_RIGHT | GUI_WINDOW_RESIZE_BOTTOM;
    }

    return edges;
}

static void clamp_window_bounds(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h) {
    uint64_t max_x;
    uint64_t max_y;
    uint64_t min_y;

    if (gui_framebuffer == NULL || x == NULL || y == NULL || w == NULL || h == NULL) {
        return;
    }

    if (*w < GUI_WINDOW_MIN_W) {
        *w = GUI_WINDOW_MIN_W;
    }
    if (*h < GUI_WINDOW_MIN_H) {
        *h = GUI_WINDOW_MIN_H;
    }
    if (*w > gui_framebuffer->width) {
        *w = gui_framebuffer->width;
    }
    {
        const uint64_t max_h = gui_framebuffer->height > GUI_TASKBAR_HEIGHT
            ? gui_framebuffer->height - GUI_TASKBAR_HEIGHT
            : gui_framebuffer->height;
        if (*h > max_h) {
            *h = max_h;
        }
    }

    min_y = GUI_TASKBAR_HEIGHT;
    max_x = gui_framebuffer->width > *w ? gui_framebuffer->width - *w : 0u;
    max_y = gui_framebuffer->height > *h ? gui_framebuffer->height - *h : min_y;
    if (max_y < min_y) {
        max_y = min_y;
    }

    if (*x > max_x) {
        *x = max_x;
    }
    if (*y < min_y) {
        *y = min_y;
    } else if (*y > max_y) {
        *y = max_y;
    }
}

static void begin_window_drag(gui_window_drag_target_t target, uint8_t resize_edges) {
    dragged_window = target;
    drag_resize_edges = resize_edges;
    drag_app_surface_id = 0u;
    drag_origin_cursor_x = cursor_x;
    drag_origin_cursor_y = cursor_y;
    if (target == GUI_WINDOW_DRAG_BROWSER) {
        drag_origin_win_x = browser_win_x;
        drag_origin_win_y = browser_win_y;
        drag_origin_win_w = browser_win_w;
        drag_origin_win_h = browser_win_h;
    } else if (target == GUI_WINDOW_DRAG_APP_SURFACE) {
        app_surface_t *surface = app_surface_hit(cursor_x, cursor_y);

        if (surface == NULL) {
            dragged_window = GUI_WINDOW_DRAG_NONE;
            drag_resize_edges = 0u;
            return;
        }
        drag_app_surface_id = surface->window_id;
        drag_origin_win_x = surface->x;
        drag_origin_win_y = surface->y;
        drag_origin_win_w = surface->w;
        drag_origin_win_h = surface->h;
    } else {
        drag_origin_win_x = terminal_win_x;
        drag_origin_win_y = terminal_win_y;
        drag_origin_win_w = terminal_win_w;
        drag_origin_win_h = terminal_win_h;
    }
}

static void update_window_drag(void) {
    uint64_t *x;
    uint64_t *y;
    uint64_t *w;
    uint64_t *h;
    uint64_t new_x;
    uint64_t new_y;
    uint64_t new_w;
    uint64_t new_h;
    uint64_t old_x;
    uint64_t old_y;
    uint64_t old_w;
    uint64_t old_h;

    if (dragged_window == GUI_WINDOW_DRAG_NONE || gui_framebuffer == NULL) {
        return;
    }

    if (dragged_window == GUI_WINDOW_DRAG_BROWSER) {
        x = &browser_win_x;
        y = &browser_win_y;
        w = &browser_win_w;
        h = &browser_win_h;
    } else if (dragged_window == GUI_WINDOW_DRAG_APP_SURFACE) {
        app_surface_t *surface = app_surface_find(drag_app_surface_id);

        if (surface == NULL || !surface->visible || surface->maximized) {
            return;
        }
        x = &surface->x;
        y = &surface->y;
        w = &surface->w;
        h = &surface->h;
    } else {
        x = &terminal_win_x;
        y = &terminal_win_y;
        w = &terminal_win_w;
        h = &terminal_win_h;
    }

    old_x = *x;
    old_y = *y;
    old_w = *w;
    old_h = *h;

    new_x = drag_origin_win_x;
    new_y = drag_origin_win_y;
    new_w = drag_origin_win_w;
    new_h = drag_origin_win_h;

    if (drag_resize_edges != 0u) {
        const uint64_t origin_right = drag_origin_win_x + drag_origin_win_w;
        const uint64_t origin_bottom = drag_origin_win_y + drag_origin_win_h;

        if ((drag_resize_edges & GUI_WINDOW_RESIZE_LEFT) != 0u) {
            if (cursor_x >= origin_right - GUI_WINDOW_MIN_W) {
                new_x = origin_right - GUI_WINDOW_MIN_W;
                new_w = GUI_WINDOW_MIN_W;
            } else {
                new_x = cursor_x;
                new_w = origin_right - new_x;
            }
        } else if ((drag_resize_edges & GUI_WINDOW_RESIZE_RIGHT) != 0u) {
            new_w = cursor_x > drag_origin_win_x ? cursor_x - drag_origin_win_x : GUI_WINDOW_MIN_W;
        }

        if ((drag_resize_edges & GUI_WINDOW_RESIZE_TOP) != 0u) {
            if (cursor_y >= origin_bottom - GUI_WINDOW_MIN_H) {
                new_y = origin_bottom - GUI_WINDOW_MIN_H;
                new_h = GUI_WINDOW_MIN_H;
            } else {
                new_y = cursor_y;
                new_h = origin_bottom - new_y;
            }
        } else if ((drag_resize_edges & GUI_WINDOW_RESIZE_BOTTOM) != 0u) {
            new_h = cursor_y > drag_origin_win_y ? cursor_y - drag_origin_win_y : GUI_WINDOW_MIN_H;
        }
    } else {
        if (cursor_x >= drag_origin_cursor_x) {
            new_x += cursor_x - drag_origin_cursor_x;
        } else {
            const uint64_t delta = drag_origin_cursor_x - cursor_x;
            new_x = drag_origin_win_x > delta ? drag_origin_win_x - delta : 0u;
        }
        if (cursor_y >= drag_origin_cursor_y) {
            new_y += cursor_y - drag_origin_cursor_y;
        } else {
            const uint64_t delta = drag_origin_cursor_y - cursor_y;
            new_y = drag_origin_win_y > delta ? drag_origin_win_y - delta : 0u;
        }
    }

    clamp_window_bounds(&new_x, &new_y, &new_w, &new_h);
    if (drag_resize_edges != 0u) {
        const uint64_t origin_right = drag_origin_win_x + drag_origin_win_w;
        const uint64_t origin_bottom = drag_origin_win_y + drag_origin_win_h;

        if ((drag_resize_edges & GUI_WINDOW_RESIZE_LEFT) != 0u && new_x < origin_right) {
            new_w = origin_right - new_x;
        }
        if ((drag_resize_edges & GUI_WINDOW_RESIZE_TOP) != 0u && new_y < origin_bottom) {
            new_h = origin_bottom - new_y;
        }
        clamp_window_bounds(&new_x, &new_y, &new_w, &new_h);
    }
    if ((dragged_window == GUI_WINDOW_DRAG_TERMINAL && terminal_maximized) ||
        (dragged_window == GUI_WINDOW_DRAG_BROWSER && browser_window_maximized)) {
        return;
    }
    if (*x == new_x && *y == new_y && *w == new_w && *h == new_h) {
        return;
    }

    {
        if (dragged_window == GUI_WINDOW_DRAG_APP_SURFACE) {
            app_surface_set_bounds(drag_app_surface_id, new_x, new_y, new_w, new_h);
        } else {
            *x = new_x;
            *y = new_y;
            *w = new_w;
            *h = new_h;
        }
    }
    if (dragged_window == GUI_WINDOW_DRAG_TERMINAL) {
        terminal_snapshot_valid = 0u;
    }

    /* Restore only pixels exposed by the old window. For a same-size move,
     * the new window covers the overlap, so repainting the whole old/new union
     * needlessly recalculated almost an entire screen for a one-pixel drag.
     * Foreground layers are still composed normally afterward. */
    {
        const uint64_t old_right = old_x + old_w;
        const uint64_t old_bottom = old_y + old_h;
        const uint64_t new_right = new_x + new_w;
        const uint64_t new_bottom = new_y + new_h;
        drag_repaint_count = 0u;
        if (drag_resize_edges == 0u && old_w == new_w && old_h == new_h) {
            const uint64_t ix0 = old_x > new_x ? old_x : new_x;
            const uint64_t iy0 = old_y > new_y ? old_y : new_y;
            const uint64_t ix1 = old_right < new_right ? old_right : new_right;
            const uint64_t iy1 = old_bottom < new_bottom ? old_bottom : new_bottom;

            if (ix0 >= ix1 || iy0 >= iy1) {
                drag_repaint_add(old_x, old_y, old_w, old_h);
            } else {
                drag_repaint_add(old_x, old_y, old_w, iy0 - old_y);
                drag_repaint_add(old_x, iy1, old_w, old_bottom - iy1);
                drag_repaint_add(old_x, iy0, ix0 - old_x, iy1 - iy0);
                drag_repaint_add(ix1, iy0, old_right - ix1, iy1 - iy0);
            }
        } else {
            const uint64_t damage_x = old_x < new_x ? old_x : new_x;
            const uint64_t damage_y = old_y < new_y ? old_y : new_y;
            const uint64_t damage_right = old_right > new_right ? old_right : new_right;
            const uint64_t damage_bottom = old_bottom > new_bottom ? old_bottom : new_bottom;
            drag_repaint_add(damage_x, damage_y,
                             damage_right - damage_x, damage_bottom - damage_y);
        }
        drag_repaint_active = 1u;
        render_active_app();
        drag_repaint_active = 0u;
        drag_repaint_count = 0u;
    }
}

static void end_window_drag(void) {
    dragged_window = GUI_WINDOW_DRAG_NONE;
    drag_resize_edges = 0u;
    drag_app_surface_id = 0u;
}

/* Windows-style resize feedback: every edge and every corner is active.
 * The glow only appears for the edge under the pointer; the actual hit area
 * is still the full 8px border, so it is easy to grab. */
static void draw_resize_hover(void) {
    uint64_t x;
    uint64_t y;
    uint64_t w;
    uint64_t h;
    const uint32_t color = pack_color(150u, 105u, 183u);

    if (resize_hover_edges == 0u) {
        return;
    }
    if (resize_hover_target == GUI_WINDOW_DRAG_APP_SURFACE) {
        app_surface_t *surface = app_surface_find(resize_hover_app_surface_id);

        if (surface == NULL || !surface->visible) {
            return;
        }
        x = surface->x;
        y = surface->y;
        w = surface->w;
        h = surface->h;
    } else if (resize_hover_target == GUI_WINDOW_DRAG_BROWSER) {
        x = browser_win_x;
        y = browser_win_y;
        w = browser_win_w;
        h = browser_win_h;
    } else if (resize_hover_target == GUI_WINDOW_DRAG_TERMINAL) {
        x = terminal_win_x;
        y = terminal_win_y;
        w = terminal_win_w;
        h = terminal_win_h;
    } else {
        return;
    }

    if ((resize_hover_edges & GUI_WINDOW_RESIZE_LEFT) != 0u) {
        fill_rect(x, y + 5u, 2u, h > 10u ? h - 10u : h, color);
    }
    if ((resize_hover_edges & GUI_WINDOW_RESIZE_RIGHT) != 0u) {
        fill_rect(x + w - 2u, y + 5u, 2u, h > 10u ? h - 10u : h, color);
    }
    if ((resize_hover_edges & GUI_WINDOW_RESIZE_TOP) != 0u) {
        fill_rect(x + 5u, y, w > 10u ? w - 10u : w, 2u, color);
    }
    if ((resize_hover_edges & GUI_WINDOW_RESIZE_BOTTOM) != 0u) {
        fill_rect(x + 5u, y + h - 2u, w > 10u ? w - 10u : w, 2u, color);
    }
    compositor_mark_dirty(x, y, w, h);
}

static gui_window_control_t terminal_control_hit(uint64_t x, uint64_t y) {
    if (terminal_minimized) {
        return GUI_WINDOW_CONTROL_NONE;
    }

    if (window_control_circle_hit_at(x, y, terminal_win_y, terminal_win_x + terminal_win_w - 22u)) {
        return GUI_WINDOW_CONTROL_CLOSE;
    }
    if (window_control_circle_hit_at(x, y, terminal_win_y, terminal_win_x + terminal_win_w - 48u)) {
        return GUI_WINDOW_CONTROL_MINIMIZE;
    }
    if (window_control_circle_hit_at(x, y, terminal_win_y, terminal_win_x + terminal_win_w - 74u)) {
        return GUI_WINDOW_CONTROL_MAXIMIZE;
    }

    return GUI_WINDOW_CONTROL_NONE;
}

static void set_terminal_minimized(uint8_t minimized) {
    if (gui_framebuffer == NULL || terminal_minimized == minimized) {
        return;
    }

    if (minimized) {
        send_app_config(file_manager_pid_value, 0u, 0u);
        send_app_config(editor_pid_value, 0u, 0u);
        send_app_config(terminal_pid_value, 0u, 0u);
        if (active_pid == terminal_pid_value) {
            save_terminal_snapshot();
        }
        terminal_minimized = 1;
        render_active_app();
        draw_taskbar_static();
        return;
    }

    terminal_minimized = 0;
    render_active_app();
    draw_taskbar_static();
}

static void toggle_terminal_maximized(void) {
    if (gui_framebuffer == NULL || terminal_minimized) {
        return;
    }

    if (!terminal_maximized) {
        terminal_restore_x = terminal_win_x;
        terminal_restore_y = terminal_win_y;
        terminal_restore_w = terminal_win_w;
        terminal_restore_h = terminal_win_h;
        terminal_win_x = 0u;
        terminal_win_y = GUI_TASKBAR_HEIGHT;
        terminal_win_w = gui_framebuffer->width;
        terminal_win_h = gui_framebuffer->height > GUI_TASKBAR_HEIGHT
            ? gui_framebuffer->height - GUI_TASKBAR_HEIGHT
            : gui_framebuffer->height;
        terminal_maximized = 1u;
    } else {
        terminal_win_x = terminal_restore_x;
        terminal_win_y = terminal_restore_y;
        terminal_win_w = terminal_restore_w;
        terminal_win_h = terminal_restore_h;
        terminal_maximized = 0u;
    }

    terminal_snapshot_valid = 0u;
    draw_wallpaper();
    render_active_app();
    draw_taskbar_static();
}

static void toggle_browser_window_maximized(void) {
    if (gui_framebuffer == NULL ||
        !((browser_active && !browser_minimized) || (paint_active && !paint_minimized))) {
        return;
    }

    if (!browser_window_maximized) {
        browser_restore_x = browser_win_x;
        browser_restore_y = browser_win_y;
        browser_restore_w = browser_win_w;
        browser_restore_h = browser_win_h;
        browser_win_x = 0u;
        browser_win_y = GUI_TASKBAR_HEIGHT;
        browser_win_w = gui_framebuffer->width;
        browser_win_h = gui_framebuffer->height > GUI_TASKBAR_HEIGHT
            ? gui_framebuffer->height - GUI_TASKBAR_HEIGHT
            : gui_framebuffer->height;
        browser_window_maximized = 1u;
    } else {
        browser_win_x = browser_restore_x;
        browser_win_y = browser_restore_y;
        browser_win_w = browser_restore_w;
        browser_win_h = browser_restore_h;
        clamp_window_bounds(&browser_win_x, &browser_win_y, &browser_win_w, &browser_win_h);
        browser_window_maximized = 0u;
    }

    render_active_app();
    draw_taskbar_static();
}

#include "gui_overlays.inc"

static int cursor_pixel(uint64_t x, uint64_t y, uint32_t *color) {
    if (x < 16u && y < 16u && (cursor_shape[y] & (uint16_t)(0x8000u >> x)) != 0) {
        *color = gui_text_color;
        return 1;
    }

    if (x > 0 && y > 0 &&
        x - 1u < 16u && y - 1u < 16u &&
        (cursor_shape[y - 1u] & (uint16_t)(0x8000u >> (x - 1u))) != 0) {
        *color = gui_cursor_shadow;
        return 1;
    }

    return 0;
}

static uint32_t restored_pixel(uint64_t x, uint64_t y) {
    if (cursor_backing_valid &&
        x >= cursor_backing_x && x < cursor_backing_x + GUI_CURSOR_SIZE &&
        y >= cursor_backing_y && y < cursor_backing_y + GUI_CURSOR_SIZE) {
        const uint64_t bx = x - cursor_backing_x;
        const uint64_t by = y - cursor_backing_y;
        return cursor_backing[by * GUI_CURSOR_SIZE + bx];
    }

    return scene_pixel(x, y);
}

static void compose_mouse_cursor(void) {
    uint32_t new_backing[GUI_CURSOR_SIZE * GUI_CURSOR_SIZE];
    uint64_t min_x = cursor_x;
    uint64_t min_y = cursor_y;
    uint64_t max_x = cursor_x + GUI_CURSOR_SIZE;
    uint64_t max_y = cursor_y + GUI_CURSOR_SIZE;

    if (gui_framebuffer == NULL) {
        return;
    }

    if (cursor_backing_valid) {
        if (cursor_backing_x < min_x) {
            min_x = cursor_backing_x;
        }
        if (cursor_backing_y < min_y) {
            min_y = cursor_backing_y;
        }
        if (cursor_backing_x + GUI_CURSOR_SIZE > max_x) {
            max_x = cursor_backing_x + GUI_CURSOR_SIZE;
        }
        if (cursor_backing_y + GUI_CURSOR_SIZE > max_y) {
            max_y = cursor_backing_y + GUI_CURSOR_SIZE;
        }
    }

    if (max_x > gui_framebuffer->width) {
        max_x = gui_framebuffer->width;
    }
    if (max_y > gui_framebuffer->height) {
        max_y = gui_framebuffer->height;
    }

    for (uint64_t i = 0; i < GUI_CURSOR_SIZE * GUI_CURSOR_SIZE; ++i) {
        new_backing[i] = 0u;
    }

    for (uint64_t y = min_y; y < max_y; ++y) {
        for (uint64_t x = min_x; x < max_x; ++x) {
            const uint32_t base = restored_pixel(x, y);
            uint32_t final = base;

            if (x >= cursor_x && x < cursor_x + GUI_CURSOR_SIZE &&
                y >= cursor_y && y < cursor_y + GUI_CURSOR_SIZE) {
                const uint64_t cx = x - cursor_x;
                const uint64_t cy = y - cursor_y;
                uint32_t cursor_color;

                new_backing[cy * GUI_CURSOR_SIZE + cx] = base;
                if (cursor_pixel(cx, cy, &cursor_color)) {
                    final = cursor_color;
                }
            }

            put_pixel_physical(x, y, final);
        }
    }

    for (uint64_t i = 0; i < GUI_CURSOR_SIZE * GUI_CURSOR_SIZE; ++i) {
        cursor_backing[i] = new_backing[i];
    }
    cursor_backing_x = cursor_x;
    cursor_backing_y = cursor_y;
    cursor_backing_valid = 1;
    cursor_needs_present = 0u;
}

static void compositor_present_cursor(void) {
    restore_mouse_cursor_background();
    compose_mouse_cursor();
}

void gui_flush_scene_and_cursor(void) {
    GUI_GUARD;
    if (gui_compositor_ready && dirty_rect_count != 0u) {
        compositor_present();
    } else {
        compositor_present_cursor();
    }
    if (gui_loop_tid != 0u && scheduler_current_tid() != gui_loop_tid) {
        gui_wake();
    }
}

void gui_compositor_restore_foreground(const vex_rect_t *damage) {
    GUI_GUARD;
    const vex_rect_t *old_clip = gui_paint_clip;
    gui_paint_clip = damage;
    /* Browser content is below app windows, exactly as render_active_app and
     * app_surface_hit specify. Recompose affected foreground before scanout,
     * rather than acknowledging a lower-layer write over a native window. */
    /* A completed Web frame is below native browser status UI as well.
       A late blank/error-page frame must not erase the actual TLS/load error. */
    if(browser_active && !browser_minimized && browser_loading_visible)
        draw_browser_loading_state();
    app_surface_render_damage(damage);
    if(context_menu_visible)draw_context_menu();
    if(spotlight_visible)spotlight_ui_redraw();
    if(app_switcher_visible)draw_app_switcher_overlay();
    draw_elevation_prompt_overlay();
    gui_paint_clip = old_clip;
}

static void gui_lvgl_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixel_map) {
    uint64_t width;
    uint64_t height;
    const lv_color32_t *color_p = (const lv_color32_t *)pixel_map;

    if (gui_framebuffer == NULL || compositor_scene == NULL || area == NULL || color_p == NULL) {
        lv_display_flush_ready(display);
        return;
    }
    if (area->x2 < area->x1 || area->y2 < area->y1) {
        lv_display_flush_ready(display);
        return;
    }

    width = (uint64_t)(area->x2 - area->x1 + 1);
    height = (uint64_t)(area->y2 - area->y1 + 1);
    {
        const uint64_t dst_x = (uint64_t)area->x1;
        const uint64_t copy_cols = dst_x + width > gui_framebuffer->width
            ? gui_framebuffer->width - dst_x : width;
        for (uint64_t y = 0; y < height; ++y) {
            const uint64_t py = (uint64_t)area->y1 + y;
            if (py >= gui_framebuffer->height) {
                break;
            }
            uint32_t *dst = compositor_scene + py * gui_framebuffer->width + dst_x;
            const lv_color32_t *src = color_p + y * width;
            for (uint64_t x = 0u; x < copy_cols; ++x) {
                if (src[x].alpha == LV_OPA_TRANSP) {
                    continue;
                }
                const uint32_t over = pack_color(src[x].red, src[x].green, src[x].blue);
                dst[x] = src[x].alpha == LV_OPA_COVER
                    ? over : blend_color(dst[x], over, src[x].alpha, LV_OPA_COVER);
            }
        }
    }
    compositor_mark_dirty((uint64_t)area->x1, (uint64_t)area->y1, width, height);
    lv_display_flush_ready(display);
}

/* LVGL supplies widget rasterization; Vex owns scene composition. A display
 * refresh redraws sibling window bodies which do not contain app surfaces.
 * Drawing a requested subtree in bounded tiles preserves that ownership and
 * makes LVGL's object stacking independent of native window/overlay stacking.
 * This follows LVGL's offscreen layer lifecycle, without per-paint allocations. */
void vex_lvgl_ui_paint_object(lv_obj_t *obj) {
    GUI_GUARD;
    if (!obj || !gui_lvgl_display || !gui_lvgl_draw_buffer_a ||
        !gui_framebuffer || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    lv_obj_update_layout(obj);
    lv_area_t bounds, screen = {0, 0, (int32_t)gui_framebuffer->width - 1,
                                     (int32_t)gui_framebuffer->height - 1};
    lv_obj_get_coords(obj, &bounds);
    const int32_t ext = lv_obj_get_ext_draw_size(obj);
    lv_area_increase(&bounds, ext, ext);
    if (!lv_area_intersect(&bounds, &bounds, &screen)) return;
    if (gui_paint_clip) {
        const vex_rect_t *c = gui_paint_clip;
        if (c->w <= 0 || c->h <= 0) return;
        int64_t right=(int64_t)c->x+c->w-1,bottom=(int64_t)c->y+c->h-1;
        if(right>screen.x2)right=screen.x2;
        if(bottom>screen.y2)bottom=screen.y2;
        const lv_area_t clip = {c->x,c->y,(int32_t)right,(int32_t)bottom};
        if (!lv_area_intersect(&bounds, &bounds, &clip)) return;
    }
    lv_display_t *display = lv_obj_get_display(obj);
    lv_display_t *old_display = lv_refr_get_disp_refreshing();
    lv_layer_t *old_layer = display->layer_head;
    lv_refr_set_disp_refreshing(display);
    const uint32_t width = (uint32_t)(bounds.x2 - bounds.x1 + 1);
    for (int32_t y = bounds.y1; y <= bounds.y2;) {
        const uint32_t rows = (uint32_t)(bounds.y2-y+1) < GUI_LVGL_DRAW_ROWS
            ? (uint32_t)(bounds.y2-y+1) : GUI_LVGL_DRAW_ROWS;
        lv_draw_buf_t buffer;
        lv_draw_buf_init(&buffer, width, rows, LV_COLOR_FORMAT_ARGB8888,
            width * sizeof(uint32_t), gui_lvgl_draw_buffer_a,
            (uint32_t)(gui_framebuffer->width * GUI_LVGL_DRAW_ROWS * sizeof(uint32_t)));
        lv_draw_buf_clear(&buffer, NULL);
        lv_layer_t layer;
        lv_layer_init(&layer);
        layer.draw_buf = &buffer;
        layer.buf_area = (lv_area_t){bounds.x1,y,bounds.x2,y+(int32_t)rows-1};
        layer._clip_area = layer.phy_clip_area = layer.buf_area;
        layer.color_format = LV_COLOR_FORMAT_ARGB8888;
        display->layer_head = &layer;
        lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_CREATED, &layer);
        lv_obj_redraw(&layer, obj);
        layer.all_tasks_added = true;
        while (layer.draw_task_head) {
            lv_draw_dispatch_wait_for_request();
            lv_draw_dispatch();
        }
        gui_lvgl_flush(display, &layer.buf_area, buffer.data);
        lv_draw_unit_send_event(NULL, LV_EVENT_SCREEN_LOAD_START, &layer);
        lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_DELETED, &layer);
        y += (int32_t)rows;
    }
    display->layer_head = old_layer;
    lv_refr_set_disp_refreshing(old_display);
    /* Vex's explicit damage/layer transactions consumed these widget changes;
     * they must not be replayed later as a whole-screen LVGL refresh. */
    display->inv_p = 0;
}

static void gui_lvgl_pointer_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    data->point.x = cursor_x > INT32_MAX ? INT32_MAX : (int32_t)cursor_x;
    data->point.y = cursor_y > INT32_MAX ? INT32_MAX : (int32_t)cursor_y;
    data->state = (cursor_buttons & 0x01u) != 0u
        ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void compositor_present(void) {
    profiler_span_t span __attribute__((cleanup(profiler_span_end)))=profiler_span_begin(232);
    vex_rect_t present_rects[GUI_DIRTY_RECT_MAX];

    if (gui_framebuffer == NULL || !gui_compositor_ready || compositor_scene == NULL) {
        dirty_rect_count = 0u;
        return;
    }

    if (dirty_rect_count == 0u) {
        return;
    }

#if VEX_DESKTOP_TRACE
    const uint64_t copy_start = sched_clock_ns();
    const int had_cursor = cursor_backing_valid;
#endif
    /* Presentation is synchronous in the framebuffer driver. Keep the old
     * cursor on scanout while preparing/copying the scene, and include the new
     * cursor in that copy. The scene gate excludes painters throughout this
     * short overlay transaction. No backing exposed to an app is modified. */
    if (cursor_backing_valid &&
        (cursor_backing_x != cursor_x || cursor_backing_y != cursor_y))
        compositor_mark_dirty(cursor_backing_x, cursor_backing_y, GUI_CURSOR_SIZE, GUI_CURSOR_SIZE);
    compositor_mark_dirty(cursor_x, cursor_y, GUI_CURSOR_SIZE, GUI_CURSOR_SIZE);
    cursor_overlay_begin(compositor_scene, gui_framebuffer->width, gui_framebuffer->height,
        cursor_x, cursor_y, cursor_shape, gui_text_color, gui_cursor_shadow, cursor_backing);

    for (uint64_t i = 0; i < dirty_rect_count; ++i) {
        const gui_rect_t *rect = &dirty_rects[i];

        present_rects[i].x = rect->x > INT32_MAX ? INT32_MAX : (int32_t)rect->x;
        present_rects[i].y = rect->y > INT32_MAX ? INT32_MAX : (int32_t)rect->y;
        present_rects[i].w = rect->w > INT32_MAX ? INT32_MAX : (int32_t)rect->w;
        present_rects[i].h = rect->h > INT32_MAX ? INT32_MAX : (int32_t)rect->h;
    }

    if (compositor_scene_handle != 0u) {
        (void)gpu_kernel_present(compositor_scene_handle,
                                 present_rects, dirty_rect_count);
    } else {
        gpu_present_scene(compositor_scene, (uint32_t)gui_framebuffer->width, present_rects, dirty_rect_count);
    }
    dirty_rect_count = 0u;
#if VEX_DESKTOP_TRACE
    static uint64_t copies, copy_ns, longest_ns, report_ns, missing;
    const uint64_t finished = sched_clock_ns();
    if (had_cursor) {
        const uint64_t elapsed = finished - copy_start;
        ++copies; copy_ns += elapsed;
        if (elapsed > longest_ns) longest_ns = elapsed;
    }
#endif
    cursor_overlay_end(compositor_scene, gui_framebuffer->width, gui_framebuffer->height,
        cursor_x, cursor_y, cursor_backing);
    cursor_backing_x=cursor_x;cursor_backing_y=cursor_y;
    cursor_backing_valid=1;cursor_needs_present=0;
#if VEX_DESKTOP_TRACE
    for(unsigned y=0;y<GUI_CURSOR_SIZE;++y)for(unsigned x=0;x<GUI_CURSOR_SIZE;++x) {
        uint32_t color;
        if(cursor_x+x<gui_framebuffer->width && cursor_y+y<gui_framebuffer->height && cursor_pixel(x,y,&color)) {
            const uint32_t *row=(const uint32_t *)((const uint8_t *)gui_framebuffer->address+(cursor_y+y)*gui_framebuffer->pitch);
            if(row[cursor_x+x]!=color)++missing;
        }
    }
    if (finished - report_ns > 5000000000ull) {
        char line[192];
        snprintf(line, sizeof(line), "cursor-trace: integrated=1 presents=%u copy-us=%u copy-max-us=%u missing-pixels=%u\n",
                 (unsigned)copies, (unsigned)(copy_ns/1000),
                 (unsigned)(longest_ns/1000),(unsigned)missing);
        debug_log_puts(line); report_ns=finished;
    }
#endif
}

static void clamp_cursor(void) {
    if (gui_framebuffer == NULL) {
        return;
    }

    const uint64_t max_x = gui_framebuffer->width > GUI_CURSOR_SIZE ? gui_framebuffer->width - GUI_CURSOR_SIZE : 0;
    const uint64_t max_y = gui_framebuffer->height > GUI_CURSOR_SIZE ? gui_framebuffer->height - GUI_CURSOR_SIZE : 0;

    if (cursor_x > max_x) {
        cursor_x = max_x;
    }

    if (cursor_y > max_y) {
        cursor_y = max_y;
    }
}

static int poll_mouse_cursor(void) {
    int dx = 0;
    int dy = 0;
    uint8_t buttons = 0;
    int moved = 0;

    /* Drain PS/2 mouse bytes directly in case IRQ12 isn't delivering. */
    mouse_update();

    /* Preserve button edges: draining down+up into one final state loses a
     * complete click before either native widgets or the viewport see it. */
    if (mouse_get_event(&dx, &dy, &buttons)) {
        if (dx < 0 && cursor_x < (uint64_t)(-(int64_t)dx)) {
            cursor_x = 0;
        } else {
            cursor_x = (uint64_t)((int64_t)cursor_x + dx);
        }

        if (dy < 0 && cursor_y < (uint64_t)(-(int64_t)dy)) {
            cursor_y = 0;
        } else {
            cursor_y = (uint64_t)((int64_t)cursor_y + dy);
        }

        clamp_cursor();
        cursor_buttons = buttons;
        if ((cursor_buttons & 0x01u) != 0u && paint_drawing) {
            paint_continue_stroke(cursor_x, cursor_y);
        }
        moved = 1;
    }

    if (!moved) {
        return 0;
    }

    return 1;
}

static void compute_elevation_layout(gui_elevation_layout_t *layout) {
    const uint64_t screen_w  = gui_framebuffer == NULL ? 800u : gui_framebuffer->width;
    /* Figma notification card: 469×138, buttons row 42px at the bottom */
    const uint64_t max_panel = screen_w > 40u ? screen_w - 40u : screen_w;
    const uint64_t panel_w   = max_panel > 469u ? 469u : max_panel;
    const uint64_t panel_h   = 138u;
    const uint64_t margin_x  = 20u;
    const uint64_t margin_t  = 14u;
    const uint64_t button_y  = 96u;
    const uint64_t button_h  = 41u;
    const uint64_t half_w    = panel_w / 2u;

    if (layout == NULL) {
        return;
    }

    memset(layout, 0, sizeof(*layout));
    layout->panel_w = panel_w;
    layout->panel_h = panel_h;
    layout->panel_x = screen_w > panel_w + margin_x ? screen_w - panel_w - margin_x : 0u;
    layout->panel_y = GUI_TASKBAR_HEIGHT + margin_t;

    layout->approve_x = layout->panel_x + 1u;
    layout->approve_y = layout->panel_y + button_y;
    layout->approve_w = half_w > 1u ? half_w - 1u : half_w;
    layout->approve_h = button_h;
    layout->deny_x    = layout->panel_x + half_w;
    layout->deny_y    = layout->approve_y;
    layout->deny_w    = panel_w - half_w - 1u;
    layout->deny_h    = button_h;
}

static int elevation_button_hit(uint64_t x, uint64_t y) {
    gui_elevation_layout_t layout;

    if (!elevation_has_active_prompt()) {
        return 0;
    }

    compute_elevation_layout(&layout);
    if (x >= layout.approve_x && x < layout.approve_x + layout.approve_w &&
        y >= layout.approve_y && y < layout.approve_y + layout.approve_h) {
        return 1;
    }
    if (x >= layout.deny_x && x < layout.deny_x + layout.deny_w &&
        y >= layout.deny_y && y < layout.deny_y + layout.deny_h) {
        return 2;
    }
    return 0;
}

static void draw_elevation_prompt_overlay(void) {
    elevation_prompt_view_t prompt;
    gui_elevation_layout_t layout;
    /* Figma Frame 10 notification card palette (sampled) */
    const uint32_t panel_col    = pack_color(73, 38, 88);    /* glass card */
    const uint32_t divider_col  = pack_color(96, 62, 110);   /* inner dividers */
    const uint32_t icon_bg      = pack_color(62, 35, 86);    /* icon chip */
    const uint32_t icon_edge    = pack_color(205, 99, 244);  /* purple accent */
    const uint32_t header_col   = pack_color(205, 99, 244);  /* "VEX - SYSTEM" */
    const uint32_t title_col    = pack_color(255, 255, 255);
    const uint32_t body_col     = pack_color(204, 204, 204);
    const uint32_t meta_col     = pack_color(150, 138, 160);
    const uint32_t approve_col  = pack_color(123, 69, 232);  /* purple Approve */
    const uint32_t deny_col     = pack_color(140, 140, 140); /* gray Refuse */
    char proc_name[48];
    char meta_text[16];
    uint64_t proc_len;
    uint64_t proc_max_chars;
    uint64_t meta_w;

    if (gui_framebuffer == NULL || elevation_copy_active_prompt(&prompt) != 0) {
        return;
    }

    proc_len = 0u;
    while (prompt.reason[proc_len] != '\0' && prompt.reason[proc_len] != ' ' &&
           proc_len < sizeof(proc_name) - 1u) {
        proc_name[proc_len] = prompt.reason[proc_len];
        ++proc_len;
    }
    proc_name[proc_len] = '\0';

    memcpy(meta_text, "a l'instant", sizeof("a l'instant"));
    meta_w = gui_font_text_width(meta_text, 13u);

    compute_elevation_layout(&layout);
    proc_max_chars = layout.panel_w > (44u + 10u + meta_w)
        ? (layout.panel_w - 44u - 10u - meta_w) / 8u
        : 0u;
    if (proc_max_chars == 0u) {
        proc_name[0] = '\0';
    } else if (proc_len > proc_max_chars) {
        if (proc_max_chars > 3u) {
            proc_name[proc_max_chars - 3u] = '.';
            proc_name[proc_max_chars - 2u] = '.';
            proc_name[proc_max_chars - 1u] = '.';
        }
        proc_name[proc_max_chars] = '\0';
    }
    if (proc_name[0] == '\0') {
        memcpy(proc_name, "Process", sizeof("Process"));
    }

    if (vex_lvgl_ui_ready() && layout.panel_x <= UINT32_MAX &&
        layout.panel_y <= UINT32_MAX && layout.panel_w <= UINT32_MAX &&
        layout.panel_h <= UINT32_MAX) {
        vex_lvgl_ui_draw_elevation((uint32_t)layout.panel_x,
            (uint32_t)layout.panel_y, (uint32_t)layout.panel_w,
            (uint32_t)layout.panel_h, proc_name);
        return;
    }

    /* Flat Figma card */
    fill_rounded_rect(layout.panel_x, layout.panel_y,
                      layout.panel_w, layout.panel_h, 10u, panel_col);

    /* Header: icon chip + "VEX - SYSTEM" + process name */
    fill_rounded_rect(layout.panel_x + 18u, layout.panel_y + 12u,
                      23u, 23u, 6u, icon_bg);
    fill_rect(layout.panel_x + 25u, layout.panel_y + 17u, 9u, 1u, icon_edge);
    fill_rect(layout.panel_x + 29u, layout.panel_y + 17u, 1u, 13u, icon_edge);

    gui_font_text(layout.panel_x + 54u, layout.panel_y + 15u,
                  "VEX - SYSTEM", header_col, 13u);
    gui_font_text(layout.panel_x + layout.panel_w - 10u - meta_w,
                  layout.panel_y + 15u, meta_text, meta_col, 13u);

    /* Divider under header (Figma Line 45) */
    fill_rect(layout.panel_x + 1u, layout.panel_y + 47u,
              layout.panel_w - 2u, 1u, divider_col);

    /* Body: title + reason */
    gui_font_text(layout.panel_x + 21u, layout.panel_y + 53u,
                  "Administrator Access", title_col, 16u);
    gui_font_text(layout.panel_x + 21u, layout.panel_y + 75u,
                  proc_name, body_col, 13u);

    /* Divider above buttons (Figma Line 46) + central split (Line 47) */
    fill_rect(layout.panel_x + 1u, layout.panel_y + 96u,
              layout.panel_w - 2u, 1u, divider_col);
    fill_rect(layout.panel_x + layout.panel_w / 2u, layout.panel_y + 97u,
              1u, 40u, divider_col);

    {
        const uint64_t aw = gui_font_text_width("Approve", 16u);
        const uint64_t dw = gui_font_text_width("Refuse", 16u);
        gui_font_text(layout.approve_x + (layout.approve_w - aw) / 2u,
                      layout.approve_y + 11u, "Approve", approve_col, 16u);
        gui_font_text(layout.deny_x + (layout.deny_w - dw) / 2u,
                      layout.deny_y + 11u, "Refuse", deny_col, 16u);
    }
}

void render_active_app(void) {
    const uint8_t elevation_prompt_visible = elevation_has_active_prompt();

    if (vex_lvgl_ui_ready()) {
        vex_lvgl_ui_begin_scene();
    }

    if (elevation_prompt_visible &&
        !terminal_minimized &&
        active_pid == terminal_pid_value) {
        /* Keep the terminal snapshot in sync while the elevation prompt is
           visible so new output is not erased by restoring an older frame. */
        save_terminal_snapshot();
    }
    /* Full scene compositor: repaint the desktop then draw every visible
     * window back-to-front.  This eliminates ghost pixels entirely — the
     * wallpaper is always the ground truth beneath all windows. */
    draw_wallpaper();

    /* ── Layer 0: terminal / file-manager / editor ── */
    if (!terminal_minimized) {
        draw_main_window_frame(window_title_for_pid(active_pid));
        if (active_pid == terminal_pid_value) {
            send_app_config(terminal_pid_value, 1u, 0u);
            if (vex_lvgl_ui_ready()) {
                vex_lvgl_ui_draw_terminal_content(tty_lvgl_text());
                terminal_lvgl_content_dirty = 0u;
            } else {
                restore_terminal_snapshot();
            }
        } else {
            send_app_config(terminal_pid_value, 0u, 0u);
        }
        send_app_config(file_manager_pid_value,
            active_pid == file_manager_pid_value,
            active_pid == file_manager_pid_value);
        send_app_config(editor_pid_value,
            active_pid == editor_pid_value,
            active_pid == editor_pid_value);
    } else {
        send_app_config(terminal_pid_value, 0u, 0u);
        send_app_config(file_manager_pid_value, 0u, 0u);
        send_app_config(editor_pid_value, 0u, 0u);
    }

    /* ── Layer 1: VexPaint (exclusive with browser) ── */
    if (paint_active && !paint_minimized) {
        draw_window_frame_at(browser_win_x, browser_win_y, browser_win_w, browser_win_h, "VexPaint");
        draw_paint_workspace();
    } else if (browser_active && !browser_minimized) {
        draw_window_frame_at(browser_win_x, browser_win_y, browser_win_w, browser_win_h, NULL);
        draw_browser_shell();
        if (ladybird_browser_results_visible()) {
            draw_browser_native_results();
        } else if (browser_loading_visible) {
            draw_browser_loading_state();
        } else if (browser_native_home_visible) {
            draw_browser_native_home();
        } else if (browser_presented_is_live(gui_browser_active_tab())) {
            const scene_image_t *presented=browser_presented_find(gui_browser_active_tab(), 0);
            const uint32_t width=(uint32_t)gui_content_width();
            const uint32_t height=(uint32_t)gui_content_height();
            const uint64_t x=gui_content_x(),y=gui_content_y();
            /* Recompose the last completed layer, including at a new geometry
             * while the renderer is preparing its resized version. */
            if(presented->pixels && presented->width && presented->height && width && height) {
                if (presented->width == width && presented->height == height &&
                    gui_compositor_ready && compositor_scene != NULL) {
                    const size_t row_bytes = (size_t)width * sizeof(uint32_t);
                    for (uint32_t row = 0u; row < height; ++row) {
                        memcpy(compositor_scene + (size_t)(y + row) * gui_framebuffer->width + x,
                               presented->pixels + (size_t)row * width, row_bytes);
                    }
                } else {
                    for(uint32_t row=0;row<height;++row)
                        for(uint32_t col=0;col<width;++col)
                            scene_write(x+col,y+row,presented->pixels[
                                (size_t)((uint64_t)row*presented->height/height)*presented->width+
                                (uint32_t)((uint64_t)col*presented->width/width)]);
                }
                compositor_mark_dirty(x,y,width,height);
            }
        }
    }

    app_surface_render_all();

    /* Window affordances belong below desktop overlays, just like chrome. */
    draw_resize_hover();
    if (context_menu_visible) {
        draw_context_menu();
    }

    if (elevation_prompt_visible) {
        draw_elevation_prompt_overlay();
    }
}

void switch_to_pid(uint32_t pid) {
    if (pid == 0u) {
        return;
    }

    if (!terminal_minimized && active_pid == terminal_pid_value) {
        save_terminal_snapshot();
    }

    if (browser_active) {
        browser_minimized = 1u;
        draw_wallpaper_rect(browser_win_x, browser_win_y, browser_win_w, browser_win_h);
    }
    if (paint_active) {
        paint_minimized = 1u;
        paint_drawing = 0u;
        draw_wallpaper_rect(browser_win_x, browser_win_y, browser_win_w, browser_win_h);
    }

    if (pid == file_manager_pid_value) {
        file_manager_opened = 1u;
    } else if (pid == editor_pid_value) {
        editor_opened = 1u;
    }

    active_pid = pid;
    terminal_minimized = 0;
    render_active_app();
    draw_taskbar_static();
}

void send_app_config(uint32_t pid, uint8_t active, uint8_t refresh) {
    gui_app_command_t command;

    if (pid == 0u) {
        return;
    }

    (void)refresh;
    command.type = GUI_APP_CMD_CONFIGURE;
    command.flags = active;
    command.x = terminal_win_x + 8u;
    command.y = terminal_win_y + GUI_TITLE_HEIGHT + 6u;
    command.w = terminal_win_w - 16u;
    command.h = terminal_win_h - GUI_TITLE_HEIGHT - 12u;
    command.key = 0;
    command.path[0] = '\0';

    if (pid == file_manager_pid_value) {
        file_manager_app_send(&command);
    } else if (pid == editor_pid_value) {
        text_editor_app_send(&command);
    } else if (pid == terminal_pid_value) {
        shell_app_send(&command);
    }
}

static void send_key_to_active_app(char key) {
    gui_app_command_t command;

    if (terminal_minimized || key == 0 || active_pid == 0u) {
        return;
    }

    command.type = GUI_APP_CMD_KEY;
    command.flags = 0u;
    command.x = 0u;
    command.y = 0u;
    command.w = 0u;
    command.h = 0u;
    command.key = key;
    command.path[0] = '\0';

    if (active_pid == terminal_pid_value) {
        shell_app_send(&command);
    } else if (active_pid == editor_pid_value) {
        text_editor_app_send(&command);
    }
}

static const char *window_title_for_pid(uint32_t pid) {
    if (pid == file_manager_pid_value) {
        return "GFM";
    }
    if (pid == editor_pid_value) {
        return "Editor";
    }

    return "Terminal";
}

void gui_init(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count == 0) {
        return;
    }

    gui_framebuffer = framebuffer_request.response->framebuffers[0];
    lv_init();
    gui_lvgl_display = lv_display_create((int32_t)gui_framebuffer->width, (int32_t)gui_framebuffer->height);
    gui_lvgl_draw_buffer_a = kmalloc((size_t)gui_framebuffer->width * GUI_LVGL_DRAW_ROWS * sizeof(uint32_t));
    gui_lvgl_draw_buffer_b = kmalloc((size_t)gui_framebuffer->width * GUI_LVGL_DRAW_ROWS * sizeof(uint32_t));
    if (gui_lvgl_display != NULL && gui_lvgl_draw_buffer_a != NULL && gui_lvgl_draw_buffer_b != NULL) {
        lv_display_set_color_format(gui_lvgl_display, LV_COLOR_FORMAT_ARGB8888);
        lv_display_set_buffers(gui_lvgl_display,
                               gui_lvgl_draw_buffer_a, gui_lvgl_draw_buffer_b,
                               (uint32_t)(gui_framebuffer->width * GUI_LVGL_DRAW_ROWS * sizeof(uint32_t)),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(gui_lvgl_display, gui_lvgl_flush);
        /* Keep the timer object for lv_refr_now(), but prevent periodic full
           refreshes: VexOS drives composition from dirty-region events. */
        lv_timer_pause(lv_display_get_refr_timer(gui_lvgl_display));
        gui_lvgl_pointer = lv_indev_create();
        if (gui_lvgl_pointer != NULL) {
            lv_indev_set_type(gui_lvgl_pointer, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(gui_lvgl_pointer, gui_lvgl_display);
            lv_indev_set_read_cb(gui_lvgl_pointer, gui_lvgl_pointer_read);
            lv_indev_set_mode(gui_lvgl_pointer, LV_INDEV_MODE_EVENT);
        }
    }
    gpu_init(gui_framebuffer);
    compositor_surface_system_init();
    compositor_scene_pixels = 0u;
    compositor_scene = NULL;
    compositor_scene_handle = 0u;
    gui_compositor_ready = 0u;
    dirty_rect_count = 0u;

    const uint32_t text     = pack_color(230, 236, 248);
    const uint32_t shadow   = pack_color(4, 4, 8);

    gui_text_color  = text;
    gui_cursor_shadow = shadow;

    compositor_init_scene();
    draw_wallpaper();
    compositor_mark_dirty(0u, 0u, gui_framebuffer->width, gui_framebuffer->height);

    {
        const uint64_t avail_h = gui_framebuffer->height > GUI_TASKBAR_HEIGHT + GUI_WINDOW_MARGIN * 2u
            ? gui_framebuffer->height - GUI_TASKBAR_HEIGHT - GUI_WINDOW_MARGIN * 2u
            : gui_framebuffer->height;
        const uint64_t half_w = gui_framebuffer->width > GUI_WINDOW_MARGIN * 3u
            ? (gui_framebuffer->width - GUI_WINDOW_MARGIN * 3u) / 2u
            : gui_framebuffer->width / 2u;
        terminal_win_x = GUI_WINDOW_MARGIN;
        terminal_win_y = GUI_TASKBAR_HEIGHT + GUI_WINDOW_MARGIN;
        terminal_win_w = half_w;
        terminal_win_h = avail_h;
        browser_win_x  = GUI_WINDOW_MARGIN + half_w + GUI_WINDOW_MARGIN;
        browser_win_y  = terminal_win_y;
        browser_win_w  = gui_framebuffer->width > browser_win_x + GUI_WINDOW_MARGIN
            ? gui_framebuffer->width - browser_win_x - GUI_WINDOW_MARGIN
            : half_w;
        browser_win_h  = avail_h;
    }

    tty_set_region(
        terminal_win_x + 8u,
        terminal_win_y + GUI_TITLE_HEIGHT + 6u,
        terminal_win_w - 16u,
        terminal_win_h - GUI_TITLE_HEIGHT - 12u
    );
    tty_set_colors_rgb(255, 255, 255, 20, 20, 20);
    tty_clear();

    file_manager_pid_value = 0u;
    editor_pid_value = 0u;
    spotlight_pid_value = 0u;
    active_pid = 0u;
    terminal_pid_value = 0u;
    file_manager_opened = 0u;
    editor_opened = 0u;
    browser_previous_paint = 0u;
    editor_has_document = 0u;
    spotlight_visible = 0u;
    browser_active = 0u;
    browser_previous_pid = 0u;
    browser_minimized = 0u;
    browser_window_maximized = 0u;
    browser_restore_x = browser_win_x;
    browser_restore_y = browser_win_y;
    browser_restore_w = browser_win_w;
    browser_restore_h = browser_win_h;
    browser_address_focused = 0u;
    browser_native_home_visible = 0u;
    browser_tab_count = 1u;
    browser_loading_visible = 0u;
    browser_loading_text[0] = '\0';
    browser_shell_set_address("vex://home");
    paint_active = 0u;
    paint_minimized = 0u;
    paint_drawing = 0u;
    paint_eraser = 0u;
    paint_previous_browser = 0u;
    paint_previous_pid = 0u;
    paint_canvas = NULL;
    paint_canvas_width = 0u;
    paint_canvas_height = 0u;
    paint_canvas_pixels = 0u;
    paint_color = pack_color(18, 18, 22);
    paint_brush_radius = 4u;
    app_switcher_visible = 0u;
    app_switcher_count = 0u;
    app_switcher_selected = 0u;
    app_switcher_anim_step = 0u;
    app_switcher_deadline_tick = 0u;
    app_switcher_anim_start_tick = 0u;
    /* Keep the kernel shell service alive, but leave its window closed on the
     * normal desktop until the user explicitly opens Terminal. */
    terminal_minimized = 1u;
    terminal_maximized = 0u;
    terminal_restore_x = terminal_win_x;
    terminal_restore_y = terminal_win_y;
    terminal_restore_w = terminal_win_w;
    terminal_restore_h = terminal_win_h;
    terminal_snapshot_valid = 0;
    terminal_snapshot_pixels = (terminal_win_w > 16u ? terminal_win_w - 16u : 0u) *
        (terminal_win_h > GUI_TITLE_HEIGHT + 12u ? terminal_win_h - GUI_TITLE_HEIGHT - 12u : 0u);
    terminal_snapshot = kmalloc(sizeof(uint32_t) * terminal_snapshot_pixels);
    if (terminal_snapshot != NULL) {
        save_terminal_snapshot();
    }

    (void)vex_lvgl_ui_init((uint32_t)gui_framebuffer->width,
                           (uint32_t)gui_framebuffer->height);
    tty_set_bitmap_render_enabled(vex_lvgl_ui_ready() ? 0 : 1);
    draw_taskbar_static();
    cursor_x = gui_framebuffer->width / 2u;
    cursor_y = gui_framebuffer->height / 2u;
    cursor_backing_valid = 0;
    cursor_needs_present = 1u;
    cursor_buttons = 0;
    previous_cursor_buttons = 0;
    context_menu_visible = 0;
    last_clock_ticks = 0;
    last_clock_second = rtc_seconds();
    if (gui_compositor_ready) {
        compositor_present();
    } else {
        compose_mouse_cursor();
    }
}

void gui_set_terminal_pid(uint32_t pid) {
    terminal_pid_value = pid;
    /* PID registration is service setup, not a request to show a window.
     * gui_focus_terminal_for_launch() opens the same shell on user demand. */
}

/* Double-click on a desktop icon (Figma Frame 15) opens it:
   folder → file manager at that path, file → text editor. */
static uint64_t desktop_icon_click_tick;
static int desktop_icon_click_index = -1;

static int handle_desktop_icon_click(uint64_t x, uint64_t y) {
    if (gui_framebuffer == NULL || y < GUI_TASKBAR_HEIGHT) {
        return 0;
    }
    /* Ignore clicks that land on a visible window */
    if (!terminal_minimized &&
        x >= terminal_win_x && x < terminal_win_x + terminal_win_w &&
        y >= terminal_win_y && y < terminal_win_y + terminal_win_h) {
        return 0;
    }
    if (((browser_active && !browser_minimized) || (paint_active && !paint_minimized)) &&
        x >= browser_win_x && x < browser_win_x + browser_win_w &&
        y >= browser_win_y && y < browser_win_y + browser_win_h) {
        return 0;
    }

    for (uint64_t i = 0u; i < desktop_icon_count; ++i) {
        const uint64_t col = i % 3u;
        const uint64_t row = i / 3u;
        const uint64_t cx = 52u + col * 109u;
        const uint64_t cy = GUI_TASKBAR_HEIGHT + 38u + row * 115u;

        if (x >= cx && x < cx + 81u && y >= cy && y < cy + 86u) {
            const uint64_t now = irq_timer_ticks();
            if (desktop_icon_click_index == (int)i &&
                now - desktop_icon_click_tick <= 45u) {   /* ~450 ms */
                desktop_icon_click_index = -1;
                if (desktop_icons[i].is_dir) {
                    gui_open_file_manager_path(desktop_icons[i].path);
                } else {
                    gui_open_path(desktop_icons[i].path);
                }
            } else {
                desktop_icon_click_index = (int)i;
                desktop_icon_click_tick = now;
            }
            return 1;
        }
    }
    return 0;
}

/* ── Hover machinery ─────────────────────────────────────────── */
static int wctrl_hover = -1;        /* 0 close, 1 min, 2 max */
static uint8_t wctrl_hover_win;     /* 0 = terminal frame, 1 = browser/paint frame */

static void redraw_window_controls(uint64_t x, uint64_t y, uint64_t w, int hover) {
    const uint64_t cy = y + GUI_TITLE_HEIGHT / 2u;
    if (w < 100u) {
        return;
    }
    fill_rect(x + w - 90u, y + 2u, 78u, GUI_TITLE_HEIGHT - 3u, pack_color(20u, 20u, 20u));
    fill_circle(x + w - 22u, cy, hover == 0 ? 9u : GUI_WINDOW_CONTROL_RADIUS, pack_color(255u,   0u,   0u));
    fill_circle(x + w - 48u, cy, hover == 1 ? 9u : GUI_WINDOW_CONTROL_RADIUS, pack_color(232u, 224u,   2u));
    fill_circle(x + w - 74u, cy, hover == 2 ? 9u : GUI_WINDOW_CONTROL_RADIUS, pack_color( 68u, 255u,   0u));
    compositor_mark_dirty(x + w - 90u, y + 2u, 78u, GUI_TITLE_HEIGHT);
}

static int desktop_rect_free(uint64_t rx, uint64_t ry, uint64_t rw, uint64_t rh) {
    if(context_menu_visible &&
        rx<context_menu_x+GUI_CONTEXT_MENU_WIDTH && rx+rw>context_menu_x &&
        ry<context_menu_y+GUI_CONTEXT_MENU_HEIGHT && ry+rh>context_menu_y)return 0;
    const vex_rect_t rect={(int32_t)rx,(int32_t)ry,(int32_t)rw,(int32_t)rh};
    if(app_surface_intersects_rect(&rect))return 0;
    if (!terminal_minimized &&
        rx < terminal_win_x + terminal_win_w && rx + rw > terminal_win_x &&
        ry < terminal_win_y + terminal_win_h && ry + rh > terminal_win_y) {
        return 0;
    }
    if (((browser_active && !browser_minimized) || (paint_active && !paint_minimized)) &&
        rx < browser_win_x + browser_win_w && rx + rw > browser_win_x &&
        ry < browser_win_y + browser_win_h && ry + rh > browser_win_y) {
        return 0;
    }
    return 1;
}

static void redraw_desktop_cell(int i) {
    if (i < 0 || (uint64_t)i >= desktop_icon_count) {
        return;
    }
    {
        const uint64_t col = (uint64_t)i % 3u;
        const uint64_t row = (uint64_t)i / 3u;
        const uint64_t cx = 52u + col * 109u;
        const uint64_t cy = GUI_TASKBAR_HEIGHT + 38u + row * 115u;
        const uint64_t rx = cx > 16u ? cx - 16u : 0u;
        const uint64_t ry = cy > 8u ? cy - 8u : 0u;
        if (!desktop_rect_free(rx, ry, 116u, 112u)) {
            return;
        }
        draw_wallpaper_rect(rx, ry, 116u, 112u);
        draw_desktop_icon_cell((uint64_t)i);
    }
}

static void gui_update_hovers(void) {
    /* Taskbar chips → translucent highlight */
    const int th = taskbar_hover_at(cursor_x, cursor_y);
    if (th != taskbar_hover) {
        taskbar_hover = th;
        draw_taskbar_static();
    }

    /* Resize affordance follows the topmost window under the mouse. */
    {
        gui_window_drag_target_t target = GUI_WINDOW_DRAG_NONE;
        uint32_t app_id = 0u;
        uint8_t edges = 0u;
        app_surface_t *surface = app_surface_hit(cursor_x, cursor_y);

        if (surface != NULL) {
            edges = app_surface_resize_edges_at(surface, cursor_x, cursor_y);
            if (edges != 0u) {
                target = GUI_WINDOW_DRAG_APP_SURFACE;
                app_id = surface->window_id;
            }
        } else if ((browser_active && !browser_minimized) || (paint_active && !paint_minimized)) {
            if (!browser_window_maximized) {
                edges = window_resize_edges_at(cursor_x, cursor_y,
                                               browser_win_x, browser_win_y, browser_win_w, browser_win_h);
                if (edges != 0u) {
                    target = GUI_WINDOW_DRAG_BROWSER;
                }
            }
        } else if (!terminal_minimized && !terminal_maximized) {
            edges = window_resize_edges_at(cursor_x, cursor_y,
                                           terminal_win_x, terminal_win_y, terminal_win_w, terminal_win_h);
            if (edges != 0u) {
                target = GUI_WINDOW_DRAG_TERMINAL;
            }
        }

        if (target != resize_hover_target || app_id != resize_hover_app_surface_id ||
            edges != resize_hover_edges) {
            resize_hover_target = target;
            resize_hover_app_surface_id = app_id;
            resize_hover_edges = edges;
            render_active_app();
            draw_taskbar_static();
            gui_flush_scene_and_cursor();
        }
    }

    /* Window traffic lights → zoom */
    {
        gui_window_control_t hit;
        uint8_t win = 0u;
        int hc;

        hit = browser_control_hit(cursor_x, cursor_y);
        if (hit != GUI_WINDOW_CONTROL_NONE) {
            win = 1u;
        } else {
            hit = paint_control_hit(cursor_x, cursor_y);
            if (hit != GUI_WINDOW_CONTROL_NONE) {
                win = 1u;
            } else {
                hit = terminal_control_hit(cursor_x, cursor_y);
                win = 0u;
            }
        }
        hc = hit == GUI_WINDOW_CONTROL_CLOSE ? 0
           : hit == GUI_WINDOW_CONTROL_MINIMIZE ? 1
           : hit == GUI_WINDOW_CONTROL_MAXIMIZE ? 2 : -1;

        if (hc != wctrl_hover || (hc >= 0 && win != wctrl_hover_win)) {
            if (wctrl_hover >= 0) {
                if (wctrl_hover_win == 0u && !terminal_minimized) {
                    redraw_window_controls(terminal_win_x, terminal_win_y, terminal_win_w, -1);
                } else if (wctrl_hover_win == 1u &&
                           ((browser_active && !browser_minimized) || (paint_active && !paint_minimized))) {
                    redraw_window_controls(browser_win_x, browser_win_y, browser_win_w, -1);
                }
            }
            wctrl_hover = hc;
            wctrl_hover_win = win;
            if (hc >= 0) {
                if (win == 0u && !terminal_minimized) {
                    redraw_window_controls(terminal_win_x, terminal_win_y, terminal_win_w, hc);
                } else if (win == 1u) {
                    redraw_window_controls(browser_win_x, browser_win_y, browser_win_w, hc);
                }
            }
        }
    }

    /* Desktop icons: stable-size hover plate (no zoom). */
    {
        int dh = -1;
        if (cursor_y >= GUI_TASKBAR_HEIGHT) {
            for (uint64_t i = 0u; i < desktop_icon_count; ++i) {
                const uint64_t col = i % 3u;
                const uint64_t row = i / 3u;
                const uint64_t cx = 52u + col * 109u;
                const uint64_t cy = GUI_TASKBAR_HEIGHT + 38u + row * 115u;
                if (cursor_x >= cx && cursor_x < cx + 81u &&
                    cursor_y >= cy && cursor_y < cy + 86u) {
                    dh = (int)i;
                    break;
                }
            }
            if (dh >= 0 && !desktop_rect_free(cursor_x, cursor_y, 1u, 1u)) {
                dh = -1;
            }
        }
        if (dh != desktop_hover) {
            const int old = desktop_hover;
            desktop_hover = dh;
            redraw_desktop_cell(old);
            redraw_desktop_cell(dh);
        }
    }

    /* Paint toolbar → rectangular highlight (handled by the paint module) */
    if (paint_active && !paint_minimized) {
        paint_handle_hover(cursor_x, cursor_y);
    }
}

void gui_tick(void) {
    GUI_GUARD;
    static uint8_t wm_consumed_left;
    const int moved = poll_mouse_cursor();
    const uint8_t pressed = (uint8_t)(cursor_buttons & (uint8_t)~previous_cursor_buttons);
    const uint8_t released = (uint8_t)(previous_cursor_buttons & (uint8_t)~cursor_buttons);
    const uint64_t now_ticks = irq_timer_ticks();
    gui_browser_poll_lifecycle();
    uint8_t elevation_modal_active = 0u;
    char gui_key = 0;
    uint8_t gui_shortcut = 0u;
    gui_app_event_t app_event;
    if (terminal_lvgl_content_dirty != 0u && vex_lvgl_ui_ready() &&
        !terminal_minimized && active_pid == terminal_pid_value) {
        /* Only invalidate the text label. Rebuilding the desktop/window scene
           for every keystroke both wastes work and replays stale pixel content. */
        vex_lvgl_ui_draw_terminal_content(tty_lvgl_text());
        terminal_lvgl_content_dirty = 0u;
        /* The label is a lower layer. Its refresh must preserve native app
         * windows covering it, just like a browser Surface presentation. */
        const vex_rect_t damage = {(int32_t)(terminal_win_x + 8u),
            (int32_t)(terminal_win_y + GUI_TITLE_HEIGHT + 6u),
            (int32_t)(terminal_win_w - 16u),
            (int32_t)(terminal_win_h - GUI_TITLE_HEIGHT - 12u)};
        gui_compositor_restore_foreground(&damage);
        gui_flush_scene_and_cursor();
    }
    if (gui_lvgl_pointer != NULL && (moved || pressed != 0u || released != 0u)) {
        lv_indev_read(gui_lvgl_pointer);
    }
    ladybird_display_pump();
    if (browser_active && !browser_minimized && ladybird_browser_results_visible() && !browser_results_drawn) {
        browser_results_drawn = 1u;
        render_active_app();
        gui_flush_scene_and_cursor();
    }

    /* Window creation may run on the launcher syscall stack. Keyboard ownership
     * belongs to this GUI dispatcher, never to the calling application. */
    if(browser_active && !browser_minimized && browser_window_focused)keyboard_focus_capture_current();
    /* A pointer edge changes native/DOM focus later in this tick. Keep the
       scancode queued until that edge has been dispatched, rather than send
       the first typed key ahead of the click to the previous focus target. */
    if(!pressed && !released)gui_key = keyboard_pollchar_kind(&gui_shortcut);

    elevation_tick();
    {
        elevation_prompt_view_t prompt;
        uint8_t prompt_visible = 0u;
        uint32_t prompt_request = 0u;
        uint64_t prompt_bucket = 0u;

        if (elevation_copy_active_prompt(&prompt) == 0) {
            const uint64_t remaining = prompt.expires_ticks > now_ticks ? prompt.expires_ticks - now_ticks : 0u;
            prompt_visible = 1u;
            prompt_request = prompt.request_id;
            prompt_bucket = remaining / GUI_FRAME_INTERVAL_TICKS;
        }

        if (prompt_visible != elevation_prompt_visible_cached ||
            (prompt_visible != 0u &&
             (prompt_request != elevation_prompt_request_cached ||
              prompt_bucket != elevation_prompt_timer_bucket_cached))) {
            render_active_app();
            draw_taskbar_static();
            gui_flush_scene_and_cursor();
        }

        elevation_prompt_visible_cached = prompt_visible;
        elevation_prompt_request_cached = prompt_request;
        elevation_prompt_timer_bucket_cached = prompt_bucket;
        elevation_modal_active = prompt_visible;
    }

    if (elevation_modal_active) {
        if (spotlight_visible) {
            spotlight_ui_close();
            spotlight_visible = (uint8_t)spotlight_ui_is_open();
        }
        if (context_menu_visible) {
            hide_context_menu();
        }
    }

    if (app_switcher_visible) {
        browser_viewport_blur();
        const uint8_t anim_step = now_ticks > app_switcher_anim_start_tick
            ? (uint8_t)((now_ticks - app_switcher_anim_start_tick) >= GUI_APP_SWITCHER_ANIM_TICKS
                ? GUI_APP_SWITCHER_ANIM_TICKS
                : (now_ticks - app_switcher_anim_start_tick))
            : 0u;
        if (anim_step != app_switcher_anim_step) {
            app_switcher_anim_step = anim_step;
            render_active_app();
            draw_taskbar_static();
            gui_flush_scene_and_cursor();
        }
        if (pressed != 0u || (gui_key != 0 &&
            (!gui_shortcut || (gui_key != KEYBOARD_KEY_APP_SWITCH_NEXT &&
            gui_key != KEYBOARD_KEY_APP_SWITCH_PREV)))) {
            app_switcher_commit();
            previous_cursor_buttons = cursor_buttons;
            return;
        }
        if (now_ticks >= app_switcher_deadline_tick) {
            app_switcher_commit();
            previous_cursor_buttons = cursor_buttons;
            return;
        }
    }

    if(gui_shortcut && (gui_key==KEYBOARD_KEY_CTRL_TAB || gui_key==KEYBOARD_KEY_CTRL_SHIFT_TAB)){
        if(current_switch_target()==GUI_SWITCH_TARGET_BROWSER){
            gui_browser_cycle_tab(gui_key==KEYBOARD_KEY_CTRL_SHIFT_TAB);
            previous_cursor_buttons=cursor_buttons;return;
        }
        if(app_surface_has_raw_keyboard_focus()){
            gui_key=0;gui_shortcut=0; /* deliver physical edges to the app below */
        }else gui_key=gui_key==KEYBOARD_KEY_CTRL_TAB?KEYBOARD_KEY_APP_SWITCH_NEXT:KEYBOARD_KEY_APP_SWITCH_PREV;
    }
    if (gui_shortcut && gui_key == KEYBOARD_KEY_APP_SWITCH_NEXT) {
        browser_viewport_blur();
        app_switcher_cycle(0);
        previous_cursor_buttons = cursor_buttons;
        return;
    }
    if (gui_shortcut && gui_key == KEYBOARD_KEY_APP_SWITCH_PREV) {
        browser_viewport_blur();
        app_switcher_cycle(1);
        previous_cursor_buttons = cursor_buttons;
        return;
    }
    if (!(gui_shortcut && (gui_key == KEYBOARD_KEY_SUPER || gui_key == KEYBOARD_KEY_CTRL_SPACE)) &&
        !spotlight_visible &&
        !(browser_active && browser_address_focused) &&
        !app_surface_has_keyboard_focus() &&
        terminal_minimized) {
        gui_key = 0;
    }

    while (file_manager_app_poll_event(&app_event)) {
        if (app_event.type == GUI_APP_EVENT_OPEN_FILE) {
            gui_open_editor_path(app_event.path);
            restore_mouse_cursor_background();
        }
    }

    if ((cursor_buttons & 0x01u) == 0u) {
        if ((released & 0x01u) != 0u && dragged_window == GUI_WINDOW_DRAG_NONE) {
            (void)app_surface_send_mouse_to_focused(cursor_x, cursor_y, VEX_APP_MOUSE_LEFT_UP);
        }
        if (paint_drawing) {
            paint_stop_stroke();
        }
        if (dragged_window != GUI_WINDOW_DRAG_NONE) {
            end_window_drag();
        }
    } else if (moved && paint_drawing) {
        paint_continue_stroke(cursor_x, cursor_y);
    }

    if ((cursor_buttons & 0x01u) != 0u && moved && dragged_window != GUI_WINDOW_DRAG_NONE) {
        update_window_drag();
    }
    if ((cursor_buttons & 0x01u) != 0u && moved && dragged_window == GUI_WINDOW_DRAG_NONE &&
        !spotlight_visible && !elevation_modal_active) {
        (void)app_surface_send_mouse_to_focused(cursor_x, cursor_y,
            VEX_APP_MOUSE_LEFT_DOWN | VEX_APP_MOUSE_MOVE | VEX_APP_MOUSE_DRAG);
    }

    if (moved && gui_framebuffer != NULL && !spotlight_visible && !elevation_modal_active &&
        dragged_window == GUI_WINDOW_DRAG_NONE && (cursor_buttons & 0x01u) == 0u) {
        gui_update_hovers();
    }

    if ((pressed & 0x01u) != 0 && gui_framebuffer != NULL) {
        if(!(browser_active && cursor_x>=gui_content_x() && cursor_y>=gui_content_y() &&
            cursor_x<gui_content_x()+gui_content_width() && cursor_y<gui_content_y()+gui_content_height()))browser_viewport_blur();
        const int logo_hit = cursor_y < GUI_TASKBAR_HEIGHT && cursor_x >= 8u && cursor_x < 80u;
        const int menu_item = context_menu_item_at(cursor_x, cursor_y);
        if(menu_item>=0)wm_consumed_left=1; /* Overlay owns the click before any underlying surface. */
        const int elevation_hit = elevation_button_hit(cursor_x, cursor_y);
        app_surface_t *app_hit = app_surface_hit(cursor_x, cursor_y);
        uint8_t browser_hit_focus=browser_active && !browser_minimized && !app_hit &&
            cursor_x>=browser_win_x && cursor_x<browser_win_x+browser_win_w &&
            cursor_y>=browser_win_y && cursor_y<browser_win_y+browser_win_h;
        if(browser_window_focused && !browser_hit_focus){browser_viewport_blur();browser_address_focused=0;keyboard_focus_release();}
        browser_window_focused=browser_hit_focus;
        if(browser_window_focused)keyboard_focus_capture_current();
        const uint32_t app_control_hit = app_surface_control_at(app_hit, cursor_x, cursor_y);
        const uint8_t app_resize_edges = app_surface_resize_edges_at(app_hit, cursor_x, cursor_y);
        const gui_window_control_t browser_hit = browser_control_hit(cursor_x, cursor_y);
        const gui_window_control_t paint_hit = paint_control_hit(cursor_x, cursor_y);
        const uint8_t front_resize_edges = ((browser_active && !browser_minimized) || (paint_active && !paint_minimized))
            ? window_resize_edges_at(cursor_x, cursor_y, browser_win_x, browser_win_y, browser_win_w, browser_win_h)
            : 0u;
        const uint8_t terminal_resize_edges = !terminal_minimized
            ? window_resize_edges_at(cursor_x, cursor_y, terminal_win_x, terminal_win_y, terminal_win_w, terminal_win_h)
            : 0u;
        gui_window_control_t control_hit = GUI_WINDOW_CONTROL_NONE;

        if (browser_hit != GUI_WINDOW_CONTROL_NONE) {
            control_hit = browser_hit;
        } else if (paint_hit != GUI_WINDOW_CONTROL_NONE) {
            control_hit = paint_hit;
        } else {
            control_hit = terminal_control_hit(cursor_x, cursor_y);
        }

        if (logo_hit || control_hit != GUI_WINDOW_CONTROL_NONE ||
            app_control_hit != APP_SURFACE_CONTROL_NONE ||
            menu_item >= 0 || elevation_hit != 0) {
            restore_mouse_cursor_background();
        }
        if (context_menu_visible) {
            hide_context_menu();
        }

        if (elevation_modal_active) {
            if (elevation_hit == 1) {
                (void)elevation_approve_active();
            } else if (elevation_hit == 2) {
                (void)elevation_deny_active();
            }
        } else if (elevation_hit == 1) {
            (void)elevation_approve_active();
        } else if (elevation_hit == 2) {
            (void)elevation_deny_active();
        } else if (spotlight_visible) {
            spotlight_ui_handle_click(cursor_x, cursor_y);
            spotlight_visible = (uint8_t)spotlight_ui_is_open();
        } else if (logo_hit) {
            spotlight_ui_toggle();
            spotlight_visible = (uint8_t)spotlight_ui_is_open();
        } else if (menu_item == 0) {
            gui_focus_terminal_for_launch();
        } else if (menu_item == 1) {
            gui_open_file_manager();
        } else if (menu_item == 2) {
            if (browser_active) {
                set_browser_minimized(0u);
            } else {
                gui_browser_launch();
            }
        } else if (menu_item == 3) {
            uint32_t pid=0;
            if (elf_spawn_pid("/usr/bin/kyil-paint",&pid) != 0 || !pid) {
                tty_puts("paint launch failed\n");
            } else scheduler_detach_process(pid);
        } else if (menu_item == 4) {
            gui_open_editor();
        } else if(menu_item==5 || menu_item==6) {
            if(power_request(menu_item==5?POWER_OFF:POWER_REBOOT))tty_puts("power: transition unavailable\n");
        } else if (app_control_hit == APP_SURFACE_CONTROL_CLOSE) {
            wm_consumed_left=1;
            app_surface_focus(app_hit->window_id);
            app_surface_request_close(app_hit->window_id);
        } else if (app_control_hit == APP_SURFACE_CONTROL_MINIMIZE) {
            wm_consumed_left=1;
            app_surface_focus(app_hit->window_id);
            app_surface_toggle_minimized(app_hit->window_id);
        } else if (app_control_hit == APP_SURFACE_CONTROL_MAXIMIZE) {
            wm_consumed_left=1;
            app_surface_focus(app_hit->window_id);
            app_surface_toggle_maximized(app_hit->window_id);
        } else if (app_resize_edges != 0u) {
            app_surface_focus(app_hit->window_id);
            begin_window_drag(GUI_WINDOW_DRAG_APP_SURFACE, app_resize_edges);
        } else if (app_surface_titlebar_hit(app_hit, cursor_x, cursor_y)) {
            app_surface_focus(app_hit->window_id);
            begin_window_drag(GUI_WINDOW_DRAG_APP_SURFACE, 0u);
        } else if (app_hit != NULL) {
            app_surface_focus(app_hit->window_id);
            app_surface_send_mouse_event(app_hit->window_id, cursor_x, cursor_y, VEX_APP_MOUSE_LEFT_DOWN);
        } else if (browser_hit == GUI_WINDOW_CONTROL_CLOSE) {
            /* Keep the reliable control channel alive until children exit. */
            gui_browser_end();
        } else if (browser_hit == GUI_WINDOW_CONTROL_MINIMIZE) {
            set_browser_minimized(1u);
        } else if (browser_hit == GUI_WINDOW_CONTROL_MAXIMIZE) {
            toggle_browser_window_maximized();
        } else if (paint_hit == GUI_WINDOW_CONTROL_CLOSE) {
            close_paint_view(0u);
        } else if (paint_hit == GUI_WINDOW_CONTROL_MINIMIZE) {
            set_paint_minimized(1u);
        } else if (paint_hit == GUI_WINDOW_CONTROL_MAXIMIZE) {
            toggle_browser_window_maximized();
        } else if (control_hit == GUI_WINDOW_CONTROL_CLOSE) {
            set_terminal_minimized(1u);
        } else if (control_hit == GUI_WINDOW_CONTROL_MINIMIZE) {
            set_terminal_minimized(1u);
        } else if (control_hit == GUI_WINDOW_CONTROL_MAXIMIZE) {
            toggle_terminal_maximized();
        } else if (front_resize_edges != 0u && !browser_window_maximized) {
            begin_window_drag(GUI_WINDOW_DRAG_BROWSER, front_resize_edges);
        } else if (((browser_active && !browser_minimized) || (paint_active && !paint_minimized)) &&
                !browser_window_maximized &&
                window_titlebar_hit_at(cursor_x, cursor_y, browser_win_x, browser_win_y, browser_win_w)) {
            begin_window_drag(GUI_WINDOW_DRAG_BROWSER, 0u);
        } else if (terminal_resize_edges != 0u && !terminal_maximized) {
            begin_window_drag(GUI_WINDOW_DRAG_TERMINAL, terminal_resize_edges);
        } else if (!terminal_minimized &&
                !terminal_maximized &&
                window_titlebar_hit_at(cursor_x, cursor_y, terminal_win_x, terminal_win_y, terminal_win_w)) {
            begin_window_drag(GUI_WINDOW_DRAG_TERMINAL, 0u);
        } else if (browser_handle_shell_click(cursor_x, cursor_y)) {
            /* handled by browser runtime input injection */
        } else if (!terminal_minimized &&
                cursor_x >= terminal_win_x && cursor_x < terminal_win_x + terminal_win_w &&
                cursor_y >= terminal_win_y && cursor_y < terminal_win_y + terminal_win_h) {
            browser_viewport_blur();
            browser_address_focused = 0u;
            app_surface_blur();
            keyboard_focus_capture_current();
            if (active_pid == file_manager_pid_value) {
                gui_app_command_t command = {.type=GUI_APP_CMD_MOUSE_CLICK,
                    .x=cursor_x,.y=cursor_y,.w=terminal_win_w-16u,
                    .h=terminal_win_h-GUI_TITLE_HEIGHT-12u};
                file_manager_app_send(&command);
            }
        } else if (paint_handle_click(cursor_x, cursor_y)) {
            /* handled by paint workspace */
        } else if (handle_desktop_icon_click(cursor_x, cursor_y)) {
            /* desktop icon (single or double click) */
        } else if (!terminal_minimized && active_pid == file_manager_pid_value) {
            gui_app_command_t command;

            command.type = GUI_APP_CMD_MOUSE_CLICK;
            command.flags = 0u;
            command.x = cursor_x;
            command.y = cursor_y;
            command.w = terminal_win_w - 16u;
            command.h = terminal_win_h - GUI_TITLE_HEIGHT - 12u;
            command.key = 0;
            command.path[0] = '\0';
            file_manager_app_send(&command);
        }
    }

    const int wheel=(moved || pressed || released)?mouse_take_wheel():0;
    if(moved || pressed || released) {
        browser_viewport_pointer((int)cursor_x,(int)cursor_y,cursor_buttons,wheel,
            browser_active && !browser_minimized && !browser_native_home_visible &&
            !browser_address_focused && !spotlight_visible && !elevation_modal_active &&
            !context_menu_visible && dragged_window==GUI_WINDOW_DRAG_NONE && app_surface_hit(cursor_x,cursor_y)==NULL);
    }
    if ((pressed & 0x02u) != 0 && !elevation_modal_active &&
        !(browser_active && cursor_x>=gui_content_x() && cursor_y>=gui_content_y() &&
          cursor_x<gui_content_x()+gui_content_width() && cursor_y<gui_content_y()+gui_content_height())) {
        hide_context_menu();
        open_context_menu();
    }
    /* A pointer edge establishes the current input target before keyboard
     * events sampled in this turn are routed. Sending keys first can send
     * the first character to the previously focused window or Web widget. */
    {
        keyboard_event_t event;
        while(keyboard_poll_event(&event)) {
            const uint8_t modifier_slot=browser_modifier_slot(event.key);
            if(modifier_slot) {
                if(event.pressed)browser_physical_modifier_mask|=modifier_slot;
                else browser_physical_modifier_mask&=(uint8_t)~modifier_slot;
            }
            /* A RAW_INPUT app window takes physical key edges directly. */
            if(!spotlight_visible && !elevation_modal_active &&
               !(browser_active && browser_address_focused) &&
               !(browser_active && !browser_minimized && browser_window_focused) &&
               app_surface_send_raw_key(event.key,event.hardware,event.pressed,event.modifiers))
                continue;
            /* Ctrl+Tab's legacy shortcut already owns non-RAW targets. Do not
             * process its parallel physical queue entry a second time. */
            if(event.hardware==0x0fu && (event.modifiers&1u))continue;
            const int browser_target=!spotlight_visible && !elevation_modal_active &&
                current_switch_target()==GUI_SWITCH_TARGET_BROWSER;
            if(browser_target && event.key==KEYBOARD_KEY_ALT_F10) {
                if(event.pressed && (event.modifiers&4u)) {
                    browser_suppressed_modifier_releases|=browser_physical_modifier_mask;
                    browser_discard_deferred_modifiers();
                    toggle_browser_window_maximized();
                }
                gui_key=0;
                continue;
            }
            if(browser_target && gui_browser_shortcut(&event)) {
                /* Discard the modifier edges withheld while deciding whether
                 * this was a browser chord, and consume their releases too. */
                if(event.pressed && !modifier_slot) {
                    browser_suppressed_modifier_releases|=browser_physical_modifier_mask;
                    browser_discard_deferred_modifiers();
                }
                gui_key=0;
                continue;
            }
            const int viewport_target=browser_target && browser_active && !browser_minimized &&
                !browser_address_focused && browser_window_focused &&
                !(gui_shortcut && (gui_key==KEYBOARD_KEY_SUPER || gui_key==KEYBOARD_KEY_CTRL_SPACE));
            if(browser_filter_modifier_event(&event,viewport_target))continue;
            if(!viewport_target)
                browser_viewport_blur();
            else browser_viewport_key(event.key,event.hardware,event.pressed,event.modifiers);
        }
    }
    if (elevation_modal_active) {
        gui_key = 0;
    } else if (gui_shortcut && gui_key == KEYBOARD_KEY_ALT_F10) {
        if(browser_active && !browser_minimized && browser_window_focused) {
            browser_suppressed_modifier_releases|=browser_physical_modifier_mask;
            browser_discard_deferred_modifiers();
            toggle_browser_window_maximized();
        }
        gui_key=0;
    } else if (gui_shortcut && gui_key == KEYBOARD_KEY_CTRL_SPACE) {
        spotlight_ui_voice_listen();
        spotlight_visible = (uint8_t)spotlight_ui_is_open();
    } else if (gui_shortcut && gui_key == KEYBOARD_KEY_SUPER) {
        spotlight_ui_toggle();
        spotlight_visible = (uint8_t)spotlight_ui_is_open();
    } else if (spotlight_visible && gui_key != 0) {
        spotlight_ui_handle_key(gui_key);
        spotlight_visible = (uint8_t)spotlight_ui_is_open();
    } else if (browser_active && browser_address_focused && gui_key != 0) {
        browser_shell_handle_key(gui_key);
    } else if (browser_active && !browser_minimized && browser_window_focused) {
        /* The structured event above belongs to Web. Its legacy character
         * shadow must not also reach a previously focused native app. */
    } else if (gui_key != 0 && app_surface_send_key_to_focused(gui_key)) {
        /* focused userspace app consumed the key */
    } else if (gui_key != 0) {
        send_key_to_active_app(gui_key);
    }

    if ((moved || pressed || released) && !spotlight_visible && !elevation_modal_active &&
        dragged_window == GUI_WINDOW_DRAG_NONE) {
        /* Geometry may have changed on this same WM click. Do not turn the
         * control's press into a client click at the new content coordinates. */
        app_surface_send_raw_pointer(cursor_x, cursor_y,
            wm_consumed_left?(cursor_buttons&~1u):cursor_buttons,wheel);
    }
    if(!(cursor_buttons&1u))wm_consumed_left=0;

    previous_cursor_buttons = cursor_buttons;

    {
        const uint64_t ticks = irq_timer_ticks();
        /* CMOS access polls for a stable calendar and traps into the VM.
         * Sample it at the clock's update cadence, not on every mouse frame. */
        if (ticks - last_clock_ticks >= GUI_CLOCK_UPDATE_TICKS) {
            last_clock_ticks = ticks;
            last_clock_second = rtc_seconds();
            /* Never repaint the bright taskbar over the spotlight dim overlay */
            if (!spotlight_visible) {
                draw_taskbar_static();
            }
        }
    }

    /* Advance spotlight animation every frame */
    if (spotlight_visible) {
        spotlight_ui_tick();
        spotlight_visible = (uint8_t)spotlight_ui_is_open();
    }

    {
        compositor_surface_present_completion_t completed[16];
        compositor_target_t target;
        uint32_t completed_count = 0u;
        if (gui_compositor_get_target(&target) == 0) {
            completed_count = compositor_surface_process_queued(
                &target, completed,
                (uint32_t)(sizeof(completed) / sizeof(completed[0])));
            for (uint32_t i = 0u; i < completed_count; ++i) {
                if (completed[i].status == COMPOSITOR_SURFACE_OK &&
                    (completed[i].damage.w > 0 && completed[i].damage.h > 0)) {
                    gui_compositor_damage(&completed[i].damage);
                }
            }
        }
        if (dirty_rect_count != 0u) {
            compositor_present();
        }
        for (uint32_t i = 0u; i < completed_count; ++i) {
            compositor_surface_finish_queued(&completed[i]);
        }
    }
    if (dirty_rect_count != 0u) {
        compositor_present();
    }
    if (!cursor_backing_valid || cursor_needs_present ||
        cursor_backing_x != cursor_x || cursor_backing_y != cursor_y) {
        compositor_present_cursor();
    }
}

void gui_wake(void) {
    const uint32_t tid = gui_loop_tid;
    gui_work_pending = 1u;
    if (tid != 0u) {
        scheduler_wake_flow(tid,scheduler_flow_current());
    }
}
void gui_input_wake(void) {
    gui_work_pending=1u;
    if(gui_loop_tid)scheduler_wake_origin(gui_loop_tid,FLOW_INPUT,5000000ull);
}
uint32_t gui_service_tid(void) {return gui_loop_tid;}

static uint64_t gui_next_wait_ticks(void) {
    const uint64_t now = irq_timer_ticks();
    uint64_t deadline = last_clock_ticks > UINT64_MAX - GUI_CLOCK_UPDATE_TICKS
        ? UINT64_MAX : last_clock_ticks + GUI_CLOCK_UPDATE_TICKS;

    /* These paths have real per-tick visual or lifecycle work in gui_tick(). */
    if (elevation_has_active_prompt() || spotlight_ui_is_animating() ||
        gui_browser_lifecycle_pending()) {
        return 1u;
    }

    if (app_switcher_visible) {
        if (app_switcher_anim_step < GUI_APP_SWITCHER_ANIM_TICKS) {
            const uint64_t animation_deadline = app_switcher_anim_start_tick >
                UINT64_MAX - GUI_APP_SWITCHER_ANIM_TICKS
                ? UINT64_MAX
                : app_switcher_anim_start_tick + GUI_APP_SWITCHER_ANIM_TICKS;
            if (animation_deadline < deadline) deadline = animation_deadline;
            /* Preserve the existing per-tick animation steps. */
            if (now < animation_deadline) return 1u;
        }
        if (app_switcher_deadline_tick < deadline) {
            deadline = app_switcher_deadline_tick;
        }
    }

    /* Clock repaint is the normal idle deadline. Input and queued frames wake
       this loop directly, so no frame-rate polling is needed. */
    return deadline > now ? deadline - now : 1u;
}

uint64_t gui_width(void) {
    return gui_framebuffer == NULL ? 0 : gui_framebuffer->width;
}

uint64_t gui_height(void) {
    return gui_framebuffer == NULL ? 0 : gui_framebuffer->height;
}

int gui_compositor_get_target(compositor_target_t *target_out) {
    GUI_GUARD;
    if (target_out == NULL || gui_framebuffer == NULL ||
        !gui_compositor_ready || compositor_scene == NULL ||
        gui_framebuffer->width > UINT32_MAX ||
        gui_framebuffer->height > UINT32_MAX) {
        return -1;
    }
    target_out->pixels = compositor_scene;
    target_out->width = (uint32_t)gui_framebuffer->width;
    target_out->height = (uint32_t)gui_framebuffer->height;
    target_out->stride = (uint32_t)gui_framebuffer->width;
    target_out->format = COMPOSITOR_SURFACE_FORMAT_XRGB8888;
    target_out->backend_pid = 0u;
    target_out->backend_handle = compositor_scene_handle;
    target_out->reserved = 0u;
    return 0;
}

int gui_browser_store_presented(const vex_rect_t *bounds) {
    GUI_GUARD;
    if(!bounds || !compositor_scene || bounds->x<0 || bounds->y<0 ||
       bounds->w<=0 || bounds->h<=0 ||
       (uint64_t)bounds->x+(uint32_t)bounds->w>gui_framebuffer->width ||
       (uint64_t)bounds->y+(uint32_t)bounds->h>gui_framebuffer->height)return -1;
    scene_image_t *image=browser_presented_find(gui_browser_active_tab(),1);
    if(!image)return -1;
    browser_presented_slot_for_image(image)->suspended=0; /* a fresh frame is live */
    return scene_image_store(image,
        compositor_scene+(size_t)bounds->y*gui_framebuffer->width+bounds->x,
        (uint32_t)gui_framebuffer->width,(uint32_t)bounds->w,(uint32_t)bounds->h);
}
int gui_browser_has_presented(uint32_t tab_id) {
    scene_image_t *image=browser_presented_find(tab_id,0);
    return image && image->pixels && image->width && image->height &&
           browser_presented_is_live(tab_id);
}
void gui_browser_suspend_tab_presented(uint32_t tab_id) {
    GUI_GUARD;
    scene_image_t *image=browser_presented_find(tab_id,0);
    if(image)browser_presented_slot_for_image(image)->suspended=1;
}
void gui_browser_resume_tab_presented(uint32_t tab_id) {
    GUI_GUARD;
    scene_image_t *image=browser_presented_find(tab_id,0);
    if(image)browser_presented_slot_for_image(image)->suspended=0;
}
void gui_browser_release_tab_presented(uint32_t tab_id) {
    GUI_GUARD;
    scene_image_t *image=browser_presented_find(tab_id,0);
    if(!image)return;
    scene_image_release(image);
    for(uint32_t i=0;i<VEX_BROWSER_MAX_TABS;i++)if(&browser_presented[i].image==image){browser_presented[i].tab_id=0;browser_presented[i].suspended=0;break;}
}
void gui_browser_release_presented(void) {
    GUI_GUARD;
    for(uint32_t i=0;i<VEX_BROWSER_MAX_TABS;i++){
        scene_image_release(&browser_presented[i].image);
        browser_presented[i].tab_id=0;
        browser_presented[i].suspended=0;
    }
}

void gui_compositor_damage(const vex_rect_t *damage) {
    GUI_GUARD;
    if (damage == NULL || damage->x < 0 || damage->y < 0 ||
        damage->w <= 0 || damage->h <= 0) {
        return;
    }
    compositor_mark_dirty((uint32_t)damage->x, (uint32_t)damage->y,
                          (uint32_t)damage->w, (uint32_t)damage->h);
    if (gui_loop_tid != 0u && scheduler_current_tid() != gui_loop_tid) {
        gui_wake();
    }
}

uint32_t gui_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (gui_framebuffer == NULL) {
        return 0;
    }

    return pack_color(r, g, b);
}

void gui_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    if (gui_framebuffer == NULL) {
        return;
    }

    fill_rect(x, y, w, h, color);
}

void gui_elevation_prompt_changed(void) {
    if (gui_framebuffer == NULL) {
        return;
    }

    if (elevation_has_active_prompt()) {
        if (spotlight_visible) {
            spotlight_ui_close();
            spotlight_visible = (uint8_t)spotlight_ui_is_open();
        }
        context_menu_visible = 0u;
    }

    if (elevation_has_active_prompt() &&
        !terminal_minimized &&
        active_pid == terminal_pid_value) {
        save_terminal_snapshot();
    }

    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

void gui_text_put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (gui_framebuffer == NULL || x >= gui_framebuffer->width || y >= gui_framebuffer->height) {
        return;
    }

    if (gui_compositor_ready && compositor_scene != NULL) {
        compositor_scene[y * gui_framebuffer->width + x] = color;
    }
    compositor_mark_dirty(x, y, 1u, 1u);
}

void gui_sync_rect_from_framebuffer(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    if (gui_framebuffer == NULL || !gui_compositor_ready || compositor_scene == NULL || gui_framebuffer->bpp != 32) {
        return;
    }

    const uint64_t x1 = x + w > gui_framebuffer->width ? gui_framebuffer->width : x + w;
    const uint64_t y1 = y + h > gui_framebuffer->height ? gui_framebuffer->height : y + h;
    for (uint64_t py = y; py < y1; ++py) {
        const uint8_t *row = (const uint8_t *)gui_framebuffer->address + py * gui_framebuffer->pitch;
        memcpy(compositor_scene + py * gui_framebuffer->width + x,
               (const uint32_t *)row + x,
               (x1 - x) * sizeof(uint32_t));
    }

    compositor_mark_dirty(x, y, x1 > x ? x1 - x : 0u, y1 > y ? y1 - y : 0u);
}

void gui_blit_argb_scaled(uint64_t x, uint64_t y, uint64_t w, uint64_t h, const uint32_t *pixels, uint32_t src_w, uint32_t src_h, uint32_t stride) {
    if (gui_framebuffer == NULL || pixels == NULL || w == 0u || h == 0u || src_w == 0u || src_h == 0u || stride < src_w) {
        return;
    }

    uint64_t x0=x,y0=y;
    uint64_t x1 = x + w > gui_framebuffer->width ? gui_framebuffer->width : x + w;
    uint64_t y1 = y + h > gui_framebuffer->height ? gui_framebuffer->height : y + h;
    if (!scene_clip_bounds(&x0,&y0,&x1,&y1)) {
        return;
    }

    for (uint64_t py = y0; py < y1; ++py) {
        const uint32_t src_y = (uint32_t)(((py - y) * src_h) / h);
        for (uint64_t px = x0; px < x1; ++px) {
            const uint32_t src_x = (uint32_t)(((px - x) * src_w) / w);
            const uint32_t pixel = pixels[src_y * stride + src_x];
            const uint32_t color = pack_color(
                (uint8_t)((pixel >> 16u) & 0xffu),
                (uint8_t)((pixel >> 8u) & 0xffu),
                (uint8_t)(pixel & 0xffu)
            );

            if (gui_compositor_ready && compositor_scene != NULL) {
                compositor_scene[py * gui_framebuffer->width + px] = color;
            } else {
                put_pixel_physical(px, py, color);
            }
        }
    }

    compositor_mark_dirty(x0, y0, x1 - x0, y1 - y0);
}

void gui_blit_argb_scaled_partial(uint64_t x, uint64_t y, uint64_t w, uint64_t h, const uint32_t *pixels, uint32_t src_w, uint32_t src_h, uint32_t stride, uint32_t clip_x, uint32_t clip_y, uint32_t clip_w, uint32_t clip_h) {
    uint64_t dest_x0;
    uint64_t dest_y0;
    uint64_t dest_x1;
    uint64_t dest_y1;

    if (gui_framebuffer == NULL || pixels == NULL || w == 0u || h == 0u || src_w == 0u || src_h == 0u || stride < src_w) {
        return;
    }
    if (clip_w == 0u || clip_h == 0u || clip_x >= src_w || clip_y >= src_h || clip_x + clip_w > src_w || clip_y + clip_h > src_h) {
        gui_blit_argb_scaled(x, y, w, h, pixels, src_w, src_h, stride);
        return;
    }

    dest_x0 = x + (((uint64_t)clip_x * w) / src_w);
    dest_y0 = y + (((uint64_t)clip_y * h) / src_h);
    dest_x1 = x + ((((uint64_t)clip_x + (uint64_t)clip_w) * w + src_w - 1u) / src_w);
    dest_y1 = y + ((((uint64_t)clip_y + (uint64_t)clip_h) * h + src_h - 1u) / src_h);

    if (dest_x0 >= gui_framebuffer->width || dest_y0 >= gui_framebuffer->height) {
        return;
    }

    if (dest_x1 > x + w) {
        dest_x1 = x + w;
    }
    if (dest_y1 > y + h) {
        dest_y1 = y + h;
    }
    if (dest_x1 > gui_framebuffer->width) {
        dest_x1 = gui_framebuffer->width;
    }
    if (dest_y1 > gui_framebuffer->height) {
        dest_y1 = gui_framebuffer->height;
    }
    if (dest_x1 <= dest_x0 || dest_y1 <= dest_y0) {
        return;
    }

    for (uint64_t py = dest_y0; py < dest_y1; ++py) {
        const uint32_t src_y = (uint32_t)(((py - y) * src_h) / h);
        for (uint64_t px = dest_x0; px < dest_x1; ++px) {
            const uint32_t src_x = (uint32_t)(((px - x) * src_w) / w);
            const uint32_t pixel = pixels[src_y * stride + src_x];
            const uint32_t color = pack_color(
                (uint8_t)((pixel >> 16u) & 0xffu),
                (uint8_t)((pixel >> 8u) & 0xffu),
                (uint8_t)(pixel & 0xffu)
            );

            if (gui_compositor_ready && compositor_scene != NULL) {
                compositor_scene[py * gui_framebuffer->width + px] = color;
            } else {
                put_pixel_physical(px, py, color);
            }
        }
    }

    compositor_mark_dirty(dest_x0, dest_y0, dest_x1 - dest_x0, dest_y1 - dest_y0);
}

void gui_fill_circle(uint64_t cx, uint64_t cy, uint64_t radius, uint32_t color) {
    if (gui_framebuffer == NULL) {
        return;
    }

    fill_circle(cx, cy, radius, color);
}

void gui_draw_vex_logo(uint64_t x, uint64_t y, uint64_t size) {
    if (gui_framebuffer == NULL || size == 0u || vex_logo_width == 0u || vex_logo_height == 0u) {
        return;
    }

    for (uint64_t dy = 0; dy < size && y + dy < gui_framebuffer->height; ++dy) {
        const uint32_t sy_fp = (uint32_t)(((dy * (uint64_t)(vex_logo_height - 1u)) << 16) / (size > 1u ? (size - 1u) : 1u));
        for (uint64_t dx = 0; dx < size && x + dx < gui_framebuffer->width; ++dx) {
            const uint32_t sx_fp = (uint32_t)(((dx * (uint64_t)(vex_logo_width - 1u)) << 16) / (size > 1u ? (size - 1u) : 1u));
            uint8_t red;
            uint8_t green;
            uint8_t blue;
            uint8_t alpha;

            vex_logo_sample_bilinear(sx_fp, sy_fp, &red, &green, &blue, &alpha);
            if (alpha == 0u) {
                continue;
            }

            const uint32_t over = pack_color(red, green, blue);
            scene_write(x + dx, y + dy,
                alpha == 255u ? over : blend_color(scene_pixel(x + dx, y + dy), over, alpha, 255u));
        }
    }
    compositor_mark_dirty(x, y, size, size);
}

void gui_fill_rounded_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t radius, uint32_t color) {
    if (gui_framebuffer == NULL) {
        return;
    }

    fill_rounded_rect(x, y, w, h, radius, color);
}

void gui_fill_rounded_rect_horizontal_gradient(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t radius, uint32_t left, uint32_t right) {
    if (gui_framebuffer == NULL) {
        return;
    }

    fill_rounded_rect_horizontal_gradient(x, y, w, h, radius, left, right);
}

void gui_draw_rounded_border(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t radius, uint32_t color) {
    if (gui_framebuffer == NULL) {
        return;
    }

    draw_rounded_border(x, y, w, h, radius, color);
}

void gui_draw_border(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    if (gui_framebuffer == NULL) {
        return;
    }

    draw_border(x, y, w, h, color);
}

void gui_blur_desktop_background(void) {
    if (gui_framebuffer == NULL) {
        return;
    }

    restore_mouse_cursor_background();

    const uint64_t pixel_count = gui_framebuffer->width * gui_framebuffer->height;
    if (blur_buffer == NULL || blur_buffer_pixels < pixel_count) {
        blur_buffer = kmalloc(sizeof(uint32_t) * pixel_count);
        blur_buffer_pixels = blur_buffer == NULL ? 0u : pixel_count;
    }
    if (blur_buffer == NULL) {
        return;
    }

    for (uint64_t y = 0; y < gui_framebuffer->height; ++y) {
        for (uint64_t x = 0; x < gui_framebuffer->width; ++x) {
            blur_buffer[y * gui_framebuffer->width + x] = get_pixel(x, y);
        }
    }

    /* Figma Frame 12: the dim overlay covers the WHOLE screen, taskbar included */
    const uint64_t start_y = 0u;
    for (uint64_t y = start_y; y < gui_framebuffer->height; ++y) {
        const uint64_t y_up = y > 4u ? y - 4u : y;
        const uint64_t y_down = y + 4u < gui_framebuffer->height ? y + 4u : y;

        for (uint64_t x = 0; x < gui_framebuffer->width; ++x) {
            const uint64_t x_left = x > 4u ? x - 4u : x;
            const uint64_t x_right = x + 4u < gui_framebuffer->width ? x + 4u : x;
            const uint32_t c0 = blur_buffer[y * gui_framebuffer->width + x];
            const uint32_t c1 = blur_buffer[y * gui_framebuffer->width + x_left];
            const uint32_t c2 = blur_buffer[y * gui_framebuffer->width + x_right];
            const uint32_t c3 = blur_buffer[y_up * gui_framebuffer->width + x];
            const uint32_t c4 = blur_buffer[y_down * gui_framebuffer->width + x];
            const uint32_t c5 = blur_buffer[y_up * gui_framebuffer->width + x_left];
            const uint32_t c6 = blur_buffer[y_up * gui_framebuffer->width + x_right];
            const uint32_t c7 = blur_buffer[y_down * gui_framebuffer->width + x_left];
            const uint32_t c8 = blur_buffer[y_down * gui_framebuffer->width + x_right];
            const uint8_t r = (uint8_t)((color_r(c0) * 2u + color_r(c1) + color_r(c2) + color_r(c3) + color_r(c4) + color_r(c5) + color_r(c6) + color_r(c7) + color_r(c8)) / 10u);
            const uint8_t g = (uint8_t)((color_g(c0) * 2u + color_g(c1) + color_g(c2) + color_g(c3) + color_g(c4) + color_g(c5) + color_g(c6) + color_g(c7) + color_g(c8)) / 10u);
            const uint8_t b = (uint8_t)((color_b(c0) * 2u + color_b(c1) + color_b(c2) + color_b(c3) + color_b(c4) + color_b(c5) + color_b(c6) + color_b(c7) + color_b(c8)) / 10u);
            const uint32_t final = dim_color(pack_color(r, g, b), 58u);
            blur_buffer[y * gui_framebuffer->width + x] = final;
            scene_write(x, y, final);
        }
    }
    compositor_mark_dirty(0u, start_y, gui_framebuffer->width, gui_framebuffer->height - start_y);
}

void gui_restore_blurred_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    if (gui_framebuffer == NULL || blur_buffer == NULL || blur_buffer_pixels < gui_framebuffer->width * gui_framebuffer->height) {
        return;
    }

    restore_mouse_cursor_background();
    const uint64_t x1 = x + w > gui_framebuffer->width ? gui_framebuffer->width : x + w;
    const uint64_t y1 = y + h > gui_framebuffer->height ? gui_framebuffer->height : y + h;
    if (x1 <= x || y1 <= y) {
        return;
    }
    if (gui_compositor_ready && compositor_scene != NULL) {
        const size_t row_bytes = (size_t)(x1 - x) * sizeof(uint32_t);
        for (uint64_t py = y; py < y1; ++py) {
            uint32_t *dst = compositor_scene + py * gui_framebuffer->width + x;
            const uint32_t *src = blur_buffer + py * gui_framebuffer->width + x;
            memcpy(dst, src, row_bytes);
        }
        compositor_mark_dirty(x, y, x1 - x, y1 - y);
    } else {
        for (uint64_t py = y; py < y1; ++py) {
            for (uint64_t px = x; px < x1; ++px) {
                put_pixel_physical(px, py, blur_buffer[py * gui_framebuffer->width + px]);
            }
        }
    }
}

void gui_redraw_cursor_overlay(void) {
    if (gui_framebuffer == NULL) {
        return;
    }

    gui_flush_scene_and_cursor();
}

void gui_restore_after_overlay(void) {
    if (gui_framebuffer == NULL) {
        return;
    }

    restore_mouse_cursor_background();
    draw_wallpaper();
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

void gui_focus_terminal_for_launch(void) {
    static uint32_t frontend;
    if(frontend && app_surface_raise_owner(frontend)==0)return;
    if(frontend) {
        const char *state=scheduler_state_name(frontend);
        if(strcmp(state,"missing") && strcmp(state,"zombie"))return;
    }
    uint32_t pid=0;
    if(scheduler_current_has_capability(VEX_CAP_ALL) && elf_spawn_pid_caps("/usr/bin/kyil-terminal",&pid,VEX_CAP_ALL)==0 && pid) {
        frontend=pid;scheduler_detach_process(pid);return;
    }
    tty_puts("Suchu Terminal launch failed; opening the existing native console\n");
    if (gui_framebuffer == NULL || terminal_pid_value == 0u) {
        return;
    }

    restore_mouse_cursor_background();
    browser_viewport_blur();
    browser_address_focused = 0u;
    app_surface_blur();
    keyboard_focus_capture_current();
    terminal_minimized = 0u;
    active_pid = terminal_pid_value;
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

static void gui_copy_open_path(char *destination, size_t capacity,
                               const char *source) {
    size_t i = 0u;

    if (destination == NULL || capacity == 0u) {
        return;
    }
    while (source != NULL && source[i] != '\0' && i + 1u < capacity) {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
}

/* The new process may already run, on another CPU, and ask for its context
 * before elf_spawn_pid() returns. The context is therefore registered first,
 * as pending for its application, and bound to the real PID afterwards; a
 * claim made in between takes the pending slot directly. */
#define GUI_OPEN_CONTEXT_PENDING UINT32_MAX

static gui_open_context_slot_t *gui_find_pending_context(uint32_t application,
                                                          const char *path) {
    for (size_t i = 0u; i < GUI_OPEN_CONTEXT_SLOTS; ++i) {
        gui_open_context_slot_t *slot = &open_context_slots[i];
        if (slot->pid == GUI_OPEN_CONTEXT_PENDING && slot->application == application &&
            (path == NULL || strcmp(slot->path, path) == 0)) {
            return slot;
        }
    }
    return NULL;
}

static void gui_register_pending_open_context(uint32_t application, const char *path) {
    GUI_GUARD;
    for (size_t i = 0u; i < GUI_OPEN_CONTEXT_SLOTS; ++i) {
        gui_open_context_slot_t *slot = &open_context_slots[i];
        if (slot->pid == 0u) {
            slot->pid = GUI_OPEN_CONTEXT_PENDING;
            slot->application = application;
            gui_copy_open_path(slot->path, sizeof(slot->path), path);
            return;
        }
    }
}

static void gui_bind_pending_open_context(uint32_t pid, uint32_t application, const char *path) {
    GUI_GUARD;
    gui_open_context_slot_t *slot = gui_find_pending_context(application, path);

    if (slot != NULL) {
        slot->pid = pid;   /* still unclaimed: the process has not asked yet */
    }
}

static void gui_cancel_pending_open_context(uint32_t application, const char *path) {
    GUI_GUARD;
    gui_open_context_slot_t *slot = gui_find_pending_context(application, path);

    if (slot != NULL) {
        memset(slot, 0, sizeof(*slot));
    }
}

static int gui_spawn_with_context(const char *executable,
                                  uint32_t application,
                                  const char *path) {
    uint32_t pid = 0u;
    const int has_context = path != NULL && path[0] != '\0';

    if (has_context) {
        gui_register_pending_open_context(application, path);
    }
    if (elf_spawn_pid(executable, &pid) != 0) {
        if (has_context) {
            gui_cancel_pending_open_context(application, path);
        }
        return -1;
    }
    if (has_context) {
        gui_bind_pending_open_context(pid, application, path);
    }
    if(application==VEX_OPEN_CONTEXT_EDITOR || application==VEX_OPEN_CONTEXT_FILES)
        scheduler_detach_process(pid);
    return 0;
}

int gui_claim_open_context(uint32_t application, uint32_t pid,
                           char *buffer, size_t size) {
    GUI_GUARD;
    if (buffer == NULL || size == 0u || pid == 0u) {
        return -1;
    }
    buffer[0] = '\0';
    for (size_t i = 0u; i < GUI_OPEN_CONTEXT_SLOTS; ++i) {
        gui_open_context_slot_t *slot = &open_context_slots[i];
        if (slot->pid == pid && slot->application == application) {
            gui_copy_open_path(buffer, size, slot->path);
            memset(slot, 0, sizeof(*slot));
            return 0;
        }
    }
    /* Asked before the spawner could bind the PID: take the pending slot. */
    {
        gui_open_context_slot_t *pending = gui_find_pending_context(application, NULL);

        if (pending != NULL) {
            gui_copy_open_path(buffer, size, pending->path);
            memset(pending, 0, sizeof(*pending));
            return 0;
        }
    }
    return -1;
}

void gui_open_file_manager_path(const char *path) {
    if (gui_framebuffer == NULL) {
        return;
    }

    if (gui_spawn_with_context("/usr/bin/kyil-files",
                               VEX_OPEN_CONTEXT_FILES, path) != 0) {
        tty_puts("gfm launch failed\n");
    }
}

void gui_open_file_manager(void) {
    gui_open_file_manager_path(NULL);
}

void gui_open_editor_path(const char *path) {
    if (gui_framebuffer == NULL) {
        return;
    }

    if (path != NULL && path[0] != '\0' && !fs_exists(path)) {
        tty_puts("editor open skipped: file does not exist\n");
        return;
    }
    if (gui_spawn_with_context("/usr/bin/kyil-editor",
                               VEX_OPEN_CONTEXT_EDITOR, path) != 0) {
        tty_puts("editor launch failed\n");
    }
}

void gui_open_path(const char *path) {
    fs_metadata_t metadata;

    if (path == NULL || path[0] == '\0' ||
        fs_get_metadata(path, &metadata, 1u) != 0) {
        return;
    }
    if (metadata.type == FS_ENTRY_DIRECTORY) {
        gui_open_file_manager_path(path);
    } else if (metadata.type == FS_ENTRY_REGULAR &&
               (metadata.mode & 0111u) != 0u) {
        if (elf_spawn(path) != 0) {
            tty_puts("application launch failed\n");
        }
    } else {
        gui_open_editor_path(path);
    }
}

void gui_open_editor(void) {
    gui_open_editor_path(NULL);
}

int gui_terminal_surface_visible(void) {
    if (gui_framebuffer == NULL || terminal_minimized) {
        return 0;
    }
    if (paint_active && !paint_minimized) {
        return 0;
    }
    return 1;
}

void gui_loop(void) {
    gui_loop_tid = scheduler_current_tid();
    debug_log_puts("desktop: GUI service ready\n");

    for (;;) {
        gui_work_pending = 0u;

        const uint64_t tick_start_ns = profiler_active ? sched_clock_ns() : 0u;
        gui_tick();
        if (tick_start_ns != 0u) {
            profiler_latency(PROF_LAT_GUI_TICK, sched_clock_ns() - tick_start_ns);
        }
        profiler_poll();
        /* Completed composition is a safe point; no partially updated scene
           or borrowed present backing crosses this switch. */
        scheduler_safe_point();

        /* IRQ wakeups are edges, whereas the input queues retain work. A
         * tick deliberately consumes only one button/wheel/direction run
         * and one scancode. Never put the GUI to sleep with an unread tail
         * merely because the IRQ that enqueued it preceded this tick. */
        if (mouse_events_pending() || keyboard_input_pending()) {
            continue;
        }

        process_wait_event_timeout(&gui_work_pending, gui_next_wait_ticks());
    }
}
/* Explicit scanout diagnostic only; no synthetic image or permanent redraw. */
static __attribute__((noinline)) void benchmark_legacy_copy(void *dest,const void *source,size_t size) {
    uint8_t *d=dest;const uint8_t *s=source;for(size_t i=0;i<size;i++)d[i]=s[i];
}
void gui_performance_benchmark(void) {
    GUI_GUARD;
    if(!gui_framebuffer || !compositor_scene || gui_framebuffer->bpp!=32)return;
    uint64_t width=gui_framebuffer->width,height=gui_framebuffer->height;
    if(!height || width>SIZE_MAX/height/4)return;
    size_t bytes=width*height*4;uint32_t *temporary=kmalloc(bytes);
    if(!temporary){tty_puts("guibench: allocation failed\n");return;}
    uint64_t ram[2],scanout[2];
    for(unsigned variant=0;variant<2;variant++) {
        uint64_t start=sched_clock_ns();
        for(unsigned i=0;i<8;i++) {
            if(!variant)benchmark_legacy_copy(temporary,compositor_scene,bytes);
            else memcpy(temporary,compositor_scene,bytes);
        }
        ram[variant]=sched_clock_ns()-start;start=sched_clock_ns();
        for(unsigned i=0;i<8;i++)for(uint64_t y=0;y<height;y++) {
            void *destination=(uint8_t *)gui_framebuffer->address+y*gui_framebuffer->pitch;
            if(!variant)benchmark_legacy_copy(destination,compositor_scene+y*width,width*4);
            else memcpy(destination,compositor_scene+y*width,width*4);
        }
        __asm__ volatile("sfence":::"memory");scanout[variant]=sched_clock_ns()-start;
    }
    int mismatch=memcmp(temporary,compositor_scene,bytes)!=0;
    char line[160];
    snprintf(line,sizeof(line),"guibench: bytes=%u copies=8 RAM-us scalar=%u rep=%u scanout-us scalar=%u rep=%u equal=%u\n",
        (unsigned)bytes,(unsigned)(ram[0]/1000),(unsigned)(ram[1]/1000),
        (unsigned)(scanout[0]/1000),(unsigned)(scanout[1]/1000),!mismatch);
    tty_puts(line);debug_log_puts(line);kfree(temporary);gui_redraw_cursor_overlay();
}
