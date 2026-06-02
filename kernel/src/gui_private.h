#ifndef GUI_PRIVATE_H
#define GUI_PRIVATE_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/keyboard.h>

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

extern struct limine_framebuffer *gui_framebuffer;
extern uint32_t *compositor_scene;
extern uint8_t gui_compositor_ready;
extern uint64_t terminal_win_x;
extern uint64_t terminal_win_y;
extern uint64_t terminal_win_w;
extern uint64_t terminal_win_h;
extern uint8_t terminal_minimized;
extern uint8_t terminal_maximized;
extern uint64_t terminal_restore_x;
extern uint64_t terminal_restore_y;
extern uint64_t terminal_restore_w;
extern uint64_t terminal_restore_h;
extern uint32_t *terminal_snapshot;
extern uint64_t terminal_snapshot_pixels;
extern uint8_t terminal_snapshot_valid;
extern uint32_t terminal_pid_value;
extern uint32_t file_manager_pid_value;
extern uint32_t editor_pid_value;
extern uint32_t spotlight_pid_value;
extern uint32_t active_pid;
extern uint32_t browser_previous_pid;
extern uint8_t file_manager_opened;
extern uint8_t editor_opened;
extern uint8_t browser_previous_paint;
extern uint8_t editor_has_document;
extern uint8_t spotlight_visible;
extern uint8_t browser_active;
extern uint8_t browser_minimized;
extern uint8_t browser_native_home_visible;
extern uint64_t browser_win_x;
extern uint64_t browser_win_y;
extern uint64_t browser_win_w;
extern uint64_t browser_win_h;
extern char browser_address[GUI_BROWSER_ADDRESS_MAX];
extern uint64_t browser_address_len;
extern uint8_t browser_address_focused;
extern uint8_t browser_tab_count;
extern uint8_t browser_results_drawn;
extern uint8_t browser_loading_visible;
extern char browser_loading_text[GUI_BROWSER_ADDRESS_MAX];
extern uint8_t paint_active;
extern uint8_t paint_minimized;
extern uint8_t paint_drawing;
extern uint8_t paint_eraser;
extern uint8_t paint_previous_browser;
extern uint32_t paint_previous_pid;
extern uint32_t *paint_canvas;
extern uint64_t paint_canvas_width;
extern uint64_t paint_canvas_height;
extern uint64_t paint_canvas_pixels;
extern uint32_t paint_color;
extern uint64_t paint_brush_radius;
extern uint64_t paint_last_x;
extern uint64_t paint_last_y;

uint32_t pack_color(uint8_t r, uint8_t g, uint8_t b);
void fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);
void fill_circle(uint64_t cx, uint64_t cy, uint64_t r, uint32_t color);
void fill_rounded_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t r, uint32_t color);
void draw_taskbar_static(void);
void compositor_mark_dirty(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
void gui_flush_scene_and_cursor(void);
void put_pixel(uint64_t x, uint64_t y, uint32_t color);
void render_active_app(void);
void switch_to_pid(uint32_t pid);
void send_app_config(uint32_t pid, uint8_t active, uint8_t refresh);
void save_terminal_snapshot(void);
void restore_mouse_cursor_background(void);
int window_control_circle_hit_at(uint64_t x, uint64_t y, uint64_t window_y, uint64_t center_x);

void draw_browser_shell(void);
void draw_browser_native_home(void);
void draw_browser_native_results(void);
void draw_browser_loading_state(void);
void draw_browser_text_clipped(uint64_t x, uint64_t y, const char *text, uint32_t color, uint64_t max_width);
void browser_shell_set_address(const char *address);
void browser_submit_address_to_web(const char *address);
void browser_submit_content_click(uint64_t x, uint64_t y);
void browser_shell_handle_key(char key);
gui_window_control_t browser_control_hit(uint64_t x, uint64_t y);
void set_browser_minimized(uint8_t minimized);
int browser_handle_shell_click(uint64_t x, uint64_t y);
void gui_browser_begin(void);
void gui_browser_end(void);
void gui_browser_results_changed(void);
void gui_browser_frame_ready(void);
uint64_t gui_content_x(void);
uint64_t gui_content_y(void);
uint64_t gui_content_width(void);
uint64_t gui_content_height(void);

void draw_paint_workspace(void);
void paint_content_rect(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h);
void paint_canvas_rect(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h);
void paint_clear_canvas(void);
int paint_ensure_canvas(uint64_t width, uint64_t height);
void paint_blit_canvas_region(uint64_t local_x, uint64_t local_y, uint64_t width, uint64_t height);
void paint_stamp(uint64_t x, uint64_t y);
void paint_stroke_line(uint64_t x0, uint64_t y0, uint64_t x1, uint64_t y1);
gui_window_control_t paint_control_hit(uint64_t x, uint64_t y);
void set_paint_minimized(uint8_t minimized);
int paint_handle_click(uint64_t x, uint64_t y);
void paint_continue_stroke(uint64_t x, uint64_t y);
void paint_stop_stroke(void);
void gui_open_paint(void);

#endif
