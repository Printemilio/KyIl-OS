#include <unos/vex_lvgl_ui.h>

#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

#include "vex_icon_hex.h"

#define CONTEXT_WIDTH 250
#define CONTEXT_HEADER_HEIGHT 8
#define CONTEXT_ROW_HEIGHT 38
#define SWITCHER_WIDTH 280
#define SPOTLIGHT_BAR_HEIGHT 50
#define SPOTLIGHT_PANEL_GAP 0
#define SPOTLIGHT_PANEL_PAD 14
#define SPOTLIGHT_ROW_HEIGHT 46

typedef struct overlay_row {
    lv_obj_t *root;
    lv_obj_t *marker;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *meta;
} overlay_row_t;

typedef struct overlay_renderer {
    uint8_t initialized;

    lv_obj_t *context;
    lv_obj_t *context_title;
    lv_obj_t *context_meta;
    overlay_row_t context_rows[VEX_LVGL_UI_MAX_ITEMS];

    lv_obj_t *switcher;
    lv_obj_t *switcher_title;
    lv_obj_t *switcher_hint;
    overlay_row_t switcher_rows[VEX_LVGL_UI_MAX_ITEMS];

    lv_obj_t *elevation;
    lv_obj_t *elevation_brand;
    lv_obj_t *elevation_meta;
    lv_obj_t *elevation_title;
    lv_obj_t *elevation_process;
    lv_obj_t *elevation_approve;
    lv_obj_t *elevation_deny;

    lv_obj_t *spotlight;
    lv_obj_t *spotlight_bar;
    lv_obj_t *spotlight_query;
    lv_obj_t *spotlight_brand;
    lv_obj_t *spotlight_hint;
    lv_obj_t *spotlight_panel;
    lv_obj_t *spotlight_confirmation;
    overlay_row_t spotlight_rows[VEX_LVGL_UI_MAX_ITEMS];
} overlay_renderer_t;

static overlay_renderer_t renderer;
static uint8_t overlay_hex_bgra[VEX_ICON_HEX_W * VEX_ICON_HEX_H * 4u];
static lv_image_dsc_t overlay_hex_dsc;

static void init_hex_image(void) {
    for (size_t i = 0u; i < VEX_ICON_HEX_W * VEX_ICON_HEX_H; ++i) {
        overlay_hex_bgra[i * 4u + 0u] = vex_icon_hex[i * 4u + 2u];
        overlay_hex_bgra[i * 4u + 1u] = vex_icon_hex[i * 4u + 1u];
        overlay_hex_bgra[i * 4u + 2u] = vex_icon_hex[i * 4u + 0u];
        overlay_hex_bgra[i * 4u + 3u] = vex_icon_hex[i * 4u + 3u];
    }
    overlay_hex_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    overlay_hex_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    overlay_hex_dsc.header.flags = 0u;
    overlay_hex_dsc.header.w = VEX_ICON_HEX_W;
    overlay_hex_dsc.header.h = VEX_ICON_HEX_H;
    overlay_hex_dsc.header.stride = VEX_ICON_HEX_W * 4u;
    overlay_hex_dsc.header.reserved_2 = 0u;
    overlay_hex_dsc.data_size = sizeof(overlay_hex_bgra);
    overlay_hex_dsc.data = overlay_hex_bgra;
    overlay_hex_dsc.reserved = NULL;
    overlay_hex_dsc.reserved_2 = NULL;
}

static void make_passive(lv_obj_t *obj) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                      LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
}

static lv_obj_t *clean_object(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    make_passive(obj);
    return obj;
}

static lv_obj_t *label_create(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text == NULL ? "" : text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    make_passive(label);
    return label;
}

static void style_panel(lv_obj_t *obj, uint32_t color, int32_t radius) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
}

static void style_marker(lv_obj_t *obj, uint32_t color, int32_t size) {
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
}

static void row_create(overlay_row_t *row, lv_obj_t *parent) {
    row->root = clean_object(parent);
    row->marker = clean_object(row->root);
    row->title = label_create(row->root, "", &lv_font_montserrat_16, 0xd4ceecu);
    row->subtitle = label_create(row->root, "", &lv_font_montserrat_12, 0x9682b9u);
    row->meta = label_create(row->root, "", &lv_font_montserrat_12, 0x6c648au);
    lv_obj_add_flag(row->root, LV_OBJ_FLAG_HIDDEN);
}

