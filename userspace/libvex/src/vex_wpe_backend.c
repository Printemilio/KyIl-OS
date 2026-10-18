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
 * Real EGL/GLES renders into CPU-visible Vex Surface backings. Typed IPC
 * transfers a read/present capability to the UI; ACK follows compositor
 * consumption. This backend does not manufacture snapshots or pixels.
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <wpe/wpe.h>
#include <wpe/wpe-egl.h>
#include <vex_wpe_input.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <glib.h>
#include <glib-unix.h>
#include <stdatomic.h>
#include <errno.h>
#include <fcntl.h>
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "vex_egl_backend.h"
#include "vex_browser_protocol.h"

#ifndef EGL_DEFAULT_DISPLAY
#  define EGL_DEFAULT_DISPLAY  ((EGLNativeDisplayType)0)
#endif

/* ── backend data ────────────────────────────────────────────────────────── */

typedef struct {
    int host_fd;
} vex_backend_t;

/* UI-process view backend state.  The endpoints use VexOS's existing
 * socketpair compatibility path; no X11, Wayland or GPU object is created. */
typedef struct {
    struct wpe_view_backend *backend;
    int renderer_host_fd;
    int renderer_peer_fd;
    guint source;
    int viewport_fd;
    guint viewport_source;
    uint32_t width,height;
    vex_browser_frame pending;
    int awaiting_frame;
} vex_view_backend_t;

#define VEX_MAX_RENDERER_CLIENTS 16u
typedef struct {
    int peer_fds[VEX_MAX_RENDERER_CLIENTS];
} vex_renderer_host_t;

typedef struct {
    struct wpe_renderer_backend_egl_target *target;
    vex_backend_t *backend;
    int host_fd;
    uint32_t width;
    uint32_t height;
    /* Off-screen buffer for software rendering */
    vex_egl_window window;
    uint64_t frame_sequence;
    uint64_t completed_sequence;
    atomic_uint references;
} vex_target_t;

#define VEX_WPE_FRAME_MAGIC UINT64_C(0x5657504546524d31)
typedef struct {uint64_t magic,sequence;uint32_t width,height;} vex_wpe_frame;

static void target_retain(void *owner){vex_target_t *t=owner;atomic_fetch_add(&t->references,1);}
static void target_release(void *owner){vex_target_t *t=owner;if(atomic_fetch_sub(&t->references,1)==1){if(t->host_fd>=0)close(t->host_fd);free(t);}}
static gboolean view_fail(vex_view_backend_t *view){
    if(view->renderer_peer_fd>=0){close(view->renderer_peer_fd);view->renderer_peer_fd=-1;}
    view->source=0;return G_SOURCE_REMOVE;
}
static int target_present_surface(void *owner,uint64_t capability,uint64_t serial){
    vex_target_t *t=owner;if(!t||t->host_fd<0)return -1;
    vex_wpe_frame frame={VEX_WPE_FRAME_MAGIC,serial,t->window.width,t->window.height};
    vex_message_attachment_req_t attachment={capability,VEX_MESSAGE_OBJECT_SURFACE,
        VEX_SURFACE_RIGHT_QUERY|VEX_SURFACE_RIGHT_MAP_READ|VEX_SURFACE_RIGHT_PRESENT|VEX_SURFACE_RIGHT_COMPOSE|VEX_SURFACE_RIGHT_EXPORT};
    vex_message_io_req_t send_req={t->host_fd,0,&frame,sizeof(frame),&attachment,1};
    if(serial<=8)fprintf(stderr,"vex_wpe_backend: send Surface fd=%d sequence=%llu cap=%llu rights=%x\n",t->host_fd,(unsigned long long)serial,(unsigned long long)capability,attachment.rights);
    if((int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send_req)!=(int64_t)sizeof(frame))return -1;
    vex_wpe_frame ack={0};vex_message_io_req_t receive={t->host_fd,0,&ack,sizeof(ack),NULL,0};
    if((int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&receive)!=(int64_t)sizeof(ack)||ack.magic!=frame.magic||ack.sequence!=frame.sequence)return -1;
    if(serial<=8)fprintf(stderr,"vex_wpe_backend: received ACK fd=%d sequence=%llu\n",t->host_fd,(unsigned long long)ack.sequence);
    t->completed_sequence=ack.sequence;return 0;
}

