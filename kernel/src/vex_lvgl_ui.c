#include <unos/vex_lvgl_ui.h>

#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#include <unos/ui_brand.h>
#include <unos/ui_fonts.h>
#include <unos/ui_theme.h>

#include "vex_icon_hex.h"
#include "vex_icon_power.h"
#include "vex_icon_volume.h"
#include "vex_icon_wifi.h"

#define VEX_BAR_SIDE_MARGIN 20
#define VEX_BAR_TOP 13
#define VEX_BAR_HEIGHT 36
#define VEX_BAR_RADIUS 12
#define VEX_LVGL_MAX_WINDOWS 10u

typedef struct vex_lvgl_window_renderer {
    uint32_t native_window_id;
    lv_obj_t *root;            /* the frame ring: gradient when focused */
    lv_obj_t *body;            /* the window itself, inset by the ring */
    lv_obj_t *title;
    lv_obj_t *terminal_tab;
    lv_obj_t *terminal_tab_title;
    lv_obj_t *terminal_add_tab;
    lv_obj_t *terminal_content;
    lv_obj_t *divider;
    lv_obj_t *controls[3];
} vex_lvgl_window_renderer_t;

typedef struct vex_lvgl_shell {
    uint32_t width;
    uint32_t height;
    uint8_t initialized;
    lv_obj_t *screen;
    lv_obj_t *bar;
    lv_obj_t *date;
    lv_obj_t *chips[5];
    vex_lvgl_window_renderer_t windows[VEX_LVGL_MAX_WINDOWS];
    vex_lvgl_window_renderer_t *terminal_window;
    uint8_t window_count;
    uint32_t theme_generation;
    uint32_t active_window_id;
    lv_obj_t *app_label;
    lv_obj_t *center;
    lv_obj_t *clock;
    lv_obj_t *pill_outline;
    lv_obj_t *pill_dot;
    lv_obj_t *pill_count;
} vex_lvgl_shell_t;

static vex_lvgl_shell_t shell;

static int title_is_terminal(const char *title) {
    static const char terminal[] = "Terminal";
    uint32_t i;

    if (title == NULL) {
        return 0;
    }
    for (i = 0u; terminal[i] != '\0'; ++i) {
        if (title[i] != terminal[i]) {
            return 0;
        }
    }
    return title[i] == '\0';
}

/* LVGL ARGB8888 pixels are BGRA byte-ordered on little-endian x86. The bar
 * icons are monochrome: only their coverage is kept, and they are re-tinted
 * with the theme's icon colour whenever the theme changes. */
static uint8_t icon_hex_bgra[UI_MASK_HEX_BAR_W * UI_MASK_HEX_BAR_H * 4u];
static uint8_t icon_power_bgra[VEX_ICON_POWER_W * VEX_ICON_POWER_H * 4u];
static uint8_t icon_volume_bgra[VEX_ICON_VOLUME_W * VEX_ICON_VOLUME_H * 4u];
static uint8_t icon_wifi_bgra[VEX_ICON_WIFI_W * VEX_ICON_WIFI_H * 4u];

static lv_image_dsc_t icon_hex_dsc;
static lv_image_dsc_t icon_power_dsc;
static lv_image_dsc_t icon_volume_dsc;
static lv_image_dsc_t icon_wifi_dsc;

static void tint_coverage(uint8_t *dst, const uint8_t *coverage, size_t step,
                          size_t pixel_count, uint32_t rgb) {
    for (size_t i = 0u; i < pixel_count; ++i) {
        dst[i * 4u + 0u] = (uint8_t)rgb;
        dst[i * 4u + 1u] = (uint8_t)(rgb >> 8);
        dst[i * 4u + 2u] = (uint8_t)(rgb >> 16);
        dst[i * 4u + 3u] = coverage[i * step];
    }
}

static void init_image_dsc(lv_image_dsc_t *dsc, const uint8_t *data,
                           uint32_t width, uint32_t height) {
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
    dsc->header.flags = 0u;
    dsc->header.w = width;
    dsc->header.h = height;
    dsc->header.stride = width * 4u;
    dsc->header.reserved_2 = 0u;
    dsc->data_size = width * height * 4u;
    dsc->data = data;
    dsc->reserved = NULL;
    dsc->reserved_2 = NULL;
}

