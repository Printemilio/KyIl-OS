#include "gui_private.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/elf.h>
#include <unos/heap.h>
#include <unos/tty.h>

void paint_content_rect(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h) {
    *x = browser_win_x + 8u;
    *y = browser_win_y + GUI_TITLE_HEIGHT + 6u;
    *w = browser_win_w > 16u ? browser_win_w - 16u : browser_win_w;
    *h = browser_win_h > GUI_TITLE_HEIGHT + 12u ? browser_win_h - GUI_TITLE_HEIGHT - 12u : 0u;
}

/* Figma Frame 22: the canvas IS the whole content area; the tool palette
   floats over it on the left. */
void paint_canvas_rect(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h) {
    paint_content_rect(x, y, w, h);
}

/* Floating left toolbar — Figma: 72×535 anchored bottom-left of the card */
#define PAINT_CANVAS_COL  pack_color(217, 217, 217)
#define PAINT_TOOLBAR_COL pack_color(39, 39, 39)
#define PAINT_SEP_COL     pack_color(72, 49, 86)

static void paint_toolbar_rect(uint64_t *x, uint64_t *y, uint64_t *w, uint64_t *h) {
    uint64_t cx, cy, cw, ch;
    paint_content_rect(&cx, &cy, &cw, &ch);
    *w = 72u;
    *h = ch > 575u ? 535u : (ch > 80u ? ch - 40u : ch);
    *x = cx + 15u;
    *y = cy + ch - *h - 14u;
}