/* Readiness is scheduler/IPC driven. Present requires the existing COMPOSE
 * right because this UI endpoint asks the compositor to read the Surface.
 * The imported capability grants no write or export rights. ACK follows real compositor consumption and release, so
 * the renderer cannot overwrite a backing that the UI is still reading. */
static gboolean view_receive_frame(gint fd,GIOCondition condition,gpointer data){
    vex_view_backend_t *view=data;
    if(!(condition&G_IO_IN))return view_fail(view);
    vex_wpe_frame frame={0};vex_message_attachment_req_t attached[1]={{0}};
    vex_message_io_req_t r={fd,MSG_DONTWAIT,&frame,sizeof(frame),attached,1};
    int64_t bytes=(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&r);
    if(bytes<0)return bytes==-EAGAIN?G_SOURCE_CONTINUE:view_fail(view);
    uint64_t cap=r.attachment_count==1&&attached[0].type==VEX_MESSAGE_OBJECT_SURFACE?attached[0].handle:0;
    int valid=bytes==sizeof(frame)&&cap&&frame.magic==VEX_WPE_FRAME_MAGIC;
    vex_surface_query_req_t q={0};q.surface_handle=cap;int64_t query_result=-1,present_result=-1;
    if(valid){query_result=(int64_t)vex_syscall1(VEX_SYS_SURFACE_QUERY,(uintptr_t)&q);valid=query_result==0&&q.width==frame.width&&q.height==frame.height;}
    if(valid && !view->awaiting_frame){
        attached[0].rights=VEX_SURFACE_RIGHT_QUERY|VEX_SURFACE_RIGHT_MAP_READ|VEX_SURFACE_RIGHT_PRESENT|VEX_SURFACE_RIGHT_COMPOSE;
        vex_message_io_req_t forward={view->viewport_fd,0,&frame,sizeof(frame),attached,1};
        present_result=(int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&forward);
        valid=present_result==sizeof(frame);
    }else valid=0;
    if(cap)(void)vex_syscall1(VEX_SYS_GPU_SURFACE_DESTROY,cap);
    if(!valid){fprintf(stderr,"vex_wpe_backend: rejected frame bytes=%lld attachments=%llu type=%u rights=%x cap=%llu magic=%llx sequence=%llu dimensions=%ux%u query=%lld actual=%ux%u present=%lld\n",(long long)bytes,(unsigned long long)r.attachment_count,attached[0].type,attached[0].rights,(unsigned long long)cap,(unsigned long long)frame.magic,(unsigned long long)frame.sequence,frame.width,frame.height,(long long)query_result,q.width,q.height,(long long)present_result);return view_fail(view);}
    view->pending=(vex_browser_frame){frame.magic,frame.sequence,frame.width,frame.height};view->awaiting_frame=1;
    return G_SOURCE_CONTINUE;
}