static void tint_images(uint32_t rgb) {
    tint_coverage(icon_hex_bgra, ui_mask_hex_bar, 1u, UI_MASK_HEX_BAR_W * UI_MASK_HEX_BAR_H, rgb);
    tint_coverage(icon_power_bgra, vex_icon_power + 3u, 4u, VEX_ICON_POWER_W * VEX_ICON_POWER_H, rgb);
    tint_coverage(icon_volume_bgra, vex_icon_volume + 3u, 4u, VEX_ICON_VOLUME_W * VEX_ICON_VOLUME_H, rgb);
    tint_coverage(icon_wifi_bgra, vex_icon_wifi + 3u, 4u, VEX_ICON_WIFI_W * VEX_ICON_WIFI_H, rgb);
}

static void init_images(void) {
    tint_images(ui_theme()->bar_icon);

    init_image_dsc(&icon_hex_dsc, icon_hex_bgra, UI_MASK_HEX_BAR_W, UI_MASK_HEX_BAR_H);
    init_image_dsc(&icon_power_dsc, icon_power_bgra, VEX_ICON_POWER_W, VEX_ICON_POWER_H);
    init_image_dsc(&icon_volume_dsc, icon_volume_bgra, VEX_ICON_VOLUME_W, VEX_ICON_VOLUME_H);
    init_image_dsc(&icon_wifi_dsc, icon_wifi_bgra, VEX_ICON_WIFI_W, VEX_ICON_WIFI_H);
}

static void make_passive(lv_obj_t *obj) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                      LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                      LV_OBJ_FLAG_SCROLL_CHAIN_HOR | LV_OBJ_FLAG_SCROLL_CHAIN_VER);
}

static lv_obj_t *make_clean_object(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    make_passive(obj);
    return obj;
}

static lv_obj_t *make_image(lv_obj_t *parent, const lv_image_dsc_t *source) {
    lv_obj_t *image = lv_image_create(parent);
    lv_obj_remove_style_all(image);
    lv_image_set_src(image, source);
    make_passive(image);
    return image;
}

static lv_obj_t *make_chip(lv_obj_t *parent, int32_t width) {
    lv_obj_t *chip = make_clean_object(parent);
    lv_obj_set_size(chip, width, VEX_BAR_HEIGHT - 8);
    lv_obj_set_style_radius(chip, 8, LV_PART_MAIN);
    return chip;
}

