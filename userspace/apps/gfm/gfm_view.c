#include "gfm_view.h"

#include <stddef.h>
#include <stdint.h>

/* Colours follow the existing Vex shell (dark #141414, violet dividers). */
#define COLOR_BG 0x141414u
#define COLOR_PANEL 0x1a1a1eu
#define COLOR_LINE 0x483156u
#define COLOR_FIELD 0x3b2b5au
#define COLOR_SELECTED 0x562b5au
#define COLOR_HOVER 0x2a2233u
#define COLOR_ACCENT 0xb64cffu
#define COLOR_TEXT 0xf2edf4u
#define COLOR_MUTED 0x9a909fu
#define COLOR_ERROR 0xff6b7au
#define COLOR_GOLD 0xc99914u
#define COLOR_GOLD_TOP 0x9b780du
#define COLOR_PAPER 0xd9d9d9u
#define COLOR_BUTTON 0x27272fu
#define COLOR_BUTTON_ACTIVE 0x34343eu

#define HEADER_H 44
#define TOOLBAR_H 40
#define PROMPT_H 44
#define COLUMNS_H 26
#define STATUS_H 26
#define ROW_H 30
#define SIDEBAR_W 150
#define INFO_W 240
#define SCROLLBAR_ALLOWANCE 14

#define PLACE_COUNT 4u

typedef struct place {
    const char *label;
    const char *path;
    lv_obj_t *row;
    lv_obj_t *label_object;
} place_t;

typedef struct row {
    lv_obj_t *object;
    lv_obj_t *name;
    lv_obj_t *size;
    lv_obj_t *kind;
} row_t;

static vex_lvgl_app_t *app;
static gfm_view_callbacks_t callbacks;
static gfm_view_enabled_t enabled;
static uint32_t view_width;
static uint32_t view_height;

static lv_obj_t *screen;
static lv_obj_t *header;
static lv_obj_t *nav_buttons[4];
static lv_obj_t *path_field;
static lv_obj_t *toolbar;
static lv_obj_t *toolbar_buttons[GFM_ACTION_COUNT];
static lv_obj_t *prompt_bar;
static lv_obj_t *prompt_label;
static lv_obj_t *prompt_field;
static lv_obj_t *prompt_accept;
static lv_obj_t *prompt_accept_label;
static lv_obj_t *prompt_cancel;
static lv_obj_t *sidebar;
static lv_obj_t *columns;
static lv_obj_t *column_labels[3];
static lv_obj_t *list;
static lv_obj_t *list_empty;
static lv_obj_t *info_panel;
static lv_obj_t *info_lines[7];
static lv_obj_t *info_open;
static lv_obj_t *info_executable;
static lv_obj_t *info_executable_label;
static lv_obj_t *status_bar;
static lv_obj_t *status_label;
static lv_obj_t *summary_label;
static lv_obj_t *focus_sink;

static place_t places[PLACE_COUNT] = {
    { "Root", "/", 0, 0 },
    { "Home", "/home/root", 0, 0 },
    { "Desktop", "/home/root/Desktop", 0, 0 },
    { "Downloads", "/home/root/Downloads", 0, 0 },
};

static row_t rows[GFM_MAX_ENTRIES];
static size_t row_count;
static int selected_row = -1;
static gfm_prompt_kind_t prompt_kind = GFM_PROMPT_NONE;
static int info_shown;
static char current_path[GFM_PATH_MAX];

/* ── building blocks ─────────────────────────────────────────────────────── */

static lv_obj_t *plain(lv_obj_t *parent) {
    lv_obj_t *object = lv_obj_create(parent);

    lv_obj_remove_style_all(object);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color) {
    lv_obj_t *object = lv_label_create(parent);

    lv_obj_remove_style_all(object);
    lv_label_set_text(object, text);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(object, lv_color_hex(color), LV_PART_MAIN);
    return object;
}