static void build_context(lv_obj_t *screen) {
    renderer.context = clean_object(screen);
    style_panel(renderer.context, 0x141414u, 7);
    lv_obj_add_flag(renderer.context, LV_OBJ_FLAG_HIDDEN);

    renderer.context_title = label_create(renderer.context, "", &lv_font_montserrat_12, 0xffffffu);
    renderer.context_meta = label_create(renderer.context, "", &lv_font_montserrat_12, 0xffffffu);
    lv_obj_add_flag(renderer.context_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(renderer.context_meta, LV_OBJ_FLAG_HIDDEN);

    for (uint32_t i = 0u; i < VEX_LVGL_UI_MAX_ITEMS; ++i) {
        overlay_row_t *row = &renderer.context_rows[i];
        row_create(row, renderer.context);
        lv_obj_set_pos(row->root, 8, CONTEXT_HEADER_HEIGHT + (int32_t)i * CONTEXT_ROW_HEIGHT);
        lv_obj_set_size(row->root, CONTEXT_WIDTH - 16, CONTEXT_ROW_HEIGHT);
        style_panel(row->root, 0x141414u, 0);
        lv_obj_set_pos(row->marker, 8, 9);
        lv_obj_set_size(row->marker, 20, 20);
        lv_obj_set_style_radius(row->marker, i == 2u ? LV_RADIUS_CIRCLE : 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row->marker, lv_color_hex(
            i == 0u ? 0x292929u :
            (i == 1u ? 0xc99914u :
            (i == 2u ? 0x141414u : 0xd9d9d9u))), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->marker, LV_OPA_COVER, LV_PART_MAIN);
        if (i == 2u) {
            lv_obj_set_style_border_width(row->marker, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(row->marker, lv_color_hex(0xb64cffu), LV_PART_MAIN);
        }
        lv_obj_set_pos(row->title, 38, 10);
        lv_obj_set_size(row->title, CONTEXT_WIDTH - 58, 20);
        lv_obj_set_style_text_font(row->title, &lv_font_montserrat_12, LV_PART_MAIN);
        if (i == 1u || i == 3u) {
            lv_obj_set_style_border_width(row->root, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(row->root, lv_color_hex(0x684574u), LV_PART_MAIN);
            lv_obj_set_style_border_side(row->root, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
        }
        lv_obj_add_flag(row->subtitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(row->meta, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_switcher(lv_obj_t *screen) {
    renderer.switcher = clean_object(screen);
    style_panel(renderer.switcher, 0x161424u, 16);
    lv_obj_set_style_border_width(renderer.switcher, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(renderer.switcher, lv_color_hex(0x343050u), LV_PART_MAIN);
    lv_obj_add_flag(renderer.switcher, LV_OBJ_FLAG_HIDDEN);

    renderer.switcher_title = label_create(renderer.switcher, "Switcher", &lv_font_montserrat_16, 0xd4ceecu);
    lv_obj_set_pos(renderer.switcher_title, 18, 13);
    lv_obj_set_size(renderer.switcher_title, 150, 22);
    renderer.switcher_hint = label_create(renderer.switcher, "Alt+Tab", &lv_font_montserrat_12, 0x6c648au);
    lv_obj_align(renderer.switcher_hint, LV_ALIGN_TOP_RIGHT, -18, 15);

    for (uint32_t i = 0u; i < VEX_LVGL_UI_MAX_ITEMS; ++i) {
        overlay_row_t *row = &renderer.switcher_rows[i];
        row_create(row, renderer.switcher);
        lv_obj_set_pos(row->root, 10, 48 + (int32_t)i * 34);
        lv_obj_set_size(row->root, SWITCHER_WIDTH - 20, 28);
        lv_obj_set_pos(row->marker, 9, 8);
        style_marker(row->marker, 0x342e4eu, 12);
        lv_obj_set_pos(row->title, 32, 4);
        lv_obj_set_size(row->title, SWITCHER_WIDTH - 52, 20);
        lv_obj_add_flag(row->subtitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(row->meta, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_elevation(lv_obj_t *screen) {
    renderer.elevation = clean_object(screen);
    style_panel(renderer.elevation, 0x492658u, 10);
    lv_obj_add_flag(renderer.elevation, LV_OBJ_FLAG_HIDDEN);

    renderer.elevation_brand = label_create(renderer.elevation, "VEX - SYSTEM", &lv_font_montserrat_12, 0xcd63f4u);
    lv_obj_set_pos(renderer.elevation_brand, 54, 15);
    lv_obj_set_size(renderer.elevation_brand, 160, 18);
    renderer.elevation_meta = label_create(renderer.elevation, "a l'instant", &lv_font_montserrat_12, 0x968aa0u);
    lv_obj_align(renderer.elevation_meta, LV_ALIGN_TOP_RIGHT, -10, 16);
    renderer.elevation_title = label_create(renderer.elevation, "Administrator Access", &lv_font_montserrat_16, 0xffffffu);
    lv_obj_set_pos(renderer.elevation_title, 21, 53);
    lv_obj_set_size(renderer.elevation_title, 360, 20);
    renderer.elevation_process = label_create(renderer.elevation, "Process", &lv_font_montserrat_12, 0xccccccu);
    lv_obj_set_pos(renderer.elevation_process, 21, 76);
    lv_obj_set_size(renderer.elevation_process, 360, 18);

    renderer.elevation_approve = clean_object(renderer.elevation);
    renderer.elevation_deny = clean_object(renderer.elevation);
    (void)label_create(renderer.elevation_approve, "Approve", &lv_font_montserrat_16, 0x7b45e8u);
    (void)label_create(renderer.elevation_deny, "Refuse", &lv_font_montserrat_16, 0x8c8c8cu);
    lv_obj_center(lv_obj_get_child(renderer.elevation_approve, 0));
    lv_obj_center(lv_obj_get_child(renderer.elevation_deny, 0));
}

static void build_spotlight(lv_obj_t *screen) {
    renderer.spotlight = clean_object(screen);
    lv_obj_add_flag(renderer.spotlight, LV_OBJ_FLAG_HIDDEN);

    renderer.spotlight_bar = clean_object(renderer.spotlight);
    style_panel(renderer.spotlight_bar, 0x2c264fu, 10);
    renderer.spotlight_query = label_create(renderer.spotlight_bar, "", &lv_font_montserrat_16, 0xffffffu);
    lv_obj_set_pos(renderer.spotlight_query, 19, 15);
    renderer.spotlight_brand = lv_image_create(renderer.spotlight_bar);
    lv_image_set_src(renderer.spotlight_brand, &overlay_hex_dsc);
    lv_obj_set_size(renderer.spotlight_brand, VEX_ICON_HEX_W, VEX_ICON_HEX_H);
    lv_obj_align(renderer.spotlight_brand, LV_ALIGN_RIGHT_MID, -19, 0);
    make_passive(renderer.spotlight_brand);

    renderer.spotlight_hint = label_create(renderer.spotlight, "Ctrl + Space to talk", &lv_font_montserrat_12, 0x7c7187u);
    renderer.spotlight_panel = clean_object(renderer.spotlight);
    style_panel(renderer.spotlight_panel, 0x20163au, 10);
    renderer.spotlight_confirmation = label_create(renderer.spotlight_panel,
        "Enter confirme l'action, Esc annule", &lv_font_montserrat_12, 0xc8701cu);
    lv_obj_add_flag(renderer.spotlight_confirmation, LV_OBJ_FLAG_HIDDEN);

    for (uint32_t i = 0u; i < VEX_LVGL_UI_MAX_ITEMS; ++i) {
        overlay_row_t *row = &renderer.spotlight_rows[i];
        row_create(row, renderer.spotlight_panel);
        lv_obj_set_pos(row->root, SPOTLIGHT_PANEL_PAD,
            SPOTLIGHT_PANEL_PAD + (int32_t)i * SPOTLIGHT_ROW_HEIGHT);
        lv_obj_set_height(row->root, SPOTLIGHT_ROW_HEIGHT - 6);
        lv_obj_set_pos(row->marker, 12, 8);
        lv_obj_set_size(row->marker, 24, 24);
        lv_obj_set_style_radius(row->marker, 4, LV_PART_MAIN);
        lv_obj_set_pos(row->title, 58, 4);
        lv_obj_set_pos(row->subtitle, 58, 23);
        lv_obj_set_pos(row->meta, 0, 10);
        lv_obj_set_size(row->meta, 86, 20);
        lv_obj_set_style_text_align(row->meta, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

static int ensure_renderer(void) {
    lv_obj_t *screen;

    if (renderer.initialized != 0u) {
        return 0;
    }
    if (!vex_lvgl_ui_ready()) {
        return -1;
    }
    screen = lv_screen_active();
    if (screen == NULL) {
        return -1;
    }

    init_hex_image();
    build_context(screen);
    build_switcher(screen);
    build_elevation(screen);
    build_spotlight(screen);
    renderer.initialized = 1u;
    return 0;
}

static void refresh(lv_obj_t *obj) {
    lv_display_t *display = lv_display_get_default();
    if (obj == NULL || display == NULL) {
        return;
    }
    vex_lvgl_ui_paint_object(obj);
}

void vex_lvgl_overlays_begin_scene(void) {
    if (renderer.initialized == 0u) {
        return;
    }
    lv_obj_add_flag(renderer.context, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(renderer.switcher, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(renderer.elevation, LV_OBJ_FLAG_HIDDEN);
}

void vex_lvgl_ui_draw_context_menu(uint32_t x, uint32_t y,
                                   const char *const *labels,
                                   const char *const *hints, uint32_t count) {
    if (ensure_renderer() != 0) {
        return;
    }
    if (count > VEX_LVGL_UI_MAX_ITEMS) {
        count = VEX_LVGL_UI_MAX_ITEMS;
    }

    lv_obj_set_pos(renderer.context, (int32_t)x, (int32_t)y);
    lv_obj_set_size(renderer.context, CONTEXT_WIDTH,
        CONTEXT_HEADER_HEIGHT + (int32_t)count * CONTEXT_ROW_HEIGHT + 8);
    lv_obj_remove_flag(renderer.context, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(renderer.context);

    for (uint32_t i = 0u; i < VEX_LVGL_UI_MAX_ITEMS; ++i) {
        overlay_row_t *row = &renderer.context_rows[i];
        if (i >= count) {
            lv_obj_add_flag(row->root, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_label_set_text(row->title, labels == NULL || labels[i] == NULL ? "" : labels[i]);
        lv_label_set_text(row->meta, hints == NULL || hints[i] == NULL ? "" : hints[i]);
        lv_obj_remove_flag(row->root, LV_OBJ_FLAG_HIDDEN);
    }
    refresh(renderer.context);
}

void vex_lvgl_ui_hide_context_menu(void) {
    if (renderer.initialized != 0u) {
        lv_obj_add_flag(renderer.context, LV_OBJ_FLAG_HIDDEN);
    }
}

void vex_lvgl_ui_draw_app_switcher(const char *const *labels, uint32_t count,
                                   uint32_t selected, int32_t offset_y) {
    int32_t screen_w;
    int32_t screen_h;
    int32_t width;
    int32_t height;

    if (ensure_renderer() != 0 || count == 0u) {
        return;
    }
    if (count > VEX_LVGL_UI_MAX_ITEMS) {
        count = VEX_LVGL_UI_MAX_ITEMS;
    }
    screen_w = lv_display_get_horizontal_resolution(lv_display_get_default());
    screen_h = lv_display_get_vertical_resolution(lv_display_get_default());
    width = screen_w > SWITCHER_WIDTH + 32 ? SWITCHER_WIDTH : screen_w - 32;
    if (width < 160) width = 160;
    height = 56 + (int32_t)count * 34;
    lv_obj_set_pos(renderer.switcher, (screen_w - width) / 2,
                   (screen_h - height) / 2 - offset_y);
    lv_obj_set_size(renderer.switcher, width, height);
    lv_obj_remove_flag(renderer.switcher, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(renderer.switcher);

    for (uint32_t i = 0u; i < VEX_LVGL_UI_MAX_ITEMS; ++i) {
        overlay_row_t *row = &renderer.switcher_rows[i];
        if (i >= count) {
            lv_obj_add_flag(row->root, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_width(row->root, width - 20);
        lv_label_set_text(row->title, labels == NULL || labels[i] == NULL ? "" : labels[i]);
        style_panel(row->root, i == selected ? 0x2c2844u : 0x1c1a2cu, 9);
        style_marker(row->marker, i == selected ? 0x605698u : 0x342e4eu, 12);
        lv_obj_remove_flag(row->root, LV_OBJ_FLAG_HIDDEN);
    }
    refresh(renderer.switcher);
}

void vex_lvgl_ui_draw_elevation(uint32_t x, uint32_t y, uint32_t width,
                                uint32_t height, const char *process_name) {
    int32_t half;

    if (ensure_renderer() != 0 || width == 0u || height == 0u) {
        return;
    }
    lv_obj_set_pos(renderer.elevation, (int32_t)x, (int32_t)y);
    lv_obj_set_size(renderer.elevation, (int32_t)width, (int32_t)height);
    lv_obj_remove_flag(renderer.elevation, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(renderer.elevation);
    lv_label_set_text(renderer.elevation_process, process_name == NULL ? "Process" : process_name);
    lv_obj_set_width(renderer.elevation_process, width > 42u ? (int32_t)width - 42 : 1);

    half = (int32_t)width / 2;
    lv_obj_set_pos(renderer.elevation_approve, 1, 97);
    lv_obj_set_size(renderer.elevation_approve, half - 1, (int32_t)height - 98);
    lv_obj_set_pos(renderer.elevation_deny, half, 97);
    lv_obj_set_size(renderer.elevation_deny, (int32_t)width - half - 1, (int32_t)height - 98);
    refresh(renderer.elevation);
}

void vex_lvgl_ui_draw_spotlight(uint32_t x, uint32_t y, uint32_t width,
                                const char *query,
                                const vex_lvgl_ui_item_t *items,
                                uint32_t count, uint8_t confirmation,
                                uint8_t reveal_percent) {
    int32_t panel_h;
    int32_t full_h;
    int32_t visible_h;

    if (ensure_renderer() != 0 || width == 0u) {
        return;
    }
    if (count > VEX_LVGL_UI_MAX_ITEMS) {
        count = VEX_LVGL_UI_MAX_ITEMS;
    }
    if (reveal_percent > 100u) {
        reveal_percent = 100u;
    }

    panel_h = SPOTLIGHT_PANEL_PAD * 2 + (int32_t)count * SPOTLIGHT_ROW_HEIGHT;
    full_h = count == 0u ? 84 : SPOTLIGHT_BAR_HEIGHT + SPOTLIGHT_PANEL_GAP + panel_h;
    visible_h = full_h * reveal_percent / 100;
    if (visible_h < 1) visible_h = 1;

    lv_obj_set_pos(renderer.spotlight, (int32_t)x, (int32_t)y);
    lv_obj_set_size(renderer.spotlight, (int32_t)width, visible_h);
    lv_obj_remove_flag(renderer.spotlight, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(renderer.spotlight);
    lv_obj_set_size(renderer.spotlight_bar, (int32_t)width, SPOTLIGHT_BAR_HEIGHT);
    lv_label_set_text(renderer.spotlight_query,
        query == NULL || query[0] == '\0' ? "Search what you want ..." : query);
    lv_obj_set_width(renderer.spotlight_query, width > 90u ? (int32_t)width - 90 : 1);
    lv_obj_set_pos(renderer.spotlight_hint, (int32_t)width - 174, SPOTLIGHT_BAR_HEIGHT + 4);
    lv_obj_set_size(renderer.spotlight_hint, 174, 18);

    if (count == 0u) {
        lv_obj_add_flag(renderer.spotlight_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(renderer.spotlight_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(renderer.spotlight_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(renderer.spotlight_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(renderer.spotlight_panel, 0, SPOTLIGHT_BAR_HEIGHT + SPOTLIGHT_PANEL_GAP);
        lv_obj_set_size(renderer.spotlight_panel, (int32_t)width, panel_h);
    }

    for (uint32_t i = 0u; i < VEX_LVGL_UI_MAX_ITEMS; ++i) {
        overlay_row_t *row = &renderer.spotlight_rows[i];
        if (i >= count) {
            lv_obj_add_flag(row->root, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_width(row->root, width > 28u ? (int32_t)width - 28 : 1);
        lv_obj_set_width(row->title, width > 250u ? (int32_t)width - 250 : 60);
        lv_obj_set_width(row->subtitle, width > 260u ? (int32_t)width - 260 : 60);
        lv_obj_align(row->meta, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_label_set_text(row->title, items[i].title == NULL ? "" : items[i].title);
        lv_label_set_text(row->subtitle, items[i].subtitle == NULL ? "" : items[i].subtitle);
        lv_label_set_text(row->meta, items[i].meta == NULL ? "" : items[i].meta);
        style_panel(row->root, items[i].selected ? 0x315887u : 0x191522u, 7);
        lv_obj_set_style_bg_color(row->marker,
            lv_color_hex(items[i].kind == VEX_LVGL_UI_ITEM_COMMAND ? 0xc99914u :
                         (items[i].kind == VEX_LVGL_UI_ITEM_FILE ? 0xd9d9d9u : 0x76539du)),
            LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->marker, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_remove_flag(row->root, LV_OBJ_FLAG_HIDDEN);
    }

    if (confirmation != 0u && count != 0u) {
        lv_obj_remove_flag(renderer.spotlight_confirmation, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(renderer.spotlight_confirmation, 24, panel_h - 22);
        lv_obj_set_width(renderer.spotlight_confirmation, width > 48u ? (int32_t)width - 48 : 1);
    } else {
        lv_obj_add_flag(renderer.spotlight_confirmation, LV_OBJ_FLAG_HIDDEN);
    }
    refresh(renderer.spotlight);
}

void vex_lvgl_ui_hide_spotlight(void) {
    if (renderer.initialized != 0u) {
        lv_obj_add_flag(renderer.spotlight, LV_OBJ_FLAG_HIDDEN);
    }
}
