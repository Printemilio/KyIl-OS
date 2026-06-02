/*
 * webkit_launcher.c — VexOS native bootstrap for the real WPEWebKit browser.
 *
 * Architecture
 * ────────────
 *   This is a freestanding VexOS native ELF.  It is responsible for:
 *     1. Opening the display surface (framebuffer).
 *     2. Creating shared-memory files used to exchange pixel data and
 *        navigation commands with the Linux-ABI child process.
 *     3. Spawning webkit_linux_main (a real Linux ELF that drives WPEWebKit).
 *     4. Main event loop:
 *          – Reading browser commands via VEX_SYS_BROWSER_COMMAND_READ.
 *          – Writing them into the command file so the child picks them up.
 *          – Detecting new rendered frames in the SHM framebuffer.
 *          – Presenting those frames to the VexOS display.
 *
 * Shared-memory protocol
 * ──────────────────────
 *   /tmp/vex_webkit_fb   – header (vex_webkit_shm_hdr_t) + XRGB8888 pixels
 *   /tmp/vex_webkit_cmd  – null-terminated URL string (max 512 bytes)
 *
 *   The child increments hdr.frame_seq after writing each new frame.
 *   The child increments hdr.cmd_seq_ack after consuming each command.
 *   This launcher increments hdr.cmd_seq when it writes a new command.
 */

#include <stddef.h>
#include <stdint.h>

#include <unos/vex_abi.h>
#include "vex_syscall.h"

/* ── tunables ──────────────────────────────────────────────────────────────── */

#define VEX_WEBKIT_WIDTH    1280u
#define VEX_WEBKIT_HEIGHT    720u
#define VEX_WEBKIT_STRIDE   1280u          /* pixels per row   */

#define SHM_FB_PATH   "/tmp/vex_webkit_fb"
#define SHM_CMD_PATH  "/tmp/vex_webkit_cmd"
#define LINUX_MAIN_PATH "/home/root/bin/webkit_linux_main"

#define SHM_MAGIC        0x56455842u       /* "VEXB"            */
#define SHM_CMD_MAX      512u
#define PIXEL_BUF_BYTES  (VEX_WEBKIT_WIDTH * VEX_WEBKIT_HEIGHT * 4u)
#define SHM_FB_SIZE      (sizeof(vex_webkit_shm_hdr_t) + PIXEL_BUF_BYTES)

#define FRAME_INTERVAL_MS  16u             /* ~60 fps poll       */

/* ── SHM header (shared with webkit_linux_main) ────────────────────────────── */

typedef struct vex_webkit_shm_hdr {
    uint32_t magic;          /* SHM_MAGIC                              */
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t frame_seq;      /* child increments on each new frame     */
    uint32_t cmd_seq;        /* launcher increments on new command     */
    uint32_t cmd_seq_ack;    /* child increments after reading command */
    uint32_t reserved;
} vex_webkit_shm_hdr_t;

/* ── local helpers ─────────────────────────────────────────────────────────── */

static size_t vwl_strlen(const char *s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static void vwl_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = s[i];
}

static void vwl_memset(void *dst, int val, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = (uint8_t)val;
}

static int vwl_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void vwl_debug(const char *msg) {
    vex_syscall1(VEX_SYS_DEBUG_LOG, (uint64_t)(uintptr_t)msg);
}

static void vwl_sleep_ms(uint32_t ms) {
    /* VEX_SYS_USLEEP takes microseconds */
    vex_syscall1(VEX_SYS_USLEEP, (uint64_t)ms * 1000u);
}

/* ── buffers ────────────────────────────────────────────────────────────────── */

static uint8_t  *g_shm_base      = 0;   /* mmap'd SHM — header+pixels (shared with webkit) */
static uint32_t *g_shm_pixels    = 0;   /* pixel area inside g_shm_base (read-only for us) */
static uint32_t *g_display_pixels = 0;  /* heap copy — passed to VEX_SYS_DISPLAY_PRESENT   */

/* ── SHM mmap helper ────────────────────────────────────────────────────────── */

static void *shm_mmap_file(int fd, size_t size) {
    vex_mmap_req_t req;
    req.addr   = 0;
    req.length = size;
    req.prot   = PROT_READ | PROT_WRITE;
    req.flags  = MAP_SHARED;
    req.fd     = fd;
    req.offset = 0;
    void *ptr = (void *)(uintptr_t)vex_syscall1(VEX_SYS_MMAP, (uint64_t)(uintptr_t)&req);
    return (ptr == MAP_FAILED) ? 0 : ptr;
}

/* ── display helpers ───────────────────────────────────────────────────────── */

static int display_open(void) {
    return (int)vex_syscall2(VEX_SYS_DISPLAY_OPEN,
                             (uint64_t)VEX_WEBKIT_WIDTH,
                             (uint64_t)VEX_WEBKIT_HEIGHT);
}

