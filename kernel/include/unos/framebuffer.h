#ifndef UNOS_FRAMEBUFFER_H
#define UNOS_FRAMEBUFFER_H

#include <limine.h>
#include <stddef.h>
#include <stdint.h>

struct unos_color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

void framebuffer_clear(struct limine_framebuffer *framebuffer, struct unos_color color);
void framebuffer_draw_wordmark(struct limine_framebuffer *framebuffer);

#endif

