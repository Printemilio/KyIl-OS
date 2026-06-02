#include "gui_private.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/heap.h>
#include <unos/tty.h>

void paint_content_rect(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h) {
    *x = browser_win_x + 8u;
    *y = browser_win_y + GUI_TITLE_HEIGHT + 6u;
    *w = browser_win_w > 16u ? browser_win_w - 16u : browser_win_w;
    *h = browser_win_h > GUI_TITLE_HEIGHT + 12u ? browser_win_h - GUI_TITLE_HEIGHT - 12u : 0u;
}

void paint_canvas_rect(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h) {
    uint64_t content_x;
    uint64_t content_y;
    uint64_t content_w;
    uint64_t content_h;

    paint_content_rect(&content_x, &content_y, &content_w, &content_h);
    *x = content_x + 10u;
    *y = content_y + GUI_PAINT_TOOLBAR_HEIGHT + 8u;
    *w = content_w > 20u ? content_w - 20u : 0u;
    *h = content_h > GUI_PAINT_TOOLBAR_HEIGHT + 18u ? content_h - GUI_PAINT_TOOLBAR_HEIGHT - 18u : 0u;
}

void paint_clear_canvas(void) {
    const uint32_t white = pack_color(255, 255, 255);
    for (uint64_t i = 0; i < paint_canvas_pixels; ++i) {
        paint_canvas[i] = white;
    }
}

int paint_ensure_canvas(uint64_t width, uint64_t height) {
    const uint64_t needed = width * height;
    uint32_t *new_canvas;

    if (width == 0u || height == 0u) {
        return 0;
    }
    if (paint_canvas != NULL && paint_canvas_width == width && paint_canvas_height == height) {
        return 1;
    }

    new_canvas = kmalloc(sizeof(uint32_t) * needed);
    if (new_canvas == NULL) {
        return 0;
    }
    if (paint_canvas != NULL) {
        kfree(paint_canvas);
    }

    paint_canvas = new_canvas;
    paint_canvas_width = width;
    paint_canvas_height = height;
    paint_canvas_pixels = needed;
    paint_clear_canvas();
    return 1;
}

void paint_blit_canvas_region(uint64_t local_x, uint64_t local_y, uint64_t width, uint64_t height) {
    uint64_t canvas_x;
    uint64_t canvas_y;
    uint64_t canvas_w;
    uint64_t canvas_h;

    if (paint_canvas == NULL || width == 0u || height == 0u) {
        return;
    }

    paint_canvas_rect(&canvas_x, &canvas_y, &canvas_w, &canvas_h);
    if (canvas_w == 0u || canvas_h == 0u || local_x >= canvas_w || local_y >= canvas_h) {
        return;
    }

    if (local_x + width > canvas_w) {
        width = canvas_w - local_x;
    }
    if (local_y + height > canvas_h) {
        height = canvas_h - local_y;
    }

    if (gui_compositor_ready && compositor_scene != NULL && gui_framebuffer != NULL) {
        const size_t row_bytes = (size_t)width * sizeof(uint32_t);
        for (uint64_t y = 0; y < height; ++y) {
            uint32_t *dst = compositor_scene + (canvas_y + local_y + y) * gui_framebuffer->width + (canvas_x + local_x);
            const uint32_t *src = paint_canvas + (local_y + y) * paint_canvas_width + local_x;
            memcpy(dst, src, row_bytes);
        }
    } else {
        for (uint64_t y = 0; y < height; ++y) {
            for (uint64_t x = 0; x < width; ++x) {
                put_pixel(canvas_x + local_x + x, canvas_y + local_y + y,
                    paint_canvas[(local_y + y) * paint_canvas_width + (local_x + x)]);
            }
        }
    }

    compositor_mark_dirty(canvas_x + local_x, canvas_y + local_y, width, height);
}

