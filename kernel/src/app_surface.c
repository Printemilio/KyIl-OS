#include <unos/app_surface.h>
#include <unos/console.h>
#include <unos/event_counter.h>
#include <unos/window_damage.h>
#include <unos/vex_lvgl_ui.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unos/debug_log.h>
#include <unos/gui.h>
#include <unos/scheduler.h>
#include <unos/keyboard.h>

#define APP_SURFACE_MAX 8u
#define APP_SURFACE_MARGIN 18ull
#define APP_SURFACE_TITLE_H 45ull
#define APP_SURFACE_MIN_W 320ull
#define APP_SURFACE_MIN_H 180ull
#define APP_SURFACE_CONTROL_RADIUS 7ull
#define APP_SURFACE_RESIZE_EDGE 8ull
#define APP_SURFACE_RESIZE_HANDLE 18ull
#define APP_SURFACE_MAX_DIMENSION 4096u
#define APP_SURFACE_MAX_PIXELS (8u * 1024u * 1024u)

static app_surface_t app_surfaces[APP_SURFACE_MAX];
unsigned app_surface_request_close_owner(uint32_t owner) {
    GUI_GUARD;unsigned count=0;
    for(unsigned i=0;i<APP_SURFACE_MAX;i++)if(app_surfaces[i].window_id && app_surfaces[i].owner_pid==owner){
        app_surface_request_close(app_surfaces[i].window_id);++count;
    }return count;
}
void app_surface_request_close_all(void) {
    GUI_GUARD;
    for(unsigned i=0;i<APP_SURFACE_MAX;i++)if(app_surfaces[i].window_id)
        app_surface_request_close(app_surfaces[i].window_id);
}
static uint32_t app_surface_focused_window_id;
static uint64_t app_surface_next_z = 1u;

static uint64_t app_surface_client_width(const app_surface_t *surface) {
    return surface != NULL && surface->w > 16u ? surface->w - 16u : 0u;
}

static uint64_t app_surface_client_height(const app_surface_t *surface) {
    return surface != NULL && surface->h > APP_SURFACE_TITLE_H + 12u
        ? surface->h - APP_SURFACE_TITLE_H - 12u : 0u;
}

static int app_surface_dimensions_valid(uint32_t width, uint32_t height, size_t *pixels_out) {
    size_t pixels;

    if (width == 0u || height == 0u ||
        width > APP_SURFACE_MAX_DIMENSION || height > APP_SURFACE_MAX_DIMENSION ||
        (size_t)width > SIZE_MAX / (size_t)height) {
        return 0;
    }
    pixels = (size_t)width * (size_t)height;
    if (pixels > APP_SURFACE_MAX_PIXELS || pixels > UINT32_MAX ||
        pixels > SIZE_MAX / sizeof(uint32_t)) {
        return 0;
    }
    if (pixels_out != NULL) {
        *pixels_out = pixels;
    }
    return 1;
}

static uint32_t app_surface_next_window_id(void) {
    static uint32_t next_id = 1u;
    return next_id++;
}

