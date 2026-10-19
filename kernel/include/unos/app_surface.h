#ifndef UNOS_APP_SURFACE_H
#define UNOS_APP_SURFACE_H

#include <stdint.h>

#include <unos/compositor_surface.h>
#include <unos/vex_abi.h>

#define APP_SURFACE_CONTROL_NONE     0u
#define APP_SURFACE_CONTROL_CLOSE    1u
#define APP_SURFACE_CONTROL_MINIMIZE 2u
#define APP_SURFACE_CONTROL_MAXIMIZE 3u
#define APP_SURFACE_RESIZE_LEFT      0x01u
#define APP_SURFACE_RESIZE_RIGHT     0x02u
#define APP_SURFACE_RESIZE_TOP       0x04u
#define APP_SURFACE_RESIZE_BOTTOM    0x08u
#define APP_SURFACE_EVENT_QUEUE      32u

typedef struct app_surface {
    uint32_t window_id;
    uint32_t owner_pid;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t flags;
    uint32_t *pixels;
    compositor_surface_handle_t gpu_surface;
    compositor_surface_identity_t gpu_surface_identity;
    uint64_t last_present_serial;
    uint64_t event_counter; /* private kernel-owned notification reference */
    uint64_t x;
    uint64_t y;
    uint64_t w;
    uint64_t h;
    uint64_t restore_x;
    uint64_t restore_y;
    uint64_t restore_w;
    uint64_t restore_h;
    uint64_t minimize_restore_h;
    uint64_t z_order;
    uint8_t visible;
    uint8_t maximized;
    uint8_t minimized;
    uint8_t minimize_was_maximized;
    uint8_t configure_pending;
    uint8_t configure_inflight;
    uint8_t close_pending;
    uint8_t event_head;
    uint8_t event_tail;
    uint8_t event_count;
    uint32_t configure_width;
    uint32_t configure_height;
    vex_app_window_event_t event_queue[APP_SURFACE_EVENT_QUEUE];
    char title[64];
} app_surface_t;

int app_surface_open(const char *title, uint32_t width, uint32_t height, uint32_t flags, uint32_t *window_id);
app_surface_t *app_surface_current(void);
app_surface_t *app_surface_find(uint32_t window_id);
int app_surface_present(app_surface_t *surface, const vex_rect_t *damage);
void app_surface_close(uint32_t window_id);
void app_surface_reap_pid(uint32_t owner_pid);
void app_surface_poll_event(uint32_t window_id, vex_app_window_event_t *event);
int app_surface_accept_resize(uint32_t window_id, uint32_t width, uint32_t height);
void app_surface_render_all(void);
void app_surface_render_damage(const vex_rect_t *damage);
app_surface_t *app_surface_hit(uint64_t x, uint64_t y);
/* Kernel desktop damage/hit-testing; no userspace ABI. */
int app_surface_intersects_rect(const vex_rect_t *rect);
uint32_t app_surface_control_at(app_surface_t *surface, uint64_t x, uint64_t y);
uint8_t app_surface_resize_edges_at(app_surface_t *surface, uint64_t x, uint64_t y);
int app_surface_titlebar_hit(app_surface_t *surface, uint64_t x, uint64_t y);
void app_surface_set_bounds(uint32_t window_id, uint64_t x, uint64_t y, uint64_t w, uint64_t h);
void app_surface_request_close(uint32_t window_id);
void app_surface_request_close_all(void);
unsigned app_surface_request_close_owner(uint32_t owner);
void app_surface_toggle_minimized(uint32_t window_id);
void app_surface_toggle_maximized(uint32_t window_id);
void app_surface_send_mouse_click(uint32_t window_id, uint64_t x, uint64_t y);
void app_surface_send_mouse_event(uint32_t window_id, uint64_t x, uint64_t y, uint32_t flags);
int app_surface_send_mouse_to_focused(uint64_t x, uint64_t y, uint32_t flags);
void app_surface_focus(uint32_t window_id);
int app_surface_raise_owner(uint32_t owner_pid);
int app_surface_notify(uint32_t owner_pid,uint32_t window_id,uint32_t tag);
/* Coalesced NOTIFY to every open window, e.g. VEX_APP_NOTIFY_THEME. */
void app_surface_notify_all(uint32_t tag);
/* Name of the focused window without the "KyIl " prefix, or NULL. */
const char *app_surface_focused_name(void);
void app_surface_blur(void);
int app_surface_has_keyboard_focus(void);
int app_surface_has_raw_keyboard_focus(void);
int app_surface_send_key_to_focused(char key);
int app_surface_send_raw_key(uint32_t key, uint32_t hardware, uint32_t pressed, uint32_t modifiers);
void app_surface_send_raw_pointer(uint64_t x, uint64_t y, uint32_t buttons, int wheel);

#endif
