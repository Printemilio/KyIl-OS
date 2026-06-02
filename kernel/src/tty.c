#include <unos/tty.h>

#include <limine.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/boot_requests.h>
#include <unos/gui.h>

#define FONT_WIDTH 8ull
#define FONT_HEIGHT 8ull

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
static uint8_t stream_output_enabled = 1u;

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
    const uint8_t *glyph = font[(uint8_t)c < 128 ? (uint8_t)c : (uint8_t)'?'];
    const uint64_t px = region_x + cell_x * FONT_WIDTH;
    const uint64_t py = region_y + cell_y * FONT_HEIGHT;

    for (uint64_t y = 0; y < FONT_HEIGHT; ++y) {
        for (uint64_t x = 0; x < FONT_WIDTH; ++x) {
            const uint32_t color = (glyph[y] & (uint8_t)(0x80u >> x)) != 0 ? foreground : background;
            put_pixel(px + x, py + y, color);
        }
    }
}

static void draw_glyph_clipped(uint64_t x, uint64_t y, char c, uint32_t fg, uint32_t bg, uint8_t transparent, uint64_t clip_x0, uint64_t clip_y0, uint64_t clip_x1, uint64_t clip_y1) {
    const uint8_t *glyph = font[(uint8_t)c < 128 ? (uint8_t)c : (uint8_t)'?'];

    for (uint64_t gy = 0; gy < FONT_HEIGHT; ++gy) {
        const uint64_t py = y + gy;
        if (py < clip_y0 || py >= clip_y1) {
            continue;
        }

        for (uint64_t gx = 0; gx < FONT_WIDTH; ++gx) {
            const uint64_t px = x + gx;
            const uint8_t bit_on = (glyph[gy] & (uint8_t)(0x80u >> gx)) != 0u;

            if (px < clip_x0 || px >= clip_x1) {
                continue;
            }
            if (transparent && !bit_on) {
                continue;
            }

            put_pixel(px, py, bit_on ? fg : bg);
        }
    }
}

static void scroll(void) {
    if (!stream_output_enabled) {
        return;
    }

    uint8_t *fb = tty_framebuffer->address;

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
    foreground = pack_color(238, 242, 247);
    background = pack_color(9, 11, 16);
    tty_clear();
}

void tty_putchar(char c) {
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
        }
        return;
    }

    draw_cell(cursor_x, cursor_y, c);
    ++cursor_x;

    if (cursor_x >= columns) {
        newline();
    }
}

void tty_puts(const char *str) {
    if (str == NULL) {
        return;
    }

    for (size_t i = 0; str[i] != '\0'; ++i) {
        tty_putchar(str[i]);
    }
}

void tty_clear(void) {
    if (tty_framebuffer == NULL) {
        return;
    }
    if (!stream_output_enabled) {
        return;
    }

    for (uint64_t y = region_y; y < region_y + region_height && y < tty_framebuffer->height; ++y) {
        for (uint64_t x = region_x; x < region_x + region_width && x < tty_framebuffer->width; ++x) {
            put_pixel(x, y, background);
        }
    }

    cursor_x = 0;
    cursor_y = 0;
}

void tty_set_region(unsigned long x, unsigned long y, unsigned long width, unsigned long height) {
    if (tty_framebuffer == NULL) {
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
}

void tty_set_stream_output_enabled(int enabled) {
    stream_output_enabled = enabled != 0 ? 1u : 0u;
}

void tty_set_colors_rgb(unsigned char fg_r, unsigned char fg_g, unsigned char fg_b, unsigned char bg_r, unsigned char bg_g, unsigned char bg_b) {
    foreground = pack_color(fg_r, fg_g, fg_b);
    background = pack_color(bg_r, bg_g, bg_b);
}

void tty_draw_text_at(unsigned long x, unsigned long y, const char *str, unsigned int fg, unsigned int bg) {
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