static void app_surface_copy_title(char dst[64], const char *src) {
    size_t i = 0u;

    if (src == NULL || src[0] == '\0') {
        src = "Vex App";
    }
    while (src[i] != '\0' && i + 1u < 64u) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static app_surface_t *app_surface_find_slot(uint32_t window_id, uint32_t owner_pid) {
    for (uint32_t i = 0u; i < APP_SURFACE_MAX; ++i) {
        if (app_surfaces[i].window_id == 0u) {
            continue;
        }
        if (window_id != 0u && app_surfaces[i].window_id == window_id) {
            return &app_surfaces[i];
        }
        if (window_id == 0u && owner_pid != 0u && app_surfaces[i].owner_pid == owner_pid) {
            return &app_surfaces[i];
        }
    }
    return NULL;
}

static int app_surface_event_is_move(const vex_app_window_event_t *event) {
    return event != NULL && event->type == VEX_APP_EVENT_MOUSE_CLICK &&
        (event->flags & VEX_APP_MOUSE_MOVE) != 0u;
}

static int app_surface_drop_oldest_move(app_surface_t *surface) {
    if (surface == NULL) {
        return -1;
    }
    for (uint8_t position = 0u; position < surface->event_count; ++position) {
        uint8_t index = (uint8_t)((surface->event_head + position) % APP_SURFACE_EVENT_QUEUE);

        if (!app_surface_event_is_move(&surface->event_queue[index])) {
            continue;
        }
        for (uint8_t shift = position; shift + 1u < surface->event_count; ++shift) {
            const uint8_t destination = (uint8_t)((surface->event_head + shift) % APP_SURFACE_EVENT_QUEUE);
            const uint8_t source = (uint8_t)((surface->event_head + shift + 1u) % APP_SURFACE_EVENT_QUEUE);
            memcpy(&surface->event_queue[destination], &surface->event_queue[source],
                   sizeof(surface->event_queue[destination]));
        }
        surface->event_tail = surface->event_tail == 0u
            ? (uint8_t)(APP_SURFACE_EVENT_QUEUE - 1u)
            : (uint8_t)(surface->event_tail - 1u);
        --surface->event_count;
        return 0;
    }
    return -1;
}

static int app_surface_queue_event(app_surface_t *surface, const vex_app_window_event_t *event) {
    uint8_t last;

    if (surface == NULL || event == NULL || surface->close_pending) {
        return -1;
    }

    if (app_surface_event_is_move(event) && surface->event_count != 0u) {
        last = surface->event_tail == 0u
            ? (uint8_t)(APP_SURFACE_EVENT_QUEUE - 1u)
            : (uint8_t)(surface->event_tail - 1u);
        if (app_surface_event_is_move(&surface->event_queue[last])) {
            memcpy(&surface->event_queue[last], event, sizeof(*event));
            if(surface->event_counter)(void)vex_event_counter_signal(surface->event_counter,1u);
            return 0;
        }
    }

    /* Motion events are disposable; button releases and keystrokes are not.
     * Evict only an obsolete motion sample when the ring is saturated. */
    if (surface->event_count >= APP_SURFACE_EVENT_QUEUE &&
        app_surface_drop_oldest_move(surface) != 0) {
    GUI_GUARD;
        return -1;
    }

    memcpy(&surface->event_queue[surface->event_tail], event, sizeof(*event));
    surface->event_tail = (uint8_t)((surface->event_tail + 1u) % APP_SURFACE_EVENT_QUEUE);
    ++surface->event_count;
    if(surface->event_counter)(void)vex_event_counter_signal(surface->event_counter,1u);
    return 0;
}

static int app_surface_pop_event(app_surface_t *surface, vex_app_window_event_t *event) {
    if (surface == NULL || event == NULL || surface->event_count == 0u) {
        return -1;
    }

    memcpy(event, &surface->event_queue[surface->event_head], sizeof(*event));
    surface->event_head = (uint8_t)((surface->event_head + 1u) % APP_SURFACE_EVENT_QUEUE);
    --surface->event_count;
    return 0;
}

static int app_surface_raw_input(const app_surface_t *surface) {
    return surface != NULL && (surface->flags & VEX_APP_WINDOW_FLAG_RAW_INPUT) != 0u;
}

/* RAW_INPUT windows: tell the toolkit when it gains or loses the keyboard,
 * so that keys held across a focus change are not left pressed. */
static void app_surface_send_focus(app_surface_t *surface, uint32_t gained) {
    vex_app_window_event_t event;

    if (!app_surface_raw_input(surface) || surface->close_pending) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = VEX_APP_EVENT_FOCUS;
    event.flags = gained;
    (void)app_surface_queue_event(surface, &event);
}

static void app_surface_layout_default(app_surface_t *surface) {
    const uint64_t screen_w = gui_width();
    const uint64_t screen_h = gui_height();
    const uint64_t index = surface->window_id % 4u;

    surface->w = surface->width + 16u;
    surface->h = surface->height + APP_SURFACE_TITLE_H + 12u;
    if (surface->w < APP_SURFACE_MIN_W) {
        surface->w = APP_SURFACE_MIN_W;
    }
    if (surface->h < APP_SURFACE_MIN_H) {
        surface->h = APP_SURFACE_MIN_H;
    }
    if (screen_w != 0u && surface->w > screen_w) {
        surface->w = screen_w;
    }
    if (screen_h > APP_SURFACE_TITLE_H && surface->h > screen_h - APP_SURFACE_TITLE_H) {
        surface->h = screen_h - APP_SURFACE_TITLE_H;
    }

    surface->x = APP_SURFACE_MARGIN + index * 28u;
    surface->y = 54u + APP_SURFACE_MARGIN + index * 22u;
    if (screen_w > surface->w && surface->x > screen_w - surface->w) {
        surface->x = screen_w - surface->w;
    }
    if (screen_h > surface->h && surface->y > screen_h - surface->h) {
        surface->y = screen_h - surface->h;
    }
    surface->restore_x = surface->x;
    surface->restore_y = surface->y;
    surface->restore_w = surface->w;
    surface->restore_h = surface->h;
}

int app_surface_open(const char *title, uint32_t width, uint32_t height, uint32_t flags, uint32_t *window_id) {
    GUI_GUARD;
    app_surface_t *surface = NULL;
    const uint32_t owner_pid = scheduler_current_pid();
    compositor_surface_desc_t desc;
    compositor_surface_mapping_t mapping;

    debug_log_puts("app_surface: open enter\n");
    if (!app_surface_dimensions_valid(width, height, NULL)) {
        debug_log_puts("app_surface: open bad dimensions\n");
        return -1;
    }

    surface = app_surface_find_slot(0u, owner_pid);
    if (surface == NULL) {
        for (uint32_t i = 0u; i < APP_SURFACE_MAX; ++i) {
            if (!app_surfaces[i].visible && app_surfaces[i].pixels == NULL) {
                surface = &app_surfaces[i];
                break;
            }
        }
    }
    if (surface == NULL) {
        debug_log_puts("app_surface: open no slot\n");
        return -1;
    }

    if (surface->gpu_surface != COMPOSITOR_SURFACE_INVALID) {
        (void)compositor_surface_unmap(surface->owner_pid, surface->gpu_surface,
                                       COMPOSITOR_SURFACE_MAP_WRITE);
        (void)compositor_surface_release(surface->owner_pid, surface->gpu_surface);
        compositor_surface_unpin(surface->gpu_surface_identity);
    }

    if(surface->event_counter)(void)vex_event_counter_release(surface->event_counter);
    memset(surface, 0, sizeof(*surface));
    memset(&desc, 0, sizeof(desc));
    desc.width = width;
    desc.height = height;
    desc.format = COMPOSITOR_SURFACE_FORMAT_XRGB8888;
    desc.alpha_mode = COMPOSITOR_SURFACE_ALPHA_OPAQUE;
    desc.usage = COMPOSITOR_SURFACE_USAGE_CPU_WRITE |
                 COMPOSITOR_SURFACE_USAGE_SAMPLED;
    if (compositor_surface_create(owner_pid, &desc, &surface->gpu_surface) !=
        COMPOSITOR_SURFACE_OK) {
        debug_log_puts("app_surface: GPU surface alloc failed\n");
        return -1;
    }
    if (compositor_surface_pin(owner_pid, surface->gpu_surface,
                               &surface->gpu_surface_identity) !=
        COMPOSITOR_SURFACE_OK) {
        (void)compositor_surface_release(owner_pid, surface->gpu_surface);
        memset(surface, 0, sizeof(*surface));
        debug_log_puts("app_surface: GPU surface pin failed\n");
        return -1;
    }
    memset(&mapping, 0, sizeof(mapping));
    if (compositor_surface_map(owner_pid, surface->gpu_surface,
                               COMPOSITOR_SURFACE_MAP_WRITE, &mapping) !=
        COMPOSITOR_SURFACE_OK) {
        (void)compositor_surface_release(owner_pid, surface->gpu_surface);
        compositor_surface_unpin(surface->gpu_surface_identity);
        memset(surface, 0, sizeof(*surface));
        debug_log_puts("app_surface: GPU surface map failed\n");
        return -1;
    }
    debug_log_puts("app_surface: GPU surface ready\n");
    surface->pixels = mapping.pixels;
    memset(surface->pixels, 0, (size_t)mapping.stride * height * sizeof(uint32_t));

    surface->window_id = app_surface_next_window_id();
    surface->owner_pid = owner_pid;
    surface->width = width;
    surface->height = height;
    surface->stride = mapping.stride;
    surface->flags = flags;
    surface->visible = 1u;
    if(vex_event_counter_create(1u,0u,&surface->event_counter)) {
        app_surface_close(surface->window_id);return -1;
    }
    surface->z_order = app_surface_next_z++;
    surface->configure_pending = 1u;
    app_surface_copy_title(surface->title, title);
    app_surface_layout_default(surface);
    app_surface_focused_window_id = surface->window_id;

    if (window_id != NULL) {
        *window_id = surface->window_id;
    }
    /* Creation changes geometry/z order independently of client content.
     * Establish native chrome here, once, rather than relying on the first
     * client presentation to redraw an entire window as a side effect. */
    render_active_app();
    gui_flush_scene_and_cursor();
    debug_log_puts("app_surface: open exit ok\n");
    return 0;
}

int app_surface_accept_resize(uint32_t window_id, uint32_t width, uint32_t height) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(window_id);
    compositor_surface_mapping_t mapping;

    if (surface == NULL || surface->owner_pid != scheduler_current_pid() ||
        !surface->configure_inflight ||
        width != surface->configure_width || height != surface->configure_height ||
        !app_surface_dimensions_valid(width, height, NULL)) {
        return -1;
    }
    if (width == surface->width && height == surface->height) {
        /* The initial configure repeats the size the surface already has.
         * Nothing to resize; leaving it in flight would block every later
         * configure and the app would never see a resize. */
        surface->configure_inflight = 0u;
        return 0;
    }
    if (compositor_surface_unmap(surface->owner_pid, surface->gpu_surface,
                                 COMPOSITOR_SURFACE_MAP_WRITE) !=
        COMPOSITOR_SURFACE_OK) {
        return -1;
    }
    surface->pixels = NULL;
    if (compositor_surface_resize(surface->owner_pid, surface->gpu_surface,
                                  width, height, 0u) != COMPOSITOR_SURFACE_OK) {
        if (compositor_surface_map(surface->owner_pid, surface->gpu_surface,
                                   COMPOSITOR_SURFACE_MAP_WRITE, &mapping) ==
            COMPOSITOR_SURFACE_OK) {
            surface->pixels = mapping.pixels;
            surface->stride = mapping.stride;
        }
        return -1;
    }
    memset(&mapping, 0, sizeof(mapping));
    if (compositor_surface_map(surface->owner_pid, surface->gpu_surface,
                               COMPOSITOR_SURFACE_MAP_WRITE, &mapping) !=
        COMPOSITOR_SURFACE_OK) {
        return -1;
    }
    surface->pixels = mapping.pixels;
    surface->width = width;
    surface->height = height;
    surface->stride = mapping.stride;
    surface->configure_inflight = 0u;
    memset(surface->pixels, 0, (size_t)mapping.stride * height * sizeof(uint32_t));
    return 0;
}

