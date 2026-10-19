#ifndef UNOS_GUI_H
#define UNOS_GUI_H

#include <stddef.h>
#include <stdint.h>

#include <unos/compositor_surface.h>
#include <unos/kernel_guard.h>
typedef struct gui_state_guard {kernel_guard_t memory,scene;} gui_state_guard_t;
gui_state_guard_t gui_state_begin(void);
void gui_state_end(gui_state_guard_t *guard);
#define GUI_GUARD gui_state_guard_t gui_guard __attribute__((cleanup(gui_state_end)))=gui_state_begin()

struct limine_framebuffer;

void gui_init(void);
int gui_compositor_init_headless(struct limine_framebuffer *framebuffer);
void gui_set_terminal_pid(uint32_t pid);
void gui_loop(void);
void gui_tick(void);
void gui_performance_benchmark(void);
/* Notify the compositor loop that queued input or presentation work is ready. */
void gui_wake(void);
void gui_input_wake(void);
uint32_t gui_service_tid(void);
void render_active_app(void);
void draw_taskbar_static(void);
void gui_flush_scene_and_cursor(void);
uint64_t gui_width(void);
uint64_t gui_height(void);
int gui_compositor_get_target(compositor_target_t *target_out);
void gui_compositor_damage(const vex_rect_t *damage);
void gui_compositor_restore_foreground(const vex_rect_t *damage);
int gui_browser_store_presented(const vex_rect_t *bounds);
uint32_t gui_browser_active_tab(void);
int gui_browser_has_presented(uint32_t tab_id);
void gui_browser_release_tab_presented(uint32_t tab_id);
/* Keep a tab's last frame but stop showing it while a navigation is pending. */
void gui_browser_suspend_tab_presented(uint32_t tab_id);
void gui_browser_resume_tab_presented(uint32_t tab_id);
void gui_browser_release_presented(void);
uint32_t gui_color_rgb(uint8_t r, uint8_t g, uint8_t b);
/* 0xRRGGBB theme colour (unos/ui_theme.h) in the framebuffer's format. */
uint32_t gui_theme_color(uint32_t rgb);
/* Switches the system palette, repaints the shell and notifies every window.
 * mode is a ui_theme_mode_t. Returns 1 when the theme changed. */
int gui_set_theme(int mode);
void gui_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);
void gui_text_put_pixel(uint64_t x, uint64_t y, uint32_t color);
void gui_sync_rect_from_framebuffer(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
void gui_terminal_snapshot_refresh(void);
void gui_terminal_snapshot_refresh_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
void gui_blit_argb_scaled(uint64_t x, uint64_t y, uint64_t w, uint64_t h, const uint32_t *pixels, uint32_t src_w, uint32_t src_h, uint32_t stride);
void gui_blit_argb_scaled_partial(uint64_t x, uint64_t y, uint64_t w, uint64_t h, const uint32_t *pixels, uint32_t src_w, uint32_t src_h, uint32_t stride, uint32_t clip_x, uint32_t clip_y, uint32_t clip_w, uint32_t clip_h);
void gui_fill_circle(uint64_t cx, uint64_t cy, uint64_t radius, uint32_t color);
void gui_fill_rounded_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t radius, uint32_t color);
void gui_fill_rounded_rect_horizontal_gradient(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t radius, uint32_t left, uint32_t right);
void gui_draw_rounded_border(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t radius, uint32_t color);
void gui_draw_border(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);
void gui_redraw_cursor_overlay(void);
void gui_restore_after_overlay(void);
void gui_blur_desktop_background(void);
void gui_restore_blurred_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
void gui_draw_vex_logo(uint64_t x, uint64_t y, uint64_t size);
void gui_font_text(uint64_t x, uint64_t y, const char *text, uint32_t color, uint8_t size);
void gui_font_text_clipped(uint64_t x, uint64_t y, const char *text,
                           uint32_t color, uint8_t size, uint64_t max_w);
uint64_t gui_font_text_width(const char *text, uint8_t size);
uint8_t gui_font_cell_height(uint8_t size);
void gui_focus_terminal_for_launch(void);
void gui_open_file_manager(void);
void gui_open_file_manager_path(const char *path);
void gui_open_editor(void);
void gui_open_editor_path(const char *path);
void gui_open_path(const char *path);
int gui_claim_open_context(uint32_t application, uint32_t pid,
                           char *buffer, size_t size);
void gui_open_paint(void);
void gui_browser_begin(void);
void gui_browser_launch(void);
void gui_browser_end(void);
/* Queue a cancellation for a running download; 0 when queued. */
int gui_browser_download_cancel(uint32_t id);
void gui_browser_results_changed(void);
void gui_browser_frame_ready(void);
void gui_elevation_prompt_changed(void);
int gui_terminal_surface_visible(void);
uint64_t gui_content_x(void);
uint64_t gui_content_y(void);
uint64_t gui_content_width(void);
uint64_t gui_content_height(void);

#endif