static void display_present(const uint32_t *pixels) {
    vex_display_present_req_t req;
    req.pixels = pixels;
    req.width  = VEX_WEBKIT_WIDTH;
    req.height = VEX_WEBKIT_HEIGHT;
    req.stride = VEX_WEBKIT_STRIDE;
    req.dirty  = 0;
    vex_syscall1(VEX_SYS_DISPLAY_PRESENT, (uint64_t)(uintptr_t)&req);
}

/* 64-bit word copy — much faster than the byte-wise vwl_memcpy for large buffers */
static void vwl_memcpy_fast(void *dst, const void *src, size_t n) {
    uint64_t       *d = (uint64_t *)dst;
    const uint64_t *s = (const uint64_t *)src;
    size_t i, n64 = n >> 3;
    uint8_t       *d8;
    const uint8_t *s8;
    for (i = 0; i < n64; ++i) d[i] = s[i];
    d8 = (uint8_t *)(d + n64);
    s8 = (const uint8_t *)(s + n64);
    for (i = 0; i < (n & 7u); ++i) d8[i] = s8[i];
}

/* Copy pixels from mmap → heap buffer, then present the heap buffer.
 * The kernel's display_present requires heap memory (user_alloc range).
 * The mmap'd SHM isn't always in that range, so we must copy first. */
static void display_present_shm(void) {
    if (!g_shm_pixels || !g_display_pixels) return;
    vwl_memcpy_fast(g_display_pixels, g_shm_pixels, PIXEL_BUF_BYTES);
    display_present(g_display_pixels);
}

/* ── SHM file helpers ──────────────────────────────────────────────────────── */

/* Open or create a file and extend it to `size` bytes.
   Returns fd or -1. */
static int shm_open_or_create(const char *path, size_t size) {
    /* O_RDWR | O_CREAT | O_TRUNC = 0x242 in Linux conventions; VexOS maps
       the same flags as Linux for its native open syscall. */
    int fd = (int)vex_syscall2(VEX_SYS_OPEN, (uint64_t)(uintptr_t)path, 0x242u);
    if (fd < 0) {
        return -1;
    }
    /* Extend to required size by writing a zero byte at the last position */
    if (size > 0) {
        uint8_t zero = 0;
        vex_rw_offset_req_t req;
        req.fd     = fd;
        req.buffer = &zero;
        req.count  = 1;
        req.offset = (off_t)(size - 1u);
        vex_syscall1(VEX_SYS_PWRITE, (uint64_t)(uintptr_t)&req);
    }
    return fd;
}

/* Write `count` bytes to `fd` at `offset`. */
static int shm_pwrite(int fd, const void *buf, size_t count, off_t offset) {
    vex_rw_offset_req_t req;
    req.fd     = fd;
    req.buffer = (void *)buf;
    req.count  = count;
    req.offset = offset;
    return (int)vex_syscall1(VEX_SYS_PWRITE, (uint64_t)(uintptr_t)&req);
}

/* Read `count` bytes from `fd` at `offset`. */
static int shm_pread(int fd, void *buf, size_t count, off_t offset) {
    vex_rw_offset_req_t req;
    req.fd     = fd;
    req.buffer = buf;
    req.count  = count;
    req.offset = offset;
    return (int)vex_syscall1(VEX_SYS_PREAD, (uint64_t)(uintptr_t)&req);
}

/* ── process spawn ─────────────────────────────────────────────────────────── */

static int spawn_linux_main(void) {
    vex_process_spawn_req_t req;
    req.path  = LINUX_MAIN_PATH;
    req.flags = 0;
    req.pid   = 0;
    if ((int64_t)vex_syscall1(VEX_SYS_PROCESS_SPAWN,
                              (uint64_t)(uintptr_t)&req) != 0) {
        return -1;
    }
    return (int)req.pid;
}

/* ── browser command read ──────────────────────────────────────────────────── */

static int command_read(char *buf, size_t size) {
    vex_browser_command_req_t req;
    req.buffer = buf;
    req.size   = size;
    return (int)vex_syscall1(VEX_SYS_BROWSER_COMMAND_READ,
                             (uint64_t)(uintptr_t)&req);
}

/* ── present a loading splash while waiting for first frame ────────────────── */

static void draw_splash(void) {
    uint32_t x, y;
    if (!g_display_pixels) return;
    for (y = 0; y < VEX_WEBKIT_HEIGHT; ++y) {
        for (x = 0; x < VEX_WEBKIT_WIDTH; ++x) {
            g_display_pixels[y * VEX_WEBKIT_STRIDE + x] = 0xff1a1a2eu;
        }
    }
    for (y = VEX_WEBKIT_HEIGHT / 2u - 4u; y < VEX_WEBKIT_HEIGHT / 2u + 4u; ++y) {
        for (x = VEX_WEBKIT_WIDTH / 4u; x < VEX_WEBKIT_WIDTH * 3u / 4u; ++x) {
            g_display_pixels[y * VEX_WEBKIT_STRIDE + x] = 0xffffffff;
        }
    }
}

/* ── entry point ───────────────────────────────────────────────────────────── */