app_surface_t *app_surface_current(void) {
    GUI_GUARD;
    return app_surface_find_slot(0u, scheduler_current_pid());
}

app_surface_t *app_surface_find(uint32_t window_id) {
    GUI_GUARD;
    return app_surface_find_slot(window_id, 0u);
}

static void app_surface_focus_next(void) {
    app_surface_t *next=NULL;
    for(uint32_t i=0;i<APP_SURFACE_MAX;++i) {
        app_surface_t *s=&app_surfaces[i];
        if(!s->window_id || !s->visible || s->minimized || s->close_pending)continue;
        if(!next || s->z_order>next->z_order)next=s;
    }
    if(next)app_surface_focus(next->window_id);
    else keyboard_focus_release();
}

int app_surface_present(app_surface_t *surface, const vex_rect_t *damage) {
    GUI_GUARD;
    compositor_surface_present_info_t present;

    if (surface == NULL || surface->gpu_surface == COMPOSITOR_SURFACE_INVALID) {
        return -1;
    }
    memset(&present, 0, sizeof(present));
    present.damage = damage;
    present.damage_count = damage != NULL ? 1u : 0u;
    present.wait_serial = surface->last_present_serial;
    if (compositor_surface_present(surface->owner_pid, surface->gpu_surface,
                                   &present, &surface->last_present_serial) !=
        COMPOSITOR_SURFACE_OK) {
        return -1;
    }
    /* A content presentation does not invalidate native chrome. Restrict the
     * layer transaction to the client, including its occluding peers. Blending
     * unchanged rounded chrome repeatedly onto itself accumulates coverage. */
    const vex_rect_t content_damage = {(int32_t)(surface->x+8u),
        (int32_t)(surface->y+APP_SURFACE_TITLE_H+6u),
        (int32_t)(surface->w>16u?surface->w-16u:0u),
        (int32_t)(surface->h>APP_SURFACE_TITLE_H+12u?
            surface->h-APP_SURFACE_TITLE_H-12u:0u)};
    gui_compositor_restore_foreground(&content_damage);
    gui_flush_scene_and_cursor();
    (void)compositor_surface_complete(surface->gpu_surface_identity,
                                      surface->last_present_serial);
    return 0;
}

