#include <unos/gui.h>

#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/boot_requests.h>
#include <unos/elf.h>
#include <unos/file_manager_ui.h>
#include <unos/gpu.h>
#include <unos/heap.h>
#include <unos/irq.h>
#include <unos/keyboard.h>
#include <unos/vex_legacy_compat.h>
#include <unos/mouse.h>
#include <unos/rtc.h>
#include <unos/scheduler.h>
#include <unos/shell.h>
#include <unos/spotlight_ui.h>
#include <unos/text_editor_ui.h>
#include <unos/timezone.h>
#include <unos/tty.h>
#include <unos/vex_logo.h>
#include <unos/vex_wallpaper.h>

#include "lvgl/lvgl.h"

#define GUI_TASKBAR_HEIGHT 42ull
#define GUI_WINDOW_MARGIN 12ull
#define GUI_TITLE_HEIGHT 24ull
#define GUI_CURSOR_SIZE 17ull
#define GUI_TARGET_FPS 30ull
#define GUI_FRAME_INTERVAL_TICKS 3ull
#define GUI_CLOCK_UPDATE_TICKS 100ull
#define GUI_WINDOW_CONTROL_RADIUS 5ull
#define GUI_CONTEXT_MENU_WIDTH 224ull
#define GUI_CONTEXT_MENU_HEIGHT 244ull
#define GUI_CONTEXT_MENU_ROW_HEIGHT 32ull
#define GUI_CONTEXT_MENU_PAD 8ull
#define GUI_DIRTY_RECT_MAX 96u
#define GUI_BROWSER_SHELL_HEIGHT 86ull
#define GUI_BROWSER_ADDRESS_MAX 160u
#define GUI_PAINT_TOOLBAR_HEIGHT 44ull
#define GUI_PAINT_SWATCH_SIZE 20ull
#define GUI_WINDOW_RESIZE_HANDLE 18ull
#define GUI_WINDOW_MIN_W 320ull
#define GUI_WINDOW_MIN_H 180ull
#define GUI_APP_SWITCHER_MAX 5u
#define GUI_APP_SWITCHER_TIMEOUT_TICKS 120ull
#define GUI_APP_SWITCHER_ANIM_TICKS 6ull

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
    GUI_WINDOW_DRAG_BROWSER
} gui_window_drag_target_t;

typedef enum gui_switch_target {
    GUI_SWITCH_TARGET_NONE = 0,
    GUI_SWITCH_TARGET_TERMINAL,
    GUI_SWITCH_TARGET_FILES,
    GUI_SWITCH_TARGET_EDITOR,
    GUI_SWITCH_TARGET_BROWSER,
    GUI_SWITCH_TARGET_PAINT
} gui_switch_target_t;

struct limine_framebuffer *gui_framebuffer;
uint32_t *compositor_scene;
static uint64_t compositor_scene_pixels;
static uint32_t gui_scanout_handles[2];
static uint32_t *gui_scanout_pixels[2];
static uint32_t gui_scanout_stride;
static uint8_t gui_scanout_count;
static uint8_t gui_scanout_index;
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
static uint64_t context_menu_x;
static uint64_t context_menu_y;
static uint64_t last_clock_ticks;
static uint8_t last_clock_second;
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
char browser_address[GUI_BROWSER_ADDRESS_MAX];
uint64_t browser_address_len;
uint8_t browser_address_focused;
uint8_t browser_tab_count;
uint8_t browser_results_drawn;
uint8_t browser_loading_visible;
char browser_loading_text[GUI_BROWSER_ADDRESS_MAX];
static uint32_t *blur_buffer;
static uint64_t blur_buffer_pixels;
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
static uint8_t drag_resize_active;
static uint64_t drag_origin_cursor_x;
static uint64_t drag_origin_cursor_y;
static uint64_t drag_origin_win_x;
static uint64_t drag_origin_win_y;
static uint64_t drag_origin_win_w;
static uint64_t drag_origin_win_h;
static uint8_t app_switcher_visible;
static uint8_t app_switcher_count;
static uint8_t app_switcher_selected;
static uint8_t app_switcher_anim_step;
static gui_switch_target_t app_switcher_targets[GUI_APP_SWITCHER_MAX];
static uint64_t app_switcher_deadline_tick;
static uint64_t app_switcher_anim_start_tick;

static int cursor_pixel(uint64_t x, uint64_t y, uint32_t *color);
static void draw_border(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);
void fill_circle(uint64_t cx, uint64_t cy, uint64_t r, uint32_t color);
void fill_rounded_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t r, uint32_t color);
static void draw_rounded_border(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t r, uint32_t color);
static void draw_wallpaper_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
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
void save_terminal_snapshot(void);
void restore_terminal_snapshot(void);
static gui_window_control_t terminal_control_hit(uint64_t x, uint64_t y);
gui_window_control_t browser_control_hit(uint64_t x, uint64_t y);
gui_window_control_t paint_control_hit(uint64_t x, uint64_t y);
static void set_terminal_minimized(uint8_t minimized);
static void toggle_terminal_maximized(void);
void set_browser_minimized(uint8_t minimized);
void set_paint_minimized(uint8_t minimized);
int browser_handle_shell_click(uint64_t x, uint64_t y);
int paint_handle_click(uint64_t x, uint64_t y);
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
static void gui_lvgl_flush(const lv_area_t *area, const lv_color_t *color_p, void *user);
static void put_pixel_physical(uint64_t x, uint64_t y, uint32_t color);
static uint32_t scene_pixel(uint64_t x, uint64_t y);
static int window_titlebar_hit_at(uint64_t px, uint64_t py, uint64_t x, uint64_t y, uint64_t w);
static int window_resize_hit_at(uint64_t px, uint64_t py, uint64_t x, uint64_t y, uint64_t w, uint64_t h);
static void clamp_window_bounds(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h);
static void begin_window_drag(gui_window_drag_target_t target, uint8_t resize);
static void update_window_drag(void);
static void end_window_drag(void);
static void draw_app_switcher_overlay(void);
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

