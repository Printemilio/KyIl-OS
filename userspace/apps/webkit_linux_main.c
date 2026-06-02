/* Expose POSIX + GNU extensions (pread/pwrite/setenv/putenv) before any include */
#define _GNU_SOURCE

/*
 * webkit_linux_main.c — Real WPEWebKit browser process.
 *
 * This is a standard Linux ELF (compiled with a normal Linux toolchain).
 * It runs under VexOS's Linux ABI compatibility layer.
 *
 * It links against:
 *   libWPEWebKit-2.0.so        – WebKit WPE port (2.x API)
 *   libwpe-2.0.so              – libwpe base
 *   libglib-2.0.so             – GLib main loop
 *   libgio-2.0.so              – GIO (for async IO used by WebKit)
 *
 * Rendering strategy
 * ──────────────────
 *  1. Create a WebKitWebView with hardware acceleration disabled.
 *  2. On every WEBKIT_LOAD_COMMITTED / WEBKIT_LOAD_FINISHED event, request
 *     a snapshot.  Also fire a 60 ms periodic timer so live pages refresh.
 *  3. Each snapshot yields a WebKitImage* (BGRA_8888 format, WPE 2.x API).
 *     Extract pixels via webkit_image_as_bytes() → GBytes → raw pointer,
 *     copy as XRGB8888 into the shared-memory file /tmp/vex_webkit_fb,
 *     and bump frame_seq.
 *  4. Poll /tmp/vex_webkit_cmd (driven by hdr.cmd_seq) for new URLs and
 *     call webkit_web_view_load_uri().
 *
 * Shared-memory protocol (matches webkit_launcher.c)
 * ─────────────────────────────────────────────────
 *  /tmp/vex_webkit_fb  : vex_webkit_shm_hdr_t  +  W*H*4 XRGB8888 pixels
 *  /tmp/vex_webkit_cmd : null-terminated URL string
 */

#include <wpe/webkit.h>
#include <wpe/wpe.h>
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

/* ── display dimensions (must match webkit_launcher.c) ──────────────────── */

#define FB_WIDTH   1280
#define FB_HEIGHT   720
#define FB_STRIDE  1280   /* pixels per row */

#define SHM_FB_PATH   "/tmp/vex_webkit_fb"
#define SHM_CMD_PATH  "/tmp/vex_webkit_cmd"
#define SHM_MAGIC     0x56455842u   /* "VEXB" */
#define SHM_CMD_MAX   512

#define PIXEL_BUF_BYTES  ((size_t)(FB_WIDTH * FB_HEIGHT * 4))
#define SHM_FB_SIZE      (sizeof(vex_webkit_shm_hdr_t) + PIXEL_BUF_BYTES)

/* ── SHM header (keep in sync with webkit_launcher.c) ───────────────────── */

typedef struct {
    uint32_t magic;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t frame_seq;      /* we increment on each new frame      */
    uint32_t cmd_seq;        /* launcher increments on new command  */
    uint32_t cmd_seq_ack;    /* we increment after reading command  */
    uint32_t reserved;
} vex_webkit_shm_hdr_t;

/* ── globals ─────────────────────────────────────────────────────────────── */

static int              g_fb_fd   = -1;
static int              g_cmd_fd  = -1;
static uint8_t         *g_shm_ptr = NULL;   /* mmap'd SHM region            */
static WebKitWebView   *g_view    = NULL;
static GMainLoop       *g_loop    = NULL;
static gboolean         g_snapshot_pending = FALSE;

/* Last command sequence number seen from the launcher */
static uint32_t g_cmd_seq_seen = 0;

/* ── SHM helpers ─────────────────────────────────────────────────────────── */

static inline vex_webkit_shm_hdr_t *shm_hdr(void) {
    return (vex_webkit_shm_hdr_t *)g_shm_ptr;
}

static inline uint32_t *shm_pixels(void) {
    return (uint32_t *)(g_shm_ptr + sizeof(vex_webkit_shm_hdr_t));
}

