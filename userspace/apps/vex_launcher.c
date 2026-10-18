#include <stdint.h>
#include <unos/vex_legacy_compat.h>

#include "vex_lvgl.h"
#include "vex_runtime.h"
#include "vex_syscall.h"

#define LAUNCHER_W 560u
#define LAUNCHER_H 430u

typedef struct launcher_item {
    const char *label;
    const char *path;
} launcher_item_t;

static const launcher_item_t items[] = {
    { "GFM", "/home/root/bin/gfm" },
    { "Editor", "/usr/bin/kyil-editor" },
    { "Paint", "/home/root/bin/vex_paint" },
    { "Browser", "/home/root/bin/webkit" },
};

static lv_obj_t *launcher_screen;
static lv_obj_t *launcher_header;
static lv_obj_t *launcher_subtitle;
static lv_obj_t *launcher_list;
static lv_obj_t *launcher_buttons[sizeof(items) / sizeof(items[0])];
static lv_obj_t *status_label;
/* The launcher owns spawn status, never the child's resources. Collect exits
 * while this window stays alive instead of retaining closed apps as zombies. */
typedef struct launched_child {uint32_t pid;struct launched_child *next;} launched_child_t;
static launched_child_t *children;

static int spawn_app(const char *path) {
    launched_child_t *child=lv_malloc(sizeof(*child));
    if(!child)return -1;
    vex_process_spawn_req_t request = { .path = path, .flags = 0u, .pid = 0u };
    int result=(int)vex_syscall1(VEX_SYS_PROCESS_SPAWN, (uint64_t)(uintptr_t)&request);
    if(result!=0 || !request.pid){lv_free(child);return -1;}
    child->pid=request.pid;child->next=children;children=child;
    return 0;
}
static void collect_children(void) {
    launched_child_t **link=&children;
    while(*link) {
        launched_child_t *child=*link;int status=0;
        vex_process_wait_req_t r={(int32_t)child->pid,&status,VEX_PROCESS_WAIT_NOHANG};
        int64_t result=(int64_t)vex_syscall1(VEX_SYS_PROCESS_WAIT,(uintptr_t)&r);
        if(result>0 || result==-10){*link=child->next;lv_free(child);}else link=&child->next;
    }
}

static void launch_clicked(lv_event_t *event) {
    const launcher_item_t *item = (const launcher_item_t *)lv_event_get_user_data(event);
    const int result = item == 0 ? -1 : spawn_app(item->path);
    lv_label_set_text(status_label, result == 0 ? "Application launched." : "Launch failed.");
}

static void launcher_layout(vex_lvgl_app_t *app,
                            const vex_ui_layout_t *layout,
                            void *user_data) {
    const uint8_t use_columns =
        layout->size_class == VEX_UI_SIZE_EXPANDED && layout->height >= 320u;

    (void)app;
    (void)user_data;
    vex_ui_apply_page(launcher_screen, layout);
    vex_ui_apply_panel(launcher_header, layout);
    vex_ui_apply_panel(launcher_list, layout);
    vex_ui_set_visible(launcher_header, layout->show_header);
    vex_ui_set_visible(launcher_subtitle, layout->show_secondary);
    vex_ui_set_visible(status_label, layout->height >= 235u);

    lv_obj_set_height(launcher_header, layout->header_height);
    lv_obj_set_flex_flow(launcher_list,
        use_columns ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(items) / sizeof(items[0])); ++i) {
        lv_obj_set_size(launcher_buttons[i],
            use_columns ? LV_PCT(48) : LV_PCT(100),
            layout->control_height);
    }
}

static void build_launcher_ui(vex_lvgl_app_t *app) {
    launcher_screen = vex_lvgl_screen(app);
    vex_ui_prepare_screen(launcher_screen);
    lv_obj_set_flex_flow(launcher_screen, LV_FLEX_FLOW_COLUMN);

    launcher_header = vex_ui_panel(launcher_screen);
    lv_obj_set_width(launcher_header, LV_PCT(100));
    lv_obj_set_flex_flow(launcher_header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(launcher_header, 4, LV_PART_MAIN);
    (void)vex_ui_label(launcher_header, "Vex Launcher", &lv_font_montserrat_20, VEX_UI_TEXT);
    launcher_subtitle = vex_ui_label(
        launcher_header, "Native applications", &lv_font_montserrat_14, VEX_UI_MUTED);

    launcher_list = vex_ui_panel(launcher_screen);
    lv_obj_set_width(launcher_list, LV_PCT(100));
    lv_obj_set_flex_grow(launcher_list, 1);

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(items) / sizeof(items[0])); ++i) {
        launcher_buttons[i] = vex_ui_button(launcher_list, items[i].label);
        lv_obj_add_event_cb(
            launcher_buttons[i], launch_clicked, LV_EVENT_CLICKED, (void *)&items[i]);
    }

    status_label = vex_ui_label(
        launcher_screen,
        "Choose an app to launch it as an isolated ELF process.",
        &lv_font_montserrat_12,
        VEX_UI_MUTED);
    vex_lvgl_app_set_layout_handler(app, launcher_layout, 0);
    vex_lvgl_app_apply_layout(app);
}

int main(void) {
    vex_lvgl_app_t app;
    vex_app_window_event_t event;

    if (vex_lvgl_app_open(&app, "Vex Launcher", LAUNCHER_W, LAUNCHER_H,
            VEX_APP_WINDOW_FLAG_RESIZABLE) != 0) {
        return 1;
    }
    build_launcher_ui(&app);
    vex_lvgl_app_render(&app);

    for (;;) {
        collect_children();
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
