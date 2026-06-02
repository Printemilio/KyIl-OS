#ifndef UNOS_GUI_H
#define UNOS_GUI_H

#include <stdint.h>

void gui_init(void);
void gui_set_terminal_pid(uint32_t pid);
void gui_loop(void);
void gui_tick(void);
uint64_t gui_width(void);
uint64_t gui_height(void);
uint32_t gui_color_rgb(uint8_t r, uint8_t g, uint8_t b);
void gui_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);
void gui_text_put_pixel(uint64_t x, uint64_t y, uint32_t color);
void gui_sync_rect_from_framebuffer(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
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
void gui_focus_terminal_for_launch(void);
void gui_open_file_manager(void);
void gui_open_editor(void);
void gui_open_editor_path(const char *path);
void gui_open_paint(void);
void gui_browser_begin(void);
void gui_browser_end(void);
void gui_browser_results_changed(void);
void gui_browser_frame_ready(void);
int gui_terminal_surface_visible(void);
uint64_t gui_content_x(void);
uint64_t gui_content_y(void);
uint64_t gui_content_width(void);
uint64_t gui_content_height(void);

#endif