void paint_stamp(uint64_t x, uint64_t y) {
    const uint32_t color = paint_eraser ? pack_color(255, 255, 255) : paint_color;
    const uint64_t radius = paint_brush_radius == 0u ? 1u : paint_brush_radius;
    uint64_t min_x = x > radius ? x - radius : 0u;
    uint64_t min_y = y > radius ? y - radius : 0u;
    uint64_t max_x = x + radius + 1u;
    uint64_t max_y = y + radius + 1u;

    if (paint_canvas == NULL || x >= paint_canvas_width || y >= paint_canvas_height) {
        return;
    }
    if (max_x > paint_canvas_width) {
        max_x = paint_canvas_width;
    }
    if (max_y > paint_canvas_height) {
        max_y = paint_canvas_height;
    }

    for (uint64_t py = min_y; py < max_y; ++py) {
        for (uint64_t px = min_x; px < max_x; ++px) {
            const int64_t dx = (int64_t)px - (int64_t)x;
            const int64_t dy = (int64_t)py - (int64_t)y;
            if ((uint64_t)(dx * dx + dy * dy) <= radius * radius) {
                paint_canvas[py * paint_canvas_width + px] = color;
            }
        }
    }

    paint_blit_canvas_region(min_x, min_y, max_x - min_x, max_y - min_y);
}

void paint_stroke_line(uint64_t x0, uint64_t y0, uint64_t x1, uint64_t y1) {
    int64_t dx = x1 > x0 ? (int64_t)(x1 - x0) : (int64_t)(x0 - x1);
    int64_t sx = x0 < x1 ? 1 : -1;
    int64_t dy = y1 > y0 ? -(int64_t)(y1 - y0) : -(int64_t)(y0 - y1);
    int64_t sy = y0 < y1 ? 1 : -1;
    int64_t err = dx + dy;
    int64_t current_x = (int64_t)x0;
    int64_t current_y = (int64_t)y0;

    for (;;) {
        paint_stamp((uint64_t)current_x, (uint64_t)current_y);
        if (current_x == (int64_t)x1 && current_y == (int64_t)y1) {
            break;
        }
        {
            const int64_t twice = err * 2;
            if (twice >= dy) {
                err += dy;
                current_x += sx;
            }
            if (twice <= dx) {
                err += dx;
                current_y += sy;
            }
        }
    }
}