static void shm_write_header(const vex_webkit_shm_hdr_t *hdr) {
    if (!g_shm_ptr) return;
    memcpy(g_shm_ptr, hdr, sizeof(*hdr));
}

static void shm_read_header(vex_webkit_shm_hdr_t *hdr) {
    if (!g_shm_ptr) { memset(hdr, 0, sizeof(*hdr)); return; }
    memcpy(hdr, g_shm_ptr, sizeof(*hdr));
}

static int shm_read_cmd(char *buf, size_t size) {
    if (g_cmd_fd < 0) return -1;
    ssize_t n = pread(g_cmd_fd, buf, size - 1, 0);
    if (n <= 0) { buf[0] = '\0'; return 0; }
    buf[n] = '\0';
    return (int)n;
}

/* ── snapshot callback ───────────────────────────────────────────────────── */

static void on_snapshot_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    GError       *error  = NULL;
    WebKitImage  *image  = NULL;
    GBytes       *bytes  = NULL;
    (void)user_data;

    g_snapshot_pending = FALSE;

    /* WPE 2.x: snapshot_finish returns WebKitImage*, not cairo_surface_t* */
    image = webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(source), res, &error);
    if (error) {
        g_error_free(error);
        return;
    }
    if (!image) return;

    /* webkit_image_as_bytes() gives the raw BGRA_8888 pixel data.
     * As little-endian uint32: byte layout = B,G,R,A → value = 0xAARRGGBB.
     * Forcing alpha=0xFF yields XRGB8888, identical to our framebuffer format. */
    bytes = webkit_image_as_bytes(image);
    if (bytes && g_shm_ptr) {
        gsize      data_size  = 0;
        const uint8_t *src_data = (const uint8_t *)g_bytes_get_data(bytes, &data_size);
        int src_width  = webkit_image_get_width(image);
        int src_height = webkit_image_get_height(image);
        guint src_stride = webkit_image_get_stride(image);
        int dst_width  = (FB_WIDTH  < src_width)  ? FB_WIDTH  : src_width;
        int dst_height = (FB_HEIGHT < src_height) ? FB_HEIGHT : src_height;
        uint32_t *dst_pixels = shm_pixels();
        int y;

        for (y = 0; y < dst_height; ++y) {
            const uint32_t *src_row =
                (const uint32_t *)(const void *)(src_data + (gsize)y * src_stride);
            uint32_t *dst_row = dst_pixels + y * FB_STRIDE;
            int x;
            for (x = 0; x < dst_width; ++x) {
                /* BGRA_8888 as LE uint32 = 0xAARRGGBB → force A=FF → XRGB8888 */
                dst_row[x] = src_row[x] | 0xFF000000u;
            }
        }

        g_bytes_unref(bytes);

        /* Bump frame counter — written after pixels are visible (x86 TSO) */
        vex_webkit_shm_hdr_t *hdrp = shm_hdr();
        if (hdrp->magic == SHM_MAGIC)
            hdrp->frame_seq++;
    } else {
        if (bytes) g_bytes_unref(bytes);
    }

    g_object_unref(image);
}

/* ── request a snapshot (guarded against queuing multiple at once) ────────── */

static void request_snapshot(void) {
    if (g_snapshot_pending || !g_view) return;
    g_snapshot_pending = TRUE;
    webkit_web_view_get_snapshot(g_view,
                                 WEBKIT_SNAPSHOT_REGION_VISIBLE,
                                 WEBKIT_SNAPSHOT_OPTIONS_NONE,
                                 NULL,
                                 on_snapshot_ready,
                                 NULL);
}

/* ── periodic timer: poll for commands and refresh frame ─────────────────── */