/* Outline pill over the glass (Figma): 1px ring, dot left, count right. */
static void build_notification_pill(lv_obj_t *chip) {
    lv_obj_t *outline = make_clean_object(chip);
    lv_obj_set_size(outline, 36, 16);
    lv_obj_center(outline);
    lv_obj_set_style_radius(outline, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(outline, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(outline, LV_OPA_COVER, LV_PART_MAIN);
    shell.pill_outline = outline;

    lv_obj_t *dot = make_clean_object(outline);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    shell.pill_dot = dot;

    lv_obj_t *count = lv_label_create(outline);
    lv_obj_remove_style_all(count);
    lv_label_set_text(count, "0");
    lv_obj_set_style_text_font(count, &lv_font_inter_12, LV_PART_MAIN);
    lv_obj_align(count, LV_ALIGN_RIGHT_MID, -6, 0);
    make_passive(count);
    shell.pill_count = count;
}

static void layout_shell(void) {
    const int32_t bar_width = shell.width > VEX_BAR_SIDE_MARGIN * 2u
        ? (int32_t)shell.width - VEX_BAR_SIDE_MARGIN * 2 : 1;

    lv_obj_set_pos(shell.bar, VEX_BAR_SIDE_MARGIN, VEX_BAR_TOP);
    lv_obj_set_size(shell.bar, bar_width, VEX_BAR_HEIGHT);

    /* Keep the clock useful instead of letting it collide with the system area. */
    if (shell.width < 650u) {
        lv_obj_add_flag(shell.center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(shell.app_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(shell.center, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(shell.app_label, LV_OBJ_FLAG_HIDDEN);
    }

    /* On narrow displays retain the essential status items first. */
    if (shell.width < 470u) {
        lv_obj_add_flag(shell.chips[1], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(shell.chips[1], LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_window_renderer(vex_lvgl_window_renderer_t *window) {
    if (window == NULL) {
        return;
    }
    window->root = make_clean_object(shell.screen);
    lv_obj_set_style_radius(window->root, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(window->root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(window->root, LV_OBJ_FLAG_HIDDEN);

    /* Created first, so everything else sits on top of it. */
    window->body = make_clean_object(window->root);
    lv_obj_set_style_bg_opa(window->body, LV_OPA_COVER, LV_PART_MAIN);

    window->title = lv_label_create(window->root);
    lv_obj_remove_style_all(window->title);
    lv_label_set_long_mode(window->title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(window->title, &lv_font_inter_16, LV_PART_MAIN);
    make_passive(window->title);

    /* The kernel TTY is a single, real shell.  Its chrome nevertheless
     * exposes the tab strip now, so future shell sessions have a stable home
     * without spawning the legacy userspace terminal preview. */
    window->terminal_tab = make_clean_object(window->root);
    lv_obj_set_size(window->terminal_tab, 112, 32);
    lv_obj_set_style_radius(window->terminal_tab, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(window->terminal_tab, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(window->terminal_tab, LV_OBJ_FLAG_HIDDEN);

    window->terminal_tab_title = lv_label_create(window->terminal_tab);
    lv_obj_remove_style_all(window->terminal_tab_title);
    lv_label_set_text(window->terminal_tab_title, "Terminal");
    lv_obj_set_style_text_font(window->terminal_tab_title, &lv_font_inter_14, LV_PART_MAIN);
    lv_obj_center(window->terminal_tab_title);
    make_passive(window->terminal_tab_title);

    window->terminal_add_tab = lv_label_create(window->root);
    lv_obj_remove_style_all(window->terminal_add_tab);
    lv_label_set_text(window->terminal_add_tab, "+");
    lv_obj_set_style_text_font(window->terminal_add_tab, &lv_font_inter_16, LV_PART_MAIN);
    lv_obj_add_flag(window->terminal_add_tab, LV_OBJ_FLAG_HIDDEN);
    make_passive(window->terminal_add_tab);

    window->terminal_content = lv_label_create(window->root);
    lv_obj_remove_style_all(window->terminal_content);
    lv_label_set_long_mode(window->terminal_content, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_bg_opa(window->terminal_content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(window->terminal_content, &lv_font_inter_14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(window->terminal_content, 14, LV_PART_MAIN);
    lv_obj_add_flag(window->terminal_content, LV_OBJ_FLAG_HIDDEN);
    make_passive(window->terminal_content);

    window->divider = make_clean_object(window->root);
    lv_obj_set_height(window->divider, 1);
    lv_obj_set_style_bg_opa(window->divider, LV_OPA_COVER, LV_PART_MAIN);

    for (int i = 0; i < 3; ++i) {
        window->controls[i] = make_clean_object(window->root);
        lv_obj_set_size(window->controls[i], 12, 12);
        lv_obj_set_style_radius(window->controls[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(window->controls[i], LV_OPA_COVER, LV_PART_MAIN);
    }
}

static void theme_window_renderer(vex_lvgl_window_renderer_t *window, const ui_theme_t *theme) {
    lv_obj_set_style_bg_color(window->body, lv_color_hex(theme->window), LV_PART_MAIN);
    lv_obj_set_style_text_color(window->title, lv_color_hex(theme->text), LV_PART_MAIN);
    lv_obj_set_style_bg_color(window->terminal_tab, lv_color_hex(theme->panel), LV_PART_MAIN);
    lv_obj_set_style_text_color(window->terminal_tab_title, lv_color_hex(theme->text), LV_PART_MAIN);
    lv_obj_set_style_text_color(window->terminal_add_tab, lv_color_hex(theme->muted), LV_PART_MAIN);
    lv_obj_set_style_bg_color(window->terminal_content, lv_color_hex(theme->window), LV_PART_MAIN);
    lv_obj_set_style_text_color(window->terminal_content, lv_color_hex(theme->text), LV_PART_MAIN);
    lv_obj_set_style_bg_color(window->divider, lv_color_hex(theme->divider), LV_PART_MAIN);
}

/* Focus is shown the Hyprland way: a 2px violet-to-blue ring around the
 * focused window, a 1px hairline around the others, whose traffic lights
 * also go grey as on macOS. */
static void style_window_focus(vex_lvgl_window_renderer_t *window, int active,
                               int32_t width, int32_t height) {
    const ui_theme_t *theme = ui_theme();
    const int32_t ring = active ? 2 : 1;
    const uint32_t controls[3] = {
        theme->control_maximize, theme->control_minimize, theme->control_close
    };

    if (active) {
        lv_obj_set_style_bg_color(window->root, lv_color_hex(theme->border_active_a), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(window->root, lv_color_hex(theme->border_active_b), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(window->root, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(window->root, lv_color_hex(theme->border_inactive), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(window->root, LV_GRAD_DIR_NONE, LV_PART_MAIN);
    }
    lv_obj_set_pos(window->body, ring, ring);
    lv_obj_set_size(window->body, width > 2 * ring ? width - 2 * ring : 1,
                    height > 2 * ring ? height - 2 * ring : 1);
    lv_obj_set_style_radius(window->body, 12 - ring, LV_PART_MAIN);
    lv_obj_set_pos(window->divider, ring, 45);
    lv_obj_set_width(window->divider, width > 2 * ring ? width - 2 * ring : 1);
    for (int i = 0; i < 3; ++i) {
        lv_obj_set_style_bg_color(window->controls[i],
            lv_color_hex(active ? controls[i] : theme->control_idle), LV_PART_MAIN);
    }
}

/* Re-skins every shell object when the system theme changed since the last
 * frame. Cheap when nothing changed: one integer comparison. */
static void apply_theme(void) {
    const ui_theme_t *theme = ui_theme();

    if (shell.theme_generation == ui_theme_generation()) {
        return;
    }
    shell.theme_generation = ui_theme_generation();

    lv_obj_set_style_bg_color(shell.bar, lv_color_hex(theme->bar), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(shell.bar, theme->bar_alpha, LV_PART_MAIN);
    /* The glass edge: a hairline of light on dark glass, of shade on light. */
    lv_obj_set_style_border_color(shell.bar, ui_theme_mode() == UI_THEME_DARK
        ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(shell.bar, ui_theme_mode() == UI_THEME_DARK ? 26 : 20, LV_PART_MAIN);
    lv_obj_set_style_text_color(shell.date, lv_color_hex(theme->bar_text), LV_PART_MAIN);
    lv_obj_set_style_text_color(shell.clock, lv_color_hex(theme->bar_text), LV_PART_MAIN);
    lv_obj_set_style_text_color(shell.app_label, lv_color_hex(theme->bar_text), LV_PART_MAIN);
    for (int i = 0; i < 5; ++i) {
        lv_obj_set_style_bg_color(shell.chips[i], lv_color_hex(theme->bar_hover), LV_PART_MAIN);
    }
    lv_obj_set_style_border_color(shell.pill_outline, lv_color_hex(theme->bar_icon), LV_PART_MAIN);
    lv_obj_set_style_bg_color(shell.pill_dot, lv_color_hex(theme->bar_icon), LV_PART_MAIN);
    lv_obj_set_style_text_color(shell.pill_count, lv_color_hex(theme->bar_icon), LV_PART_MAIN);

    tint_images(theme->bar_icon);
    lv_image_cache_drop(&icon_hex_dsc);
    lv_image_cache_drop(&icon_power_dsc);
    lv_image_cache_drop(&icon_volume_dsc);
    lv_image_cache_drop(&icon_wifi_dsc);

    for (uint32_t i = 0u; i < VEX_LVGL_MAX_WINDOWS; ++i) {
        theme_window_renderer(&shell.windows[i], theme);
    }
}

int vex_lvgl_ui_init(uint32_t screen_width, uint32_t screen_height) {
    lv_obj_t *left;
    lv_obj_t *right;
    lv_obj_t *image;

    if (shell.initialized != 0u) {
        return 0;
    }
    if (lv_display_get_default() == NULL || screen_width == 0u || screen_height == 0u) {
        return -1;
    }

    shell.width = screen_width;
    shell.height = screen_height;
    shell.screen = lv_screen_active();
    if (shell.screen == NULL) {
        return -1;
    }

    init_images();

    lv_obj_remove_style_all(shell.screen);
    lv_obj_set_style_bg_opa(shell.screen, LV_OPA_TRANSP, LV_PART_MAIN);
    make_passive(shell.screen);
    lv_obj_remove_style_all(lv_layer_bottom());
    lv_obj_set_style_bg_opa(lv_layer_bottom(), LV_OPA_TRANSP, LV_PART_MAIN);
    make_passive(lv_layer_bottom());

    shell.bar = make_clean_object(shell.screen);
    lv_obj_set_style_radius(shell.bar, VEX_BAR_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(shell.bar, 1, LV_PART_MAIN);

    left = make_clean_object(shell.bar);
    lv_obj_set_size(left, 50, LV_PCT(100));
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
    shell.chips[0] = make_chip(left, 43);
    lv_obj_align(shell.chips[0], LV_ALIGN_LEFT_MID, 2, 0);
    image = make_image(shell.chips[0], &icon_hex_dsc);
    lv_obj_center(image);

    /* macOS menu bar: the focused application's name beside the mark. */
    shell.app_label = lv_label_create(shell.bar);
    lv_obj_remove_style_all(shell.app_label);
    lv_label_set_text(shell.app_label, "");
    lv_label_set_long_mode(shell.app_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(shell.app_label, 240);
    lv_obj_set_style_text_font(shell.app_label, &lv_font_inter_semibold_14, LV_PART_MAIN);
    lv_obj_align(shell.app_label, LV_ALIGN_LEFT_MID, 54, 0);
    make_passive(shell.app_label);

    /* Centre: the date, then the time in the heavier weight. */
    shell.center = make_clean_object(shell.bar);
    lv_obj_set_size(shell.center, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_flow(shell.center, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(shell.center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(shell.center, 10, LV_PART_MAIN);
    lv_obj_center(shell.center);

    shell.date = lv_label_create(shell.center);
    lv_obj_remove_style_all(shell.date);
    lv_label_set_text(shell.date, "");
    lv_obj_set_style_text_font(shell.date, &lv_font_inter_14, LV_PART_MAIN);
    lv_obj_set_style_text_opa(shell.date, 190, LV_PART_MAIN);
    make_passive(shell.date);

    shell.clock = lv_label_create(shell.center);
    lv_obj_remove_style_all(shell.clock);
    lv_label_set_text(shell.clock, "");
    lv_obj_set_style_text_font(shell.clock, &lv_font_inter_semibold_14, LV_PART_MAIN);
    make_passive(shell.clock);

    right = make_clean_object(shell.bar);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_width(right, LV_SIZE_CONTENT);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 2, LV_PART_MAIN);

    shell.chips[1] = make_chip(right, 58);
    image = make_image(shell.chips[1], &icon_power_dsc);
    lv_obj_center(image);

    shell.chips[2] = make_chip(right, 38);
    image = make_image(shell.chips[2], &icon_volume_dsc);
    lv_obj_center(image);

    shell.chips[3] = make_chip(right, 38);
    image = make_image(shell.chips[3], &icon_wifi_dsc);
    lv_obj_center(image);

    shell.chips[4] = make_chip(right, 50);
    build_notification_pill(shell.chips[4]);

    for (uint32_t i = 0u; i < VEX_LVGL_MAX_WINDOWS; ++i) {
        build_window_renderer(&shell.windows[i]);
    }
    shell.theme_generation = 0u;
    apply_theme();
    layout_shell();
    shell.initialized = 1u;
    return 0;
}

int vex_lvgl_ui_ready(void) {
    return shell.initialized != 0u;
}

void vex_lvgl_ui_begin_scene(void) {
    if (shell.initialized == 0u) {
        return;
    }

    for (uint32_t i = 0u; i < VEX_LVGL_MAX_WINDOWS; ++i) {
        lv_obj_add_flag(shell.windows[i].root, LV_OBJ_FLAG_HIDDEN);
    }
    shell.window_count = 0u;
    shell.terminal_window = NULL;
    vex_lvgl_overlays_begin_scene();
    vex_lvgl_browser_begin_scene();
}

void vex_lvgl_ui_draw_taskbar(const char *date_text, int hovered_item) {
    if (shell.initialized == 0u) {
        return;
    }

    apply_theme();
    lv_label_set_text(shell.date, date_text == NULL ? "" : date_text);
    lv_obj_center(shell.center);
    for (int i = 0; i < 5; ++i) {
        lv_obj_set_style_bg_opa(shell.chips[i],
            i == hovered_item ? ui_theme()->bar_hover_alpha : LV_OPA_TRANSP,
            LV_PART_MAIN);
    }
    layout_shell();
    lv_obj_invalidate(shell.bar);
    vex_lvgl_ui_paint_object(shell.bar);
}

void vex_lvgl_ui_set_active_window(uint32_t window_id) {
    shell.active_window_id = window_id;
}

void vex_lvgl_ui_set_taskbar_info(const char *application, const char *clock) {
    if (shell.initialized == 0u) {
        return;
    }
    lv_label_set_text(shell.app_label, application == NULL ? "" : application);
    lv_label_set_text(shell.clock, clock == NULL ? "" : clock);
}

void vex_lvgl_ui_draw_window_frame_id(uint32_t window_id,uint32_t x,uint32_t y,uint32_t width,
                                   uint32_t height,const char *title) {
    vex_lvgl_window_renderer_t *window;
    const int terminal_window = title_is_terminal(title);

    if (shell.initialized == 0u || width == 0u || height == 0u) {
        return;
    }
    apply_theme();
    window=NULL;
    if(window_id)for(uint32_t i=0;i<shell.window_count;++i)
        if(shell.windows[i].native_window_id==window_id){window=&shell.windows[i];break;}
    if(!window) {
        if(shell.window_count>=VEX_LVGL_MAX_WINDOWS)return;
        window=&shell.windows[shell.window_count++];
        window->native_window_id=window_id;
    }

    lv_obj_remove_flag(window->root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(window->root);
    lv_obj_set_pos(window->root, (int32_t)x, (int32_t)y);
    /* Keep the complete native frame aligned with its Vex window bounds.
     * Browser pixels are composed after this backing, so the rounded body
     * remains visible around the inset Web viewport without owning its pixels. */
    lv_obj_set_size(window->root, (int32_t)width, (int32_t)height);

    /* The Figma frames carry no title text: applications own their content
     * and the title names the window in the switcher only. */
    lv_label_set_text(window->title, "");
    lv_obj_set_pos(window->title, 23, 13);
    lv_obj_set_size(window->title, width > 130u ? (int32_t)width - 130 : 8, 22);

    if (terminal_window) {
        shell.terminal_window = window;
        lv_obj_remove_flag(window->terminal_tab, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(window->terminal_add_tab, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(window->terminal_tab, 11, 7);
        lv_obj_set_pos(window->terminal_add_tab, 133, 14);
        lv_obj_remove_flag(window->terminal_content, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(window->terminal_content, 8, 46);
        lv_obj_set_size(window->terminal_content,
            width > 16u ? (int32_t)width - 16 : 1,
            height > 54u ? (int32_t)height - 54 : 1);
    } else {
        lv_obj_add_flag(window->terminal_tab, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(window->terminal_add_tab, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(window->terminal_content, LV_OBJ_FLAG_HIDDEN);
    }

    style_window_focus(window, window_id == 0u || window_id == shell.active_window_id,
                       (int32_t)width, (int32_t)height);

    /* Same right-side ordering as the Figma chrome; the centres stay where
     * the window manager hit-tests them (width-74/-48/-22, y 23). */
    lv_obj_set_pos(window->controls[0], (int32_t)width - 80, 17);
    lv_obj_set_pos(window->controls[1], (int32_t)width - 54, 17);
    lv_obj_set_pos(window->controls[2], (int32_t)width - 28, 17);

    if (width < 100u) {
        for (int i = 0; i < 3; ++i) {
            lv_obj_add_flag(window->controls[i], LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            lv_obj_remove_flag(window->controls[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    vex_lvgl_ui_paint_object(window->root);
}
void vex_lvgl_ui_draw_window_frame(uint32_t x,uint32_t y,uint32_t width,uint32_t height,const char *title) {
    vex_lvgl_ui_draw_window_frame_id(0u,x,y,width,height,title);
}

void vex_lvgl_ui_draw_terminal_content(const char *text) {
    if (shell.initialized == 0u || shell.terminal_window == NULL) {
        return;
    }
    lv_label_set_text(shell.terminal_window->terminal_content, text == NULL ? "" : text);
    vex_lvgl_ui_paint_object(shell.terminal_window->terminal_content);
}
