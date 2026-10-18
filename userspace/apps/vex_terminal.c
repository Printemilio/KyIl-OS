#include <stdint.h>

#include "vex_lvgl.h"
#include "vex_runtime.h"

#define TERM_W 760u
#define TERM_H 420u

static lv_obj_t *terminal_screen;
static lv_obj_t *terminal_panel;
static lv_obj_t *terminal_title;
static lv_obj_t *terminal_subtitle;
static lv_obj_t *terminal_detail;
static lv_obj_t *terminal_input_row;
static lv_obj_t *terminal_input;

static void command_ready(lv_event_t *event) {
    lv_obj_t *input = lv_event_get_target_obj(event);
    lv_textarea_set_text(input, "");
}

static void terminal_layout(vex_lvgl_app_t *app,
                            const vex_ui_layout_t *layout,
                            void *user_data) {
    (void)app;
    (void)user_data;
    vex_ui_apply_page(terminal_screen, layout);
    vex_ui_apply_panel(terminal_panel, layout);
    vex_ui_set_visible(terminal_title, layout->height >= 150u);
    vex_ui_set_visible(terminal_subtitle, layout->show_secondary);
    vex_ui_set_visible(terminal_detail,
        layout->size_class != VEX_UI_SIZE_COMPACT && layout->height >= 330u);

    lv_obj_set_style_pad_row(terminal_panel, layout->gap, LV_PART_MAIN);
    lv_obj_set_height(terminal_input_row, layout->control_height + 6);
    lv_obj_set_height(terminal_input, layout->control_height);
}

static void build_terminal_ui(vex_lvgl_app_t *app) {
    terminal_screen = vex_lvgl_screen(app);
    vex_ui_prepare_screen(terminal_screen);
    lv_obj_set_flex_flow(terminal_screen, LV_FLEX_FLOW_COLUMN);

    terminal_panel = vex_ui_panel(terminal_screen);
    lv_obj_set_width(terminal_panel, LV_PCT(100));
    lv_obj_set_flex_grow(terminal_panel, 1);
    lv_obj_set_flex_flow(terminal_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(terminal_panel, lv_color_hex(0x0b0c10), LV_PART_MAIN);
    terminal_title = vex_ui_label(
        terminal_panel, "Vex Terminal", &lv_font_montserrat_20, VEX_UI_TEXT);
    terminal_subtitle = vex_ui_label(
        terminal_panel, "ELF userspace terminal host", &lv_font_montserrat_14, VEX_UI_MUTED);
    terminal_detail = vex_ui_label(terminal_panel,
        "The interface is now responsive LVGL. The next backend step is attaching this widget to the PTY shell.",
        &lv_font_montserrat_14, 0x65d6a6u);
    lv_label_set_long_mode(terminal_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(terminal_detail, LV_PCT(100));

    terminal_input_row = lv_obj_create(terminal_panel);
    lv_obj_remove_style_all(terminal_input_row);
    lv_obj_set_width(terminal_input_row, LV_PCT(100));
    lv_obj_set_flex_flow(terminal_input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        terminal_input_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    (void)vex_ui_label(terminal_input_row, "vex$", &lv_font_montserrat_16, VEX_UI_TEXT);

    terminal_input = lv_textarea_create(terminal_input_row);
    lv_obj_set_flex_grow(terminal_input, 1);
    lv_textarea_set_one_line(terminal_input, true);
    lv_textarea_set_placeholder_text(terminal_input, "type a command");
    lv_obj_set_style_bg_color(terminal_input, lv_color_hex(0x181a20), LV_PART_MAIN);
    lv_obj_set_style_border_color(terminal_input, lv_color_hex(VEX_UI_DIVIDER), LV_PART_MAIN);
    lv_obj_set_style_text_color(terminal_input, lv_color_hex(VEX_UI_TEXT), LV_PART_MAIN);
    lv_obj_add_event_cb(terminal_input, command_ready, LV_EVENT_READY, 0);
    lv_group_add_obj(app->keyboard_group, terminal_input);
    lv_group_focus_obj(terminal_input);

    vex_lvgl_app_set_layout_handler(app, terminal_layout, 0);
    vex_lvgl_app_apply_layout(app);
}

int main(void) {
    vex_lvgl_app_t app;
    vex_app_window_event_t event;

    if (vex_lvgl_app_open(&app, "Vex Terminal", TERM_W, TERM_H,
            VEX_APP_WINDOW_FLAG_RESIZABLE) != 0) {
        return 1;
    }
    build_terminal_ui(&app);
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