static gboolean on_timer(gpointer user_data) {
    vex_webkit_shm_hdr_t hdr;
    (void)user_data;

    /* Check for new navigation command from launcher */
    shm_read_header(&hdr);
    if (hdr.magic == SHM_MAGIC && hdr.cmd_seq != g_cmd_seq_seen) {
        char url[SHM_CMD_MAX];
        g_cmd_seq_seen = hdr.cmd_seq;
        if (shm_read_cmd(url, sizeof(url)) > 0 && url[0] != '\0') {
            /* Skip internal VexOS scheme — show about:blank fallback */
            if (strncmp(url, "vex://", 6) == 0) {
                webkit_web_view_load_html(g_view,
                    "<html><body style='background:#1a1a2e;color:#fff;font-family:sans-serif'>"
                    "<h1 style='padding:40px'>VexOS Browser</h1>"
                    "<p>Navigate to a URL using the browser command interface.</p>"
                    "</body></html>",
                    "about:blank");
            } else {
                webkit_web_view_load_uri(g_view, url);
            }
            /* Acknowledge the command */
            shm_read_header(&hdr);
            if (hdr.magic == SHM_MAGIC) {
                hdr.cmd_seq_ack = g_cmd_seq_seen;
                shm_write_header(&hdr);
            }
        }
    }

    /* Periodic frame refresh for live/animated content */
    request_snapshot();

    return G_SOURCE_CONTINUE;
}

/* ── load-changed signal ─────────────────────────────────────────────────── */

static void on_load_changed(WebKitWebView *view, WebKitLoadEvent load_event, gpointer user_data) {
    (void)user_data;
    (void)view;
    switch (load_event) {
    case WEBKIT_LOAD_COMMITTED:
    case WEBKIT_LOAD_FINISHED:
        request_snapshot();
        break;
    default:
        break;
    }
}

/* ── SHM file setup ──────────────────────────────────────────────────────── */

