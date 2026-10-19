#include <unos/tty.h>
#include <unos/console.h>

#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/boot_requests.h>
#include <unos/gui.h>
#include "vex_font_inter.h"

#define FONT_BITMAP_WIDTH 8ull
#define FONT_BITMAP_HEIGHT 8ull
#define FONT_WIDTH 13ull
#define FONT_HEIGHT ((uint64_t)INTER13_CELL_H)
#define TTY_LVGL_CELL_CAPACITY 16384u
#define TTY_LVGL_TEXT_CAPACITY (TTY_LVGL_CELL_CAPACITY + 256u)

static const uint8_t font[128][8] = {
    [' '] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['!'] = { 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00 },
    ['"'] = { 0x66, 0x66, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['#'] = { 0x36, 0x36, 0x7f, 0x36, 0x7f, 0x36, 0x36, 0x00 },
    ['$'] = { 0x18, 0x3e, 0x60, 0x3c, 0x06, 0x7c, 0x18, 0x00 },
    ['%'] = { 0x62, 0x66, 0x0c, 0x18, 0x30, 0x66, 0x46, 0x00 },
    ['&'] = { 0x38, 0x6c, 0x38, 0x76, 0xdc, 0xcc, 0x76, 0x00 },
    ['\''] = { 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['('] = { 0x0c, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0c, 0x00 },
    [')'] = { 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x18, 0x30, 0x00 },
    ['*'] = { 0x00, 0x66, 0x3c, 0xff, 0x3c, 0x66, 0x00, 0x00 },
    ['+'] = { 0x00, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x00, 0x00 },
    [','] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30 },
    ['-'] = { 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00 },
    ['.'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 },
    ['/'] = { 0x02, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x40, 0x00 },
    [':'] = { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00 },
    [';'] = { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30 },
    ['<'] = { 0x0c, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0c, 0x00 },
    ['='] = { 0x00, 0x00, 0x7e, 0x00, 0x7e, 0x00, 0x00, 0x00 },
    ['>'] = { 0x30, 0x18, 0x0c, 0x06, 0x0c, 0x18, 0x30, 0x00 },
    ['?'] = { 0x3c, 0x66, 0x06, 0x0c, 0x18, 0x00, 0x18, 0x00 },
    ['@'] = { 0x3c, 0x66, 0x6e, 0x6a, 0x6e, 0x60, 0x3e, 0x00 },
    ['0'] = { 0x3c, 0x66, 0x6e, 0x76, 0x66, 0x66, 0x3c, 0x00 },
    ['1'] = { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x00 },
    ['2'] = { 0x3c, 0x66, 0x06, 0x1c, 0x30, 0x66, 0x7e, 0x00 },
    ['3'] = { 0x3c, 0x66, 0x06, 0x1c, 0x06, 0x66, 0x3c, 0x00 },
    ['4'] = { 0x0c, 0x1c, 0x3c, 0x6c, 0x7e, 0x0c, 0x0c, 0x00 },
    ['5'] = { 0x7e, 0x60, 0x7c, 0x06, 0x06, 0x66, 0x3c, 0x00 },
    ['6'] = { 0x1c, 0x30, 0x60, 0x7c, 0x66, 0x66, 0x3c, 0x00 },
    ['7'] = { 0x7e, 0x66, 0x06, 0x0c, 0x18, 0x18, 0x18, 0x00 },
    ['8'] = { 0x3c, 0x66, 0x66, 0x3c, 0x66, 0x66, 0x3c, 0x00 },
    ['9'] = { 0x3c, 0x66, 0x66, 0x3e, 0x06, 0x0c, 0x38, 0x00 },
    ['A'] = { 0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x00 },
    ['B'] = { 0x7c, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x7c, 0x00 },
    ['C'] = { 0x3c, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3c, 0x00 },
    ['D'] = { 0x78, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0x78, 0x00 },
    ['E'] = { 0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x7e, 0x00 },
    ['F'] = { 0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0x00 },
    ['G'] = { 0x3c, 0x66, 0x60, 0x6e, 0x66, 0x66, 0x3c, 0x00 },
    ['H'] = { 0x66, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66, 0x00 },
    ['I'] = { 0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00 },
    ['J'] = { 0x1e, 0x0c, 0x0c, 0x0c, 0x0c, 0x6c, 0x38, 0x00 },
    ['K'] = { 0x66, 0x6c, 0x78, 0x70, 0x78, 0x6c, 0x66, 0x00 },
    ['L'] = { 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7e, 0x00 },
    ['M'] = { 0x63, 0x77, 0x7f, 0x6b, 0x63, 0x63, 0x63, 0x00 },
    ['N'] = { 0x66, 0x76, 0x7e, 0x7e, 0x6e, 0x66, 0x66, 0x00 },
    ['O'] = { 0x3c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00 },
    ['P'] = { 0x7c, 0x66, 0x66, 0x7c, 0x60, 0x60, 0x60, 0x00 },
    ['Q'] = { 0x3c, 0x66, 0x66, 0x66, 0x6a, 0x6c, 0x36, 0x00 },
    ['R'] = { 0x7c, 0x66, 0x66, 0x7c, 0x78, 0x6c, 0x66, 0x00 },
    ['S'] = { 0x3c, 0x66, 0x60, 0x3c, 0x06, 0x66, 0x3c, 0x00 },
    ['T'] = { 0x7e, 0x5a, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00 },
    ['U'] = { 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00 },
    ['V'] = { 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x18, 0x00 },
    ['W'] = { 0x63, 0x63, 0x63, 0x6b, 0x7f, 0x77, 0x63, 0x00 },
    ['X'] = { 0x66, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0x66, 0x00 },
    ['Y'] = { 0x66, 0x66, 0x66, 0x3c, 0x18, 0x18, 0x3c, 0x00 },
    ['Z'] = { 0x7e, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x7e, 0x00 },
    ['['] = { 0x3c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3c, 0x00 },
    ['\\'] = { 0x40, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x02, 0x00 },
    [']'] = { 0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x3c, 0x00 },
    ['^'] = { 0x18, 0x3c, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['_'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00 },
    ['`'] = { 0x30, 0x18, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['a'] = { 0x00, 0x00, 0x3c, 0x06, 0x3e, 0x66, 0x3e, 0x00 },
    ['b'] = { 0x60, 0x60, 0x7c, 0x66, 0x66, 0x66, 0x7c, 0x00 },
    ['c'] = { 0x00, 0x00, 0x3c, 0x66, 0x60, 0x66, 0x3c, 0x00 },
    ['d'] = { 0x06, 0x06, 0x3e, 0x66, 0x66, 0x66, 0x3e, 0x00 },
    ['e'] = { 0x00, 0x00, 0x3c, 0x66, 0x7e, 0x60, 0x3c, 0x00 },
    ['f'] = { 0x1c, 0x36, 0x30, 0x78, 0x30, 0x30, 0x30, 0x00 },
    ['g'] = { 0x00, 0x00, 0x3e, 0x66, 0x66, 0x3e, 0x06, 0x7c },
    ['h'] = { 0x60, 0x60, 0x7c, 0x66, 0x66, 0x66, 0x66, 0x00 },
    ['i'] = { 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3c, 0x00 },
    ['j'] = { 0x06, 0x00, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3c },
    ['k'] = { 0x60, 0x60, 0x66, 0x6c, 0x78, 0x6c, 0x66, 0x00 },
    ['l'] = { 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00 },
    ['m'] = { 0x00, 0x00, 0x66, 0x7f, 0x7f, 0x6b, 0x63, 0x00 },
    ['n'] = { 0x00, 0x00, 0x7c, 0x66, 0x66, 0x66, 0x66, 0x00 },
    ['o'] = { 0x00, 0x00, 0x3c, 0x66, 0x66, 0x66, 0x3c, 0x00 },
    ['p'] = { 0x00, 0x00, 0x7c, 0x66, 0x66, 0x7c, 0x60, 0x60 },
    ['q'] = { 0x00, 0x00, 0x3e, 0x66, 0x66, 0x3e, 0x06, 0x06 },
    ['r'] = { 0x00, 0x00, 0x6c, 0x76, 0x60, 0x60, 0x60, 0x00 },
    ['s'] = { 0x00, 0x00, 0x3e, 0x60, 0x3c, 0x06, 0x7c, 0x00 },
    ['t'] = { 0x30, 0x30, 0x78, 0x30, 0x30, 0x36, 0x1c, 0x00 },
    ['u'] = { 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3e, 0x00 },
    ['v'] = { 0x00, 0x00, 0x66, 0x66, 0x66, 0x3c, 0x18, 0x00 },
    ['w'] = { 0x00, 0x00, 0x63, 0x6b, 0x7f, 0x3e, 0x36, 0x00 },
    ['x'] = { 0x00, 0x00, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0x00 },
    ['y'] = { 0x00, 0x00, 0x66, 0x66, 0x66, 0x3e, 0x0c, 0x78 },
    ['z'] = { 0x00, 0x00, 0x7e, 0x0c, 0x18, 0x30, 0x7e, 0x00 },
    ['{'] = { 0x0e, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0e, 0x00 },
    ['|'] = { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00 },
    ['}'] = { 0x70, 0x18, 0x18, 0x0e, 0x18, 0x18, 0x70, 0x00 },
    ['~'] = { 0x76, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static struct limine_framebuffer *tty_framebuffer;
static uint64_t columns;
static uint64_t rows;
static uint64_t cursor_x;
static uint64_t cursor_y;
static uint64_t region_x;
static uint64_t region_y;
static uint64_t region_width;
static uint64_t region_height;
static uint32_t foreground;
static uint32_t background;
static uint8_t foreground_r;
static uint8_t foreground_g;
static uint8_t foreground_b;
static uint8_t background_r;
static uint8_t background_g;
static uint8_t background_b;
static uint8_t stream_output_enabled = 1u;
static uint8_t bitmap_render_enabled = 1u;
static char tty_cells[TTY_LVGL_CELL_CAPACITY];
static char tty_lvgl_text_buffer[TTY_LVGL_TEXT_CAPACITY];
static uint64_t tty_cell_columns;
static uint64_t tty_cell_rows;

static void tty_cells_clear(void) {
    const uint64_t count = tty_cell_columns * tty_cell_rows;
    for (uint64_t i = 0u; i < count && i < TTY_LVGL_CELL_CAPACITY; ++i) {
        tty_cells[i] = ' ';
    }
}

static void tty_cells_scroll(void) {
    if (tty_cell_rows == 0u || tty_cell_columns == 0u) {
        return;
    }
    for (uint64_t row = 1u; row < tty_cell_rows; ++row) {
        for (uint64_t col = 0u; col < tty_cell_columns; ++col) {
            tty_cells[(row - 1u) * tty_cell_columns + col] =
                tty_cells[row * tty_cell_columns + col];
        }
    }
    for (uint64_t col = 0u; col < tty_cell_columns; ++col) {
        tty_cells[(tty_cell_rows - 1u) * tty_cell_columns + col] = ' ';
    }
}

static uint32_t scale_channel(uint8_t value, uint8_t mask_size) {
    if (mask_size == 0) {
        return 0;
    }

    const uint32_t max_value = (1u << mask_size) - 1u;
    return ((uint32_t)value * max_value) / 255u;
}

static uint32_t pack_color(uint8_t r, uint8_t g, uint8_t b) {
    return (scale_channel(r, tty_framebuffer->red_mask_size) << tty_framebuffer->red_mask_shift)
         | (scale_channel(g, tty_framebuffer->green_mask_size) << tty_framebuffer->green_mask_shift)
         | (scale_channel(b, tty_framebuffer->blue_mask_size) << tty_framebuffer->blue_mask_shift);
}

static uint8_t blend_channel(uint8_t fg, uint8_t bg, uint8_t alpha) {
    return (uint8_t)(((uint32_t)fg * (uint32_t)alpha + (uint32_t)bg * (uint32_t)(255u - alpha)) / 255u);
}

static uint32_t blend_text_color(uint8_t alpha) {
    return pack_color(
        blend_channel(foreground_r, background_r, alpha),
        blend_channel(foreground_g, background_g, alpha),
        blend_channel(foreground_b, background_b, alpha));
}

static void put_pixel(uint64_t x, uint64_t y, uint32_t color) {
    if (tty_framebuffer == NULL || x >= tty_framebuffer->width || y >= tty_framebuffer->height) {
        return;
    }

    uint8_t *row = (uint8_t *)tty_framebuffer->address + y * tty_framebuffer->pitch;

    if (tty_framebuffer->bpp == 32) {
        ((uint32_t *)row)[x] = color;
    }

    gui_text_put_pixel(x, y, color);
}

static void draw_cell(uint64_t cell_x, uint64_t cell_y, char c) {
    if (!bitmap_render_enabled) {
        return;
    }
    const uint8_t ch = (uint8_t)c;
    const uint8_t glyph_index = (ch >= 32u && ch < 127u) ? (uint8_t)(ch - 32u) : 0u;
    const uint8_t glyph_width = inter13_widths[glyph_index];
    const uint8_t *glyph = inter13_data + inter13_offsets[glyph_index];
    const uint64_t px = region_x + cell_x * FONT_WIDTH;
    const uint64_t py = region_y + cell_y * FONT_HEIGHT;
    const uint64_t glyph_x = px + (FONT_WIDTH > glyph_width ? (FONT_WIDTH - glyph_width) / 2u : 0u);

    for (uint64_t y = 0; y < FONT_HEIGHT; ++y) {
        for (uint64_t x = 0; x < FONT_WIDTH; ++x) {
            put_pixel(px + x, py + y, background);
        }
    }

    for (uint64_t gy = 0; gy < FONT_HEIGHT; ++gy) {
        for (uint64_t gx = 0; gx < glyph_width; ++gx) {
            const uint8_t alpha = glyph[gy * glyph_width + gx];
            if (alpha != 0u) {
                put_pixel(glyph_x + gx, py + gy, blend_text_color(alpha));
            }
        }
    }
}

static void draw_glyph_clipped(uint64_t x, uint64_t y, char c, uint32_t fg, uint32_t bg, uint8_t transparent, uint64_t clip_x0, uint64_t clip_y0, uint64_t clip_x1, uint64_t clip_y1) {
    const uint8_t ch = (uint8_t)c;
    const uint8_t glyph_index = (ch >= 32u && ch < 127u) ? (uint8_t)(ch - 32u) : 0u;
    const uint8_t glyph_width = inter13_widths[glyph_index];
    const uint8_t *glyph = inter13_data + inter13_offsets[glyph_index];
    const uint64_t glyph_x = x + (FONT_WIDTH > glyph_width ? (FONT_WIDTH - glyph_width) / 2u : 0u);

    if (!transparent) {
        for (uint64_t cy = 0; cy < FONT_HEIGHT; ++cy) {
            const uint64_t py = y + cy;
            if (py < clip_y0 || py >= clip_y1) {
                continue;
            }
            for (uint64_t cx = 0; cx < FONT_WIDTH; ++cx) {
                const uint64_t px = x + cx;
                if (px >= clip_x0 && px < clip_x1) {
                    put_pixel(px, py, bg);
                }
            }
        }
    }

    for (uint64_t gy = 0; gy < FONT_HEIGHT; ++gy) {
        const uint64_t py = y + gy;
        if (py < clip_y0 || py >= clip_y1) {
            continue;
        }
        for (uint64_t gx = 0; gx < glyph_width; ++gx) {
            const uint64_t px = glyph_x + gx;
            const uint8_t alpha = glyph[gy * glyph_width + gx];
            uint32_t color;

            if (alpha == 0u || px < clip_x0 || px >= clip_x1) {
                continue;
            }
            if (alpha == 255u || transparent) {
                color = fg;
            } else {
                const uint8_t r = blend_channel(foreground_r, background_r, alpha);
                const uint8_t g = blend_channel(foreground_g, background_g, alpha);
                const uint8_t b = blend_channel(foreground_b, background_b, alpha);
                color = pack_color(r, g, b);
            }
            put_pixel(px, py, color);
        }
    }
}

static void scroll(void) {
    if (!stream_output_enabled) {
        return;
    }

    uint8_t *fb = tty_framebuffer->address;

    tty_cells_scroll();

    if (!bitmap_render_enabled) {
        gui_terminal_snapshot_refresh();
        return;
    }

    for (uint64_t y = 0; y < (rows - 1u) * FONT_HEIGHT; ++y) {
        uint8_t *dst = fb + (region_y + y) * tty_framebuffer->pitch + region_x * (tty_framebuffer->bpp / 8u);
        uint8_t *src = fb + (region_y + y + FONT_HEIGHT) * tty_framebuffer->pitch + region_x * (tty_framebuffer->bpp / 8u);

        for (uint64_t x = 0; x < region_width * (tty_framebuffer->bpp / 8u); ++x) {
            dst[x] = src[x];
        }
    }

    for (uint64_t y = (rows - 1u) * FONT_HEIGHT; y < rows * FONT_HEIGHT; ++y) {
        for (uint64_t x = 0; x < region_width; ++x) {
            put_pixel(region_x + x, region_y + y, background);
        }
    }

    gui_sync_rect_from_framebuffer(region_x, region_y, region_width, region_height);
    gui_terminal_snapshot_refresh();
}

static void newline(void) {
    cursor_x = 0;
    ++cursor_y;

    if (cursor_y >= rows) {
        scroll();
        cursor_y = rows - 1u;
    }
}

void tty_init(void) {
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count == 0) {
        return;
    }

    tty_framebuffer = framebuffer_request.response->framebuffers[0];
    region_x = 0;
    region_y = 0;
    region_width = tty_framebuffer->width;
    region_height = tty_framebuffer->height;
    columns = region_width / FONT_WIDTH;
    rows = region_height / FONT_HEIGHT;
    cursor_x = 0;
    cursor_y = 0;
    tty_set_colors_rgb(255, 255, 255, 20, 20, 20);
    tty_clear();
}

void tty_putchar(char c) {
    GUI_GUARD;
    /* CSI sequences (colours of `help`) are for the terminal application;
       the bitmap console only skips them. 0 text, 1 ESC, 2 inside CSI. */
    static uint8_t escape_state;
    console_emit(c);
    if (escape_state == 1u) {
        escape_state = c == '[' ? 2u : 0u;
        return;
    }
    if (escape_state == 2u) {
        if ((unsigned char)c >= 64u && (unsigned char)c <= 126u) {
            escape_state = 0u;
        }
        return;
    }
    if (c == '\033') {
        escape_state = 1u;
        return;
    }
    if (tty_framebuffer == NULL || columns == 0 || rows == 0) {
        return;
    }
    if (!stream_output_enabled) {
        return;
    }

    if (c == '\n') {
        newline();
        return;
    }

    if (c == '\r') {
        cursor_x = 0;
        return;
    }

    if (c == '\b') {
        if (cursor_x > 0) {
            --cursor_x;
            draw_cell(cursor_x, cursor_y, ' ');
            if (cursor_y < tty_cell_rows && cursor_x < tty_cell_columns) {
                tty_cells[cursor_y * tty_cell_columns + cursor_x] = ' ';
            }
            gui_terminal_snapshot_refresh_rect(
                region_x + cursor_x * FONT_WIDTH,
                region_y + cursor_y * FONT_HEIGHT,
                FONT_WIDTH,
                FONT_HEIGHT);
        }
        return;
    }

    const uint64_t cell_x = cursor_x;
    const uint64_t cell_y = cursor_y;
    draw_cell(cursor_x, cursor_y, c);
    if (cursor_y < tty_cell_rows && cursor_x < tty_cell_columns) {
        tty_cells[cursor_y * tty_cell_columns + cursor_x] = c;
    }
    gui_terminal_snapshot_refresh_rect(
        region_x + cell_x * FONT_WIDTH,
        region_y + cell_y * FONT_HEIGHT,
        FONT_WIDTH,
        FONT_HEIGHT);
    ++cursor_x;

    if (cursor_x >= columns) {
        newline();
    }

}

void tty_puts(const char *str) {
    GUI_GUARD;
    if (str == NULL) {
        return;
    }

    for (size_t i = 0; str[i] != '\0'; ++i) {
        tty_putchar(str[i]);
    }
}

void tty_clear(void) {
    GUI_GUARD;
    console_clear();
    if (tty_framebuffer == NULL) {
        return;
    }
    if (!stream_output_enabled) {
        return;
    }

    tty_cells_clear();

    if (bitmap_render_enabled) {
        for (uint64_t y = region_y; y < region_y + region_height && y < tty_framebuffer->height; ++y) {
            for (uint64_t x = region_x; x < region_x + region_width && x < tty_framebuffer->width; ++x) {
                put_pixel(x, y, background);
            }
        }
    }

    cursor_x = 0;
    cursor_y = 0;
    gui_terminal_snapshot_refresh();
}

void tty_set_region(unsigned long x, unsigned long y, unsigned long width, unsigned long height) {
    GUI_GUARD;
    if (tty_framebuffer == NULL) {
        return;
    }

    /* CONFIGURE is resent while the window is repainted.  A repeated region
       is not a terminal reset: preserve the cell contents and live cursor. */
    if (region_x == (uint64_t)x && region_y == (uint64_t)y &&
        region_width == (uint64_t)width && region_height == (uint64_t)height) {
        return;
    }

    region_x = x;
    region_y = y;
    region_width = width;
    region_height = height;
    columns = region_width / FONT_WIDTH;
    rows = region_height / FONT_HEIGHT;
    cursor_x = 0;
    cursor_y = 0;
    if (columns != 0u && rows != 0u && columns <= TTY_LVGL_CELL_CAPACITY / rows) {
        tty_cell_columns = columns;
        tty_cell_rows = rows;
        tty_cells_clear();
    } else {
        tty_cell_columns = 0u;
        tty_cell_rows = 0u;
    }
    if (!bitmap_render_enabled) {
        gui_terminal_snapshot_refresh();
    }
}

/* Boot-smoke probe for CONFIGURE messages repeated during scene redraw.  It is
   intentionally restricted to the pre-GUI smoke path, before any TTY cell
   grid has been published to a shell. */
int tty_region_idempotence_self_test(void) {
    if (tty_framebuffer == NULL || !bitmap_render_enabled ||
        tty_cell_columns != 0u || tty_cell_rows != 0u ||
        region_width <= FONT_WIDTH || region_height < FONT_HEIGHT) {
        return -1;
    }

    const uint64_t saved_x = region_x;
    const uint64_t saved_y = region_y;
    const uint64_t saved_width = region_width;
    const uint64_t saved_height = region_height;
    const uint64_t saved_cursor_x = cursor_x;
    const uint64_t saved_cursor_y = cursor_y;
    const char saved_first_cell = tty_cells[0];
    const uint64_t probe_width = saved_width - 1u;

    tty_set_region((unsigned long)saved_x, (unsigned long)saved_y,
                   (unsigned long)probe_width, (unsigned long)saved_height);
    if (tty_cell_columns == 0u || tty_cell_rows == 0u ||
        tty_cell_columns <= 1u || tty_cell_rows <= 1u) {
        region_x = saved_x;
        region_y = saved_y;
        region_width = saved_width;
        region_height = saved_height;
        columns = saved_width / FONT_WIDTH;
        rows = saved_height / FONT_HEIGHT;
        tty_cell_columns = 0u;
        tty_cell_rows = 0u;
        cursor_x = saved_cursor_x;
        cursor_y = saved_cursor_y;
        tty_cells[0] = saved_first_cell;
        return -1;
    }

    tty_cells[0] = 'X';
    cursor_x = 1u;
    cursor_y = 1u;
    tty_set_region((unsigned long)saved_x, (unsigned long)saved_y,
                   (unsigned long)probe_width, (unsigned long)saved_height);
    const int preserved = tty_cells[0] == 'X' && cursor_x == 1u && cursor_y == 1u;

    /* Restore the pre-GUI TTY state without invoking the reset path under
       test.  The smoke boot has not yet established a logical cell grid. */
    region_x = saved_x;
    region_y = saved_y;
    region_width = saved_width;
    region_height = saved_height;
    columns = saved_width / FONT_WIDTH;
    rows = saved_height / FONT_HEIGHT;
    tty_cell_columns = 0u;
    tty_cell_rows = 0u;
    cursor_x = saved_cursor_x;
    cursor_y = saved_cursor_y;
    tty_cells[0] = saved_first_cell;
    return preserved ? 0 : -1;
}

void tty_set_stream_output_enabled(int enabled) {
    GUI_GUARD;
    stream_output_enabled = enabled != 0 ? 1u : 0u;
}

void tty_set_bitmap_render_enabled(int enabled) {
    GUI_GUARD;
    bitmap_render_enabled = enabled != 0 ? 1u : 0u;
}

const char *tty_lvgl_text(void) {
    uint64_t output = 0u;

    for (uint64_t row = 0u; row < tty_cell_rows; ++row) {
        uint64_t last = tty_cell_columns;
        while (last > 0u && tty_cells[row * tty_cell_columns + last - 1u] == ' ') {
            --last;
        }
        for (uint64_t col = 0u; col < last && output + 1u < TTY_LVGL_TEXT_CAPACITY; ++col) {
            tty_lvgl_text_buffer[output++] = tty_cells[row * tty_cell_columns + col];
        }
        if (row + 1u < tty_cell_rows && output + 1u < TTY_LVGL_TEXT_CAPACITY) {
            tty_lvgl_text_buffer[output++] = '\n';
        }
    }
    tty_lvgl_text_buffer[output] = '\0';
    return tty_lvgl_text_buffer;
}

void tty_set_colors_rgb(unsigned char fg_r, unsigned char fg_g, unsigned char fg_b, unsigned char bg_r, unsigned char bg_g, unsigned char bg_b) {
    GUI_GUARD;
    foreground_r = fg_r;
    foreground_g = fg_g;
    foreground_b = fg_b;
    background_r = bg_r;
    background_g = bg_g;
    background_b = bg_b;
    foreground = pack_color(fg_r, fg_g, fg_b);
    background = pack_color(bg_r, bg_g, bg_b);
}

void tty_draw_text_at(unsigned long x, unsigned long y, const char *str, unsigned int fg, unsigned int bg) {
    GUI_GUARD;
    tty_draw_text_clipped_at(x, y, (unsigned long)-1, str, fg, bg);
}

void tty_draw_text_transparent_at(unsigned long x, unsigned long y, const char *str, unsigned int fg) {
    tty_draw_text_transparent_clipped_at(x, y, (unsigned long)-1, str, fg);
}

void tty_draw_text_clipped_at(unsigned long x, unsigned long y, unsigned long max_width, const char *str, unsigned int fg, unsigned int bg) {
    if (tty_framebuffer == NULL || str == NULL) {
        return;
    }

    if (y >= tty_framebuffer->height || x >= tty_framebuffer->width || max_width == 0u) {
        return;
    }

    {
        const uint64_t effective_width = max_width == (unsigned long)-1
            ? (tty_framebuffer->width > x ? tty_framebuffer->width - x : 0u)
            : (uint64_t)max_width;
        const uint64_t clip_x0 = x;
        const uint64_t clip_y0 = y;
        const uint64_t clip_x1 = x + effective_width < tty_framebuffer->width ? x + effective_width : tty_framebuffer->width;
        const uint64_t clip_y1 = y + FONT_HEIGHT < tty_framebuffer->height ? y + FONT_HEIGHT : tty_framebuffer->height;

        if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) {
            return;
        }

        for (size_t i = 0; str[i] != '\0'; ++i) {
            const uint64_t px = x + i * FONT_WIDTH;
            if (px >= clip_x1) {
                break;
            }

            draw_glyph_clipped(px, y, str[i], fg, bg, 0u, clip_x0, clip_y0, clip_x1, clip_y1);
        }
    }
}

void tty_draw_text_transparent_clipped_at(unsigned long x, unsigned long y, unsigned long max_width, const char *str, unsigned int fg) {
    if (tty_framebuffer == NULL || str == NULL) {
        return;
    }

    if (y >= tty_framebuffer->height || x >= tty_framebuffer->width || max_width == 0u) {
        return;
    }

    {
        const uint64_t effective_width = max_width == (unsigned long)-1
            ? (tty_framebuffer->width > x ? tty_framebuffer->width - x : 0u)
            : (uint64_t)max_width;
        const uint64_t clip_x0 = x;
        const uint64_t clip_y0 = y;
        const uint64_t clip_x1 = x + effective_width < tty_framebuffer->width ? x + effective_width : tty_framebuffer->width;
        const uint64_t clip_y1 = y + FONT_HEIGHT < tty_framebuffer->height ? y + FONT_HEIGHT : tty_framebuffer->height;

        if (clip_x1 <= clip_x0 || clip_y1 <= clip_y0) {
            return;
        }

        for (size_t i = 0; str[i] != '\0'; ++i) {
            const uint64_t px = x + i * FONT_WIDTH;
            if (px >= clip_x1) {
                break;
            }

            draw_glyph_clipped(px, y, str[i], fg, 0u, 1u, clip_x0, clip_y0, clip_x1, clip_y1);
        }
    }
}

/* Draw text with each 8x8 glyph pixel expanded to scale×scale blocks.
 * Useful for large UI labels (e.g. scale=3 → 24px tall text). */
void tty_draw_text_large_at(unsigned long x, unsigned long y, unsigned long scale,
                             const char *str, unsigned int fg) {
    unsigned long i;

    if (tty_framebuffer == NULL || str == NULL || scale == 0u) {
        return;
    }

    for (i = 0u; str[i] != '\0'; ++i) {
        const uint8_t *glyph = font[(uint8_t)str[i] < 128u ? (uint8_t)str[i] : (uint8_t)'?'];
        const uint64_t cx = (uint64_t)x + (uint64_t)i * FONT_BITMAP_WIDTH * (uint64_t)scale;
        uint64_t gy;

        for (gy = 0u; gy < FONT_BITMAP_HEIGHT; ++gy) {
            uint64_t gx;
            for (gx = 0u; gx < FONT_BITMAP_WIDTH; ++gx) {
                if ((glyph[gy] & (uint8_t)(0x80u >> gx)) != 0u) {
                    uint64_t sy;
                    for (sy = 0u; sy < (uint64_t)scale; ++sy) {
                        uint64_t sx;
                        for (sx = 0u; sx < (uint64_t)scale; ++sx) {
                            put_pixel(cx + gx * (uint64_t)scale + sx,
                                      (uint64_t)y + gy * (uint64_t)scale + sy, fg);
                        }
                    }
                }
            }
        }
    }
}
