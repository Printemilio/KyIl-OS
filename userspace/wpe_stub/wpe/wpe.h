#ifndef VEX_WPE_STUB_WPE_H
#define VEX_WPE_STUB_WPE_H

#include <stdbool.h>
#include <stdint.h>

struct wpe_view_backend;

struct wpe_renderer_host_interface {
    void *(*create)(void);
    void (*destroy)(void *);
    int (*create_client)(void *);
    void (*_wpe_reserved0)(void);
    void (*_wpe_reserved1)(void);
    void (*_wpe_reserved2)(void);
    void (*_wpe_reserved3)(void);
};

struct wpe_view_backend_interface {
    void *(*create)(void *, struct wpe_view_backend *);
    void (*destroy)(void *);
    void (*initialize)(void *);
    int (*get_renderer_host_fd)(void *);
    void (*_wpe_reserved0)(void);
    void (*_wpe_reserved1)(void);
    void (*_wpe_reserved2)(void);
    void (*_wpe_reserved3)(void);
};

struct wpe_loader_interface {
    void *(*load_object)(const char *);
    void (*_wpe_reserved0)(void);
    void (*_wpe_reserved1)(void);
    void (*_wpe_reserved2)(void);
    void (*_wpe_reserved3)(void);
};

bool wpe_loader_init(const char *impl_library_name);
struct wpe_view_backend *wpe_view_backend_create(void);
struct wpe_view_backend *wpe_view_backend_create_with_backend_interface(struct wpe_view_backend_interface *,void *);
void wpe_view_backend_destroy(struct wpe_view_backend *backend);
void wpe_view_backend_dispatch_frame_displayed(struct wpe_view_backend *backend);
void wpe_view_backend_dispatch_set_size(struct wpe_view_backend *backend,
                                        uint32_t width, uint32_t height);
enum wpe_view_activity_state {
    wpe_view_activity_state_visible = 1 << 0,
    wpe_view_activity_state_focused = 1 << 1,
    wpe_view_activity_state_in_window = 1 << 2
};
void wpe_view_backend_add_activity_state(struct wpe_view_backend *backend,
                                         uint32_t state);
uint32_t wpe_view_backend_get_activity_state(struct wpe_view_backend *backend);
void wpe_view_backend_remove_activity_state(struct wpe_view_backend *,uint32_t);
/* ABI declarations from libwpe input.h (1.14); implementation is real libwpe. */
struct wpe_input_keyboard_event {uint32_t time,key_code,hardware_key_code;bool pressed;uint32_t modifiers;};
enum wpe_input_pointer_event_type {wpe_input_pointer_event_type_null,wpe_input_pointer_event_type_motion,wpe_input_pointer_event_type_button};
struct wpe_input_pointer_event {enum wpe_input_pointer_event_type type;uint32_t time;int x,y;uint32_t button,state,modifiers;};
enum wpe_input_axis_event_type {wpe_input_axis_event_type_null,wpe_input_axis_event_type_motion,wpe_input_axis_event_type_motion_smooth,wpe_input_axis_event_type_mask_2d=1<<16};
struct wpe_input_axis_event {enum wpe_input_axis_event_type type;uint32_t time;int x,y;uint32_t axis;int32_t value;uint32_t modifiers;};
void wpe_view_backend_dispatch_keyboard_event(struct wpe_view_backend *,struct wpe_input_keyboard_event *);
void wpe_view_backend_dispatch_pointer_event(struct wpe_view_backend *,struct wpe_input_pointer_event *);
void wpe_view_backend_dispatch_axis_event(struct wpe_view_backend *,struct wpe_input_axis_event *);
uint32_t wpe_unicode_to_key_code(uint32_t);

#endif