void app_surface_close(uint32_t window_id) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(window_id);

    if (surface == NULL || surface->owner_pid != scheduler_current_pid()) {
        return;
    }
    console_detach(surface->owner_pid,surface->window_id);
    if (surface->gpu_surface != COMPOSITOR_SURFACE_INVALID) {
        if (surface->pixels != NULL) {
            (void)compositor_surface_unmap(surface->owner_pid, surface->gpu_surface,
                                           COMPOSITOR_SURFACE_MAP_WRITE);
        }
        (void)compositor_surface_release(surface->owner_pid, surface->gpu_surface);
        compositor_surface_unpin(surface->gpu_surface_identity);
    }
    const int lost_focus=app_surface_focused_window_id == surface->window_id;
    if (lost_focus) {
        app_surface_focused_window_id = 0u;
    }
    if(surface->event_counter)(void)vex_event_counter_release(surface->event_counter);
    memset(surface, 0, sizeof(*surface));
    if(lost_focus)app_surface_focus_next();
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

void app_surface_reap_pid(uint32_t owner_pid) {
    GUI_GUARD;
    int lost_focus=0;
    if (owner_pid == 0u) {
        return;
    }
    for (uint32_t i = 0u; i < APP_SURFACE_MAX; ++i) {
        app_surface_t *surface = &app_surfaces[i];

        if (surface->window_id == 0u || surface->owner_pid != owner_pid) {
            continue;
        }
        console_detach(owner_pid,surface->window_id);
        if (surface->gpu_surface != COMPOSITOR_SURFACE_INVALID) {
            if (surface->pixels != NULL) {
                (void)compositor_surface_unmap(owner_pid, surface->gpu_surface,
                                               COMPOSITOR_SURFACE_MAP_WRITE);
            }
            (void)compositor_surface_release(owner_pid, surface->gpu_surface);
            compositor_surface_unpin(surface->gpu_surface_identity);
        }
        if (app_surface_focused_window_id == surface->window_id) {
            lost_focus=1;
            app_surface_focused_window_id = 0u;
        }
        if(surface->event_counter)(void)vex_event_counter_release(surface->event_counter);
        memset(surface, 0, sizeof(*surface));
    }
    if(lost_focus)app_surface_focus_next();
}

void app_surface_poll_event(uint32_t window_id, vex_app_window_event_t *event) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(window_id);

    if (event == NULL) {
        return;
    }
    memset(event, 0, sizeof(*event));
    if (surface == NULL) {
        event->type = VEX_APP_EVENT_CLOSE;
        return;
    }
    if (surface->close_pending) {
        event->type = VEX_APP_EVENT_CLOSE;
        if(surface->flags & VEX_APP_WINDOW_FLAG_CLOSE_CONFIRM)surface->close_pending=0u;
        return;
    }
    if (surface->configure_pending && !surface->configure_inflight) {
        const uint64_t width = app_surface_client_width(surface);
        const uint64_t height = app_surface_client_height(surface);

        if (width == 0u || height == 0u || width > UINT32_MAX || height > UINT32_MAX) {
            return;
        }
        surface->configure_pending = 0u;
        surface->configure_inflight = 1u;
        surface->configure_width = (uint32_t)width;
        surface->configure_height = (uint32_t)height;
        event->type = VEX_APP_EVENT_CONFIGURE;
        event->x = surface->x + 8u;
        event->y = surface->y + APP_SURFACE_TITLE_H + 6u;
        event->w = width;
        event->h = height;
        return;
    }
    (void)app_surface_pop_event(surface, event);
}

void app_surface_focus(uint32_t window_id) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(window_id);

    if (surface != NULL && surface->visible && !surface->close_pending) {
        if (app_surface_focused_window_id != window_id) {
            app_surface_send_focus(app_surface_find(app_surface_focused_window_id), 0u);
            app_surface_focused_window_id = window_id;
            surface->z_order = app_surface_next_z++;
            app_surface_send_focus(surface, 1u);
        }
        /* GUI dispatch owns the physical keys for this window. A shell
         * blocked in a legacy read must not consume its key shadow. */
        keyboard_focus_capture_pid(gui_service_tid());
    }
}