void draw_paint_workspace(void) {
    uint64_t content_x;
    uint64_t content_y;
    uint64_t content_w;
    uint64_t content_h;
    uint64_t canvas_x;
    uint64_t canvas_y;
    uint64_t canvas_w;
    uint64_t canvas_h;
    const uint32_t bg = pack_color(14, 10, 24);
    const uint32_t panel = pack_color(26, 18, 46);
    const uint32_t border = pack_color(110, 58, 188);
    const uint32_t text = pack_color(228, 220, 248);
    const uint32_t muted = pack_color(150, 130, 185);
    const uint32_t white = pack_color(255, 255, 255);
    static const uint8_t swatches[6][3] = {
        { 18, 18, 22 }, { 255, 255, 255 }, { 224, 68, 88 },
        { 72, 132, 248 }, { 64, 196, 128 }, { 255, 188, 72 }
    };

    paint_content_rect(&content_x, &content_y, &content_w, &content_h);
    paint_canvas_rect(&canvas_x, &canvas_y, &canvas_w, &canvas_h);
    if (content_w == 0u || content_h == 0u || canvas_w == 0u || canvas_h == 0u) {
        return;
    }
    if (!paint_ensure_canvas(canvas_w, canvas_h)) {
        fill_rect(content_x, content_y, content_w, content_h, bg);
        tty_draw_text_transparent_at(content_x + 18u, content_y + 18u, "VexPaint: allocation failed", text);
        return;
    }

    fill_rect(content_x, content_y, content_w, content_h, bg);
    fill_rounded_rect(content_x, content_y, content_w, GUI_PAINT_TOOLBAR_HEIGHT, 8u, panel);
    tty_draw_text_transparent_at(content_x + 14u, content_y + 15u, "VexPaint", text);
    tty_draw_text_transparent_at(content_x + 96u, content_y + 15u, paint_eraser ? "Tool: Eraser" : "Tool: Brush", muted);

    for (uint64_t i = 0; i < 6u; ++i) {
        const uint64_t swatch_x = content_x + 220u + i * 28u;
        const uint32_t swatch = pack_color(swatches[i][0], swatches[i][1], swatches[i][2]);
        fill_rounded_rect(swatch_x, content_y + 12u, GUI_PAINT_SWATCH_SIZE, GUI_PAINT_SWATCH_SIZE, 5u, swatch);
        if (!paint_eraser && paint_color == swatch) {
            fill_rounded_rect(swatch_x - 2u, content_y + 10u, GUI_PAINT_SWATCH_SIZE + 4u, GUI_PAINT_SWATCH_SIZE + 4u, 6u, white);
            fill_rounded_rect(swatch_x, content_y + 12u, GUI_PAINT_SWATCH_SIZE, GUI_PAINT_SWATCH_SIZE, 5u, swatch);
        }
    }

    fill_rounded_rect(content_x + content_w - 196u, content_y + 10u, 56u, 24u, 6u, paint_eraser ? pack_color(210, 60, 100) : pack_color(36, 26, 60));
    tty_draw_text_transparent_at(content_x + content_w - 184u, content_y + 17u, "Gomme", paint_eraser ? white : muted);
    fill_rounded_rect(content_x + content_w - 132u, content_y + 10u, 52u, 24u, 6u, pack_color(36, 26, 60));
    tty_draw_text_transparent_at(content_x + content_w - 120u, content_y + 17u, "Clear", muted);

    for (uint64_t i = 0; i < 3u; ++i) {
        const uint64_t radius = i == 0u ? 2u : (i == 1u ? 4u : 7u);
        const uint64_t button_x = content_x + content_w - 72u + i * 22u;
        fill_rounded_rect(button_x, content_y + 10u, 18u, 24u, 5u, paint_brush_radius == radius ? pack_color(140, 60, 220) : pack_color(36, 26, 60));
        fill_circle(button_x + 9u, content_y + 22u, radius, paint_brush_radius == radius ? white : muted);
    }

    fill_rect(canvas_x - 1u, canvas_y - 1u, canvas_w + 2u, canvas_h + 2u, border);
    paint_blit_canvas_region(0u, 0u, canvas_w, canvas_h);
}

gui_window_control_t paint_control_hit(uint64_t x, uint64_t y) {
    if (!paint_active || paint_minimized) {
        return GUI_WINDOW_CONTROL_NONE;
    }

    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + 16u)) {
        return GUI_WINDOW_CONTROL_CLOSE;
    }
    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + 32u)) {
        return GUI_WINDOW_CONTROL_MINIMIZE;
    }
    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + 48u)) {
        return GUI_WINDOW_CONTROL_MAXIMIZE;
    }

    return GUI_WINDOW_CONTROL_NONE;
}

void set_paint_minimized(uint8_t minimized) {
    if (gui_framebuffer == NULL || !paint_active || paint_minimized == minimized) {
        return;
    }

    if (minimized) {
        paint_minimized = 1u;
        paint_drawing = 0u;
        if (paint_previous_browser && browser_active) {
            browser_minimized = 0u;
            active_pid = 0u;
        } else if (active_pid == 0u) {
            active_pid = paint_previous_pid != 0u ? paint_previous_pid : terminal_pid_value;
        }
        render_active_app();
        draw_taskbar_static();
        return;
    }

    paint_minimized = 0u;
    if (paint_previous_browser && browser_active) {
        browser_minimized = 1u;
    }
    render_active_app();
    draw_taskbar_static();
}