void put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (gui_framebuffer == NULL || x >= gui_framebuffer->width || y >= gui_framebuffer->height) {
        return;
    }

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
    if (gui_framebuffer == NULL || gui_framebuffer->bpp != 32) {
        return;
    }

    const uint64_t required_pixels = gui_framebuffer->width * gui_framebuffer->height;
    if (required_pixels == 0u) {
        return;
    }

    if (compositor_scene_pixels < required_pixels || compositor_scene == NULL) {
        compositor_scene = kmalloc(sizeof(uint32_t) * required_pixels);
        compositor_scene_pixels = compositor_scene == NULL ? 0u : required_pixels;
    }

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

static uint8_t wallpaper_channel(uint32_t x, uint32_t y, uint32_t channel) {
    const uint64_t index = ((uint64_t)y * vex_wallpaper_width + x) * 3u + channel;
    return vex_wallpaper_rgb[index];
}

static uint32_t wallpaper_sample_bilinear(uint32_t x_fp, uint32_t y_fp) {
    const uint32_t x0 = x_fp >> 16;
    const uint32_t y0 = y_fp >> 16;
    const uint32_t x1 = x0 + 1u < vex_wallpaper_width ? x0 + 1u : x0;
    const uint32_t y1 = y0 + 1u < vex_wallpaper_height ? y0 + 1u : y0;
    const uint32_t fx = (x_fp >> 8) & 0xffu;
    const uint32_t fy = (y_fp >> 8) & 0xffu;
    const uint32_t inv_fx = 255u - fx;
    const uint32_t inv_fy = 255u - fy;

    const uint32_t w00 = inv_fx * inv_fy;
    const uint32_t w10 = fx * inv_fy;
    const uint32_t w01 = inv_fx * fy;
    const uint32_t w11 = fx * fy;

    const uint32_t r =
        wallpaper_channel(x0, y0, 0u) * w00 +
        wallpaper_channel(x1, y0, 0u) * w10 +
        wallpaper_channel(x0, y1, 0u) * w01 +
        wallpaper_channel(x1, y1, 0u) * w11;
    const uint32_t g =
        wallpaper_channel(x0, y0, 1u) * w00 +
        wallpaper_channel(x1, y0, 1u) * w10 +
        wallpaper_channel(x0, y1, 1u) * w01 +
        wallpaper_channel(x1, y1, 1u) * w11;
    const uint32_t b =
        wallpaper_channel(x0, y0, 2u) * w00 +
        wallpaper_channel(x1, y0, 2u) * w10 +
        wallpaper_channel(x0, y1, 2u) * w01 +
        wallpaper_channel(x1, y1, 2u) * w11;

    return pack_color((uint8_t)(r >> 16), (uint8_t)(g >> 16), (uint8_t)(b >> 16));
}

void fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color) {
    if (gui_framebuffer == NULL || w == 0u || h == 0u) {
        return;
    }
    if (x >= gui_framebuffer->width || y >= gui_framebuffer->height) {
        return;
    }
    const uint64_t x1 = x + w > gui_framebuffer->width  ? gui_framebuffer->width  : x + w;
    const uint64_t y1 = y + h > gui_framebuffer->height ? gui_framebuffer->height : y + h;
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
    if (gui_framebuffer == NULL || vex_wallpaper_width == 0u || vex_wallpaper_height == 0u) {
        return;
    }

    const uint64_t x1 = x0 + w > gui_framebuffer->width ? gui_framebuffer->width : x0 + w;
    const uint64_t y1 = y0 + h > gui_framebuffer->height ? gui_framebuffer->height : y0 + h;
    uint32_t src_x_start_fp = 0u;
    uint32_t src_y_start_fp = 0u;
    uint32_t src_x_step_fp = 0u;
    uint32_t src_y_step_fp = 0u;

    if (gui_framebuffer->width * vex_wallpaper_height > gui_framebuffer->height * vex_wallpaper_width) {
        const uint32_t visible_src_h_fp =
            (uint32_t)(((uint64_t)vex_wallpaper_width * gui_framebuffer->height << 16) / gui_framebuffer->width);
        src_y_start_fp = ((uint32_t)vex_wallpaper_height << 16) > visible_src_h_fp
            ? (((uint32_t)vex_wallpaper_height << 16) - visible_src_h_fp) / 2u
            : 0u;
        src_x_step_fp = (uint32_t)(((uint64_t)vex_wallpaper_width << 16) / gui_framebuffer->width);
        src_y_step_fp = (uint32_t)((uint64_t)visible_src_h_fp / gui_framebuffer->height);
    } else {
        const uint32_t visible_src_w_fp =
            (uint32_t)(((uint64_t)vex_wallpaper_height * gui_framebuffer->width << 16) / gui_framebuffer->height);
        src_x_start_fp = ((uint32_t)vex_wallpaper_width << 16) > visible_src_w_fp
            ? (((uint32_t)vex_wallpaper_width << 16) - visible_src_w_fp) / 2u
            : 0u;
        src_x_step_fp = (uint32_t)((uint64_t)visible_src_w_fp / gui_framebuffer->width);
        src_y_step_fp = (uint32_t)(((uint64_t)vex_wallpaper_height << 16) / gui_framebuffer->height);
    }

    for (uint64_t wy = y0; wy < y1; ++wy) {
        const uint32_t src_y_fp = src_y_start_fp + (uint32_t)(wy * src_y_step_fp);

        for (uint64_t wx = x0; wx < x1; ++wx) {
            const uint32_t src_x_fp = src_x_start_fp + (uint32_t)(wx * src_x_step_fp);
            scene_write(wx, wy, wallpaper_sample_bilinear(src_x_fp, src_y_fp));
        }
    }
    compositor_mark_dirty(x0, y0, x1 - x0, y1 - y0);
}

static void draw_wallpaper(void) {
    draw_wallpaper_rect(0, 0, gui_framebuffer->width, gui_framebuffer->height);
}