int app_surface_raise_owner(uint32_t owner_pid) {
    GUI_GUARD;
    app_surface_t *surface=app_surface_find_slot(0u,owner_pid);
    if(!surface || surface->close_pending)return -1;
    if(surface->minimized)app_surface_toggle_minimized(surface->window_id);
    app_surface_focus(surface->window_id);
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
    return 0;
}
int app_surface_notify(uint32_t owner,uint32_t id,uint32_t tag) {
    GUI_GUARD;
    app_surface_t *surface=app_surface_find(id);
    if(!surface || surface->owner_pid!=owner || surface->close_pending)return -1;
    for(unsigned i=0;i<surface->event_count;i++) {
        vex_app_window_event_t *old=&surface->event_queue[(surface->event_head+i)%APP_SURFACE_EVENT_QUEUE];
        if(old->type==VEX_APP_EVENT_NOTIFY && old->flags==tag)return 0;
    }
    vex_app_window_event_t event={.type=VEX_APP_EVENT_NOTIFY,.flags=tag};
    return app_surface_queue_event(surface,&event);
}

void app_surface_blur(void) {
    GUI_GUARD;
    app_surface_send_focus(app_surface_find(app_surface_focused_window_id), 0u);
    app_surface_focused_window_id = 0u;
}

app_surface_t *app_surface_hit(uint64_t x, uint64_t y) {
    GUI_GUARD;
    app_surface_t *best = NULL;

    for (uint32_t i = 0u; i < APP_SURFACE_MAX; ++i) {
        app_surface_t *surface = &app_surfaces[i];

        if (!surface->visible || surface->window_id == 0u) {
            continue;
        }
        if (x >= surface->x && x < surface->x + surface->w &&
            y >= surface->y && y < surface->y + surface->h) {
            if (best == NULL || surface->z_order > best->z_order) {
                best = surface;
            }
        }
    }
    return best;
}

static int app_surface_control_circle_hit(uint64_t x, uint64_t y, uint64_t window_y, uint64_t center_x) {
    const uint64_t center_y = window_y + APP_SURFACE_TITLE_H / 2u;
    const int64_t dx = (int64_t)x - (int64_t)center_x;
    const int64_t dy = (int64_t)y - (int64_t)center_y;
    const uint64_t hit_radius = APP_SURFACE_CONTROL_RADIUS + 5u;

    return dx * dx + dy * dy <= (int64_t)(hit_radius * hit_radius);
}

uint32_t app_surface_control_at(app_surface_t *surface, uint64_t x, uint64_t y) {
    GUI_GUARD;
    if (surface == NULL || !surface->visible || surface->w < 100u) {
        return APP_SURFACE_CONTROL_NONE;
    }
    if (app_surface_control_circle_hit(x, y, surface->y, surface->x + surface->w - 22u)) {
        return APP_SURFACE_CONTROL_CLOSE;
    }
    if (app_surface_control_circle_hit(x, y, surface->y, surface->x + surface->w - 48u)) {
        return APP_SURFACE_CONTROL_MINIMIZE;
    }
    if (app_surface_control_circle_hit(x, y, surface->y, surface->x + surface->w - 74u)) {
        return APP_SURFACE_CONTROL_MAXIMIZE;
    }
    return APP_SURFACE_CONTROL_NONE;
}

uint8_t app_surface_resize_edges_at(app_surface_t *surface, uint64_t x, uint64_t y) {
    GUI_GUARD;
    uint8_t edges = 0u;

    if (surface == NULL || !surface->visible || surface->maximized || surface->minimized ||
        (surface->flags & VEX_APP_WINDOW_FLAG_RESIZABLE) == 0u ||
        x < surface->x || y < surface->y ||
        x >= surface->x + surface->w || y >= surface->y + surface->h) {
        return 0u;
    }

    if (x < surface->x + APP_SURFACE_RESIZE_EDGE) {
        edges |= APP_SURFACE_RESIZE_LEFT;
    } else if (x >= surface->x + surface->w - APP_SURFACE_RESIZE_EDGE) {
        edges |= APP_SURFACE_RESIZE_RIGHT;
    }

    if (y < surface->y + APP_SURFACE_RESIZE_EDGE) {
        edges |= APP_SURFACE_RESIZE_TOP;
    } else if (y >= surface->y + surface->h - APP_SURFACE_RESIZE_EDGE) {
        edges |= APP_SURFACE_RESIZE_BOTTOM;
    }

    if (edges == 0u &&
        x >= surface->x + surface->w - APP_SURFACE_RESIZE_HANDLE &&
        y >= surface->y + surface->h - APP_SURFACE_RESIZE_HANDLE) {
        edges = APP_SURFACE_RESIZE_RIGHT | APP_SURFACE_RESIZE_BOTTOM;
    }
    return edges;
}

int app_surface_titlebar_hit(app_surface_t *surface, uint64_t x, uint64_t y) {
    GUI_GUARD;
    if (surface == NULL || !surface->visible ||
        y < surface->y || y >= surface->y + APP_SURFACE_TITLE_H ||
        x < surface->x || x >= surface->x + surface->w) {
        return 0;
    }
    if (x < surface->x + 60u || app_surface_control_at(surface, x, y) != APP_SURFACE_CONTROL_NONE) {
        return 0;
    }
    return 1;
}