static uint32_t viewport_keysym(uint32_t key,uint32_t hardware) {
    if(key<0x110000u) {
        switch(key){case 8:return 0xff08;case 9:return 0xff09;case 10:return 0xff0d;case 27:return 0xff1b;}
        return wpe_unicode_to_key_code(key);
    }
    switch(hardware){case 128+0x48:return 0xff52;case 128+0x50:return 0xff54;
        case 128+0x4b:return 0xff51;case 128+0x4d:return 0xff53;
        case 128+0x47:return 0xff50;case 128+0x4f:return 0xff57;
        case 128+0x53:return 0xffff;case 128+0x49:return 0xff55;case 128+0x51:return 0xff56;
        case 0x2a:return 0xffe1;case 0x36:return 0xffe2;case 0x1d:return 0xffe3;case 128+0x1d:return 0xffe4;
        case 0x38:return 0xffe9;case 128+0x38:return 0xffea;default:return 0;}
}
static gboolean view_receive_native(gint fd,GIOCondition condition,gpointer data) {
    vex_view_backend_t *v=data;
    if(!(condition&G_IO_IN)){v->viewport_source=0;return G_SOURCE_REMOVE;}
    union{vex_browser_frame frame;vex_browser_input input;} message;
    vex_message_io_req_t r={fd,MSG_DONTWAIT,&message,sizeof(message),NULL,0};
    int64_t n=(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&r);
    if(n==-EAGAIN)return G_SOURCE_CONTINUE;
    if(n==0) {
        /* Orderly native-window teardown. Do not leave a renderer waiting
         * forever for an ACK after its consumer disappeared. */
        if(v->source)g_source_remove(v->source);
        view_fail(v);v->viewport_source=0;
        return G_SOURCE_REMOVE;
    }
    if(n==sizeof(vex_browser_frame)&&message.frame.magic==VEX_BROWSER_FRAME && v->awaiting_frame &&
        message.frame.sequence==v->pending.sequence) {
        wpe_view_backend_dispatch_frame_displayed(v->backend);
        vex_message_io_req_t ack={v->renderer_peer_fd,0,&message.frame,sizeof(message.frame),NULL,0};
        if((int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&ack)!=sizeof(message.frame))return view_fail(v);
        v->awaiting_frame=0;return G_SOURCE_CONTINUE;
    }
    if(n!=sizeof(vex_browser_input)||message.input.magic!=VEX_BROWSER_INPUT){fprintf(stderr,"vex_wpe_backend: invalid native message %lld\n",(long long)n);v->viewport_source=0;return G_SOURCE_REMOVE;}
    vex_viewport_event_t *e=&message.input.event;
    if(e->type==VEX_VIEWPORT_CONFIGURE) {
        if(e->width && e->height) {
            v->width=e->width;v->height=e->height;
            wpe_view_backend_dispatch_set_size(v->backend,e->width,e->height);
            wpe_view_backend_add_activity_state(v->backend,wpe_view_activity_state_visible);
        }else wpe_view_backend_remove_activity_state(v->backend,wpe_view_activity_state_visible|wpe_view_activity_state_focused);
    }else if(e->type==VEX_VIEWPORT_FOCUS) {
        if(e->pressed)wpe_view_backend_add_activity_state(v->backend,wpe_view_activity_state_focused);
        else wpe_view_backend_remove_activity_state(v->backend,wpe_view_activity_state_focused);
    }else if(e->type==VEX_VIEWPORT_KEY) {
        uint32_t keysym=viewport_keysym(e->key,e->hardware);
        if(keysym){
            struct wpe_input_keyboard_event k={e->time,keysym,e->hardware+8,e->pressed!=0,e->modifiers};
            wpe_view_backend_dispatch_keyboard_event(v->backend,&k);
        }
    }else if(e->type==VEX_VIEWPORT_WHEEL) {
        /* This native device reports discrete detents, not a touchpad smooth
         * gesture. libwpe's vertical wheel consumes one signed detent per
         * event; preserve every detent rather than collapsing its magnitude. */
        int ticks=e->value<0?-e->value:e->value;
        struct wpe_input_axis_event a={wpe_input_axis_event_type_motion,e->time,e->x,e->y,0,e->value>0?-1:1,e->modifiers};
        for(int i=0;i<ticks;i++)wpe_view_backend_dispatch_axis_event(v->backend,&a);
    }else {
        struct wpe_input_pointer_event p={wpe_input_pointer_event_type_motion,e->time,e->x,e->y,0,0,e->modifiers};
        p.modifiers|=(e->hardware&1u)?1u<<20:0;
        p.modifiers|=(e->hardware&2u)?1u<<22:0;
        p.modifiers|=(e->hardware&4u)?1u<<21:0;
        if(e->type==VEX_VIEWPORT_BUTTON){
            p.button=vex_wpe_button_from_native((uint32_t)e->value);
            if(!p.button)return G_SOURCE_CONTINUE;
            p.type=wpe_input_pointer_event_type_button;p.state=e->pressed;
        }
        if(e->type==VEX_VIEWPORT_LEAVE){p.x=-1;p.y=-1;}
        wpe_view_backend_dispatch_pointer_event(v->backend,&p);
    }
    return G_SOURCE_CONTINUE;
}

