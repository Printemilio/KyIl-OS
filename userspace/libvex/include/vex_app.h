#ifndef VEX_APP_H
#define VEX_APP_H

#include <stdint.h>

#include <unos/vex_abi.h>

#include "vex_syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vex_app_window {
    uint32_t id;
    uint32_t width;
    uint32_t height;
} vex_app_window_t;

static inline int vex_app_window_open(vex_app_window_t *window, const char *title,
                                      uint32_t width, uint32_t height, uint32_t flags) {
    vex_app_window_open_req_t req;

    if (window == 0 || width == 0u || height == 0u) {
        return -1;
    }

    req.title = title;
    req.width = width;
    req.height = height;
    req.flags = flags;
    req.window_id = 0u;
    if (vex_syscall1(VEX_SYS_APP_WINDOW_OPEN, (uint64_t)(uintptr_t)&req) != 0u) {
        return -1;
    }

    window->id = req.window_id;
    window->width = width;
    window->height = height;
    return 0;
}

static inline int vex_app_window_present(vex_app_window_t *window, const uint32_t *pixels,
                                         uint32_t stride, const vex_rect_t *dirty) {
    vex_app_window_present_req_t req;

    if (window == 0 || window->id == 0u || pixels == 0 || stride < window->width) {
        return -1;
    }

    req.window_id = window->id;
    req.reserved0 = 0u;
    req.pixels = pixels;
    req.width = window->width;
    req.height = window->height;
    req.stride = stride;
    req.dirty = dirty;
    extern void vex_bench_present_notify(int) __attribute__((weak));
    if(vex_bench_present_notify)vex_bench_present_notify(0);
    int result=vex_syscall1(VEX_SYS_APP_WINDOW_PRESENT, (uint64_t)(uintptr_t)&req) == 0u ? 0 : -1;
    if(vex_bench_present_notify)vex_bench_present_notify(1);
    return result;
}

static inline int vex_app_window_poll(vex_app_window_t *window, vex_app_window_event_t *event) {
    vex_app_window_poll_req_t req;

    if (window == 0 || window->id == 0u || event == 0) {
        return -1;
    }

    req.window_id = window->id;
    req.flags = 0u;
    req.event = event;
    return vex_syscall1(VEX_SYS_APP_WINDOW_POLL, (uint64_t)(uintptr_t)&req) == 0u ? 0 : -1;
}

static inline int vex_app_window_wait(vex_app_window_t *window,vex_app_window_event_t *event) {
    if(!window || !window->id || !event)return -1;
    vex_app_window_poll_req_t req={window->id,VEX_APP_WINDOW_POLL_WAIT,event};
    return vex_syscall1(VEX_SYS_APP_WINDOW_POLL,(uint64_t)(uintptr_t)&req)==0u?0:-1;
}
static inline int vex_app_window_wait_timeout(vex_app_window_t *window,vex_app_window_event_t *event,uint16_t ticks) {
    if(!window || !window->id || !event)return -1;
    vex_app_window_poll_req_t req={window->id,VEX_APP_WINDOW_POLL_WAIT|((uint32_t)ticks<<16),event};
    return vex_syscall1(VEX_SYS_APP_WINDOW_POLL,(uint64_t)(uintptr_t)&req)==0u?0:-1;
}

static inline int vex_app_window_accept_configure(vex_app_window_t *window,
                                                   const vex_app_window_event_t *event) {
    vex_app_window_resize_req_t req;

    if (window == 0 || event == 0 || event->type != VEX_APP_EVENT_CONFIGURE ||
        event->w == 0u || event->h == 0u || event->w > 0xffffffffu || event->h > 0xffffffffu) {
        return -1;
    }
    req.window_id = window->id;
    req.width = (uint32_t)event->w;
    req.height = (uint32_t)event->h;
    req.reserved0 = 0u;
    if (vex_syscall1(VEX_SYS_APP_WINDOW_RESIZE, (uint64_t)(uintptr_t)&req) != 0u) {
        return -1;
    }
    window->width = req.width;
    window->height = req.height;
    return 0;
}

static inline void vex_app_window_close(vex_app_window_t *window) {
    if (window == 0 || window->id == 0u) {
        return;
    }

    (void)vex_syscall1(VEX_SYS_APP_WINDOW_CLOSE, window->id);
    window->id = 0u;
}

#ifdef __cplusplus
}
#endif

#endif