static int open_and_init_shm(void) {
    vex_webkit_shm_hdr_t hdr;
    off_t fsize;
    struct stat st;

    /* Open framebuffer SHM (created by the native launcher) */
    g_fb_fd = open(SHM_FB_PATH, O_RDWR);
    if (g_fb_fd < 0) {
        /* Not yet created — create it ourselves */
        g_fb_fd = open(SHM_FB_PATH, O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (g_fb_fd < 0) {
            fprintf(stderr, "webkit_linux_main: cannot open %s\n", SHM_FB_PATH);
            return -1;
        }
        /* Extend to required size */
        fsize = (off_t)SHM_FB_SIZE;
        if (ftruncate(g_fb_fd, fsize) < 0) {
            perror("ftruncate fb");
            return -1;
        }
        memset(&hdr, 0, sizeof(hdr));
        hdr.magic  = SHM_MAGIC;
        hdr.width  = FB_WIDTH;
        hdr.height = FB_HEIGHT;
        hdr.stride = FB_STRIDE;
        shm_write_header(&hdr);
    } else {
        /* Verify the launcher already set it up */
        if (fstat(g_fb_fd, &st) == 0 && (size_t)st.st_size < SHM_FB_SIZE) {
            ftruncate(g_fb_fd, (off_t)SHM_FB_SIZE);
        }
        shm_read_header(&hdr);
        if (hdr.magic != SHM_MAGIC) {
            hdr.magic  = SHM_MAGIC;
            hdr.width  = FB_WIDTH;
            hdr.height = FB_HEIGHT;
            hdr.stride = FB_STRIDE;
            shm_write_header(&hdr);
        }
    }

    /* Map the framebuffer SHM — eliminates pread/pwrite per frame */
    g_shm_ptr = (uint8_t *)mmap(NULL, SHM_FB_SIZE,
                                PROT_READ | PROT_WRITE, MAP_SHARED,
                                g_fb_fd, 0);
    if (g_shm_ptr == MAP_FAILED) {
        fprintf(stderr, "webkit_linux_main: mmap failed\n");
        g_shm_ptr = NULL;
        return -1;
    }

    /* Open command SHM */
    g_cmd_fd = open(SHM_CMD_PATH, O_RDONLY);
    if (g_cmd_fd < 0) {
        /* Non-fatal — we'll just show the default page */
        fprintf(stderr, "webkit_linux_main: cannot open %s (non-fatal)\n", SHM_CMD_PATH);
    }

    return 0;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    WebKitSettings   *settings;
    WebKitWebContext *context;
    (void)argc;
    (void)argv;

    fprintf(stderr, "webkit_linux_main: starting\n");

    /* Set up WPE backend.  We prefer the headless backend so we don't need
       a real display server.  Applications can override via env var. */
    if (!getenv("WPE_BACKEND_LIBRARY")) {
        /* Try the headless backend shipped with WPE */
        putenv("WPE_BACKEND_LIBRARY=libWPEBackend-default.so");
    }
    /* Force software rendering — no GPU required under VexOS compat */
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0);
    setenv("LIBGL_ALWAYS_SOFTWARE", "true", 0);
    setenv("GALLIUM_DRIVER", "softpipe", 0);

    /* Fonts & fontconfig — tell WebKit where to find fonts on VexOS.
     * /usr/share/fonts and /etc/fonts are staged on the boot CD-ROM.
     * Cache goes to /tmp (writable scratch space). */
    setenv("FONTCONFIG_PATH",    "/etc/fonts",       0);
    setenv("FONTCONFIG_CACHE",   "/tmp/fc-cache",    0);
    setenv("FC_CACHEDIR",        "/tmp/fc-cache",    0);
    setenv("XDG_DATA_DIRS",      "/usr/share",       0);

    /* Initialise shared memory */
    if (open_and_init_shm() < 0) {
        return 1;
    }

    /* Build a WebKitSettings that disables features that need GPU/EGL.
     *
     * WPE 2.x API note:
     *   - webkit_settings_set_enable_accelerated_2d_canvas()     → REMOVED
     *   - webkit_settings_set_hardware_acceleration_policy()      → REMOVED
     *   Use webkit_settings_set_enable_webgl() to gate GPU paths instead.
     */
    settings = webkit_settings_new();
    webkit_settings_set_enable_webgl(settings, FALSE);
    webkit_settings_set_enable_webaudio(settings, FALSE);
    webkit_settings_set_enable_media(settings, FALSE);
    webkit_settings_set_enable_mediasource(settings, FALSE);
    webkit_settings_set_javascript_can_access_clipboard(settings, FALSE);

    /* Create a web context.
     * WPE 2.x API note:
     *   - webkit_web_context_set_process_model()  → REMOVED (always multi-process now).
     *   Use WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER to minimise memory use. */
    context = webkit_web_context_new();
    webkit_web_context_set_cache_model(context, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);

    /* Create the web view */
    g_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "web-context", context,
        "settings",    settings,
        NULL));
    g_object_unref(settings);
    g_object_unref(context);

    /* Connect signals */
    g_signal_connect(g_view, "load-changed", G_CALLBACK(on_load_changed), NULL);

    /* Load the VexOS home page stub */
    webkit_web_view_load_html(g_view,
        "<html><body style='background:#1a1a2e;color:#e0e0ff;"
        "font-family:\"Segoe UI\",Arial,sans-serif;margin:0'>"
        "<div style='padding:48px'>"
        "<h1 style='font-size:2em;margin-bottom:8px'>VexOS Browser</h1>"
        "<p style='color:#a0a0cc'>Powered by WPEWebKit</p>"
        "<hr style='border-color:#333;margin:24px 0'>"
        "<p>Type a URL in the address bar to navigate.</p>"
        "</div></body></html>",
        "vex://home");

    /* GLib main loop */
    g_loop = g_main_loop_new(NULL, FALSE);

    /* Timer: 16 ms = ~60 fps */
    g_timeout_add(16, on_timer, NULL);

    fprintf(stderr, "webkit_linux_main: entering main loop\n");
    g_main_loop_run(g_loop);

    /* Cleanup */
    if (g_shm_ptr) munmap(g_shm_ptr, SHM_FB_SIZE);
    if (g_fb_fd  >= 0) close(g_fb_fd);
    if (g_cmd_fd >= 0) close(g_cmd_fd);
    g_main_loop_unref(g_loop);
    return 0;
}