static unsigned g_target_events;
static unsigned g_offscreen_events;

static void backend_trace_target(const char *event, uint32_t width, uint32_t height) {
    if (g_target_events < 12u) {
        fprintf(stderr, "vex_wpe_backend: target.%s %ux%u\n", event, width, height);
        ++g_target_events;
    }
}

static void backend_trace_offscreen(const char *event) {
    if (g_offscreen_events < 12u) {
        fprintf(stderr, "vex_wpe_backend: offscreen.%s\n", event);
        ++g_offscreen_events;
    }
}

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
    /* The actual Vex EGL display connects to native Surface backings. */
    return EGL_DEFAULT_DISPLAY;
}

static uint32_t backend_get_platform(void *data) {
    (void)data;
    return 0;
}

static const struct wpe_renderer_backend_egl_interface s_backend_interface = {
    .create             = backend_create,
    .destroy            = backend_destroy,
    .get_native_display = backend_get_native_display,
    .get_platform       = backend_get_platform,
};

/* ── renderer backend target interface ───────────────────────────────────── */

static void *target_create(struct wpe_renderer_backend_egl_target *target, int host_fd) {
    vex_target_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->target  = target;
    t->host_fd = host_fd;
    t->width   = 1280;
    t->height  =  720;
    atomic_init(&t->references,1);
    t->window=(vex_egl_window){VEX_EGL_WINDOW_MAGIC,t->width,t->height,target_present_surface,t,target_retain,target_release};
    backend_trace_target("create", t->width, t->height);
    fprintf(stderr, "vex_wpe_backend: target_create (fd=%d)\n", host_fd);
    return t;
}

static void target_destroy(void *data) {
    vex_target_t *t = (vex_target_t *)data;
    if (t) {
        target_release(t);
    }
}

static void target_initialize(void *data, void *backend_data,
                              uint32_t width, uint32_t height) {
    vex_target_t *t = (vex_target_t *)data;
    (void)backend_data;
    if (!t) return;
    t->width  = width  > 0 ? width  : 1280;
    t->height = height > 0 ? height :  720;
    t->window.width=t->width;t->window.height=t->height;
    backend_trace_target("initialize", t->width, t->height);
    fprintf(stderr, "vex_wpe_backend: target_initialize %ux%u\n",
            t->width, t->height);
}

static void target_resize(void *data, uint32_t width, uint32_t height) {
    vex_target_t *t = (vex_target_t *)data;
    if (!t) return;
    t->width  = width;
    t->height = height;
    t->window.width=width;t->window.height=height;
    backend_trace_target("resize", width, height);
}

static void target_frame_will_render(void *data) {
    vex_target_t *t = (vex_target_t *)data;
    backend_trace_target("frame_will_render", t ? t->width : 0u, t ? t->height : 0u);
    /* nothing */
}

static void target_frame_rendered(void *data) {
    vex_target_t *t = (vex_target_t *)data;
    if (!t) return;
    backend_trace_target("frame_rendered", t->width, t->height);
    if(t->completed_sequence!=0u){
        wpe_renderer_backend_egl_target_dispatch_frame_complete(t->target);
        t->completed_sequence=0u;
    }else fprintf(stderr,"vex_wpe_backend: frame_rendered without completed Surface\n");
}

static void target_deinitialize(void *data) {
    vex_target_t *t = (vex_target_t *)data;
    backend_trace_target("deinitialize", t ? t->width : 0u, t ? t->height : 0u);
}

static EGLNativeWindowType target_get_native_window(void *data) {
    vex_target_t *t = (vex_target_t *)data;
    return t ? (EGLNativeWindowType)(uintptr_t)&t->window : (EGLNativeWindowType)0;
}