void paint_clear_canvas(void) {
    const uint32_t canvas_col = PAINT_CANVAS_COL;
    for (uint64_t i = 0; i < paint_canvas_pixels; ++i) {
        paint_canvas[i] = canvas_col;
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

static void draw_paint_toolbar(void);

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

    /* The toolbar floats over the canvas — repaint it if the blit touched it */
    {
        uint64_t tx, ty, tw, th;
        paint_toolbar_rect(&tx, &ty, &tw, &th);
        if (canvas_x + local_x < tx + tw && canvas_x + local_x + width > tx &&
            canvas_y + local_y < ty + th && canvas_y + local_y + height > ty) {
            draw_paint_toolbar();
        }
    }
}

void paint_stamp(uint64_t x, uint64_t y) {
    const uint32_t color = paint_eraser ? PAINT_CANVAS_COL : paint_color;
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

/* Color palette — starts with the Figma trio (red, black, blue);
   "+" appends extra preset colors, up to 6 wells. */
#define PAINT_PALETTE_MAX 6u
static uint8_t paint_palette[PAINT_PALETTE_MAX][3] = {
    { 242, 0, 0 }, { 0, 0, 0 }, { 47, 0, 255 },
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }
};
static uint64_t paint_palette_count = 3u;
static const uint8_t paint_palette_extras[3][3] = {
    { 0, 200, 83 }, { 255, 200, 0 }, { 255, 255, 255 }
};

/* Toolbar vertical layout (relative to toolbar top) */
#define PT_TOOL_ERASER_Y 32u
#define PT_TOOL_CLEAR_Y  72u
#define PT_SEP1_Y        102u
#define PT_COLORS_Y      130u
#define PT_COLOR_PITCH   36u

static uint64_t paint_plus_y(void) { return PT_COLORS_Y + paint_palette_count * PT_COLOR_PITCH + 6u; }
static uint64_t paint_sep2_y(void) { return paint_plus_y() + 30u; }
static uint64_t paint_sizes_y(void) { return paint_sep2_y() + 28u; }

/* Hovered toolbar item: 0 eraser, 1 clear, 10+i colors, 50 plus, 60+i sizes */
static int paint_hover_item = -1;

static uint64_t paint_item_center_y(int item, uint64_t ty) {
    if (item == 0)  { return ty + PT_TOOL_ERASER_Y; }
    if (item == 1)  { return ty + PT_TOOL_CLEAR_Y; }
    if (item >= 10 && item < 50) { return ty + PT_COLORS_Y + (uint64_t)(item - 10) * PT_COLOR_PITCH; }
    if (item == 50) { return ty + paint_plus_y() + 12u; }
    if (item >= 60) { return ty + paint_sizes_y() + 16u + (uint64_t)(item - 60) * 36u; }
    return ty;
}

static void draw_paint_toolbar(void) {
    uint64_t tx, ty, tw, th;
    const uint32_t white = pack_color(255, 255, 255);
    const uint32_t accent = pack_color(205, 99, 244);
    const uint32_t pink = pack_color(244, 114, 182);

    paint_toolbar_rect(&tx, &ty, &tw, &th);
    if (th < 500u) {
        return;  /* window too small for the palette */
    }

    fill_rounded_rect(tx, ty, tw, th, 10u, PAINT_TOOLBAR_COL);

    /* Hover: light rectangular highlight behind the hovered item */
    if (paint_hover_item >= 0) {
        const uint64_t hy = paint_item_center_y(paint_hover_item, ty);
        fill_rounded_rect(tx + 6u, hy > 17u ? hy - 17u : 0u, tw - 12u, 34u, 8u,
                          pack_color(58, 58, 58));
    }

    {
        const uint64_t cx = tx + tw / 2u;

        /* Eraser tool: pink-capped block (ring when active) */
        if (paint_eraser) {
            fill_rounded_rect(cx - 16u, ty + PT_TOOL_ERASER_Y - 14u, 32u, 28u, 8u, accent);
            fill_rounded_rect(cx - 14u, ty + PT_TOOL_ERASER_Y - 12u, 28u, 24u, 7u, PAINT_TOOLBAR_COL);
        }
        fill_rounded_rect(cx - 10u, ty + PT_TOOL_ERASER_Y - 7u, 20u, 7u, 3u, pink);
        fill_rounded_rect(cx - 10u, ty + PT_TOOL_ERASER_Y, 20u, 7u, 3u, white);

        /* Clear: trash can (lid + body + slats) */
        fill_rect(cx - 9u, ty + PT_TOOL_CLEAR_Y - 9u, 18u, 2u, white);            /* lid */
        fill_rect(cx - 3u, ty + PT_TOOL_CLEAR_Y - 12u, 6u, 2u, white);            /* handle */
        fill_rounded_rect(cx - 7u, ty + PT_TOOL_CLEAR_Y - 5u, 14u, 16u, 2u, white);
        fill_rect(cx - 4u, ty + PT_TOOL_CLEAR_Y - 2u, 2u, 10u, PAINT_TOOLBAR_COL);
        fill_rect(cx + 2u, ty + PT_TOOL_CLEAR_Y - 2u, 2u, 10u, PAINT_TOOLBAR_COL);

        fill_rect(tx + 12u, ty + PT_SEP1_Y, tw - 24u, 1u, PAINT_SEP_COL);

        /* Color wells (selection = accent ring) */
        for (uint64_t i = 0u; i < paint_palette_count; ++i) {
            const uint64_t cy = ty + PT_COLORS_Y + i * PT_COLOR_PITCH;
            const uint32_t col = pack_color(paint_palette[i][0], paint_palette[i][1], paint_palette[i][2]);
            if (!paint_eraser && paint_color == col) {
                fill_circle(cx, cy, 16u, accent);
            }
            fill_circle(cx, cy, 13u, col);
        }

        /* "+" → add a color (hidden when the palette is full) */
        if (paint_palette_count < PAINT_PALETTE_MAX) {
            fill_rect(cx - 6u, ty + paint_plus_y() + 6u, 13u, 2u, white);
            fill_rect(cx, ty + paint_plus_y(), 2u, 13u, white);
        }

        fill_rect(tx + 12u, ty + paint_sep2_y(), tw - 24u, 1u, PAINT_SEP_COL);

        /* Brush sizes: 3 growing dots (selection = accent ring) */
        for (uint64_t i = 0u; i < 3u; ++i) {
            const uint64_t radius = i == 0u ? 2u : (i == 1u ? 4u : 7u);
            const uint64_t dot_r = 7u + i * 4u;
            const uint64_t cy = ty + paint_sizes_y() + 16u + i * 36u;
            if (paint_brush_radius == radius) {
                fill_circle(cx, cy, dot_r + 3u, accent);
            }
            fill_circle(cx, cy, dot_r, PAINT_CANVAS_COL);
        }
    }

    compositor_mark_dirty(tx, ty, tw, th);
}

void draw_paint_workspace(void) {
    uint64_t canvas_x;
    uint64_t canvas_y;
    uint64_t canvas_w;
    uint64_t canvas_h;
    const uint32_t text = pack_color(255, 255, 255);

    paint_canvas_rect(&canvas_x, &canvas_y, &canvas_w, &canvas_h);
    if (canvas_w == 0u || canvas_h == 0u) {
        return;
    }
    if (!paint_ensure_canvas(canvas_w, canvas_h)) {
        fill_rect(canvas_x, canvas_y, canvas_w, canvas_h, pack_color(20, 20, 20));
        tty_draw_text_transparent_at(canvas_x + 18u, canvas_y + 18u, "VexPaint: allocation failed", text);
        return;
    }

    paint_blit_canvas_region(0u, 0u, canvas_w, canvas_h);
    draw_paint_toolbar();
}

gui_window_control_t paint_control_hit(uint64_t x, uint64_t y) {
    if (!paint_active || paint_minimized) {
        return GUI_WINDOW_CONTROL_NONE;
    }

    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + browser_win_w - 22u)) {
        return GUI_WINDOW_CONTROL_CLOSE;
    }
    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + browser_win_w - 48u)) {
        return GUI_WINDOW_CONTROL_MINIMIZE;
    }
    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + browser_win_w - 74u)) {
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
    uint64_t canvas_x;
    uint64_t canvas_y;
    uint64_t canvas_w;
    uint64_t canvas_h;
    uint64_t tx, ty, tw, th;

    if (!paint_active || paint_minimized) {
        return 0;
    }

    paint_canvas_rect(&canvas_x, &canvas_y, &canvas_w, &canvas_h);
    paint_toolbar_rect(&tx, &ty, &tw, &th);

    /* Toolbar floats over the canvas — check it first */
    if (x >= tx && x < tx + tw && y >= ty && y < ty + th && th >= 500u) {
        /* Eraser tool */
        if (y >= ty + PT_TOOL_ERASER_Y - 16u && y < ty + PT_TOOL_ERASER_Y + 16u) {
            paint_eraser = !paint_eraser;
            draw_paint_toolbar();
            return 1;
        }
        /* Clear (trash) */
        if (y >= ty + PT_TOOL_CLEAR_Y - 14u && y < ty + PT_TOOL_CLEAR_Y + 14u) {
            paint_clear_canvas();
            draw_paint_workspace();
            return 1;
        }
        /* Color wells */
        for (uint64_t i = 0u; i < paint_palette_count; ++i) {
            const uint64_t cy = ty + PT_COLORS_Y + i * PT_COLOR_PITCH;
            if (y >= cy - 16u && y < cy + 16u) {
                paint_color = pack_color(paint_palette[i][0], paint_palette[i][1], paint_palette[i][2]);
                paint_eraser = 0u;
                draw_paint_toolbar();
                return 1;
            }
        }
        /* "+" → add a preset color to the palette */
        if (paint_palette_count < PAINT_PALETTE_MAX &&
            y >= ty + paint_plus_y() - 6u && y < ty + paint_plus_y() + 20u) {
            const uint64_t n = paint_palette_count - 3u;
            paint_palette[paint_palette_count][0] = paint_palette_extras[n][0];
            paint_palette[paint_palette_count][1] = paint_palette_extras[n][1];
            paint_palette[paint_palette_count][2] = paint_palette_extras[n][2];
            ++paint_palette_count;
            draw_paint_toolbar();
            return 1;
        }
        /* Brush sizes */
        for (uint64_t i = 0u; i < 3u; ++i) {
            const uint64_t radius = i == 0u ? 2u : (i == 1u ? 4u : 7u);
            const uint64_t cy = ty + paint_sizes_y() + 16u + i * 36u;
            if (y >= cy - 17u && y < cy + 17u) {
                paint_brush_radius = radius;
                draw_paint_toolbar();
                return 1;
            }
        }
        return 1;  /* click inside the palette never paints */
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

void paint_handle_hover(uint64_t x, uint64_t y) {
    uint64_t tx, ty, tw, th;
    int item = -1;

    if (!paint_active || paint_minimized) {
        return;
    }
    paint_toolbar_rect(&tx, &ty, &tw, &th);
    if (th >= 500u && x >= tx && x < tx + tw && y >= ty && y < ty + th) {
        if (y >= ty + PT_TOOL_ERASER_Y - 16u && y < ty + PT_TOOL_ERASER_Y + 16u) {
            item = 0;
        } else if (y >= ty + PT_TOOL_CLEAR_Y - 14u && y < ty + PT_TOOL_CLEAR_Y + 14u) {
            item = 1;
        } else if (paint_palette_count < PAINT_PALETTE_MAX &&
                   y >= ty + paint_plus_y() - 6u && y < ty + paint_plus_y() + 20u) {
            item = 50;
        } else {
            for (uint64_t i = 0u; i < paint_palette_count && item < 0; ++i) {
                const uint64_t cy = ty + PT_COLORS_Y + i * PT_COLOR_PITCH;
                if (y >= cy - 16u && y < cy + 16u) { item = 10 + (int)i; }
            }
            for (uint64_t i = 0u; i < 3u && item < 0; ++i) {
                const uint64_t cy = ty + paint_sizes_y() + 16u + i * 36u;
                if (y >= cy - 17u && y < cy + 17u) { item = 60 + (int)i; }
            }
        }
    }

    if (item != paint_hover_item) {
        paint_hover_item = item;
        draw_paint_toolbar();
    }
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

    if (elf_spawn("/home/root/bin/vex_paint") != 0) {
        tty_puts("paint launch failed\n");
    }
}