void app_surface_set_bounds(uint32_t window_id, uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(window_id);
    uint64_t max_h;
    uint64_t max_x;
    uint64_t max_y;
    uint8_t size_changed;

    if (surface == NULL || surface->owner_pid == 0u) {
        return;
    }
    if (surface->minimized) {
        /* Moving collapsed chrome must not silently expand it back to the
         * normal minimum size.  Only its position changes in this state. */
        w = surface->w;
        h = surface->h;
    } else {
        if (w < APP_SURFACE_MIN_W) {
            w = APP_SURFACE_MIN_W;
        }
        if (h < APP_SURFACE_MIN_H) {
            h = APP_SURFACE_MIN_H;
        }
    }
    if (gui_width() != 0u && w > gui_width()) {
        w = gui_width();
    }
    max_h = gui_height() > 54u ? gui_height() - 54u : gui_height();
    if (max_h != 0u && h > max_h) {
        h = max_h;
    }
    max_x = gui_width() > w ? gui_width() - w : 0u;
    max_y = gui_height() > h ? gui_height() - h : 54u;
    if (max_y < 54u) {
        max_y = 54u;
    }
    if (x > max_x) {
        x = max_x;
    }
    if (y < 54u) {
        y = 54u;
    } else if (y > max_y) {
        y = max_y;
    }
    size_changed = surface->w != w || surface->h != h;
    surface->x = x;
    surface->y = y;
    surface->w = w;
    surface->h = h;
    /* Position changes are compositor-only.  Applications only need to
     * reflow when their available client area actually changes. */
    if (size_changed) {
        surface->configure_pending = 1u;
        if(surface->event_counter)(void)vex_event_counter_signal(surface->event_counter,1u);
    }
}

void app_surface_request_close(uint32_t window_id) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(window_id);

    if (surface == NULL) {
        return;
    }
    if(surface->flags & VEX_APP_WINDOW_FLAG_CLOSE_CONFIRM) {
        surface->close_pending=1u;
        if(surface->event_counter)(void)vex_event_counter_signal(surface->event_counter,1u);
        return;
    }
    if (app_surface_focused_window_id == window_id) {
        app_surface_focused_window_id = 0u;
    }
    surface->close_pending = 1u;
    if(surface->event_counter)(void)vex_event_counter_signal(surface->event_counter,1u);
    surface->visible = 0u;
    if(!app_surface_focused_window_id)app_surface_focus_next();
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

void app_surface_toggle_maximized(uint32_t window_id) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(window_id);
    uint64_t new_x;
    uint64_t new_y;
    uint64_t new_w;
    uint64_t new_h;

    if (surface == NULL || !surface->visible) {
        return;
    }
    if (surface->minimized) {
        surface->minimized = 0u;
        surface->h = surface->minimize_restore_h;
        surface->maximized = surface->minimize_was_maximized;
    }
    if (!surface->maximized) {
        surface->restore_x = surface->x;
        surface->restore_y = surface->y;
        surface->restore_w = surface->w;
        surface->restore_h = surface->h;
        new_x = 0u;
        new_y = 54u;
        new_w = gui_width();
        new_h = gui_height() > 54u ? gui_height() - 54u : gui_height();
        surface->maximized = 1u;
    } else {
        new_x = surface->restore_x;
        new_y = surface->restore_y;
        new_w = surface->restore_w;
        new_h = surface->restore_h;
        surface->maximized = 0u;
    }
    app_surface_set_bounds(window_id, new_x, new_y, new_w, new_h);
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

void app_surface_toggle_minimized(uint32_t window_id) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(window_id);
    uint64_t new_h;

    if (surface == NULL || !surface->visible) {
        return;
    }
    if (!surface->minimized) {
        surface->minimize_restore_h = surface->h;
        surface->minimize_was_maximized = surface->maximized;
        surface->minimized = 1u;
        new_h = APP_SURFACE_TITLE_H + 2u;
    } else {
        surface->minimized = 0u;
        surface->maximized = surface->minimize_was_maximized;
        new_h = surface->minimize_restore_h;
    }
    surface->h = new_h;
    /* The collapsed chrome is compositor state, not a zero-height client
     * resize.  Keep the app's backing surface intact for instant restore. */
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

void app_surface_send_mouse_click(uint32_t window_id, uint64_t x, uint64_t y) {
    GUI_GUARD;
    app_surface_send_mouse_event(window_id, x, y, VEX_APP_MOUSE_LEFT_DOWN | VEX_APP_MOUSE_LEFT_UP);
}

void app_surface_send_mouse_event(uint32_t window_id, uint64_t x, uint64_t y, uint32_t flags) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(window_id);
    uint64_t content_x;
    uint64_t content_y;

    if (surface == NULL || surface->close_pending) {
        return;
    }
    if ((flags & VEX_APP_MOUSE_LEFT_DOWN) != 0u) {
        app_surface_focus(window_id);
    }

    if (app_surface_raw_input(surface)) {
        return; /* app_surface_send_raw_pointer() carries the pointer */
    }
    content_x = surface->x + 8u;
    content_y = surface->y + APP_SURFACE_TITLE_H + 6u;
    {
        vex_app_window_event_t event;

        memset(&event, 0, sizeof(event));
        event.type = VEX_APP_EVENT_MOUSE_CLICK;
        event.flags = flags;
        event.x = x > content_x ? x - content_x : 0u;
        event.y = y > content_y ? y - content_y : 0u;
        if (surface->width != 0u && event.x >= surface->width) {
            event.x = surface->width - 1u;
        }
        if (surface->height != 0u && event.y >= surface->height) {
            event.y = surface->height - 1u;
        }
        (void)app_surface_queue_event(surface, &event);
    }
}