static const struct wpe_renderer_backend_egl_target_interface s_target_interface = {
    .create            = target_create,
    .destroy           = target_destroy,
    .initialize        = target_initialize,
    .get_native_window = target_get_native_window,
    .resize            = target_resize,
    .frame_will_render = target_frame_will_render,
    .frame_rendered    = target_frame_rendered,
    .deinitialize      = target_deinitialize,
};

/* ── offscreen target interface (for off-screen renders) ─────────────────── */

static void *offscreen_target_create(void) {
    /* Allocate a minimal context object */
    void *obj = calloc(1, sizeof(uintptr_t));
    backend_trace_offscreen("create");
    return obj;
}

static void offscreen_target_destroy(void *data) {
    free(data);
}

static void offscreen_target_initialize(void *data, void *backend_data) {
    (void)data;
    (void)backend_data;
    backend_trace_offscreen("initialize");
}

static EGLNativeWindowType offscreen_target_get_native_window(void *data) {
    (void)data;
    backend_trace_offscreen("get_native_window");
    return (EGLNativeWindowType)0;
}

static const struct wpe_renderer_backend_egl_offscreen_target_interface s_offscreen_interface = {
    .create            = offscreen_target_create,
    .destroy           = offscreen_target_destroy,
    .initialize        = offscreen_target_initialize,
    .get_native_window = offscreen_target_get_native_window,
};

/* ── renderer-host and view-backend interfaces ──────────────────────────── */

static void *renderer_host_create(void) {
    vex_renderer_host_t *host = calloc(1, sizeof(*host));
    if (!host)
        return NULL;
    for (uint32_t i = 0; i < VEX_MAX_RENDERER_CLIENTS; ++i)
        host->peer_fds[i] = -1;
    fprintf(stderr, "vex_wpe_backend: renderer_host_create\n");
    return host;
}

static void renderer_host_destroy(void *data) {
    vex_renderer_host_t *host = (vex_renderer_host_t *)data;
    if (!host)
        return;
    for (uint32_t i = 0; i < VEX_MAX_RENDERER_CLIENTS; ++i) {
        if (host->peer_fds[i] >= 0) {
            close(host->peer_fds[i]);
            host->peer_fds[i] = -1;
        }
    }
    free(host);
}

static int renderer_host_create_client(void *data) {
    vex_renderer_host_t *host = (vex_renderer_host_t *)data;
    int pair[2];
    if (!host || socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) < 0)
        return -1;
    for (uint32_t i = 0; i < VEX_MAX_RENDERER_CLIENTS; ++i) {
        if (host->peer_fds[i] < 0) {
            host->peer_fds[i] = pair[1];
            fprintf(stderr, "vex_wpe_backend: renderer client fd=%d\n", pair[0]);
            return pair[0];
        }
    }
    close(pair[0]);
    close(pair[1]);
    return -1;
}

static const struct wpe_renderer_host_interface s_renderer_host_interface = {
    .create = renderer_host_create,
    .destroy = renderer_host_destroy,
    .create_client = renderer_host_create_client,
};

static void *view_backend_create(void *data, struct wpe_view_backend *backend) {
    vex_view_backend_t *view = calloc(1, sizeof(*view));
    int pair[2];
    (void)data;
    if (!view)
        return NULL;
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) < 0) {
        free(view);
        return NULL;
    }
    view->renderer_host_fd = pair[0];
    view->renderer_peer_fd = pair[1];
    view->backend = backend;
    const char *endpoint=getenv("VEX_VIEWPORT_FD");
    view->viewport_fd=data?*(const int *)data:(endpoint?atoi(endpoint):-1);
    if(view->viewport_fd<0){close(pair[0]);close(pair[1]);free(view);return NULL;}
    /* Keep the endpoint out of helper execs; the UI remains its sole owner. */
    (void)fcntl(view->viewport_fd,F_SETFD,FD_CLOEXEC);
    fprintf(stderr, "vex_wpe_backend: view_backend_create fd=%d\n", pair[0]);
    return view;
}

