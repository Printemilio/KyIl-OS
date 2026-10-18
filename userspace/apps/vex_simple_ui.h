#ifndef VEX_SIMPLE_UI_H
#define VEX_SIMPLE_UI_H

#include <stddef.h>
#include <stdint.h>

typedef struct simple_canvas {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
} simple_canvas_t;

static inline uint32_t sui_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline void sui_fill(simple_canvas_t *canvas, uint32_t color) {
    if (canvas == 0 || canvas->pixels == 0) {
        return;
    }
    for (uint32_t i = 0u; i < canvas->width * canvas->height; ++i) {
        canvas->pixels[i] = color;
    }
}

static inline void sui_rect(simple_canvas_t *canvas, uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, uint32_t color) {
    const uint32_t x1 = x + w > canvas->width ? canvas->width : x + w;
    const uint32_t y1 = y + h > canvas->height ? canvas->height : y + h;

    if (canvas == 0 || canvas->pixels == 0 || x >= canvas->width || y >= canvas->height) {
        return;
    }
    for (uint32_t py = y; py < y1; ++py) {
        for (uint32_t px = x; px < x1; ++px) {
            canvas->pixels[py * canvas->width + px] = color;
        }
    }
}

static inline uint8_t sui_font_row(char c, uint32_t row) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    switch (c) {
        case 'A': { static const uint8_t r[7] = { 14, 17, 17, 31, 17, 17, 17 }; return r[row]; }
        case 'B': { static const uint8_t r[7] = { 30, 17, 17, 30, 17, 17, 30 }; return r[row]; }
        case 'C': { static const uint8_t r[7] = { 14, 17, 16, 16, 16, 17, 14 }; return r[row]; }
        case 'D': { static const uint8_t r[7] = { 30, 17, 17, 17, 17, 17, 30 }; return r[row]; }
        case 'E': { static const uint8_t r[7] = { 31, 16, 16, 30, 16, 16, 31 }; return r[row]; }
        case 'F': { static const uint8_t r[7] = { 31, 16, 16, 30, 16, 16, 16 }; return r[row]; }
        case 'G': { static const uint8_t r[7] = { 14, 17, 16, 23, 17, 17, 15 }; return r[row]; }
        case 'H': { static const uint8_t r[7] = { 17, 17, 17, 31, 17, 17, 17 }; return r[row]; }
        case 'I': { static const uint8_t r[7] = { 14, 4, 4, 4, 4, 4, 14 }; return r[row]; }
        case 'J': { static const uint8_t r[7] = { 7, 2, 2, 2, 18, 18, 12 }; return r[row]; }
        case 'K': { static const uint8_t r[7] = { 17, 18, 20, 24, 20, 18, 17 }; return r[row]; }
        case 'L': { static const uint8_t r[7] = { 16, 16, 16, 16, 16, 16, 31 }; return r[row]; }
        case 'M': { static const uint8_t r[7] = { 17, 27, 21, 21, 17, 17, 17 }; return r[row]; }
        case 'N': { static const uint8_t r[7] = { 17, 25, 21, 19, 17, 17, 17 }; return r[row]; }
        case 'O': { static const uint8_t r[7] = { 14, 17, 17, 17, 17, 17, 14 }; return r[row]; }
        case 'P': { static const uint8_t r[7] = { 30, 17, 17, 30, 16, 16, 16 }; return r[row]; }
        case 'Q': { static const uint8_t r[7] = { 14, 17, 17, 17, 21, 18, 13 }; return r[row]; }
        case 'R': { static const uint8_t r[7] = { 30, 17, 17, 30, 20, 18, 17 }; return r[row]; }
        case 'S': { static const uint8_t r[7] = { 15, 16, 16, 14, 1, 1, 30 }; return r[row]; }
        case 'T': { static const uint8_t r[7] = { 31, 4, 4, 4, 4, 4, 4 }; return r[row]; }
        case 'U': { static const uint8_t r[7] = { 17, 17, 17, 17, 17, 17, 14 }; return r[row]; }
        case 'V': { static const uint8_t r[7] = { 17, 17, 17, 17, 17, 10, 4 }; return r[row]; }
        case 'W': { static const uint8_t r[7] = { 17, 17, 17, 21, 21, 21, 10 }; return r[row]; }
        case 'X': { static const uint8_t r[7] = { 17, 17, 10, 4, 10, 17, 17 }; return r[row]; }
        case 'Y': { static const uint8_t r[7] = { 17, 17, 10, 4, 4, 4, 4 }; return r[row]; }
        case 'Z': { static const uint8_t r[7] = { 31, 1, 2, 4, 8, 16, 31 }; return r[row]; }
        case '0': { static const uint8_t r[7] = { 14, 17, 19, 21, 25, 17, 14 }; return r[row]; }
        case '1': { static const uint8_t r[7] = { 4, 12, 4, 4, 4, 4, 14 }; return r[row]; }
        case '2': { static const uint8_t r[7] = { 14, 17, 1, 2, 4, 8, 31 }; return r[row]; }
        case '3': { static const uint8_t r[7] = { 30, 1, 1, 14, 1, 1, 30 }; return r[row]; }
        case '4': { static const uint8_t r[7] = { 2, 6, 10, 18, 31, 2, 2 }; return r[row]; }
        case '5': { static const uint8_t r[7] = { 31, 16, 16, 30, 1, 1, 30 }; return r[row]; }
        case '6': { static const uint8_t r[7] = { 6, 8, 16, 30, 17, 17, 14 }; return r[row]; }
        case '7': { static const uint8_t r[7] = { 31, 1, 2, 4, 8, 8, 8 }; return r[row]; }
        case '8': { static const uint8_t r[7] = { 14, 17, 17, 14, 17, 17, 14 }; return r[row]; }
        case '9': { static const uint8_t r[7] = { 14, 17, 17, 15, 1, 2, 12 }; return r[row]; }
        case '.': { static const uint8_t r[7] = { 0, 0, 0, 0, 0, 12, 12 }; return r[row]; }
        case ',': { static const uint8_t r[7] = { 0, 0, 0, 0, 0, 4, 8 }; return r[row]; }
        case ':': { static const uint8_t r[7] = { 0, 12, 12, 0, 12, 12, 0 }; return r[row]; }
        case ';': { static const uint8_t r[7] = { 0, 12, 12, 0, 12, 4, 8 }; return r[row]; }
        case '!': { static const uint8_t r[7] = { 4, 4, 4, 4, 4, 0, 4 }; return r[row]; }
        case '?': { static const uint8_t r[7] = { 14, 17, 1, 2, 4, 0, 4 }; return r[row]; }
        case '-': { static const uint8_t r[7] = { 0, 0, 0, 31, 0, 0, 0 }; return r[row]; }
        case '_': { static const uint8_t r[7] = { 0, 0, 0, 0, 0, 0, 31 }; return r[row]; }
        case '/': { static const uint8_t r[7] = { 1, 1, 2, 4, 8, 16, 16 }; return r[row]; }
        case '\\': { static const uint8_t r[7] = { 16, 16, 8, 4, 2, 1, 1 }; return r[row]; }
        case '$': { static const uint8_t r[7] = { 4, 15, 20, 14, 5, 30, 4 }; return r[row]; }
        case '+': { static const uint8_t r[7] = { 0, 4, 4, 31, 4, 4, 0 }; return r[row]; }
        case '=': { static const uint8_t r[7] = { 0, 0, 31, 0, 31, 0, 0 }; return r[row]; }
        case '<': { static const uint8_t r[7] = { 2, 4, 8, 16, 8, 4, 2 }; return r[row]; }
        case '>': { static const uint8_t r[7] = { 8, 4, 2, 1, 2, 4, 8 }; return r[row]; }
        case '(': { static const uint8_t r[7] = { 2, 4, 8, 8, 8, 4, 2 }; return r[row]; }
        case ')': { static const uint8_t r[7] = { 8, 4, 2, 2, 2, 4, 8 }; return r[row]; }
        case '[': { static const uint8_t r[7] = { 14, 8, 8, 8, 8, 8, 14 }; return r[row]; }
        case ']': { static const uint8_t r[7] = { 14, 2, 2, 2, 2, 2, 14 }; return r[row]; }
        case '\'': { static const uint8_t r[7] = { 4, 4, 8, 0, 0, 0, 0 }; return r[row]; }
        case '"': { static const uint8_t r[7] = { 10, 10, 0, 0, 0, 0, 0 }; return r[row]; }
        case '&': { static const uint8_t r[7] = { 12, 18, 20, 8, 21, 18, 13 }; return r[row]; }
        case '@': { static const uint8_t r[7] = { 14, 17, 23, 21, 23, 16, 14 }; return r[row]; }
        case ' ': return 0u;
        default: { static const uint8_t r[7] = { 31, 17, 5, 2, 4, 0, 4 }; return r[row]; }
    }
}

