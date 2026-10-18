#include "vex_lvgl.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vex_runtime.h"
#include "vex_syscall.h"

static uint32_t draw_buffer_a[VEX_LVGL_MAX_WIDTH * VEX_LVGL_DRAW_ROWS];
static uint32_t draw_buffer_b[VEX_LVGL_MAX_WIDTH * VEX_LVGL_DRAW_ROWS];
static uint32_t native_tick_ms(void) {
    struct timespec ts={0};vex_clock_gettime_req_t req={1,&ts};
    vex_syscall1(VEX_SYS_CLOCK_GETTIME,(uintptr_t)&req);
    return (uint32_t)((uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u);
}

static vex_ui_layout_t resolve_layout(uint32_t width, uint32_t height) {
    vex_ui_layout_t layout;

    memset(&layout, 0, sizeof(layout));
    layout.width = width;
    layout.height = height;
    layout.short_height = height < 260u ? 1u : 0u;
    if (width < 560u) {
        layout.size_class = VEX_UI_SIZE_COMPACT;
        layout.page_padding = width < 380u ? 8 : 10;
        layout.gap = 7;
        layout.panel_padding = 9;
        layout.header_height = 54;
        layout.control_height = 36;
    } else if (width < 920u) {
        layout.size_class = VEX_UI_SIZE_MEDIUM;
        layout.page_padding = 16;
        layout.gap = 10;
        layout.panel_padding = 13;
        layout.header_height = 64;
        layout.control_height = 40;
    } else {
        layout.size_class = VEX_UI_SIZE_EXPANDED;
        layout.page_padding = 20;
        layout.gap = 12;
        layout.panel_padding = 16;
        layout.header_height = 70;
        layout.control_height = 42;
    }
    layout.show_header = height >= 190u ? 1u : 0u;
    layout.show_secondary =
        width >= 430u && height >= 280u ? 1u : 0u;
    return layout;
}

static void *surface_alloc(size_t size) {
    return (void *)(uintptr_t)vex_syscall1(VEX_SYS_HEAP_ALLOC, (uint64_t)size);
}

static void surface_free(void *ptr) {
    vex_heap_req_t req;

    if (ptr == 0) {
        return;
    }
    req.ptr = ptr;
    req.size = 0u;
    (void)vex_syscall1(VEX_SYS_HEAP_FREE, (uint64_t)(uintptr_t)&req);
}

static void display_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixel_map) {
    vex_lvgl_app_t *app = (vex_lvgl_app_t *)lv_display_get_user_data(display);

    if (app != 0 && app->pixels != 0 && area != 0 && pixel_map != 0 &&
        area->x1 >= 0 && area->y1 >= 0 && area->x2 >= area->x1 && area->y2 >= area->y1) {
        const uint32_t width = (uint32_t)(area->x2 - area->x1 + 1);
        const uint32_t height = (uint32_t)(area->y2 - area->y1 + 1);
        const uint32_t *source = (const uint32_t *)pixel_map;

        for (uint32_t y = 0u; y < height && (uint32_t)area->y1 + y < app->height; ++y) {
            uint32_t *destination = app->pixels +
                ((uint32_t)area->y1 + y) * app->width + (uint32_t)area->x1;
            memcpy(destination, source + (size_t)y * width, (size_t)width * sizeof(uint32_t));
        }

        if (app->pending_dirty_valid == 0u) {
            app->pending_dirty.x = area->x1;
            app->pending_dirty.y = area->y1;
            app->pending_dirty.w = (int32_t)width;
            app->pending_dirty.h = (int32_t)height;
            app->pending_dirty_valid = 1u;
        } else {
            const int32_t left = app->pending_dirty.x < area->x1
                ? app->pending_dirty.x : area->x1;
            const int32_t top = app->pending_dirty.y < area->y1
                ? app->pending_dirty.y : area->y1;
            const int32_t old_right = app->pending_dirty.x + app->pending_dirty.w;
            const int32_t old_bottom = app->pending_dirty.y + app->pending_dirty.h;
            const int32_t new_right = area->x2 + 1;
            const int32_t new_bottom = area->y2 + 1;
            const int32_t right = old_right > new_right ? old_right : new_right;
            const int32_t bottom = old_bottom > new_bottom ? old_bottom : new_bottom;

            app->pending_dirty.x = left;
            app->pending_dirty.y = top;
            app->pending_dirty.w = right - left;
            app->pending_dirty.h = bottom - top;
        }

        /* LVGL can flush a partial render in many horizontal bands.  Copy every
         * band above, but cross the userspace/kernel boundary only once for the
         * complete refresh cycle. */
        if (lv_display_flush_is_last(display)) {
            (void)vex_app_window_present(&app->window, app->pixels, app->width,
                &app->pending_dirty);
            app->pending_dirty_valid = 0u;
        }
    }
    lv_display_flush_ready(display);
}

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data) {
    vex_lvgl_app_t *app = (vex_lvgl_app_t *)lv_indev_get_user_data(indev);

    if (app == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->point.x = app->pointer_x;
    data->point.y = app->pointer_y;
    data->state = app->pointer_state;
}

static void keyboard_read(lv_indev_t *indev, lv_indev_data_t *data) {
    vex_lvgl_app_t *app = (vex_lvgl_app_t *)lv_indev_get_user_data(indev);

    if (app == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->key = app->key;
    data->state = app->key_state;
}

static uint32_t map_key(char key) {
    if (key == '\b' || key == 127) {
        return LV_KEY_BACKSPACE;
    }
    if (key == '\r' || key == '\n') {
        return LV_KEY_ENTER;
    }
    if (key == '\t') {
        return LV_KEY_NEXT;
    }
    return (uint8_t)key;
}

static int resize_surface(vex_lvgl_app_t *app, const vex_app_window_event_t *event) {
    uint32_t *new_pixels = 0;
    size_t new_capacity;

    if (event->w == 0u || event->h == 0u || event->w > VEX_LVGL_MAX_WIDTH ||
        event->w > UINT32_MAX || event->h > UINT32_MAX) {
        return -1;
    }
    new_capacity = (size_t)event->w * (size_t)event->h;
    if (new_capacity > SIZE_MAX / sizeof(uint32_t)) {
        return -1;
    }
    if (new_capacity > app->pixel_capacity) {
        new_pixels = (uint32_t *)surface_alloc(new_capacity * sizeof(uint32_t));
        if (new_pixels == 0) {
            return -1;
        }
    }
    if (vex_app_window_accept_configure(&app->window, event) != 0) {
        surface_free(new_pixels);
        return -1;
    }
    if (new_pixels != 0) {
        surface_free(app->pixels);
        app->pixels = new_pixels;
        app->pixel_capacity = new_capacity;
    }
    memset(app->pixels, 0, new_capacity * sizeof(uint32_t));
    app->width = app->window.width;
    app->height = app->window.height;
    app->pending_dirty_valid = 0u;
    lv_display_set_resolution(app->display, (int32_t)app->width, (int32_t)app->height);
    vex_lvgl_app_apply_layout(app);
    return 0;
}

int vex_lvgl_app_open(vex_lvgl_app_t *app, const char *title,
                      uint32_t width, uint32_t height, uint32_t flags) {
    size_t pixel_capacity;

    if (app == 0 || title == 0 || width == 0u || height == 0u || width > VEX_LVGL_MAX_WIDTH) {
        return -1;
    }
    memset(app, 0, sizeof(*app));
    pixel_capacity = (size_t)width * height;
    app->pixels = (uint32_t *)surface_alloc(pixel_capacity * sizeof(uint32_t));
    if (app->pixels == 0) {
        return -1;
    }
    memset(app->pixels, 0, pixel_capacity * sizeof(uint32_t));
    app->pixel_capacity = pixel_capacity;
    app->width = width;
    app->height = height;
    app->layout = resolve_layout(width, height);

    if (vex_app_window_open(&app->window, title, width, height, flags) != 0) {
        surface_free(app->pixels);
        app->pixels = 0;
        return -1;
    }

    lv_init();
    lv_tick_set_cb(native_tick_ms);
    app->display = lv_display_create((int32_t)width, (int32_t)height);
    if (app->display == 0) {
        vex_lvgl_app_close(app);
        return -1;
    }
    lv_display_set_user_data(app->display, app);
    lv_display_set_color_format(app->display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(app->display, draw_buffer_a, draw_buffer_b,
        sizeof(draw_buffer_a), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(app->display, display_flush);
    lv_timer_pause(lv_display_get_refr_timer(app->display));

    app->pointer = lv_indev_create();
    app->keyboard = lv_indev_create();
    app->keyboard_group = lv_group_create();
    if (app->pointer == 0 || app->keyboard == 0 || app->keyboard_group == 0) {
        vex_lvgl_app_close(app);
        return -1;
    }

    lv_indev_set_type(app->pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(app->pointer, app->display);
    lv_indev_set_user_data(app->pointer, app);
    lv_indev_set_read_cb(app->pointer, pointer_read);
    lv_indev_set_mode(app->pointer, LV_INDEV_MODE_EVENT);

    lv_indev_set_type(app->keyboard, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_display(app->keyboard, app->display);
    lv_indev_set_user_data(app->keyboard, app);
    lv_indev_set_read_cb(app->keyboard, keyboard_read);
    lv_indev_set_group(app->keyboard, app->keyboard_group);
    lv_indev_set_mode(app->keyboard, LV_INDEV_MODE_EVENT);
    return 0;
}

void vex_lvgl_app_close(vex_lvgl_app_t *app) {
    if (app == 0) {
        return;
    }
    if (app->display != 0) {
        lv_display_delete(app->display);
        app->display = 0;
    }
    if (app->window.id != 0u) {
        vex_app_window_close(&app->window);
    }
    surface_free(app->pixels);
    app->pixels = 0;
    app->pixel_capacity = 0u;
}

int vex_lvgl_app_handle_event(vex_lvgl_app_t *app, const vex_app_window_event_t *event) {
    if (app == 0 || event == 0 || app->display == 0) {
        return 0;
    }
    if (event->type == VEX_APP_EVENT_CONFIGURE) {
        if (resize_surface(app, event) != 0) {
            return 0;
        }
        return 1;
    }
    if (event->type == VEX_APP_EVENT_MOUSE_CLICK) {
        app->pointer_x = event->x > INT32_MAX ? INT32_MAX : (int32_t)event->x;
        app->pointer_y = event->y > INT32_MAX ? INT32_MAX : (int32_t)event->y;
        app->pointer_state = (event->flags & VEX_APP_MOUSE_LEFT_DOWN) != 0u
            ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        lv_indev_read(app->pointer);
        lv_refr_now(app->display);
        return 1;
    }
    if (event->type == VEX_APP_EVENT_KEY && event->key != 0) {
        app->key = map_key(event->key);
        app->key_state = LV_INDEV_STATE_PRESSED;
        lv_indev_read(app->keyboard);
        app->key_state = LV_INDEV_STATE_RELEASED;
        lv_indev_read(app->keyboard);
        lv_refr_now(app->display);
        return 1;
    }
    return 0;
}

void vex_lvgl_app_render(vex_lvgl_app_t *app) {
    if (app == 0 || app->display == 0) {
        return;
    }
    lv_obj_invalidate(lv_display_get_screen_active(app->display));
    lv_refr_now(app->display);
}

lv_obj_t *vex_lvgl_screen(vex_lvgl_app_t *app) {
    return app == 0 || app->display == 0 ? 0 : lv_display_get_screen_active(app->display);
}

void vex_lvgl_app_set_layout_handler(vex_lvgl_app_t *app,
                                     vex_lvgl_layout_cb_t callback,
                                     void *user_data) {
    if (app == 0) {
        return;
    }
    app->layout_cb = callback;
    app->layout_user_data = user_data;
}

void vex_lvgl_app_apply_layout(vex_lvgl_app_t *app) {
    lv_obj_t *screen;

    if (app == 0 || app->display == 0) {
        return;
    }
    app->layout = resolve_layout(app->width, app->height);
    screen = vex_lvgl_screen(app);
    if (app->layout_cb != 0) {
        app->layout_cb(app, &app->layout, app->layout_user_data);
    }
    if (screen != 0) {
        lv_obj_update_layout(screen);
    }
}

static void clean_container(lv_obj_t *obj) {
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
}

void vex_ui_prepare_screen(lv_obj_t *screen) {
    if (screen == 0) {
        return;
    }
    clean_container(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(VEX_UI_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, lv_color_hex(VEX_UI_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

void vex_ui_apply_page(lv_obj_t *screen, const vex_ui_layout_t *layout) {
    if (screen == 0 || layout == 0) {
        return;
    }
    lv_obj_set_style_pad_all(screen, layout->page_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(screen, layout->gap, LV_PART_MAIN);
    lv_obj_set_style_pad_column(screen, layout->gap, LV_PART_MAIN);
}

void vex_ui_apply_panel(lv_obj_t *panel, const vex_ui_layout_t *layout) {
    if (panel == 0 || layout == 0) {
        return;
    }
    lv_obj_set_style_pad_all(panel, layout->panel_padding, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, layout->gap, LV_PART_MAIN);
    lv_obj_set_style_pad_column(panel, layout->gap, LV_PART_MAIN);
    lv_obj_set_style_radius(panel,
        layout->size_class == VEX_UI_SIZE_COMPACT ? 7 : 10,
        LV_PART_MAIN);
}

void vex_ui_set_visible(lv_obj_t *obj, uint8_t visible) {
    if (obj == 0) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *vex_ui_panel(lv_obj_t *parent) {
    lv_obj_t *panel = lv_obj_create(parent);
    clean_container(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(VEX_UI_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 16, LV_PART_MAIN);
    return panel;
}

lv_obj_t *vex_ui_label(lv_obj_t *parent, const char *text,
                       const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text == 0 ? "" : text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font == 0 ? &lv_font_montserrat_16 : font, LV_PART_MAIN);
    return label;
}

lv_obj_t *vex_ui_button(lv_obj_t *parent, const char *text) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(VEX_UI_SURFACE_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x34343eu), LV_PART_MAIN | LV_STATE_HOVERED);
    lv_obj_set_style_bg_color(button, lv_color_hex(VEX_UI_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(button, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(button, 10, LV_PART_MAIN);
    {
        lv_obj_t *label = vex_ui_label(button, text, &lv_font_montserrat_16, VEX_UI_TEXT);
        lv_obj_center(label);
    }
    return button;
}