int paint_handle_click(uint64_t x, uint64_t y) {
    uint64_t content_x;
    uint64_t content_y;
    uint64_t content_w;
    uint64_t content_h;
    uint64_t canvas_x;
    uint64_t canvas_y;
    uint64_t canvas_w;
    uint64_t canvas_h;
    static const uint8_t swatches[6][3] = {
        { 18, 18, 22 }, { 255, 255, 255 }, { 224, 68, 88 },
        { 72, 132, 248 }, { 64, 196, 128 }, { 255, 188, 72 }
    };

    if (!paint_active || paint_minimized) {
        return 0;
    }

    paint_content_rect(&content_x, &content_y, &content_w, &content_h);
    paint_canvas_rect(&canvas_x, &canvas_y, &canvas_w, &canvas_h);

    if (y >= content_y + 12u && y < content_y + 32u) {
        for (uint64_t i = 0; i < 6u; ++i) {
            const uint64_t swatch_x = content_x + 220u + i * 28u;
            if (x >= swatch_x && x < swatch_x + GUI_PAINT_SWATCH_SIZE) {
                paint_color = pack_color(swatches[i][0], swatches[i][1], swatches[i][2]);
                paint_eraser = 0u;
                draw_paint_workspace();
                return 1;
            }
        }
    }

    if (x >= content_x + content_w - 196u && x < content_x + content_w - 140u &&
        y >= content_y + 10u && y < content_y + 34u) {
        paint_eraser = 1u;
        draw_paint_workspace();
        return 1;
    }

    if (x >= content_x + content_w - 132u && x < content_x + content_w - 80u &&
        y >= content_y + 10u && y < content_y + 34u) {
        paint_clear_canvas();
        draw_paint_workspace();
        return 1;
    }

    for (uint64_t i = 0; i < 3u; ++i) {
        const uint64_t radius = i == 0u ? 2u : (i == 1u ? 4u : 7u);
        const uint64_t button_x = content_x + content_w - 72u + i * 22u;
        if (x >= button_x && x < button_x + 18u && y >= content_y + 10u && y < content_y + 34u) {
            paint_brush_radius = radius;
            draw_paint_workspace();
            return 1;
        }
    }

    if (x >= canvas_x && x < canvas_x + canvas_w && y >= canvas_y && y < canvas_y + canvas_h) {
        paint_drawing = 1u;
        paint_last_x = x - canvas_x;
        paint_last_y = y - canvas_y;
        paint_stamp(paint_last_x, paint_last_y);
        return 1;
    }

    return 0;
}

void paint_continue_stroke(uint64_t x, uint64_t y) {
    uint64_t canvas_x;
    uint64_t canvas_y;
    uint64_t canvas_w;
    uint64_t canvas_h;
    uint64_t local_x;
    uint64_t local_y;

    if (!paint_drawing || !paint_active || paint_minimized) {
        return;
    }

    paint_canvas_rect(&canvas_x, &canvas_y, &canvas_w, &canvas_h);
    if (x < canvas_x || y < canvas_y || x >= canvas_x + canvas_w || y >= canvas_y + canvas_h) {
        return;
    }

    local_x = x - canvas_x;
    local_y = y - canvas_y;
    paint_stroke_line(paint_last_x, paint_last_y, local_x, local_y);
    paint_last_x = local_x;
    paint_last_y = local_y;
}

void paint_stop_stroke(void) {
    paint_drawing = 0u;
}

void gui_open_paint(void) {
    if (gui_framebuffer == NULL) {
        return;
    }

    restore_mouse_cursor_background();
    if (!terminal_minimized && active_pid == terminal_pid_value) {
        save_terminal_snapshot();
    }

    paint_previous_browser = (browser_active && !browser_minimized) ? 1u : 0u;
    if (paint_previous_browser) {
        browser_minimized = 1u;
    }
    paint_previous_pid = active_pid;
    active_pid = 0u;
    paint_active = 1u;
    paint_minimized = 0u;
    paint_drawing = 0u;
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}