int app_surface_send_mouse_to_focused(uint64_t x, uint64_t y, uint32_t flags) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(app_surface_focused_window_id);

    if (surface == NULL || !surface->visible || surface->close_pending) {
        return 0;
    }
    app_surface_send_mouse_event(surface->window_id, x, y, flags);
    return 1;
}

int app_surface_has_keyboard_focus(void) {
    GUI_GUARD;
    const app_surface_t *surface = app_surface_find(app_surface_focused_window_id);
    return surface != NULL && surface->visible && !surface->minimized && !surface->close_pending;
}

int app_surface_has_raw_keyboard_focus(void) {
    GUI_GUARD;
    return app_surface_has_keyboard_focus() &&
        app_surface_raw_input(app_surface_find(app_surface_focused_window_id));
}

int app_surface_send_key_to_focused(char key) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(app_surface_focused_window_id);

    if (!app_surface_has_keyboard_focus() || key == 0) {
        return 0;
    }
    if (app_surface_raw_input(surface)) {
        return 1; /* app_surface_send_raw_key() delivered the physical key */
    }

    {
        vex_app_window_event_t event;

        memset(&event, 0, sizeof(event));
        event.type = VEX_APP_EVENT_KEY;
        event.key = key;
        return app_surface_queue_event(surface, &event) == 0 ? 1 : 0;
    }
}

int app_surface_send_raw_key(uint32_t key, uint32_t hardware, uint32_t pressed, uint32_t modifiers) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(app_surface_focused_window_id);
    vex_app_window_event_t event;

    if (!app_surface_has_keyboard_focus() || !app_surface_raw_input(surface)) {
        return 0;
    }
    memset(&event, 0, sizeof(event));
    event.type = VEX_APP_EVENT_KEY_RAW;
    event.x = key;
    event.y = hardware;
    event.flags = (pressed ? VEX_APP_KEY_RAW_PRESSED : 0u) | ((modifiers & 0xffu) << 8);
    (void)app_surface_queue_event(surface, &event);
    return 1;
}

void app_surface_send_raw_pointer(uint64_t x, uint64_t y, uint32_t buttons, int wheel) {
    GUI_GUARD;
    app_surface_t *surface = app_surface_find(app_surface_focused_window_id);
    vex_app_window_event_t event;

    if (!app_surface_raw_input(surface) || !surface->visible || surface->minimized ||
        surface->close_pending) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = VEX_APP_EVENT_POINTER;
    event.flags = buttons & (VEX_APP_POINTER_LEFT | VEX_APP_POINTER_RIGHT | VEX_APP_POINTER_MIDDLE);
    event.x = (uint64_t)((int64_t)x - (int64_t)(surface->x + 8u));
    event.y = (uint64_t)((int64_t)y - (int64_t)(surface->y + APP_SURFACE_TITLE_H + 6u));
    event.w = (uint64_t)(int64_t)wheel;
    /* Motion with unchanged buttons replaces the previous sample. */
    if (wheel == 0 && surface->event_count != 0u) {
        const uint8_t last = surface->event_tail == 0u
            ? (uint8_t)(APP_SURFACE_EVENT_QUEUE - 1u) : (uint8_t)(surface->event_tail - 1u);
        vex_app_window_event_t *previous = &surface->event_queue[last];

        if (previous->type == VEX_APP_EVENT_POINTER && previous->flags == event.flags &&
            previous->w == 0u) {
            *previous = event;
            if(surface->event_counter)(void)vex_event_counter_signal(surface->event_counter,1u);
            return;
        }
    }
    (void)app_surface_queue_event(surface, &event);
}

