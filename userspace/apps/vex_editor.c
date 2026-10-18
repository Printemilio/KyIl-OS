#include <stdint.h>

#include <unos/vex_legacy_compat.h>

#include "vex_lvgl.h"
#include "vex_runtime.h"

#define EDITOR_W 760u
#define EDITOR_H 470u
#define EDITOR_PATH_MAX 256u
#define EDITOR_FILE_LIMIT (128u * 1024u)

static lv_obj_t *editor_screen;
static lv_obj_t *editor_header;
static lv_obj_t *editor_subtitle;
static lv_obj_t *editor_area;
static lv_obj_t *editor_footer;
static char editor_path[EDITOR_PATH_MAX];
static char editor_content[EDITOR_FILE_LIMIT + 1u];

static void load_editor_document(void) {
    struct stat information;
    size_t wanted;
    size_t received = 0u;
    int fd;

    if (editor_path[0] == '\0') {
        lv_label_set_text(editor_subtitle, "New document");
        lv_textarea_set_text(editor_area, "");
        return;
    }
    lv_label_set_text(editor_subtitle, editor_path);
    if (stat(editor_path, &information) != 0) {
        lv_textarea_set_text(editor_area, "This file is no longer available.");
        return;
    }
    wanted = information.st_size > EDITOR_FILE_LIMIT
        ? EDITOR_FILE_LIMIT : (size_t)information.st_size;
    fd = open(editor_path, O_RDONLY);
    if (fd < 0) {
        lv_textarea_set_text(editor_area, "This file cannot be opened.");
        return;
    }
    while (received < wanted) {
        const ssize_t count = read(fd, editor_content + received,
                                   wanted - received);
        if (count <= 0) {
            break;
        }
        received += (size_t)count;
    }
    close(fd);
    editor_content[received] = '\0';
    for (size_t i = 0u; i < received; ++i) {
        if (editor_content[i] == '\0') {
            editor_content[i] = ' ';
        }
    }
    lv_textarea_set_text(editor_area, editor_content);
}

static void editor_layout(vex_lvgl_app_t *app,
                          const vex_ui_layout_t *layout,
                          void *user_data) {
    (void)app;
    (void)user_data;
    vex_ui_apply_page(editor_screen, layout);
    vex_ui_set_visible(editor_header, layout->show_header);
    vex_ui_set_visible(editor_subtitle, layout->show_secondary);
    vex_ui_set_visible(editor_footer,
        (uint8_t)(layout->show_secondary && layout->height >= 360u));
    if (layout->show_header) {
        lv_obj_set_size(editor_header, LV_PCT(100), layout->header_height);
        vex_ui_apply_panel(editor_header, layout);
        lv_obj_set_style_pad_row(editor_header,
            layout->size_class == VEX_UI_SIZE_COMPACT ? 2 : 4,
            LV_PART_MAIN);
    }
    lv_obj_set_style_pad_all(editor_area,
        layout->size_class == VEX_UI_SIZE_COMPACT ? 10 : 16,
        LV_PART_MAIN);
    lv_obj_set_style_radius(editor_area,
        layout->size_class == VEX_UI_SIZE_COMPACT ? 7 : 10,
        LV_PART_MAIN);
}

static void build_editor_ui(vex_lvgl_app_t *app) {
    editor_screen = vex_lvgl_screen(app);

    vex_ui_prepare_screen(editor_screen);
    lv_obj_set_flex_flow(editor_screen, LV_FLEX_FLOW_COLUMN);

    editor_header = vex_ui_panel(editor_screen);
    lv_obj_set_flex_flow(editor_header, LV_FLEX_FLOW_COLUMN);
    (void)vex_ui_label(editor_header, "Vex Editor",
                       &lv_font_montserrat_20, VEX_UI_TEXT);
    editor_subtitle = vex_ui_label(editor_header,
        "New document",
        &lv_font_montserrat_14, VEX_UI_MUTED);

    editor_area = lv_textarea_create(editor_screen);
    lv_obj_set_width(editor_area, LV_PCT(100));
    lv_obj_set_flex_grow(editor_area, 1);
    lv_obj_set_style_bg_color(editor_area, lv_color_hex(0xf0f1f4), LV_PART_MAIN);
    lv_obj_set_style_text_color(editor_area, lv_color_hex(0x20232a), LV_PART_MAIN);
    lv_obj_set_style_border_width(editor_area, 0, LV_PART_MAIN);
    lv_textarea_set_text(editor_area, "");
    lv_group_add_obj(app->keyboard_group, editor_area);
    lv_group_focus_obj(editor_area);

    editor_footer = vex_ui_label(editor_screen,
        "Backspace and Enter are handled by the focused LVGL widget.",
        &lv_font_montserrat_12, VEX_UI_MUTED);
    load_editor_document();
    vex_lvgl_app_set_layout_handler(app, editor_layout, 0);
    vex_lvgl_app_apply_layout(app);
}

int main(void) {
    vex_lvgl_app_t app;
    vex_app_window_event_t event;

    editor_path[0] = '\0';
    (void)vex_open_context(VEX_OPEN_CONTEXT_EDITOR,
                           editor_path, sizeof(editor_path));
    if (vex_lvgl_app_open(&app, "Vex Editor", EDITOR_W, EDITOR_H,
            VEX_APP_WINDOW_FLAG_RESIZABLE) != 0) {
        return 1;
    }
    build_editor_ui(&app);
    vex_lvgl_app_render(&app);

    for (;;) {
        if (vex_app_window_poll(&app.window, &event) == 0) {
            if (event.type == VEX_APP_EVENT_CLOSE) {
                break;
            }
            if (vex_lvgl_app_handle_event(&app, &event) &&
                event.type == VEX_APP_EVENT_CONFIGURE) {
                vex_lvgl_app_render(&app);
            }
        }
        (void)vex_syscall0(VEX_SYS_PROCESS_YIELD);
    }
    vex_lvgl_app_close(&app);
    return 0;
}
