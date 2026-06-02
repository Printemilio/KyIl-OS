/*
 * vex_wpe_backend.c — Minimal WPE renderer backend for VexOS.
 *
 * This shared library is loaded by WPEWebKit when
 *   WPE_BACKEND_LIBRARY=libvex_wpe_backend.so
 * is set in the environment.
 *
 * It implements the wpe_renderer_backend_egl_interface and
 * wpe_renderer_backend_egl_target_interface required by libwpe.
 *
 * Strategy
 * ────────
 * Our framebuffer lives in /tmp/vex_webkit_fb (see webkit_launcher.c).
 * WebKit renders pages in software (Cairo), and webkit_linux_main.c
 * handles capturing those pixels via the WebKit snapshot API.
 *
 * This backend therefore only needs to provide *enough* of the EGL
 * interface for WebKit to initialise without crashing.  All actual
 * pixel delivery goes through the snapshot callback path.
 *
 * We return EGL_DEFAULT_DISPLAY from get_native_display, which causes
 * WebKit to use EGL's software rendering path (Mesa swrast / lavapipe
 * if available) or fall back to Cairo 2D when WebGL is disabled.
 *
 * Build (inside WSL, from the project root):
 *
 *   PKG_CFG="$(pkg-config --cflags --libs wpe-2.0 glib-2.0)"
 *   gcc -shared -fPIC -O2 -o libvex_wpe_backend.so \
 *       userspace/libvex/src/vex_wpe_backend.c $PKG_CFG
 *
 * The resulting .so is installed at:
 *   /home/root/lib/libvex_wpe_backend.so   (inside VexOS ISO)
 */

#include <wpe/wpe.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* On systems where EGLNativeDisplayType is not defined via EGL headers,
   define a compatible placeholder.  The backend returns NULL (software). */
#ifndef EGLNativeDisplayType
#  define EGLNativeDisplayType void *
#endif
#ifndef EGLNativeWindowType
#  define EGLNativeWindowType  void *
#endif
#ifndef EGL_DEFAULT_DISPLAY
#  define EGL_DEFAULT_DISPLAY  ((EGLNativeDisplayType)0)
#endif

/* ── backend data ────────────────────────────────────────────────────────── */

typedef struct {
    int host_fd;
} vex_backend_t;

typedef struct {
    struct wpe_renderer_backend_egl_target *target;
    vex_backend_t *backend;
    int host_fd;
    uint32_t width;
    uint32_t height;
    /* Off-screen buffer for software rendering */
    uint32_t *pixels;
} vex_target_t;

/* ── renderer backend interface ──────────────────────────────────────────── */

static void *backend_create(int host_fd) {
    vex_backend_t *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->host_fd = host_fd;
    fprintf(stderr, "vex_wpe_backend: backend_create (fd=%d)\n", host_fd);
    return b;
}

static void backend_destroy(void *data) {
    free(data);
}

static EGLNativeDisplayType backend_get_native_display(void *data) {
    (void)data;
    /* Return NULL/EGL_DEFAULT_DISPLAY — WebKit will use software EGL */
    return EGL_DEFAULT_DISPLAY;
}

static const struct wpe_renderer_backend_egl_interface s_backend_interface = {
    .create             = backend_create,
    .destroy            = backend_destroy,
    .get_native_display = backend_get_native_display,
};

/* ── renderer backend target interface ───────────────────────────────────── */

static void *target_create(struct wpe_renderer_backend_egl_target *target, int host_fd) {
    vex_target_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->target  = target;
    t->host_fd = host_fd;
    t->width   = 1280;
    t->height  =  720;
    t->pixels  = NULL;
    fprintf(stderr, "vex_wpe_backend: target_create (fd=%d)\n", host_fd);
    return t;
}

static void target_destroy(void *data) {
    vex_target_t *t = (vex_target_t *)data;
    if (t) {
        free(t->pixels);
        free(t);
    }
}

static void target_initialize(void *data, void *backend_data,
                              uint32_t width, uint32_t height) {
    vex_target_t *t = (vex_target_t *)data;
    (void)backend_data;
    if (!t) return;
    t->width  = width  > 0 ? width  : 1280;
    t->height = height > 0 ? height :  720;
    free(t->pixels);
    t->pixels = calloc(t->width * t->height, sizeof(uint32_t));
    fprintf(stderr, "vex_wpe_backend: target_initialize %ux%u\n",
            t->width, t->height);
}

static void target_resize(void *data, uint32_t width, uint32_t height) {
    vex_target_t *t = (vex_target_t *)data;
    if (!t) return;
    t->width  = width;
    t->height = height;
    free(t->pixels);
    t->pixels = calloc(width * height, sizeof(uint32_t));
}

static void target_frame_will_render(void *data) {
    (void)data;
    /* nothing */
}

static void target_frame_rendered(void *data) {
    vex_target_t *t = (vex_target_t *)data;
    if (!t) return;
    /* Notify WPE that we are done with this frame */
    wpe_renderer_backend_egl_target_dispatch_frame_complete(t->target);
}

static EGLNativeWindowType target_get_native_window(void *data) {
    vex_target_t *t = (vex_target_t *)data;
    /* Return the pixels buffer as "native window" — WebKit may write
       rendered content directly into it when using software EGL. */
    return t ? (EGLNativeWindowType)t->pixels : NULL;
}

static const struct wpe_renderer_backend_egl_target_interface s_target_interface = {
    .create            = target_create,
    .destroy           = target_destroy,
    .initialize        = target_initialize,
    .resize            = target_resize,
    .frame_will_render = target_frame_will_render,
    .frame_rendered    = target_frame_rendered,
    .get_native_window = target_get_native_window,
};

/* ── offscreen target interface (for off-screen renders) ─────────────────── */

static void *offscreen_target_create(struct wpe_renderer_backend_egl_offscreen_target *target) {
    (void)target;
    /* Allocate a minimal context object */
    void *obj = calloc(1, sizeof(uintptr_t));
    return obj;
}

static void offscreen_target_destroy(void *data) {
    free(data);
}

static void offscreen_target_initialize(void *data, void *backend_data) {
    (void)data;
    (void)backend_data;
}

static EGLNativeWindowType offscreen_target_get_native_window(void *data) {
    (void)data;
    return NULL;
}

static const struct wpe_renderer_backend_egl_offscreen_target_interface s_offscreen_interface = {
    .create            = offscreen_target_create,
    .destroy           = offscreen_target_destroy,
    .initialize        = offscreen_target_initialize,
    .get_native_window = offscreen_target_get_native_window,
};

/* ── public wpe backend entry point ─────────────────────────────────────── */
/*
 * libwpe looks for the symbol _wpe_backend in the backend .so.
 * It fills in the interface pointers from this struct.
 */
__attribute__((visibility("default")))
struct {
    const struct wpe_renderer_backend_egl_interface                *backend;
    const struct wpe_renderer_backend_egl_target_interface         *target;
    const struct wpe_renderer_backend_egl_offscreen_target_interface *offscreen_target;
} _wpe_backend = {
    .backend           = &s_backend_interface,
    .target            = &s_target_interface,
    .offscreen_target  = &s_offscreen_interface,
};