int main(void) {
    int fb_fd    = -1;
    int cmd_fd   = -1;
    int child_pid;

    vex_webkit_shm_hdr_t hdr;
    vex_webkit_shm_hdr_t hdr_prev;

    char command[SHM_CMD_MAX];
    char last_command[SHM_CMD_MAX];
    uint32_t last_cmd_seq = 0u;

    vwl_debug("webkit_launcher: starting\n");

    /* Heap buffer for display_present — kernel requires memory in user_alloc range */
    g_display_pixels = (uint32_t *)(uintptr_t)
        vex_syscall1(VEX_SYS_HEAP_ALLOC, (uint64_t)PIXEL_BUF_BYTES);
    if (!g_display_pixels) {
        vwl_debug("webkit_launcher: heap alloc failed\n");
        return 1;
    }

    /* Open display */
    if (display_open() != 0) {
        vwl_debug("webkit_launcher: display_open failed\n");
        return 1;
    }

    /* Create shared framebuffer file */
    fb_fd = shm_open_or_create(SHM_FB_PATH, SHM_FB_SIZE);
    if (fb_fd < 0) {
        vwl_debug("webkit_launcher: cannot create SHM FB file\n");
        return 1;
    }

    /* Map the SHM — both header and pixels accessible as plain memory */
    g_shm_base = (uint8_t *)shm_mmap_file(fb_fd, SHM_FB_SIZE);
    if (!g_shm_base) {
        vwl_debug("webkit_launcher: mmap failed\n");
        return 1;
    }
    g_shm_pixels = (uint32_t *)(g_shm_base + sizeof(vex_webkit_shm_hdr_t));

    /* Initialise the SHM header via mmap */
    vwl_memset(&hdr, 0, sizeof(hdr));
    hdr.magic    = SHM_MAGIC;
    hdr.width    = VEX_WEBKIT_WIDTH;
    hdr.height   = VEX_WEBKIT_HEIGHT;
    hdr.stride   = VEX_WEBKIT_STRIDE;
    vwl_memcpy(g_shm_base, &hdr, sizeof(hdr));

    /* Create command file with home URL */
    cmd_fd = shm_open_or_create(SHM_CMD_PATH, SHM_CMD_MAX);
    if (cmd_fd < 0) {
        vwl_debug("webkit_launcher: cannot create SHM CMD file\n");
        return 1;
    }
    {
        const char *home = "vex://home";
        shm_pwrite(cmd_fd, home, vwl_strlen(home) + 1u, 0);
    }
    vwl_memcpy(last_command, "vex://home", vwl_strlen("vex://home") + 1u);

    /* Show splash while WebKit is initialising */
    draw_splash();
    display_present(g_display_pixels);

    /* Spawn the Linux child process */
    child_pid = spawn_linux_main();
    if (child_pid < 0) {
        vwl_debug("webkit_launcher: spawn failed\n");
        /* Still keep running — operator may manually launch the child */
    } else {
        vwl_debug("webkit_launcher: webkit_linux_main spawned\n");
    }

    vwl_memset(&hdr_prev, 0, sizeof(hdr_prev));

    /* ── main event loop ───────────────────────────────────────────────────── */
    for (;;) {
        /* 1. Read browser navigation command */
        command[0] = '\0';
        if (command_read(command, sizeof(command)) > 0 &&
            command[0] != '\0' &&
            vwl_strcmp(command, last_command) != 0) {

            vwl_memcpy(last_command, command, vwl_strlen(command) + 1u);

            vwl_debug("webkit_launcher: nav -> ");
            vwl_debug(command);
            vwl_debug("\n");

            /* Write URL to command file */
            shm_pwrite(cmd_fd, command, vwl_strlen(command) + 1u, 0);

            /* Bump the cmd_seq in the SHM header so the child notices */
            {
                vex_webkit_shm_hdr_t *hdrp = (vex_webkit_shm_hdr_t *)g_shm_base;
                if (hdrp->magic == SHM_MAGIC) {
                    hdrp->cmd_seq = ++last_cmd_seq;
                    vwl_debug("webkit_launcher: cmd_seq bumped\n");
                } else {
                    vwl_debug("webkit_launcher: bad SHM magic!\n");
                }
            }
        }

        /* 2. Check for a new frame from the child — read header via mmap (no syscall) */
        vwl_memcpy(&hdr, g_shm_base, sizeof(hdr));
        if (hdr.magic == SHM_MAGIC &&
            hdr.frame_seq != hdr_prev.frame_seq) {

            vwl_debug("webkit_launcher: new frame from WebKit\n");
            /* Pixels are already in the mmap'd region — present directly */
            display_present_shm();
            hdr_prev.frame_seq = hdr.frame_seq;
        }

        vwl_sleep_ms(FRAME_INTERVAL_MS);
    }

    /* unreachable */
    vex_syscall1(VEX_SYS_CLOSE, (uint64_t)(int64_t)fb_fd);
    vex_syscall1(VEX_SYS_CLOSE, (uint64_t)(int64_t)cmd_fd);
    return 0;
}
