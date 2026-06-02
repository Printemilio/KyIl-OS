#include <unos/framebuffer.h>

static uint32_t scale_channel(uint8_t value, uint8_t mask_size) {
    if (mask_size == 0) {
        return 0;
    }

    const uint32_t max_value = (1u << mask_size) - 1u;
    return ((uint32_t)value * max_value) / 255u;
}

static uint32_t pack_color(const struct limine_framebuffer *framebuffer, struct unos_color color) {
    return (scale_channel(color.r, framebuffer->red_mask_size) << framebuffer->red_mask_shift)
         | (scale_channel(color.g, framebuffer->green_mask_size) << framebuffer->green_mask_shift)
         | (scale_channel(color.b, framebuffer->blue_mask_size) << framebuffer->blue_mask_shift);
}

static void put_pixel(struct limine_framebuffer *framebuffer, size_t x, size_t y, struct unos_color color) {
    if (x >= framebuffer->width || y >= framebuffer->height) {
        return;
    }

    const uint32_t packed = pack_color(framebuffer, color);
    uint8_t *row = (uint8_t *)framebuffer->address + y * framebuffer->pitch;

    switch (framebuffer->bpp) {
    case 32:
        ((uint32_t *)row)[x] = packed;
        break;
    case 24:
        row[x * 3u + 0u] = (uint8_t)(packed >> 0u);
        row[x * 3u + 1u] = (uint8_t)(packed >> 8u);
        row[x * 3u + 2u] = (uint8_t)(packed >> 16u);
        break;
    default:
        break;
    }
}

void framebuffer_clear(struct limine_framebuffer *framebuffer, struct unos_color color) {
    for (size_t y = 0; y < framebuffer->height; ++y) {
        for (size_t x = 0; x < framebuffer->width; ++x) {
            put_pixel(framebuffer, x, y, color);
        }
    }
}

static const uint8_t glyph_v[7] = {
    0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04
};

static const uint8_t glyph_e[7] = {
    0x00, 0x0e, 0x11, 0x1f, 0x10, 0x11, 0x0e
};

static const uint8_t glyph_o[7] = {
    0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e
};

static const uint8_t glyph_s[7] = {
    0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e
};

static const uint8_t glyph_x[7] = {
    0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x00
};

static const uint8_t glyph_dash[7] = {
    0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00
};

static const uint8_t *glyph_for(char c) {
    switch (c) {
    case 'V':
        return glyph_v;
    case 'e':
        return glyph_e;
    case 'x':
        return glyph_x;
    case '-':
        return glyph_dash;
    case 'O':
        return glyph_o;
    case 'S':
        return glyph_s;
    default:
        return NULL;
    }
}

static void draw_glyph(
    struct limine_framebuffer *framebuffer,
    const uint8_t glyph[7],
    size_t x,
    size_t y,
    size_t scale,
    struct unos_color color
) {
    for (size_t row = 0; row < 7u; ++row) {
        for (size_t col = 0; col < 5u; ++col) {
            if ((glyph[row] & (1u << (4u - col))) == 0u) {
                continue;
            }

            for (size_t dy = 0; dy < scale; ++dy) {
                for (size_t dx = 0; dx < scale; ++dx) {
                    put_pixel(framebuffer, x + col * scale + dx, y + row * scale + dy, color);
                }
            }
        }
    }
}

void framebuffer_draw_wordmark(struct limine_framebuffer *framebuffer) {
    static const char text[] = "Vex-OS";
    const size_t scale = 8u;
    const size_t glyph_width = 5u * scale;
    const size_t glyph_height = 7u * scale;
    const size_t glyph_spacing = scale;
    const size_t glyph_count = sizeof(text) - 1u;
    const size_t text_width = glyph_count * glyph_width + (glyph_count - 1u) * glyph_spacing;
    const size_t x_origin = framebuffer->width > text_width ? (framebuffer->width - text_width) / 2u : 0u;
    const size_t y_origin = framebuffer->height > glyph_height ? (framebuffer->height - glyph_height) / 2u : 0u;

    for (size_t i = 0; i < glyph_count; ++i) {
        const uint8_t *glyph = glyph_for(text[i]);
        if (glyph == NULL) {
            continue;
        }

        draw_glyph(
            framebuffer,
            glyph,
            x_origin + i * (glyph_width + glyph_spacing),
            y_origin,
            scale,
            (struct unos_color){ .r = 245, .g = 247, .b = 250 }
        );
    }
}
