#ifndef VEX_WPE_STUB_WPE_EGL_H
#define VEX_WPE_STUB_WPE_EGL_H

#include <stdint.h>
#include <wpe/wpe.h>

typedef void *EGLNativeDisplayType;
typedef void *EGLNativeWindowType;

struct wpe_renderer_backend_egl;
struct wpe_renderer_backend_egl_target;
struct wpe_renderer_backend_egl_offscreen_target;

struct wpe_renderer_backend_egl_interface {
    void *(*create)(int);
    void (*destroy)(void *);
    EGLNativeDisplayType (*get_native_display)(void *);
    uint32_t (*get_platform)(void *);
    void (*_wpe_reserved1)(void);
    void (*_wpe_reserved2)(void);
    void (*_wpe_reserved3)(void);
};

struct wpe_renderer_backend_egl_target_interface {
    void *(*create)(struct wpe_renderer_backend_egl_target *, int);
    void (*destroy)(void *);
    void (*initialize)(void *, void *, uint32_t, uint32_t);
    EGLNativeWindowType (*get_native_window)(void *);
    void (*resize)(void *, uint32_t, uint32_t);
    void (*frame_will_render)(void *);
    void (*frame_rendered)(void *);
    void (*deinitialize)(void *);
    void (*_wpe_reserved1)(void);
    void (*_wpe_reserved2)(void);
    void (*_wpe_reserved3)(void);
};

struct wpe_renderer_backend_egl_offscreen_target_interface {
    void *(*create)(void);
    void (*destroy)(void *);
    void (*initialize)(void *, void *);
    EGLNativeWindowType (*get_native_window)(void *);
    void (*_wpe_reserved0)(void);
    void (*_wpe_reserved1)(void);
    void (*_wpe_reserved2)(void);
    void (*_wpe_reserved3)(void);
};

void wpe_renderer_backend_egl_target_dispatch_frame_complete(
    struct wpe_renderer_backend_egl_target *target);

#endif