static void draw_taskbar_brand(void) {
    gui_draw_vex_logo(14u, 10u, 30u);
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
    const uint64_t x0 = cx > r ? cx - r : 0u;
    const uint64_t y0 = cy > r ? cy - r : 0u;
    const uint64_t x1 = cx + r;
    const uint64_t y1 = cy + r;
    const int64_t r2_scaled = (int64_t)((r * 2u) * (r * 2u));

    for (uint64_t py = y0; py <= y1; ++py) {
        for (uint64_t px = x0; px <= x1; ++px) {
            uint8_t coverage = 0;

            for (uint64_t sy = 0; sy < 2u; ++sy) {
                for (uint64_t sx = 0; sx < 2u; ++sx) {
                    const int64_t dx = (int64_t)(px * 2u + sx) - (int64_t)(cx * 2u);
                    const int64_t dy = (int64_t)(py * 2u + sy) - (int64_t)(cy * 2u);
                    if (dx * dx + dy * dy <= r2_scaled) {
                        ++coverage;
                    }
                }
            }

            if (coverage == 4u) {
                scene_write(px, py, color);
            } else if (coverage != 0u) {
                scene_write(px, py, blend_color(scene_pixel(px, py), color, coverage, 4u));
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

    for (uint64_t py = y; py < y + h && py < gui_framebuffer->height; ++py) {
        for (uint64_t px = x; px < x + w && px < gui_framebuffer->width; ++px) {
            uint8_t coverage = 0;

            for (uint64_t sy = 0; sy < 2u; ++sy) {
                for (uint64_t sx = 0; sx < 2u; ++sx) {
                    const uint64_t sample_x = px * 2u + sx;
                    const uint64_t sample_y = py * 2u + sy;
                    const uint64_t left = x * 2u;
                    const uint64_t top = y * 2u;
                    const uint64_t right = (x + w) * 2u - 1u;
                    const uint64_t bottom = (y + h) * 2u - 1u;
                    const uint64_t radius = r * 2u;
                    uint64_t cx = sample_x;
                    uint64_t cy = sample_y;

                    if (sample_x < left + radius) {
                        cx = left + radius;
                    } else if (sample_x > right - radius) {
                        cx = right - radius;
                    }
                    if (sample_y < top + radius) {
                        cy = top + radius;
                    } else if (sample_y > bottom - radius) {
                        cy = bottom - radius;
                    }

                    const int64_t dx = (int64_t)sample_x - (int64_t)cx;
                    const int64_t dy = (int64_t)sample_y - (int64_t)cy;
                    if (dx * dx + dy * dy <= (int64_t)(radius * radius)) {
                        ++coverage;
                    }
                }
            }

            if (coverage == 4u) {
                scene_write(px, py, color);
            } else if (coverage != 0u) {
                scene_write(px, py, blend_color(scene_pixel(px, py), color, coverage, 4u));
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

static void format_time(char out[9]) {
    uint8_t raw_h, raw_m, raw_s;
    rtc_get_hms(&raw_h, &raw_m, &raw_s);

    int hour = (int)raw_h + (int)timezone_offset_hours();
    while (hour < 0) { hour += 24; }
    hour %= 24;
    const uint8_t minute = raw_m % 60u;
    const uint8_t second = raw_s % 60u;

    out[0] = (char)('0' + (uint8_t)(hour / 10u) % 10u);
    out[1] = (char)('0' + (uint8_t)(hour % 10u));
    out[2] = ':';
    out[3] = (char)('0' + minute / 10u);
    out[4] = (char)('0' + minute % 10u);
    out[5] = ':';
    out[6] = (char)('0' + second / 10u);
    out[7] = (char)('0' + second % 10u);
    out[8] = '\0';
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

    char time_text[9];
    char date_text[18];
    /* Text floats directly on the wallpaper — no chip, no background */
    const uint32_t date_col = pack_color(110, 104, 148); /* muted, barely there */
    const uint32_t time_col = pack_color(148, 140, 188); /* slightly brighter */
    const uint64_t width    = gui_framebuffer->width;
    const uint64_t text_y   = 14u;
    /* Date centered, time right-aligned */
    const uint64_t date_x   = width > 128u ? (width - 128u) / 2u : 0u;
    const uint64_t time_x   = width > 96u  ? width - 96u : 0u;

    format_date(date_text);
    format_time(time_text);

    tty_draw_text_transparent_at(date_x, text_y, date_text, date_col);
    tty_draw_text_transparent_at(time_x, text_y, time_text, time_col);
}

void draw_taskbar_static(void) {
    /* Fully transparent — wallpaper shows through, elements float */
    draw_wallpaper_rect(0, 0, gui_framebuffer->width, GUI_TASKBAR_HEIGHT);
    draw_taskbar_brand();
    draw_taskbar_time();
    draw_app_switcher_overlay();
}

static void draw_main_window_frame(const char *title_text) {
    draw_window_frame_at(terminal_win_x, terminal_win_y, terminal_win_w, terminal_win_h, title_text);
}

static void draw_window_frame_at(uint64_t x, uint64_t y, uint64_t w, uint64_t h, const char *title_text) {
    const uint32_t body   = pack_color(6,  5,  14);   /* near-black */
    const uint32_t chrome = pack_color(9,  8,  20);   /* title bar — barely lifted */
    const uint32_t bdr    = pack_color(20, 16, 38);   /* 1px border — faint purple tint */
    const uint32_t sep    = pack_color(14, 11, 26);   /* separator — almost invisible */

    if (gui_framebuffer == NULL || w == 0 || h == 0) {
        return;
    }

    /* Body */
    fill_rounded_rect(x, y, w, h, 14u, bdr);
    fill_rounded_rect(x + 1u, y + 1u, w - 2u, h - 2u, 13u, body);
    /* Title bar — same near-black, just slightly different */
    fill_rounded_rect(x + 1u, y + 1u, w - 2u, GUI_TITLE_HEIGHT, 13u, chrome);
    /* Separator */
    fill_rect(x + 1u, y + GUI_TITLE_HEIGHT, w - 2u, 1u, sep);

    /* The color moment: 1px gradient line blue→violet at the very top */
    if (w > 28u) {
        fill_rounded_rect_horizontal_gradient(x + 14u, y + 1u, w - 28u, 1u, 0u,
            pack_color(28, 68, 220),    /* electric blue */
            pack_color(160, 18, 220));  /* deep violet */
    }

    /* Traffic lights */
    fill_circle(x + 16u, y + GUI_TITLE_HEIGHT / 2u, GUI_WINDOW_CONTROL_RADIUS, pack_color(255, 96,  89));
    fill_circle(x + 32u, y + GUI_TITLE_HEIGHT / 2u, GUI_WINDOW_CONTROL_RADIUS, pack_color(255, 189, 46));
    fill_circle(x + 48u, y + GUI_TITLE_HEIGHT / 2u, GUI_WINDOW_CONTROL_RADIUS, pack_color( 40, 200, 64));

    /* Title — muted, doesn't compete */
    if (title_text != NULL) {
        uint64_t title_len = 0;
        while (title_text[title_len] != '\0') { ++title_len; }
        const uint64_t title_px = title_len * 8u;
        const uint64_t half_px  = title_px / 2u;
        const uint64_t center   = x + w / 2u;
        const uint64_t title_x  = (center > half_px + 64u) ? center - half_px : x + 64u;
        tty_draw_text_transparent_clipped_at(title_x, y + 8u, w > (title_x - x) + 16u ? w - (title_x - x) - 16u : 8u, title_text, pack_color(88, 82, 128));
    }

    compositor_mark_dirty(x > 4u ? x - 4u : 0u, y > 4u ? y - 4u : 0u, w + 8u, h + 8u);
}

void save_terminal_snapshot(void) {
    if (gui_framebuffer == NULL || terminal_win_w == 0 || terminal_win_h == 0) {
        return;
    }

    const uint64_t required_pixels = terminal_win_w * terminal_win_h;
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
        const size_t row_bytes = (size_t)terminal_win_w * sizeof(uint32_t);
        for (uint64_t y = 0; y < terminal_win_h; ++y) {
            const uint32_t *src = compositor_scene + (terminal_win_y + y) * gui_framebuffer->width + terminal_win_x;
            uint32_t *dst = terminal_snapshot + y * terminal_win_w;
            memcpy(dst, src, row_bytes);
        }
    } else {
        for (uint64_t y = 0; y < terminal_win_h; ++y) {
            for (uint64_t x = 0; x < terminal_win_w; ++x) {
                terminal_snapshot[y * terminal_win_w + x] = get_pixel(terminal_win_x + x, terminal_win_y + y);
            }
        }
    }

    terminal_snapshot_valid = 1;
}

void restore_terminal_snapshot(void) {
    if (gui_framebuffer == NULL || terminal_snapshot == NULL || !terminal_snapshot_valid) {
        draw_main_window_frame("Terminal");
        return;
    }

    if (gui_compositor_ready && compositor_scene != NULL) {
        const size_t row_bytes = (size_t)terminal_win_w * sizeof(uint32_t);
        for (uint64_t y = 0; y < terminal_win_h; ++y) {
            uint32_t *dst = compositor_scene + (terminal_win_y + y) * gui_framebuffer->width + terminal_win_x;
            const uint32_t *src = terminal_snapshot + y * terminal_win_w;
            memcpy(dst, src, row_bytes);
        }
        compositor_mark_dirty(terminal_win_x, terminal_win_y, terminal_win_w, terminal_win_h);
    } else {
        for (uint64_t y = 0; y < terminal_win_h; ++y) {
            for (uint64_t x = 0; x < terminal_win_w; ++x) {
                put_pixel(terminal_win_x + x, terminal_win_y + y, terminal_snapshot[y * terminal_win_w + x]);
            }
        }
    }
}

int window_control_circle_hit_at(uint64_t x, uint64_t y, uint64_t window_y, uint64_t center_x) {
    const uint64_t center_y = window_y + GUI_TITLE_HEIGHT / 2u;
    const int64_t dx = (int64_t)x - (int64_t)center_x;
    const int64_t dy = (int64_t)y - (int64_t)center_y;

    return dx * dx + dy * dy <= (int64_t)(GUI_WINDOW_CONTROL_RADIUS * GUI_WINDOW_CONTROL_RADIUS);
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

static int window_resize_hit_at(uint64_t px, uint64_t py, uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    if (w < GUI_WINDOW_RESIZE_HANDLE || h < GUI_WINDOW_RESIZE_HANDLE) {
        return 0;
    }
    return px >= x + w - GUI_WINDOW_RESIZE_HANDLE &&
        px < x + w &&
        py >= y + h - GUI_WINDOW_RESIZE_HANDLE &&
        py < y + h;
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

static void begin_window_drag(gui_window_drag_target_t target, uint8_t resize) {
    dragged_window = target;
    drag_resize_active = resize;
    drag_origin_cursor_x = cursor_x;
    drag_origin_cursor_y = cursor_y;
    if (target == GUI_WINDOW_DRAG_BROWSER) {
        drag_origin_win_x = browser_win_x;
        drag_origin_win_y = browser_win_y;
        drag_origin_win_w = browser_win_w;
        drag_origin_win_h = browser_win_h;
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

    if (dragged_window == GUI_WINDOW_DRAG_NONE || gui_framebuffer == NULL) {
        return;
    }

    if (dragged_window == GUI_WINDOW_DRAG_BROWSER) {
        x = &browser_win_x;
        y = &browser_win_y;
        w = &browser_win_w;
        h = &browser_win_h;
    } else {
        x = &terminal_win_x;
        y = &terminal_win_y;
        w = &terminal_win_w;
        h = &terminal_win_h;
    }

    new_x = drag_origin_win_x;
    new_y = drag_origin_win_y;
    new_w = drag_origin_win_w;
    new_h = drag_origin_win_h;

    if (drag_resize_active) {
        new_w = drag_origin_win_w + (cursor_x >= drag_origin_cursor_x ? cursor_x - drag_origin_cursor_x : 0u);
        if (cursor_x < drag_origin_cursor_x) {
            const uint64_t delta = drag_origin_cursor_x - cursor_x;
            new_w = drag_origin_win_w > delta ? drag_origin_win_w - delta : GUI_WINDOW_MIN_W;
        }
        new_h = drag_origin_win_h + (cursor_y >= drag_origin_cursor_y ? cursor_y - drag_origin_cursor_y : 0u);
        if (cursor_y < drag_origin_cursor_y) {
            const uint64_t delta = drag_origin_cursor_y - cursor_y;
            new_h = drag_origin_win_h > delta ? drag_origin_win_h - delta : GUI_WINDOW_MIN_H;
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
    if (dragged_window == GUI_WINDOW_DRAG_TERMINAL && terminal_maximized) {
        return;
    }
    if (*x == new_x && *y == new_y && *w == new_w && *h == new_h) {
        return;
    }

    *x = new_x;
    *y = new_y;
    *w = new_w;
    *h = new_h;
    if (dragged_window == GUI_WINDOW_DRAG_TERMINAL) {
        terminal_snapshot_valid = 0u;
    }
    render_active_app();
    draw_taskbar_static();
}

static void end_window_drag(void) {
    dragged_window = GUI_WINDOW_DRAG_NONE;
    drag_resize_active = 0u;
}

static gui_window_control_t terminal_control_hit(uint64_t x, uint64_t y) {
    if (terminal_minimized) {
        return GUI_WINDOW_CONTROL_NONE;
    }

    if (window_control_circle_hit_at(x, y, terminal_win_y, terminal_win_x + 16u)) {
        return GUI_WINDOW_CONTROL_CLOSE;
    }
    if (window_control_circle_hit_at(x, y, terminal_win_y, terminal_win_x + 32u)) {
        return GUI_WINDOW_CONTROL_MINIMIZE;
    }
    if (window_control_circle_hit_at(x, y, terminal_win_y, terminal_win_x + 48u)) {
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
    if (gui_compositor_ready && dirty_rect_count != 0u) {
        compositor_present();
        return;
    }

    compositor_present_cursor();
}

static void gui_lvgl_flush(const lv_area_t *area, const lv_color_t *color_p, void *user) {
    uint64_t width;
    uint64_t height;
    (void)user;

    if (gui_framebuffer == NULL || compositor_scene == NULL || area == NULL || color_p == NULL) {
        return;
    }
    if (area->x2 < area->x1 || area->y2 < area->y1) {
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
            const lv_color_t *src = color_p + y * width;
            memcpy(dst, src, copy_cols * sizeof(uint32_t));
        }
    }
    compositor_mark_dirty((uint64_t)area->x1, (uint64_t)area->y1, width, height);
}

static void compositor_copy_rect_to_scanout(uint32_t *scanout_pixels, const gui_rect_t *rect) {
    uint64_t x1;
    uint64_t y1;
    size_t row_bytes;

    if (scanout_pixels == NULL || rect == NULL || gui_framebuffer == NULL || compositor_scene == NULL ||
        gui_scanout_stride < gui_framebuffer->width) {
        return;
    }

    x1 = rect->x + rect->w > gui_framebuffer->width ? gui_framebuffer->width : rect->x + rect->w;
    y1 = rect->y + rect->h > gui_framebuffer->height ? gui_framebuffer->height : rect->y + rect->h;
    if (rect->x >= x1 || rect->y >= y1) {
        return;
    }

    row_bytes = (size_t)(x1 - rect->x) * sizeof(uint32_t);
    for (uint64_t y = rect->y; y < y1; ++y) {
        uint32_t *dst = scanout_pixels + y * gui_scanout_stride + rect->x;
        const uint32_t *src = compositor_scene + y * gui_framebuffer->width + rect->x;
        memcpy(dst, src, row_bytes);
    }
}

static void compositor_present(void) {
    vex_rect_t present_rects[GUI_DIRTY_RECT_MAX];
    uint32_t *scanout_pixels = NULL;
    uint32_t scanout_handle = 0u;
    uint32_t *mirror_pixels = NULL;

    if (gui_framebuffer == NULL || !gui_compositor_ready || compositor_scene == NULL) {
        dirty_rect_count = 0u;
        return;
    }

    if (dirty_rect_count == 0u) {
        return;
    }

    restore_mouse_cursor_background();

    if (gui_scanout_count != 0u) {
        const uint8_t present_index = gui_scanout_count > 1u ? (uint8_t)(gui_scanout_index ^ 1u) : 0u;
        scanout_pixels = gui_scanout_pixels[present_index];
        scanout_handle = gui_scanout_handles[present_index];
        if (gui_scanout_count > 1u) {
            mirror_pixels = gui_scanout_pixels[gui_scanout_index];
        }
    }

    for (uint64_t i = 0; i < dirty_rect_count; ++i) {
        const gui_rect_t *rect = &dirty_rects[i];
        compositor_copy_rect_to_scanout(scanout_pixels, rect);
        if (mirror_pixels != NULL && mirror_pixels != scanout_pixels) {
            compositor_copy_rect_to_scanout(mirror_pixels, rect);
        }

        present_rects[i].x = rect->x > INT32_MAX ? INT32_MAX : (int32_t)rect->x;
        present_rects[i].y = rect->y > INT32_MAX ? INT32_MAX : (int32_t)rect->y;
        present_rects[i].w = rect->w > INT32_MAX ? INT32_MAX : (int32_t)rect->w;
        present_rects[i].h = rect->h > INT32_MAX ? INT32_MAX : (int32_t)rect->h;
    }

    if (scanout_pixels != NULL && scanout_handle != 0u) {
        (void)gpu_kernel_present(scanout_handle, present_rects, dirty_rect_count);
        if (gui_scanout_count > 1u) {
            gui_scanout_index ^= 1u;
        }
    } else {
        gpu_present_scene(compositor_scene, (uint32_t)gui_framebuffer->width, present_rects, dirty_rect_count);
    }
    dirty_rect_count = 0u;
    compose_mouse_cursor();
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

    while (mouse_get_event(&dx, &dy, &buttons)) {
        if (dx < 0 && cursor_x < (uint64_t)(-dx)) {
            cursor_x = 0;
        } else {
            cursor_x = (uint64_t)((int64_t)cursor_x + dx);
        }

        if (dy < 0 && cursor_y < (uint64_t)(-dy)) {
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

void render_active_app(void) {
    /* Full scene compositor: repaint the desktop then draw every visible
     * window back-to-front.  This eliminates ghost pixels entirely — the
     * wallpaper is always the ground truth beneath all windows. */
    draw_wallpaper();

    /* ── Layer 0: terminal / file-manager / editor ── */
    if (!terminal_minimized) {
        draw_main_window_frame(window_title_for_pid(active_pid));
        if (active_pid == terminal_pid_value) {
            send_app_config(terminal_pid_value, 1u, 0u);
            restore_terminal_snapshot();
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
        return;
    }

    /* ── Layer 1: Navigateur ── */
    if (browser_active && !browser_minimized) {
        draw_window_frame_at(browser_win_x, browser_win_y, browser_win_w, browser_win_h, "Navigateur");
        draw_browser_shell();
        if (ladybird_browser_results_visible()) {
            draw_browser_native_results();
        } else if (browser_loading_visible) {
            draw_browser_loading_state();
        } else if (browser_native_home_visible) {
            draw_browser_native_home();
        }
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

    if (browser_active || terminal_minimized || key == 0 || active_pid == 0u) {
        return;
    }

    if (active_pid == terminal_pid_value && !scheduler_process_accepts_input(terminal_pid_value)) {
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
        return "Fichiers";
    }
    if (pid == editor_pid_value) {
        return "Editeur";
    }

    return "Terminal";
}

void gui_init(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count == 0) {
        return;
    }

    gui_framebuffer = framebuffer_request.response->framebuffers[0];
    lv_init();
    lv_display_set_flush_cb(gui_lvgl_flush, NULL);
    lv_display_set_resolution((uint32_t)gui_framebuffer->width, (uint32_t)gui_framebuffer->height);
    lv_display_set_demo_enabled(0u);  /* disabled: writes hardcoded ARGB colors that corrupt the scene */
    gpu_init(gui_framebuffer);
    compositor_scene_pixels = 0u;
    compositor_scene = NULL;
    gui_scanout_handles[0] = 0u;
    gui_scanout_handles[1] = 0u;
    gui_scanout_pixels[0] = NULL;
    gui_scanout_pixels[1] = NULL;
    gui_scanout_stride = 0u;
    gui_scanout_count = 0u;
    gui_scanout_index = 0u;
    gui_compositor_ready = 0u;
    dirty_rect_count = 0u;

    const uint32_t text     = pack_color(230, 236, 248);
    const uint32_t shadow   = pack_color(4, 4, 8);

    gui_text_color  = text;
    gui_cursor_shadow = shadow;

    if (gpu_kernel_buffer_create((uint32_t)gui_framebuffer->width,
            (uint32_t)gui_framebuffer->height,
            (uint32_t)gui_framebuffer->width,
            VEX_GPU_MEMORY_CPU_VISIBLE | VEX_GPU_MEMORY_SCANOUT,
            &gui_scanout_handles[0]) == 0) {
        gui_scanout_pixels[0] = gpu_kernel_buffer_map(gui_scanout_handles[0], &gui_scanout_stride, NULL, NULL);
        if (gui_scanout_pixels[0] != NULL) {
            gui_scanout_count = 1u;
        }
    }
    if (gui_scanout_count != 0u &&
        gpu_kernel_buffer_create((uint32_t)gui_framebuffer->width,
            (uint32_t)gui_framebuffer->height,
            (uint32_t)gui_framebuffer->width,
            VEX_GPU_MEMORY_CPU_VISIBLE | VEX_GPU_MEMORY_SCANOUT,
            &gui_scanout_handles[1]) == 0) {
        gui_scanout_pixels[1] = gpu_kernel_buffer_map(gui_scanout_handles[1], NULL, NULL, NULL);
        if (gui_scanout_pixels[1] != NULL) {
            gui_scanout_count = 2u;
        }
    }

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

    draw_main_window_frame("Terminal");

    tty_set_region(
        terminal_win_x + 8u,
        terminal_win_y + GUI_TITLE_HEIGHT + 6u,
        terminal_win_w - 16u,
        terminal_win_h - GUI_TITLE_HEIGHT - 12u
    );
    tty_set_colors_rgb(230, 236, 248, 12, 14, 22);
    tty_clear();

    file_manager_pid_value = file_manager_app_start();
    editor_pid_value = text_editor_app_start();
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
    terminal_minimized = 0;
    terminal_maximized = 0u;
    terminal_restore_x = terminal_win_x;
    terminal_restore_y = terminal_win_y;
    terminal_restore_w = terminal_win_w;
    terminal_restore_h = terminal_win_h;
    terminal_snapshot_valid = 0;
    terminal_snapshot_pixels = terminal_win_w * terminal_win_h;
    terminal_snapshot = kmalloc(sizeof(uint32_t) * terminal_snapshot_pixels);
    if (terminal_snapshot != NULL) {
        save_terminal_snapshot();
    }

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
    active_pid = pid;
    send_app_config(terminal_pid_value, 1u, 0u);
    draw_taskbar_static();
}

void gui_tick(void) {
    const int moved = poll_mouse_cursor();
    const uint8_t pressed = (uint8_t)(cursor_buttons & (uint8_t)~previous_cursor_buttons);
    const uint64_t now_ticks = irq_timer_ticks();
    char gui_key = 0;
    gui_app_event_t app_event;
    lv_timer_handler();
    ladybird_display_pump();
    if (browser_active && !browser_minimized && ladybird_browser_results_visible() && !browser_results_drawn) {
        browser_results_drawn = 1u;
        render_active_app();
        gui_flush_scene_and_cursor();
    }

    gui_key = keyboard_pollchar();

    if (app_switcher_visible) {
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
            gui_key != KEYBOARD_KEY_APP_SWITCH_NEXT &&
            gui_key != KEYBOARD_KEY_APP_SWITCH_PREV)) {
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

    if (gui_key == KEYBOARD_KEY_APP_SWITCH_NEXT) {
        app_switcher_cycle(0);
        previous_cursor_buttons = cursor_buttons;
        return;
    }
    if (gui_key == KEYBOARD_KEY_APP_SWITCH_PREV) {
        app_switcher_cycle(1);
        previous_cursor_buttons = cursor_buttons;
        return;
    }
    if (gui_key != KEYBOARD_KEY_SUPER &&
        gui_key != KEYBOARD_KEY_CTRL_SPACE &&
        !spotlight_visible &&
        !(browser_active && browser_address_focused) &&
        (terminal_minimized ||
         (active_pid == terminal_pid_value && !scheduler_process_accepts_input(terminal_pid_value)))) {
        gui_key = 0;
    }

    while (file_manager_app_poll_event(&app_event)) {
        if (app_event.type == GUI_APP_EVENT_OPEN_FILE) {
            gui_open_editor_path(app_event.path);
            restore_mouse_cursor_background();
        }
    }

    if (gui_key == KEYBOARD_KEY_CTRL_SPACE) {
        spotlight_ui_voice_listen();
        spotlight_visible = (uint8_t)spotlight_ui_is_open();
    } else if (gui_key == KEYBOARD_KEY_SUPER) {
        spotlight_ui_toggle();
        spotlight_visible = (uint8_t)spotlight_ui_is_open();
    } else if (spotlight_visible && gui_key != 0) {
        spotlight_ui_handle_key(gui_key);
        spotlight_visible = (uint8_t)spotlight_ui_is_open();
    } else if (browser_active && browser_address_focused && gui_key != 0) {
        browser_shell_handle_key(gui_key);
    } else if (gui_key != 0) {
        send_key_to_active_app(gui_key);
    }

    if ((cursor_buttons & 0x01u) == 0u) {
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

    if ((pressed & 0x01u) != 0 && gui_framebuffer != NULL) {
        const int logo_hit = cursor_y < GUI_TASKBAR_HEIGHT && cursor_x >= 8u && cursor_x < 80u;
        const int menu_item = context_menu_item_at(cursor_x, cursor_y);
        const gui_window_control_t browser_hit = browser_control_hit(cursor_x, cursor_y);
        const gui_window_control_t paint_hit = paint_control_hit(cursor_x, cursor_y);
        gui_window_control_t control_hit = GUI_WINDOW_CONTROL_NONE;

        if (browser_hit != GUI_WINDOW_CONTROL_NONE) {
            control_hit = browser_hit;
        } else if (paint_hit != GUI_WINDOW_CONTROL_NONE) {
            control_hit = paint_hit;
        } else {
            control_hit = terminal_control_hit(cursor_x, cursor_y);
        }

        if (logo_hit || control_hit != GUI_WINDOW_CONTROL_NONE || menu_item >= 0) {
            restore_mouse_cursor_background();
        }
        if (context_menu_visible) {
            hide_context_menu();
        }

        if (spotlight_visible) {
            spotlight_ui_handle_click(cursor_x, cursor_y);
            spotlight_visible = (uint8_t)spotlight_ui_is_open();
        } else if (logo_hit) {
            spotlight_ui_toggle();
            spotlight_visible = (uint8_t)spotlight_ui_is_open();
        } else if (browser_hit == GUI_WINDOW_CONTROL_CLOSE) {
            gui_browser_end();
        } else if (browser_hit == GUI_WINDOW_CONTROL_MINIMIZE) {
            set_browser_minimized(1u);
        } else if (browser_hit == GUI_WINDOW_CONTROL_MAXIMIZE) {
            set_browser_minimized(0u);
        } else if (paint_hit == GUI_WINDOW_CONTROL_CLOSE) {
            close_paint_view(0u);
        } else if (paint_hit == GUI_WINDOW_CONTROL_MINIMIZE) {
            set_paint_minimized(1u);
        } else if (paint_hit == GUI_WINDOW_CONTROL_MAXIMIZE) {
            set_paint_minimized(0u);
        } else if (control_hit == GUI_WINDOW_CONTROL_CLOSE) {
            set_terminal_minimized(1u);
        } else if (control_hit == GUI_WINDOW_CONTROL_MINIMIZE) {
            set_terminal_minimized(1u);
        } else if (control_hit == GUI_WINDOW_CONTROL_MAXIMIZE) {
            toggle_terminal_maximized();
        } else if (menu_item == 0) {
            switch_to_pid(terminal_pid_value);
        } else if (menu_item == 1) {
            switch_to_pid(file_manager_pid_value);
        } else if (menu_item == 2) {
            if (browser_active) {
                set_browser_minimized(0u);
            } else if (elf_spawn("/home/root/bin/webkit") != 0) {
                tty_puts("browser launch failed\n");
            }
        } else if (menu_item == 3) {
            spotlight_ui_toggle();
            spotlight_visible = (uint8_t)spotlight_ui_is_open();
        } else if (menu_item == 4) {
            gui_open_paint();
        } else if (!terminal_minimized &&
                window_resize_hit_at(cursor_x, cursor_y, terminal_win_x, terminal_win_y, terminal_win_w, terminal_win_h)) {
            begin_window_drag(GUI_WINDOW_DRAG_TERMINAL, 1u);
        } else if (!terminal_minimized &&
                window_titlebar_hit_at(cursor_x, cursor_y, terminal_win_x, terminal_win_y, terminal_win_w)) {
            begin_window_drag(GUI_WINDOW_DRAG_TERMINAL, 0u);
        } else if (((browser_active && !browser_minimized) || (paint_active && !paint_minimized)) &&
                window_resize_hit_at(cursor_x, cursor_y, browser_win_x, browser_win_y, browser_win_w, browser_win_h)) {
            begin_window_drag(GUI_WINDOW_DRAG_BROWSER, 1u);
        } else if (((browser_active && !browser_minimized) || (paint_active && !paint_minimized)) &&
                window_titlebar_hit_at(cursor_x, cursor_y, browser_win_x, browser_win_y, browser_win_w)) {
            begin_window_drag(GUI_WINDOW_DRAG_BROWSER, 0u);
        } else if (browser_handle_shell_click(cursor_x, cursor_y)) {
            /* handled by browser runtime input injection */
        } else if (paint_handle_click(cursor_x, cursor_y)) {
            /* handled by paint workspace */
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

    if ((pressed & 0x02u) != 0) {
        hide_context_menu();
        draw_context_menu();
    }
    previous_cursor_buttons = cursor_buttons;

    {
        const uint64_t ticks = irq_timer_ticks();
        const uint8_t second = rtc_seconds();
        if (second != last_clock_second || ticks - last_clock_ticks >= GUI_CLOCK_UPDATE_TICKS) {
            last_clock_ticks = ticks;
            last_clock_second = second;
            draw_taskbar_time();
        }
    }

    /* Advance spotlight animation every frame */
    if (spotlight_visible) {
        spotlight_ui_tick();
        spotlight_visible = (uint8_t)spotlight_ui_is_open();
    }

    if (dirty_rect_count != 0u) {
        compositor_present();
    }
    if (moved || !cursor_backing_valid || cursor_needs_present) {
        compositor_present_cursor();
    }
}

uint64_t gui_width(void) {
    return gui_framebuffer == NULL ? 0 : gui_framebuffer->width;
}

uint64_t gui_height(void) {
    return gui_framebuffer == NULL ? 0 : gui_framebuffer->height;
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

    const uint64_t x1 = x + w > gui_framebuffer->width ? gui_framebuffer->width : x + w;
    const uint64_t y1 = y + h > gui_framebuffer->height ? gui_framebuffer->height : y + h;
    if (x1 <= x || y1 <= y) {
        return;
    }

    for (uint64_t py = y; py < y1; ++py) {
        const uint32_t src_y = (uint32_t)(((py - y) * src_h) / h);
        for (uint64_t px = x; px < x1; ++px) {
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

    compositor_mark_dirty(x, y, x1 - x, y1 - y);
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
        const uint64_t sy = dy * vex_logo_height / size;
        for (uint64_t dx = 0; dx < size && x + dx < gui_framebuffer->width; ++dx) {
            const uint64_t sx = dx * vex_logo_width / size;
            const uint64_t index = (sy * vex_logo_width + sx) * 4u;
            const uint8_t alpha = vex_logo_rgba[index + 3u];
            if (alpha == 0u) {
                continue;
            }

            const uint32_t over = pack_color(vex_logo_rgba[index], vex_logo_rgba[index + 1u], vex_logo_rgba[index + 2u]);
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

    const uint64_t start_y = GUI_TASKBAR_HEIGHT;
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
    if (gui_framebuffer == NULL || terminal_pid_value == 0u) {
        return;
    }

    restore_mouse_cursor_background();
    browser_active = 0u;
    browser_minimized = 0u;
    terminal_minimized = 0u;
    active_pid = terminal_pid_value;
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

void gui_open_file_manager(void) {
    if (gui_framebuffer == NULL || file_manager_pid_value == 0u) {
        return;
    }

    file_manager_ui_open();
    file_manager_opened = 1u;
    switch_to_pid(file_manager_pid_value);
    gui_flush_scene_and_cursor();
}

void gui_open_editor_path(const char *path) {
    gui_app_command_t command;

    if (gui_framebuffer == NULL || editor_pid_value == 0u) {
        return;
    }

    if (path == NULL || path[0] == '\0') {
        text_editor_ui_new_document();
        editor_has_document = 0u;
    } else {
        command.type = GUI_APP_CMD_OPEN_FILE;
        command.flags = 0u;
        command.x = 0u;
        command.y = 0u;
        command.w = 0u;
        command.h = 0u;
        command.key = 0;
        size_t i = 0;
        while (path[i] != '\0' && i + 1u < sizeof(command.path)) {
            command.path[i] = path[i];
            ++i;
        }
        command.path[i] = '\0';
        text_editor_app_send(&command);
        editor_has_document = 1u;
    }

    editor_opened = 1u;
    switch_to_pid(editor_pid_value);
    gui_flush_scene_and_cursor();
}

void gui_open_editor(void) {
    gui_open_editor_path(NULL);
}

int gui_terminal_surface_visible(void) {
    if (gui_framebuffer == NULL || terminal_minimized) {
        return 0;
    }
    if (browser_active && !browser_minimized) {
        return 0;
    }
    if (paint_active && !paint_minimized) {
        return 0;
    }
    return 1;
}

void gui_loop(void) {
    uint64_t next_frame_tick = irq_timer_ticks();
    uint64_t last_seen_tick = next_frame_tick;
    uint64_t stalled_yields = 0u;

    for (;;) {
        const uint64_t now = irq_timer_ticks();
        if (now != last_seen_tick) {
            last_seen_tick = now;
            stalled_yields = 0u;
        } else if (stalled_yields < 1000u) {
            ++stalled_yields;
        }

        if (now >= next_frame_tick || stalled_yields >= 1000u) {
            gui_tick();
            next_frame_tick = irq_timer_ticks() + GUI_FRAME_INTERVAL_TICKS;
            stalled_yields = 0u;
        }
        process_yield();
    }
}
