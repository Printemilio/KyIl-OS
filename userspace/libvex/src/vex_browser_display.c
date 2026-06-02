#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>
#include <unos/vex_abi.h>

#include "vex_syscall.h"
#include "vex_runtime.h"

static ladybird_display_surface_t g_surface;

int ladybird_display_open(uint32_t width, uint32_t height) {
    size_t pixel_count;

    if (width == 0 || height == 0) {
        return -1;
    }

    if (g_surface.pixels != 0) {
        free(g_surface.pixels);
        g_surface.pixels = 0;
    }

    pixel_count = (size_t)width * (size_t)height;
    g_surface.pixels = (uint32_t*)calloc(pixel_count, sizeof(uint32_t));
    if (g_surface.pixels == 0) {
        return -1;
    }

    g_surface.width = width;
    g_surface.height = height;
    g_surface.stride = width;
    return (int)vex_syscall2(VEX_SYS_DISPLAY_OPEN, (uint64_t)width, (uint64_t)height);
}

void ladybird_display_resize(uint32_t width, uint32_t height) {
    (void)ladybird_display_open(width, height);
    (void)vex_syscall2(VEX_SYS_DISPLAY_RESIZE, (uint64_t)width, (uint64_t)height);
}

ladybird_display_surface_t* ladybird_display_surface(void) {
    return g_surface.pixels == 0 ? 0 : &g_surface;
}

void ladybird_display_present(void) {
    vex_display_present_req_t req;

    if (g_surface.pixels == 0) {
        return;
    }

    req.pixels = g_surface.pixels;
    req.width = g_surface.width;
    req.height = g_surface.height;
    req.stride = g_surface.stride;
    req.dirty = 0;
    (void)vex_syscall1(VEX_SYS_DISPLAY_PRESENT, (uint64_t)&req);
}

#ifdef VEX_DEBUG_PARTIAL_PRESENT
void ladybird_test_partial_present(int x, int y, int w, int h) {
    vex_rect_t dirty;
    vex_display_present_req_t req;

    if (g_surface.pixels == 0 || x < 0 || y < 0 || w <= 0 || h <= 0) {
        return;
    }

    dirty.x = x;
    dirty.y = y;
    dirty.w = w;
    dirty.h = h;

    req.pixels = g_surface.pixels;
    req.width = g_surface.width;
    req.height = g_surface.height;
    req.stride = g_surface.stride;
    req.dirty = &dirty;
    (void)vex_syscall1(VEX_SYS_DISPLAY_PRESENT, (uint64_t)&req);
}
#else
void ladybird_test_partial_present(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}
#endif

void ladybird_display_close(void) {
    if (g_surface.pixels != 0) {
        free(g_surface.pixels);
        g_surface.pixels = 0;
    }
    g_surface.width = 0;
    g_surface.height = 0;
    g_surface.stride = 0;
    (void)vex_syscall0(VEX_SYS_DISPLAY_CLOSE);
}

static void copy_result_string(char *dst, size_t capacity, const char *src) {
    size_t i = 0;
    if (capacity == 0) {
        return;
    }
    if (src == 0) {
        dst[0] = '\0';
        return;
    }
    while (src[i] != '\0' && i + 1 < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void browser_results_call(uint32_t op, const char *query, const char *title, const char *url, const char *snippet) {
    vex_browser_result_req_t req;
    req.op = op;
    copy_result_string(req.query, sizeof(req.query), query);
    copy_result_string(req.title, sizeof(req.title), title);
    copy_result_string(req.url, sizeof(req.url), url);
    copy_result_string(req.snippet, sizeof(req.snippet), snippet);
    (void)vex_syscall1(VEX_SYS_BROWSER_RESULTS, (uint64_t)&req);
}

void ladybird_browser_results_begin(const char *query) {
    browser_results_call(VEX_BROWSER_RESULTS_BEGIN, query, 0, 0, 0);
}

void ladybird_browser_results_add(const char *title, const char *url, const char *snippet) {
    browser_results_call(VEX_BROWSER_RESULTS_ADD, 0, title, url, snippet);
}

void ladybird_browser_results_show(void) {
    browser_results_call(VEX_BROWSER_RESULTS_SHOW, 0, 0, 0, 0);
}

void ladybird_browser_results_hide(void) {
    browser_results_call(VEX_BROWSER_RESULTS_HIDE, 0, 0, 0, 0);
}