static inline void sui_glyph(simple_canvas_t *canvas, uint32_t x, uint32_t y, char c,
                             uint32_t color) {
    if (canvas == 0 || canvas->pixels == 0 || c == ' ') {
        return;
    }
    for (uint32_t gy = 0u; gy < 7u; ++gy) {
        const uint8_t row = sui_font_row(c, gy);
        for (uint32_t gx = 0u; gx < 5u; ++gx) {
            const uint32_t px = x + gx;
            const uint32_t py = y + gy;
            if (px < canvas->width && py < canvas->height &&
                ((row >> (4u - gx)) & 1u) != 0u) {
                canvas->pixels[py * canvas->width + px] = color;
            }
        }
    }
}

static inline void sui_text(simple_canvas_t *canvas, uint32_t x, uint32_t y,
                            const char *text, uint32_t color) {
    for (size_t i = 0u; text != 0 && text[i] != '\0'; ++i) {
        sui_glyph(canvas, x + (uint32_t)i * 8u, y, text[i], color);
    }
}

static inline void sui_u64(simple_canvas_t *canvas, uint32_t x, uint32_t y,
                           uint64_t value, uint32_t color) {
    char tmp[24];
    size_t used = 0u;

    if (value == 0u) {
        sui_text(canvas, x, y, "0", color);
        return;
    }
    while (value != 0u && used + 1u < sizeof(tmp)) {
        tmp[used++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    for (size_t i = 0u; i < used / 2u; ++i) {
        const char t = tmp[i];
        tmp[i] = tmp[used - 1u - i];
        tmp[used - 1u - i] = t;
    }
    tmp[used] = '\0';
    sui_text(canvas, x, y, tmp, color);
}

#endif
