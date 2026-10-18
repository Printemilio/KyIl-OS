#include <stdint.h>

#include "vex_app.h"
#include "vex_runtime.h"

#define DEMO_W 640u
#define DEMO_H 360u

static uint32_t pixels[DEMO_W * DEMO_H];
static uint32_t demo_phase;

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void draw_demo_frame(void) {
    for (uint32_t y = 0u; y < DEMO_H; ++y) {
        for (uint32_t x = 0u; x < DEMO_W; ++x) {
            const uint8_t r = (uint8_t)(24u + (x * 80u) / DEMO_W);
            const uint8_t g = (uint8_t)(32u + ((y + demo_phase * 11u) * 96u) / (DEMO_H + 80u));
            const uint8_t b = (uint8_t)(72u + ((x + y + demo_phase * 23u) * 80u) / (DEMO_W + DEMO_H + 160u));
            pixels[y * DEMO_W + x] = rgb(r, g, b);
        }
    }

    for (uint32_t y = 118u; y < 242u; ++y) {
        for (uint32_t x = 96u; x < 544u; ++x) {
            pixels[y * DEMO_W + x] = rgb(18u, 18u, 24u);
        }
    }
    for (uint32_t y = 128u; y < 232u; ++y) {
        for (uint32_t x = 106u; x < 534u; ++x) {
            pixels[y * DEMO_W + x] = rgb(230u, 235u, 246u);
        }
    }
}

int main(void) {
    vex_app_window_t window;
    vex_app_window_event_t event;

    if (vex_app_window_open(&window, "Vex App Demo", DEMO_W, DEMO_H,
            VEX_APP_WINDOW_FLAG_RESIZABLE) != 0) {
        return 1;
    }

    draw_demo_frame();
    (void)vex_app_window_present(&window, pixels, DEMO_W, 0);

    for (;;) {
        uint8_t handled_event = 0u;

        if (vex_app_window_poll(&window, &event) == 0) {
            if (event.type == VEX_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == VEX_APP_EVENT_CONFIGURE ||
                (event.type == VEX_APP_EVENT_MOUSE_CLICK &&
                 (event.flags & VEX_APP_MOUSE_LEFT_DOWN) != 0u &&
                 (event.flags & VEX_APP_MOUSE_MOVE) == 0u)) {
                handled_event = 1u;
                ++demo_phase;
                draw_demo_frame();
                (void)vex_app_window_present(&window, pixels, DEMO_W, 0);
            }
        }
        if (handled_event) {
            (void)vex_syscall0(VEX_SYS_PROCESS_YIELD);
        } else {
            (void)vex_syscall0(VEX_SYS_PROCESS_YIELD);
        }
    }

    vex_app_window_close(&window);
    return 0;
}