static void app_surface_render_selected(uint32_t selected, const vex_rect_t *clip) {
    uint64_t last_z = 0u;

    for (;;) {
        app_surface_t *surface = NULL;
        uint64_t content_x;
        uint64_t content_y;
        uint64_t content_w;
        uint64_t content_h;
        const char *chrome_title;

        for (uint32_t i = 0u; i < APP_SURFACE_MAX; ++i) {
            app_surface_t *candidate = &app_surfaces[i];

            if (!(selected & (1u << i)) || !candidate->visible || candidate->pixels == NULL || candidate->z_order <= last_z) {
                continue;
            }
            if (surface == NULL || candidate->z_order < surface->z_order) {
                surface = candidate;
            }
        }
        if (surface == NULL) {
            break;
        }
        last_z = surface->z_order;
        chrome_title = surface->title;
        if (strcmp(surface->title, "Vex Paint") == 0 ||
            strcmp(surface->title, "GFM") == 0) {
            chrome_title = "";
        }

        if (vex_lvgl_ui_ready() && surface->x <= UINT32_MAX && surface->y <= UINT32_MAX &&
            surface->w <= UINT32_MAX && surface->h <= UINT32_MAX) {
            vex_lvgl_ui_draw_window_frame_id(surface->window_id,(uint32_t)surface->x, (uint32_t)surface->y,
                                          (uint32_t)surface->w, (uint32_t)surface->h,
                                          chrome_title);
        } else {
            gui_fill_rounded_rect(surface->x, surface->y, surface->w, surface->h, 10u,
                                  gui_color_rgb(20u, 20u, 20u));
            gui_fill_rect(surface->x + 1u, surface->y + APP_SURFACE_TITLE_H,
                          surface->w > 2u ? surface->w - 2u : surface->w, 1u,
                          gui_color_rgb(72u, 49u, 86u));
            gui_font_text_clipped(surface->x + 23u,
                                  surface->y + (APP_SURFACE_TITLE_H - gui_font_cell_height(16u)) / 2u,
                                  chrome_title, gui_color_rgb(255u, 255u, 255u), 16u,
                                  surface->w > 130u ? surface->w - 130u : 8u);
        }
        if (!vex_lvgl_ui_ready() && surface->w >= 100u) {
            const uint64_t cy = surface->y + APP_SURFACE_TITLE_H / 2u;
            const uint32_t close_col = gui_color_rgb(255u, 69u, 72u);
            const uint32_t min_col = surface->minimized
                ? gui_color_rgb(122u, 116u, 64u)
                : gui_color_rgb(244u, 205u, 50u);
            const uint32_t max_col = surface->maximized
                ? gui_color_rgb(58u, 128u, 76u)
                : gui_color_rgb(40u, 213u, 94u);

            gui_fill_circle(surface->x + surface->w - 74u, cy,
                            APP_SURFACE_CONTROL_RADIUS, min_col);
            gui_fill_circle(surface->x + surface->w - 48u, cy,
                            APP_SURFACE_CONTROL_RADIUS, max_col);
            gui_fill_circle(surface->x + surface->w - 22u, cy,
                            APP_SURFACE_CONTROL_RADIUS, close_col);
        }

        content_x = surface->x + 8u;
        content_y = surface->y + APP_SURFACE_TITLE_H + 6u;
        content_w = surface->w > 16u ? surface->w - 16u : surface->w;
        content_h = surface->h > APP_SURFACE_TITLE_H + 12u ? surface->h - APP_SURFACE_TITLE_H - 12u : 0u;
        if (!surface->minimized && content_w != 0u && content_h != 0u) {
            if (gui_width() <= UINT32_MAX &&
                gui_height() <= UINT32_MAX && content_x <= INT32_MAX &&
                content_y <= INT32_MAX && content_w <= INT32_MAX &&
                content_h <= INT32_MAX &&
                surface->gpu_surface != COMPOSITOR_SURFACE_INVALID) {
                compositor_target_t target;
                compositor_layer_t layer;
                vex_rect_t damage;

                memset(&target, 0, sizeof(target));
                memset(&layer, 0, sizeof(layer));
                layer.source.x = 0;
                layer.source.y = 0;
                layer.source.w = (int32_t)surface->width;
                layer.source.h = (int32_t)surface->height;
                layer.destination.x = (int32_t)content_x;
                layer.destination.y = (int32_t)content_y;
                layer.destination.w = (int32_t)content_w;
                layer.destination.h = (int32_t)content_h;
                layer.opacity = 255u;
                layer.filter = COMPOSITOR_SURFACE_FILTER_NEAREST;
                layer.transform = COMPOSITOR_SURFACE_TRANSFORM_IDENTITY;
                if (gui_compositor_get_target(&target) == 0 &&
                    target.pixels != NULL) {
                    if(clip)layer.clip=*clip;
                    if(compositor_surface_compose(surface->owner_pid,
                        surface->gpu_surface, &target,
                                               &layer, &damage) ==
                    COMPOSITOR_SURFACE_OK) {
                        uint32_t consumed_count = 0u;

                        (void)compositor_surface_consume_damage(
                            surface->owner_pid, surface->gpu_surface,
                            NULL, 0u, &consumed_count);
                        gui_compositor_damage(&damage);
                    }
                }
            } else {
                gui_blit_argb_scaled(content_x, content_y, content_w, content_h,
                                     surface->pixels, surface->width, surface->height,
                                     surface->stride);
            }
        }
    }
}

void app_surface_render_all(void) {
    GUI_GUARD;
    app_surface_render_selected((1u<<APP_SURFACE_MAX)-1u,NULL);
}
void app_surface_render_damage(const vex_rect_t *damage) {
    GUI_GUARD;
    vex_rect_t bounds[APP_SURFACE_MAX]={0};
    if(!damage)return;
    for(unsigned i=0;i<APP_SURFACE_MAX;i++) {
        app_surface_t *s=&app_surfaces[i];
        if(s->visible && s->pixels)
            bounds[i]=(vex_rect_t){(int32_t)s->x,(int32_t)s->y,(int32_t)s->w,(int32_t)s->h};
    }
    app_surface_render_selected(window_damage_closure(bounds,APP_SURFACE_MAX,*damage),damage);
}

int app_surface_intersects_rect(const vex_rect_t *rect) {
    GUI_GUARD;
    if(!rect)return 0;
    for(unsigned i=0;i<APP_SURFACE_MAX;++i) {
        const app_surface_t *s=&app_surfaces[i];
        if(s->visible && s->pixels && window_damage_intersects(*rect,
            (vex_rect_t){(int32_t)s->x,(int32_t)s->y,(int32_t)s->w,(int32_t)s->h}))return 1;
    }
    return 0;
}