static void fill(lv_obj_t *object, uint32_t color) {
    lv_obj_set_style_bg_color(object, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
}

static void border(lv_obj_t *object, uint32_t color, lv_border_side_t side) {
    lv_obj_set_style_border_width(object, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(object, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_border_side(object, side, LV_PART_MAIN);
}

static void style_text_field(lv_obj_t *field) {
    lv_textarea_set_one_line(field, true);
    lv_obj_set_style_bg_color(field, lv_color_hex(COLOR_FIELD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(field, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(field, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_radius(field, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(field, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, lv_color_hex(COLOR_LINE), LV_PART_MAIN);
    lv_obj_set_style_border_color(field, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_pad_hor(field, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(field, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(field, lv_color_hex(COLOR_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_width(field, 2, LV_PART_CURSOR);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_group_add_obj(app->keyboard_group, field);
}

static void button_clicked(lv_event_t *event) {
    const gfm_action_t action = (gfm_action_t)(uintptr_t)lv_event_get_user_data(event);

    if (!enabled.flags[action]) {
        return;
    }
    gfm_view_release_text_focus();
    if (callbacks.action != 0) {
        callbacks.action(action, callbacks.context);
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, gfm_action_t action,
                             const lv_font_t *font, int32_t pad_hor) {
    lv_obj_t *button = plain(parent);
    lv_obj_t *caption;

    fill(button, COLOR_BUTTON);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_BUTTON_ACTIVE), LV_PART_MAIN | LV_STATE_HOVERED);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(button, pad_hor, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(button, 5, LV_PART_MAIN);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_size(button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, button_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
    caption = label(button, text, font, COLOR_TEXT);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(caption, LV_SIZE_CONTENT);
    return button;
}

static void path_field_event(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_READY && callbacks.path_submitted != 0) {
        callbacks.path_submitted(lv_textarea_get_text(path_field), callbacks.context);
    }
}

static void prompt_field_event(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_READY) {
        button_clicked(event);
    }
}

static void list_pressed(lv_event_t *event) {
    (void)event;
    gfm_view_release_text_focus();
}

/* ── icons ───────────────────────────────────────────────────────────────── */

static void make_folder_icon(lv_obj_t *parent) {
    lv_obj_t *tab = plain(parent);
    lv_obj_t *body = plain(parent);

    lv_obj_set_pos(tab, 1, 2);
    lv_obj_set_size(tab, 9, 5);
    fill(tab, COLOR_GOLD_TOP);
    lv_obj_set_style_radius(tab, 2, LV_PART_MAIN);
    lv_obj_set_pos(body, 0, 5);
    lv_obj_set_size(body, 20, 14);
    fill(body, COLOR_GOLD);
    lv_obj_set_style_radius(body, 3, LV_PART_MAIN);
}

static void make_file_icon(lv_obj_t *parent) {
    lv_obj_t *paper = plain(parent);
    uint32_t i;

    lv_obj_set_pos(paper, 4, 1);
    lv_obj_set_size(paper, 13, 18);
    fill(paper, COLOR_PAPER);
    lv_obj_set_style_radius(paper, 2, LV_PART_MAIN);
    for (i = 0u; i < 3u; ++i) {
        lv_obj_t *line = plain(paper);

        lv_obj_set_pos(line, 3, 5 + (int32_t)i * 4);
        lv_obj_set_size(line, 7, 1);
        fill(line, 0x8d8d8du);
    }
}

/* ── rows ────────────────────────────────────────────────────────────────── */

static const char *kind_text(const gfm_entry_t *entry) {
    if (gfm_is_transient_download(entry->name)) {
        return "Download in progress";
    }
    if (entry->type == GFM_TYPE_DIRECTORY) {
        return "Folder";
    }
    if (entry->type == GFM_TYPE_SYMLINK) {
        return "Link";
    }
    return (entry->mode & 0111u) != 0u ? "Program" : "File";
}

static void row_clicked(lv_event_t *event) {
    const size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);

    gfm_view_release_text_focus();
    if (callbacks.row_clicked != 0) {
        callbacks.row_clicked(index, callbacks.context);
    }
}

static void clear_rows(void) {
    lv_obj_clean(list);
    row_count = 0u;
    selected_row = -1;
    list_empty = 0;
}

static void apply_row_geometry(void);

static void build_rows(const gfm_listing_t *listing) {
    size_t i;

    clear_rows();
    for (i = 0u; i < listing->count; ++i) {
        const gfm_entry_t *entry = &listing->entries[i];
        row_t *row = &rows[i];
        lv_obj_t *icon;
        char size_text[32];

        row->object = plain(list);
        lv_obj_set_size(row->object, LV_PCT(100), ROW_H);
        lv_obj_set_style_radius(row->object, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row->object, lv_color_hex(COLOR_HOVER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->object, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row->object, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_HOVERED);
        lv_obj_set_style_bg_color(row->object, lv_color_hex(COLOR_SELECTED),
                                  LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(row->object, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_add_flag(row->object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(row->object, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(row->object, LV_OBJ_FLAG_SCROLL_CHAIN);
        lv_obj_add_event_cb(row->object, row_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        icon = plain(row->object);
        lv_obj_set_pos(icon, 10, 5);
        lv_obj_set_size(icon, 22, 20);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        if (entry->type == GFM_TYPE_DIRECTORY) {
            make_folder_icon(icon);
        } else {
            make_file_icon(icon);
        }

        row->name = label(row->object, entry->name, &lv_font_montserrat_14, COLOR_TEXT);
        row->size = label(row->object, "", &lv_font_montserrat_12, COLOR_MUTED);
        row->kind = label(row->object, kind_text(entry), &lv_font_montserrat_12, COLOR_MUTED);
        lv_obj_set_style_text_align(row->size, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        if (entry->type != GFM_TYPE_DIRECTORY) {
            gfm_format_size(entry->size, size_text, sizeof(size_text));
            lv_label_set_text(row->size, size_text);
        }
        ++row_count;
    }
    if (listing->count == 0u) {
        list_empty = label(list, "This folder is empty", &lv_font_montserrat_14, COLOR_MUTED);
        lv_obj_set_style_pad_left(list_empty, 14, LV_PART_MAIN);
        lv_obj_set_style_pad_top(list_empty, 10, LV_PART_MAIN);
    }
    apply_row_geometry();
}

/* ── layout ──────────────────────────────────────────────────────────────── */

static int32_t content_left(void) {
    return view_width >= 560u ? SIDEBAR_W : 0;
}

static int32_t content_right(void) {
    return (info_shown && view_width >= 700u) ? (int32_t)view_width - INFO_W : (int32_t)view_width;
}

static int32_t content_top(void) {
    return HEADER_H + TOOLBAR_H + (prompt_kind != GFM_PROMPT_NONE ? PROMPT_H : 0);
}

static void apply_row_geometry(void) {
    const int32_t width = content_right() - content_left() - SCROLLBAR_ALLOWANCE;
    const int32_t kind_w = width >= 480 ? 140 : 0;
    const int32_t size_w = width >= 340 ? 84 : 0;
    const int32_t name_w = width - 40 - kind_w - size_w - 12;
    size_t i;

    for (i = 0u; i < row_count; ++i) {
        lv_obj_set_pos(rows[i].name, 40, 6);
        lv_obj_set_width(rows[i].name, name_w > 40 ? name_w : 40);
        vex_ui_set_visible(rows[i].size, size_w != 0);
        vex_ui_set_visible(rows[i].kind, kind_w != 0);
        lv_obj_set_pos(rows[i].size, width - kind_w - size_w - 8, 8);
        lv_obj_set_width(rows[i].size, size_w > 0 ? size_w : 1);
        lv_obj_set_pos(rows[i].kind, width - kind_w, 8);
        lv_obj_set_width(rows[i].kind, kind_w > 0 ? kind_w - 8 : 1);
    }
    vex_ui_set_visible(column_labels[1], size_w != 0);
    vex_ui_set_visible(column_labels[2], kind_w != 0);
    lv_obj_set_pos(column_labels[0], 40, 5);
    lv_obj_set_pos(column_labels[1], width - kind_w - size_w - 8, 5);
    lv_obj_set_width(column_labels[1], size_w > 0 ? size_w : 1);
    lv_obj_set_pos(column_labels[2], width - kind_w, 5);
}

static void relayout(void) {
    const int32_t width = (int32_t)view_width;
    const int32_t height = (int32_t)view_height;
    const int32_t left = content_left();
    const int32_t right = content_right();
    const int32_t top = content_top();
    const int32_t body_top = top + COLUMNS_H;
    const int32_t body_h = height - STATUS_H - body_top;
    size_t i;

    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, width, HEADER_H);
    for (i = 0u; i < 4u; ++i) {
        lv_obj_set_pos(nav_buttons[i], 8 + (int32_t)i * 34, 7);
    }
    lv_obj_set_pos(path_field, 8 + 4 * 34 + 6, 7);
    lv_obj_set_size(path_field, width - (8 + 4 * 34 + 6) - 10, 30);

    lv_obj_set_pos(toolbar, 0, HEADER_H);
    lv_obj_set_size(toolbar, width, TOOLBAR_H);

    lv_obj_set_pos(prompt_bar, 0, HEADER_H + TOOLBAR_H);
    lv_obj_set_size(prompt_bar, width, PROMPT_H);
    vex_ui_set_visible(prompt_bar, prompt_kind != GFM_PROMPT_NONE);
    lv_obj_set_pos(prompt_label, 12, 14);
    {
        const int32_t label_w = prompt_kind == GFM_PROMPT_CONFIRM_DELETE
            ? width - 12 - 200 : 130;
        const int32_t field_x = 12 + label_w + 6;

        lv_obj_set_width(prompt_label, label_w);
        vex_ui_set_visible(prompt_field, prompt_kind != GFM_PROMPT_CONFIRM_DELETE);
        lv_obj_set_pos(prompt_field, field_x, 7);
        lv_obj_set_size(prompt_field, width - field_x - 190, 30);
        lv_obj_set_pos(prompt_accept, width - 176, 7);
        lv_obj_set_pos(prompt_cancel, width - 86, 7);
    }

    vex_ui_set_visible(sidebar, left != 0);
    lv_obj_set_pos(sidebar, 0, top);
    lv_obj_set_size(sidebar, SIDEBAR_W, height - STATUS_H - top);

    lv_obj_set_pos(columns, left, top);
    lv_obj_set_size(columns, right - left, COLUMNS_H);
    lv_obj_set_pos(list, left, body_top);
    lv_obj_set_size(list, right - left, body_h > 0 ? body_h : 0);

    vex_ui_set_visible(info_panel, right != width);
    lv_obj_set_pos(info_panel, right, top);
    lv_obj_set_size(info_panel, INFO_W, height - STATUS_H - top);

    lv_obj_set_pos(status_bar, 0, height - STATUS_H);
    lv_obj_set_size(status_bar, width, STATUS_H);
    lv_obj_set_pos(status_label, 12, 5);
    lv_obj_set_width(status_label, width - 220);
    lv_obj_set_pos(summary_label, width - 200, 5);
    lv_obj_set_width(summary_label, 188);

    apply_row_geometry();
    if (info_shown && right == width) {
        /* Too narrow for the panel: it simply stays hidden. */
    }
}

static void layout_handler(vex_lvgl_app_t *target, const vex_ui_layout_t *layout, void *user_data) {
    (void)target;
    (void)user_data;
    view_width = layout->width;
    view_height = layout->height;
    relayout();
}

/* ── construction ────────────────────────────────────────────────────────── */

static void build_header(void) {
    static const char *const symbols[4] = {
        LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_UP, LV_SYMBOL_REFRESH
    };
    static const gfm_action_t actions[4] = {
        GFM_ACTION_BACK, GFM_ACTION_FORWARD, GFM_ACTION_UP, GFM_ACTION_REFRESH
    };
    size_t i;

    header = plain(screen);
    fill(header, COLOR_BG);
    border(header, COLOR_LINE, LV_BORDER_SIDE_BOTTOM);
    for (i = 0u; i < 4u; ++i) {
        nav_buttons[i] = make_button(header, symbols[i], actions[i], &lv_font_montserrat_14, 9);
        toolbar_buttons[actions[i]] = nav_buttons[i];
    }
    path_field = lv_textarea_create(header);
    style_text_field(path_field);
    lv_textarea_set_max_length(path_field, GFM_PATH_MAX - 1u);
    lv_textarea_set_placeholder_text(path_field, "Type a path and press Enter");
    lv_obj_add_event_cb(path_field, path_field_event, LV_EVENT_READY, 0);
}

static void build_toolbar(void) {
    static const struct {
        const char *text;
        gfm_action_t action;
    } buttons[] = {
        { LV_SYMBOL_PLUS " New folder", GFM_ACTION_NEW_FOLDER },
        { "Open", GFM_ACTION_OPEN },
        { "Rename", GFM_ACTION_RENAME },
        { LV_SYMBOL_COPY " Copy", GFM_ACTION_COPY },
        { LV_SYMBOL_CUT " Cut", GFM_ACTION_CUT },
        { LV_SYMBOL_PASTE " Paste", GFM_ACTION_PASTE },
        { LV_SYMBOL_TRASH " Delete", GFM_ACTION_DELETE },
        { "Info", GFM_ACTION_INFO },
    };
    size_t i;

    toolbar = plain(screen);
    fill(toolbar, COLOR_BG);
    border(toolbar, COLOR_LINE, LV_BORDER_SIDE_BOTTOM);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(toolbar, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(toolbar, 6, LV_PART_MAIN);
    for (i = 0u; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        toolbar_buttons[buttons[i].action] =
            make_button(toolbar, buttons[i].text, buttons[i].action, &lv_font_montserrat_12, 10);
    }
}

static void build_prompt(void) {
    prompt_bar = plain(screen);
    fill(prompt_bar, COLOR_PANEL);
    border(prompt_bar, COLOR_LINE, LV_BORDER_SIDE_BOTTOM);
    prompt_label = label(prompt_bar, "", &lv_font_montserrat_14, COLOR_TEXT);
    prompt_field = lv_textarea_create(prompt_bar);
    style_text_field(prompt_field);
    lv_textarea_set_max_length(prompt_field, GFM_NAME_LIMIT);
    lv_obj_add_event_cb(prompt_field, prompt_field_event, LV_EVENT_READY,
                        (void *)(uintptr_t)GFM_ACTION_PROMPT_ACCEPT);
    prompt_accept = make_button(prompt_bar, "OK", GFM_ACTION_PROMPT_ACCEPT, &lv_font_montserrat_14, 18);
    prompt_accept_label = lv_obj_get_child(prompt_accept, 0);
    prompt_cancel = make_button(prompt_bar, "Cancel", GFM_ACTION_PROMPT_CANCEL, &lv_font_montserrat_14, 14);
    toolbar_buttons[GFM_ACTION_PROMPT_ACCEPT] = prompt_accept;
    toolbar_buttons[GFM_ACTION_PROMPT_CANCEL] = prompt_cancel;
    vex_ui_set_visible(prompt_bar, 0);
}

static void place_clicked(lv_event_t *event) {
    const place_t *place = (const place_t *)lv_event_get_user_data(event);

    gfm_view_release_text_focus();
    if (callbacks.place_clicked != 0) {
        callbacks.place_clicked(place->path, callbacks.context);
    }
}

static void build_sidebar(void) {
    size_t i;
    lv_obj_t *title;

    sidebar = plain(screen);
    fill(sidebar, COLOR_BG);
    border(sidebar, COLOR_LINE, LV_BORDER_SIDE_RIGHT);
    title = label(sidebar, "PLACES", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_set_pos(title, 16, 12);
    for (i = 0u; i < PLACE_COUNT; ++i) {
        place_t *place = &places[i];

        place->row = plain(sidebar);
        lv_obj_set_pos(place->row, 8, 36 + (int32_t)i * 32);
        lv_obj_set_size(place->row, SIDEBAR_W - 17, 28);
        lv_obj_set_style_radius(place->row, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_color(place->row, lv_color_hex(COLOR_HOVER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(place->row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(place->row, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_HOVERED);
        lv_obj_set_style_bg_color(place->row, lv_color_hex(COLOR_SELECTED), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(place->row, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_add_flag(place->row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(place->row, place_clicked, LV_EVENT_CLICKED, place);
        place->label_object = label(place->row, place->label, &lv_font_montserrat_14, COLOR_TEXT);
        lv_obj_set_pos(place->label_object, 12, 5);
        lv_obj_set_width(place->label_object, SIDEBAR_W - 44);
    }
}

static void build_list(void) {
    static const char *const names[3] = { "Name", "Size", "Kind" };
    size_t i;

    columns = plain(screen);
    fill(columns, COLOR_BG);
    border(columns, COLOR_LINE, LV_BORDER_SIDE_BOTTOM);
    for (i = 0u; i < 3u; ++i) {
        column_labels[i] = label(columns, names[i], &lv_font_montserrat_12, COLOR_MUTED);
    }
    lv_obj_set_style_text_align(column_labels[1], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    list = plain(screen);
    fill(list, COLOR_BG);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(list, lv_color_hex(COLOR_LINE), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(list, list_pressed, LV_EVENT_PRESSED, 0);
}

static void build_info_panel(void) {
    static const char *const captions[7] = {
        "", "Kind", "Size", "Permissions", "Owner", "Location", "Contains"
    };
    size_t i;

    info_panel = plain(screen);
    fill(info_panel, COLOR_BG);
    border(info_panel, COLOR_LINE, LV_BORDER_SIDE_LEFT);
    for (i = 0u; i < 7u; ++i) {
        const int32_t y = i == 0u ? 14 : 52 + (int32_t)(i - 1u) * 44;

        if (i > 0u) {
            lv_obj_t *caption = label(info_panel, captions[i], &lv_font_montserrat_12, COLOR_MUTED);
            lv_obj_set_pos(caption, 16, y);
        }
        info_lines[i] = label(info_panel, "", i == 0u ? &lv_font_montserrat_16 : &lv_font_montserrat_14,
                              COLOR_TEXT);
        lv_obj_set_pos(info_lines[i], 16, i == 0u ? y : y + 16);
        lv_obj_set_size(info_lines[i], INFO_W - 32, i == 0u ? 22 : 18);
    }
    info_open = make_button(info_panel, "Open", GFM_ACTION_OPEN, &lv_font_montserrat_14, 18);
    lv_obj_set_pos(info_open, 16, 52 + 6 * 44);
    info_executable = make_button(info_panel, "Allow running", GFM_ACTION_TOGGLE_EXECUTABLE,
                                  &lv_font_montserrat_12, 12);
    info_executable_label = lv_obj_get_child(info_executable, 0);
    lv_obj_set_pos(info_executable, 100, 52 + 6 * 44 + 2);
    vex_ui_set_visible(info_panel, 0);
}

static void build_status(void) {
    status_bar = plain(screen);
    fill(status_bar, COLOR_BG);
    border(status_bar, COLOR_LINE, LV_BORDER_SIDE_TOP);
    status_label = label(status_bar, "", &lv_font_montserrat_12, COLOR_MUTED);
    summary_label = label(status_bar, "", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_set_style_text_align(summary_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
}

void gfm_view_build(vex_lvgl_app_t *target, const gfm_view_callbacks_t *handlers) {
    app = target;
    callbacks = *handlers;
    screen = vex_lvgl_screen(app);
    vex_ui_prepare_screen(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), LV_PART_MAIN);

    focus_sink = plain(screen);
    lv_obj_set_size(focus_sink, 1, 1);
    lv_obj_add_flag(focus_sink, LV_OBJ_FLAG_HIDDEN);
    lv_group_add_obj(app->keyboard_group, focus_sink);

    build_header();
    build_toolbar();
    build_prompt();
    build_sidebar();
    build_list();
    build_info_panel();
    build_status();

    view_width = app->width;
    view_height = app->height;
    vex_lvgl_app_set_layout_handler(app, layout_handler, 0);
    vex_lvgl_app_apply_layout(app);
    gfm_view_release_text_focus();
}

/* ── updates from the controller ─────────────────────────────────────────── */

void gfm_view_set_places(const char *path) {
    size_t best = PLACE_COUNT;
    size_t best_length = 0u;
    size_t i;

    for (i = 0u; i < PLACE_COUNT; ++i) {
        size_t length = 0u;

        while (places[i].path[length] != '\0') {
            ++length;
        }
        if (gfm_path_is_within(places[i].path, path) && length >= best_length) {
            best = i;
            best_length = length;
        }
    }
    for (i = 0u; i < PLACE_COUNT; ++i) {
        if (i == best) {
            lv_obj_add_state(places[i].row, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(places[i].row, LV_STATE_CHECKED);
        }
    }
}

void gfm_view_show_directory(const char *path, const gfm_listing_t *listing, int selected) {
    size_t i;

    for (i = 0u; i + 1u < sizeof(current_path) && path[i] != '\0'; ++i) {
        current_path[i] = path[i];
    }
    current_path[i] = '\0';
    lv_textarea_set_text(path_field, current_path);
    build_rows(listing);
    gfm_view_set_places(current_path);
    gfm_view_select(listing, selected);
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    if (selected >= 0 && (size_t)selected < row_count) {
        lv_obj_scroll_to_view(rows[selected].object, LV_ANIM_OFF);
    }
}

void gfm_view_select(const gfm_listing_t *listing, int selected) {
    (void)listing;
    if (selected_row >= 0 && (size_t)selected_row < row_count) {
        lv_obj_remove_state(rows[selected_row].object, LV_STATE_CHECKED);
    }
    selected_row = -1;
    if (selected >= 0 && (size_t)selected < row_count) {
        selected_row = selected;
        lv_obj_add_state(rows[selected].object, LV_STATE_CHECKED);
        lv_obj_scroll_to_view(rows[selected].object, LV_ANIM_OFF);
    }
}

void gfm_view_set_enabled(const gfm_view_enabled_t *state) {
    size_t i;

    enabled = *state;
    for (i = 0u; i < GFM_ACTION_COUNT; ++i) {
        if (toolbar_buttons[i] == 0) {
            continue;
        }
        if (enabled.flags[i]) {
            lv_obj_remove_state(toolbar_buttons[i], LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(toolbar_buttons[i], LV_STATE_DISABLED);
        }
    }
    /* The panel's own Open button follows the toolbar action. */
    if (enabled.flags[GFM_ACTION_OPEN]) {
        lv_obj_remove_state(info_open, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(info_open, LV_STATE_DISABLED);
    }
    if (enabled.flags[GFM_ACTION_TOGGLE_EXECUTABLE]) {
        lv_obj_remove_state(info_executable, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(info_executable, LV_STATE_DISABLED);
    }
}

void gfm_view_set_status(const char *message, int is_error) {
    lv_label_set_text(status_label, message);
    lv_obj_set_style_text_color(status_label,
        lv_color_hex(is_error ? COLOR_ERROR : COLOR_MUTED), LV_PART_MAIN);
}

void gfm_view_set_summary(const char *summary) {
    lv_label_set_text(summary_label, summary);
}

void gfm_view_show_info(const gfm_view_info_t *info) {
    const int was_visible = info_shown;

    info_shown = info != 0;
    if (info != 0) {
        lv_label_set_text(info_lines[0], info->name);
        lv_label_set_text(info_lines[1], info->kind);
        lv_label_set_text(info_lines[2], info->size);
        lv_label_set_text(info_lines[3], info->permissions);
        lv_label_set_text(info_lines[4], info->owner);
        lv_label_set_text(info_lines[5], info->location);
        lv_label_set_text(info_lines[6], info->contents);
        lv_label_set_text(info_executable_label,
                          info->executable ? "Forbid running" : "Allow running");
    }
    if (was_visible != info_shown) {
        relayout();
    }
}

int gfm_view_info_visible(void) {
    return info_shown;
}

void gfm_view_open_prompt(gfm_prompt_kind_t kind, const char *title, const char *initial_text,
                          const char *accept_label) {
    prompt_kind = kind;
    lv_label_set_text(prompt_label, title);
    lv_label_set_text(lv_obj_get_child(prompt_accept, 0), accept_label);
    lv_textarea_set_text(prompt_field, initial_text != 0 ? initial_text : "");
    relayout();
    if (kind != GFM_PROMPT_CONFIRM_DELETE) {
        lv_group_focus_obj(prompt_field);
        lv_textarea_set_cursor_pos(prompt_field, LV_TEXTAREA_CURSOR_LAST);
    } else {
        gfm_view_release_text_focus();
    }
}

void gfm_view_close_prompt(void) {
    prompt_kind = GFM_PROMPT_NONE;
    gfm_view_release_text_focus();
    relayout();
}

const char *gfm_view_prompt_text(void) {
    return lv_textarea_get_text(prompt_field);
}

gfm_prompt_kind_t gfm_view_prompt_kind(void) {
    return prompt_kind;
}

int gfm_view_text_focused(void) {
    const lv_obj_t *focused = lv_group_get_focused(app->keyboard_group);

    return focused == path_field || focused == prompt_field;
}

void gfm_view_release_text_focus(void) {
    lv_group_focus_obj(focus_sink);
}