static void view_backend_destroy(void *data) {
    vex_view_backend_t *view = (vex_view_backend_t *)data;
    if (!view)
        return;
    if(view->source)g_source_remove(view->source);
    if(view->viewport_source)g_source_remove(view->viewport_source);
    if(view->viewport_fd>=0)close(view->viewport_fd);
    if (view->renderer_host_fd >= 0)
        close(view->renderer_host_fd);
    if (view->renderer_peer_fd >= 0)
        close(view->renderer_peer_fd);
    free(view);
}

static void view_backend_initialize(void *data) {
    vex_view_backend_t *view = (vex_view_backend_t *)data;
    fprintf(stderr, "vex_wpe_backend: view_backend_initialize fd=%d\n",
            view ? view->renderer_host_fd : -1);
    if (view && view->backend) {
        view->source=g_unix_fd_add_full(G_PRIORITY_DEFAULT,view->renderer_peer_fd,
            G_IO_IN|G_IO_HUP|G_IO_ERR,view_receive_frame,view,NULL);
        view->viewport_source=g_unix_fd_add_full(G_PRIORITY_DEFAULT,view->viewport_fd,
            G_IO_IN|G_IO_HUP|G_IO_ERR,view_receive_native,view,NULL);
        wpe_view_backend_dispatch_set_size(view->backend, 1280u, 720u);
        fprintf(stderr, "vex_wpe_backend: view size 1280x720 dispatched\n");
        wpe_view_backend_add_activity_state(view->backend,
            wpe_view_activity_state_visible | wpe_view_activity_state_in_window);
        fprintf(stderr, "vex_wpe_backend: view activity=0x%x\n",
            wpe_view_backend_get_activity_state(view->backend));
    }
}

static int view_backend_get_renderer_host_fd(void *data) {
    vex_view_backend_t *view = (vex_view_backend_t *)data;
    /* WebKit takes ownership of this return value and closes it after the
     * IPC transfer. Keep our endpoint alive for subsequent WebProcesses and
     * for destruction; an exported descriptor is a separate local reference. */
    return view ? fcntl(view->renderer_host_fd,F_DUPFD_CLOEXEC,0) : -1;
}

static const struct wpe_view_backend_interface s_view_backend_interface = {
    .create = view_backend_create,
    .destroy = view_backend_destroy,
    .initialize = view_backend_initialize,
    .get_renderer_host_fd = view_backend_get_renderer_host_fd,
};

/* Browser-private factory. Each view consumes its own owned local endpoint;
 * no mutable environment variable decides routing for multiple tabs. */
struct wpe_view_backend *vex_wpe_view_backend_create_for_endpoint(int fd) {
    return wpe_view_backend_create_with_backend_interface(
        (struct wpe_view_backend_interface *)&s_view_backend_interface,&fd);
}

/* libwpe resolves every backend object through this loader interface.  The
 * previous backend only exported the legacy EGL bundle, so view creation
 * silently had no implementation to instantiate. */
static void *vex_wpe_load_object(const char *name) {
    if (!name)
        return NULL;
    if (strcmp(name, "_wpe_renderer_host_interface") == 0)
        return (void *)&s_renderer_host_interface;
    if (strcmp(name, "_wpe_renderer_backend_egl_interface") == 0)
        return (void *)&s_backend_interface;
    if (strcmp(name, "_wpe_renderer_backend_egl_target_interface") == 0)
        return (void *)&s_target_interface;
    if (strcmp(name, "_wpe_renderer_backend_egl_offscreen_target_interface") == 0)
        return (void *)&s_offscreen_interface;
    if (strcmp(name, "_wpe_view_backend_interface") == 0)
        return (void *)&s_view_backend_interface;
    fprintf(stderr, "vex_wpe_backend: unsupported interface %s\n", name);
    return NULL;
}

__attribute__((visibility("default")))
struct wpe_loader_interface _wpe_loader_interface = {
    .load_object = vex_wpe_load_object,
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
