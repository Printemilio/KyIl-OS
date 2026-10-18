#include <unos/file_manager_ui.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/fs.h>
#include <unos/gui.h>
#include <unos/ipc.h>
#include <unos/scheduler.h>
#include <unos/tty.h>
#include <unos/users.h>

#define FM_MAX_ITEMS 48u
#define FM_LABEL_MAX 40u
#define FM_PATH_MAX 128u
#define FM_SECTION_COUNT 5u
#define FILE_MANAGER_PROCESS_PRIORITY 1u

typedef struct file_manager_item {
    char label[FM_LABEL_MAX];
    char path[FM_PATH_MAX];
    size_t size;
    uint8_t is_dir;
} file_manager_item_t;

typedef struct file_manager_section {
    const char *name;
    const char *path;
} file_manager_section_t;

static file_manager_item_t items[FM_MAX_ITEMS];
static size_t item_count;
static int selected_index;
static char current_path[FM_PATH_MAX];
static char status_text[96];
static char pending_open_path[FM_PATH_MAX];
static uint64_t current_origin_x;
static uint64_t current_origin_y;
static uint64_t current_width;
static uint64_t current_height;
static uint32_t file_manager_pid_value;
static volatile uint8_t file_manager_command_woken;
static int file_manager_command_pipe = -1;
static int file_manager_event_pipe = -1;
static uint8_t file_manager_active;
static uint8_t file_manager_dirty;

static const file_manager_section_t sections[FM_SECTION_COUNT] = {
    { "Root", "/" },
    { "Home", "/home" },
    { "Desktop", "/home/root/Desktop" },
    { "Temp", "/tmp" },
    { "System", "/sys" },
};

static size_t string_length(const char *str) {
    size_t length = 0;

    while (str != NULL && str[length] != '\0') {
        ++length;
    }

    return length;
}

static int string_equals(const char *left, const char *right) {
    size_t i = 0;

    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }

    return left[i] == right[i];
}

static int starts_with(const char *str, const char *prefix) {
    size_t i = 0;

    while (prefix[i] != '\0') {
        if (str == NULL || str[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }

    return 1;
}

static void copy_string(char *dst, size_t capacity, const char *src) {
    size_t i = 0;

    if (capacity == 0) {
        return;
    }

    while (src != NULL && src[i] != '\0' && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';
}

static void append_string(char *dst, size_t capacity, const char *src) {
    size_t length = string_length(dst);
    size_t i = 0;

    while (src != NULL && src[i] != '\0' && length + 1u < capacity) {
        dst[length++] = src[i++];
    }

    dst[length] = '\0';
}

static void set_status(const char *prefix, const char *value) {
    copy_string(status_text, sizeof(status_text), prefix);
    append_string(status_text, sizeof(status_text), value);
}

static int item_exists(const char *path, uint8_t is_dir) {
    for (size_t i = 0; i < item_count; ++i) {
        if (items[i].is_dir == is_dir && string_equals(items[i].path, path)) {
            return 1;
        }
    }

    return 0;
}

static void add_item(const char *label, const char *path, size_t size, uint8_t is_dir) {
    if (item_count >= FM_MAX_ITEMS || item_exists(path, is_dir)) {
        return;
    }

    copy_string(items[item_count].label, sizeof(items[item_count].label), label);
    copy_string(items[item_count].path, sizeof(items[item_count].path), path);
    items[item_count].size = size;
    items[item_count].is_dir = is_dir;
    ++item_count;
}

static void push_parent_if_needed(void) {
    const char *last_slash = NULL;

    if (string_equals(current_path, "/")) {
        return;
    }

    for (size_t i = 0; current_path[i] != '\0'; ++i) {
        if (current_path[i] == '/') {
            last_slash = current_path + i;
        }
    }

    if (last_slash == NULL || last_slash == current_path) {
        add_item("..", "/", 0u, 1u);
        return;
    }

    {
        char parent[FM_PATH_MAX];
        const size_t parent_len = (size_t)(last_slash - current_path);

        copy_string(parent, sizeof(parent), current_path);
        parent[parent_len] = '\0';
        add_item("..", parent, 0u, 1u);
    }
}

static void append_path(char *dst, size_t capacity, const char *root, const char *segment) {
    copy_string(dst, capacity, root);
    if (!string_equals(root, "/")) {
        append_string(dst, capacity, "/");
    }
    append_string(dst, capacity, segment);
}

static void add_child_from_path(const char *path, size_t size) {
    const size_t root_len = string_length(current_path);
    const char *rest = path;

    if (!starts_with(path, current_path)) {
        return;
    }

    if (string_equals(current_path, "/")) {
        rest = path[0] == '/' ? path + 1u : path;
    } else {
        if (path[root_len] == '/') {
            rest = path + root_len + 1u;
        } else if (path[root_len] == '\0') {
            return;
        } else {
            return;
        }
    }

    if (rest[0] == '\0') {
        return;
    }

    {
        size_t segment_len = 0;

        while (rest[segment_len] != '\0' && rest[segment_len] != '/') {
            ++segment_len;
        }

        if (segment_len == 0u) {
            return;
        }

        {
            char label[FM_LABEL_MAX];
            char child_path[FM_PATH_MAX];
            size_t copy_len = segment_len;

            if (copy_len + 1u > sizeof(label)) {
                copy_len = sizeof(label) - 1u;
            }

            for (size_t i = 0; i < copy_len; ++i) {
                label[i] = rest[i];
            }
            label[copy_len] = '\0';

            if (label[0] == '.') {
                return;
            }

            append_path(child_path, sizeof(child_path), current_path, label);
            add_item(label, child_path, size, rest[segment_len] == '/');
        }
    }
}

static void collect_item(const char *path, size_t size) {
    add_child_from_path(path, size);
}

static int section_index_for_current_path(void) {
    int best = 0;
    size_t best_len = 0;

    for (int i = 0; i < (int)FM_SECTION_COUNT; ++i) {
        const size_t length = string_length(sections[i].path);
        if (starts_with(current_path, sections[i].path) && length >= best_len) {
            best = i;
            best_len = length;
        }
    }

    return best;
}

static void refresh_items(void) {
    item_count = 0;
    selected_index = -1;
    push_parent_if_needed();
    fs_list(collect_item);

    if (item_count == 0) {
        set_status("Empty in ", current_path);
    } else {
        set_status("Folder ", current_path);
    }
}

static void format_size(char *out, size_t capacity, size_t value) {
    char digits[24];
    size_t count = 0;

    if (capacity == 0) {
        return;
    }

    if (value == 0u) {
        copy_string(out, capacity, "0 B");
        return;
    }

    while (value > 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    {
        size_t pos = 0;
        while (count > 0u && pos + 1u < capacity) {
            out[pos++] = digits[--count];
        }
        if (pos + 3u < capacity) {
            out[pos++] = ' ';
            out[pos++] = 'B';
        }
        out[pos] = '\0';
    }
}

/* Figma Frame 21 palette */
#define FM_BG       gui_color_rgb(20, 20, 20)    /* card body #141414 */
#define FM_VIOLET   gui_color_rgb(59, 43, 90)    /* path bar */
#define FM_SELECT   gui_color_rgb(86, 43, 90)    /* selected row */
#define FM_LINE     gui_color_rgb(72, 49, 86)    /* tree lines / dividers */
#define FM_GOLD     gui_color_rgb(201, 153, 20)  /* folder icon */
#define FM_PAPER    gui_color_rgb(217, 217, 217) /* file icon */

static void draw_folder_icon(uint64_t x, uint64_t y, uint8_t selected) {
    const uint32_t fill = selected ? gui_color_rgb(228, 178, 40) : FM_GOLD;

    /* Folder: tab on top-left + rounded body (Figma gold folder) */
    gui_fill_rounded_rect(x, y, 16u, 8u, 3u, fill);          /* tab */
    gui_fill_rounded_rect(x, y + 5u, 40u, 26u, 4u, fill);    /* body */
}

static void draw_file_icon(uint64_t x, uint64_t y, uint8_t selected) {
    const uint32_t fill = selected ? gui_color_rgb(255, 255, 255) : FM_PAPER;
    const uint32_t line = gui_color_rgb(120, 120, 120);

    /* Document sheet with text lines (Figma light-gray file) */
    gui_fill_rounded_rect(x + 6u, y, 26u, 32u, 3u, fill);
    gui_fill_rect(x + 11u, y + 8u,  16u, 2u, line);
    gui_fill_rect(x + 11u, y + 14u, 16u, 2u, line);
    gui_fill_rect(x + 11u, y + 20u, 12u, 2u, line);
}

static void draw_sidebar(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    const uint32_t text  = gui_color_rgb(255, 255, 255);
    const uint32_t muted = gui_color_rgb(176, 168, 192);
    const int active = section_index_for_current_path();

    gui_fill_rect(x, y, w, h, FM_BG);
    gui_fill_rect(x + w - 1u, y, 1u, h, FM_LINE);  /* vertical divider */

    for (int i = 0; i < (int)FM_SECTION_COUNT; ++i) {
        const uint64_t indent = 10u + (uint64_t)i * 22u;  /* tree indentation */
        const uint64_t item_y = y + 16u + (uint64_t)i * 26u;
        const uint32_t row_text = i == active ? text : muted;

        if (i == active) {
            gui_fill_rounded_rect(x + indent - 4u, item_y - 4u, w - indent - 8u, 24u, 6u, FM_SELECT);
        }
        if (i > 0) {
            /* tree connector lines (Figma Line 63/64) */
            gui_fill_rect(x + indent - 12u, item_y - 14u, 1u, 20u, FM_LINE);
            gui_fill_rect(x + indent - 12u, item_y + 6u, 8u, 1u, FM_LINE);
        }
        gui_font_text(x + indent, item_y - 2u, sections[i].name, row_text, 16u);
    }
}

static void draw_toolbar(uint64_t x, uint64_t y, uint64_t w) {
    const uint32_t text = gui_color_rgb(255, 255, 255);

    /* Figma path bar: violet rounded bar with white path */
    gui_fill_rect(x, y, w, 34u, FM_BG);
    gui_fill_rounded_rect(x + 8u, y + 4u, w - 20u, 29u, 8u, FM_VIOLET);
    gui_font_text(x + 22u, y + 10u, current_path, text, 16u);
}

static void draw_empty_state(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    const uint32_t text = gui_color_rgb(176, 168, 192);

    gui_fill_rect(x, y, w, h, FM_BG);
    gui_font_text(x + 24u, y + 28u, "No items", text, 13u);
}

static void draw_items(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    const uint32_t bg   = FM_BG;
    const uint32_t text = gui_color_rgb(255, 255, 255);
    const uint32_t muted = gui_color_rgb(176, 168, 192);
    const uint64_t tile_w = 112u;
    const uint64_t tile_h = 74u;
    const uint64_t gap = 12u;
    const uint64_t columns = w >= tile_w ? (w + gap) / (tile_w + gap) : 1u;

    gui_fill_rect(x, y, w, h, bg);

    if (item_count == 0u) {
        draw_empty_state(x, y, w, h);
        return;
    }

    for (size_t i = 0; i < item_count; ++i) {
        const uint64_t col = i % columns;
        const uint64_t row = i / columns;
        const uint64_t tile_x = x + col * (tile_w + gap);
        const uint64_t tile_y = y + row * (tile_h + gap);
        const uint8_t selected = (int)i == selected_index;
        char meta[24];

        if (tile_x + tile_w > x + w || tile_y + tile_h > y + h) {
            continue;
        }

        /* Figma: icons float on the card — only the selection gets a chip */
        if (selected) {
            gui_fill_rounded_rect(tile_x, tile_y, tile_w, tile_h, 8u, FM_SELECT);
        }
        if (items[i].is_dir) {
            draw_folder_icon(tile_x + (tile_w - 40u) / 2u, tile_y + 8u, selected);
            copy_string(meta, sizeof(meta), "Folder");
        } else {
            draw_file_icon(tile_x + (tile_w - 38u) / 2u, tile_y + 6u, selected);
            format_size(meta, sizeof(meta), items[i].size);
        }

        /* Label centred under the icon */
        {
            const uint64_t lbl_w = gui_font_text_width(items[i].label, 13u);
            const uint64_t lbl_x = lbl_w < tile_w ? tile_x + (tile_w - lbl_w) / 2u : tile_x;
            gui_font_text(lbl_x, tile_y + 44u, items[i].label, text, 13u);
            (void)muted; (void)meta;
        }
    }
}

static void draw_status_bar(uint64_t x, uint64_t y, uint64_t w) {
    const uint32_t text = gui_color_rgb(176, 168, 192);

    gui_fill_rect(x, y, w, 24u, FM_BG);
    gui_fill_rect(x, y, w, 1u, FM_LINE);
    gui_font_text(x + 14u, y + 5u, status_text, text, 13u);
}

void file_manager_ui_open(void) {
    char home[FM_PATH_MAX];

    copy_string(home, sizeof(home), "/home/");
    append_string(home, sizeof(home), user_current_username());
    copy_string(current_path, sizeof(current_path), home);
    copy_string(status_text, sizeof(status_text), "Explorer ready");
    pending_open_path[0] = '\0';
    refresh_items();
}

void file_manager_ui_open_path(const char *path) {
    if (path == NULL || path[0] == '\0') {
        file_manager_ui_open();
        return;
    }
    copy_string(current_path, sizeof(current_path), path);
    copy_string(status_text, sizeof(status_text), "Explorer ready");
    pending_open_path[0] = '\0';
    refresh_items();
}

void file_manager_ui_render(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    const uint64_t sidebar_w = w > 180u ? 156u : w / 3u;
    const uint64_t toolbar_h = 34u;
    const uint64_t status_h = 24u;
    const uint64_t content_x = x + sidebar_w;
    const uint64_t content_y = y + toolbar_h;
    const uint64_t content_w = w - sidebar_w;
    const uint64_t content_h = h - toolbar_h - status_h;

    current_origin_x = x;
    current_origin_y = y;
    current_width = w;
    current_height = h;

    gui_fill_rect(x, y, w, h, FM_BG);   /* Figma card body #141414 */
    draw_sidebar(x, y, sidebar_w, h);
    draw_toolbar(content_x, y, content_w);
    draw_items(content_x + 12u, content_y + 12u, content_w - 24u, content_h - 24u);
    draw_status_bar(content_x, y + h - status_h, content_w);
    gui_redraw_cursor_overlay();
}

int file_manager_ui_handle_click(uint64_t x, uint64_t y, uint64_t w, uint64_t h) {
    const uint64_t sidebar_w = w > 180u ? 156u : w / 3u;
    const uint64_t toolbar_h = 34u;
    const uint64_t content_x = current_origin_x + sidebar_w + 12u;
    const uint64_t content_y = current_origin_y + toolbar_h + 12u;
    const uint64_t content_w = w - sidebar_w - 24u;
    const uint64_t tile_w = 112u;
    const uint64_t tile_h = 74u;
    const uint64_t gap = 12u;
    const uint64_t columns = content_w >= tile_w ? (content_w + gap) / (tile_w + gap) : 1u;

    (void)h;

    if (current_path[0] == '\0') {
        file_manager_ui_open();
    }

    if (x < current_origin_x || y < current_origin_y || x >= current_origin_x + current_width || y >= current_origin_y + current_height) {
        return 0;
    }

    if (x < current_origin_x + sidebar_w) {
        for (int i = 0; i < (int)FM_SECTION_COUNT; ++i) {
            const uint64_t item_y = current_origin_y + 38u + (uint64_t)i * 28u;
            if (y >= item_y - 6u && y < item_y + 16u) {
                copy_string(current_path, sizeof(current_path), sections[i].path);
                selected_index = -1;
                refresh_items();
                return 1;
            }
        }
        return 0;
    }

    if (y < content_y) {
        if (x >= current_origin_x + sidebar_w + 12u && x < current_origin_x + sidebar_w + 38u) {
            size_t len = string_length(current_path);

            while (len > 1u && current_path[len - 1u] != '/') {
                current_path[--len] = '\0';
            }
            if (len > 1u) {
                current_path[len - 1u] = '\0';
            } else {
                copy_string(current_path, sizeof(current_path), "/");
            }
            refresh_items();
            return 1;
        }
        return 0;
    }

    for (size_t i = 0; i < item_count; ++i) {
        const uint64_t col = i % columns;
        const uint64_t row = i / columns;
        const uint64_t tile_x = content_x + col * (tile_w + gap);
        const uint64_t tile_y = content_y + row * (tile_h + gap);

        if (x >= tile_x && x < tile_x + tile_w && y >= tile_y && y < tile_y + tile_h) {
            selected_index = (int)i;
            if (items[i].is_dir) {
                copy_string(current_path, sizeof(current_path), items[i].path);
                refresh_items();
            } else {
                copy_string(pending_open_path, sizeof(pending_open_path), items[i].path);
                set_status("Ouverture ", items[i].path);
            }
            return 1;
        }
    }

    return 0;
}

const char *file_manager_ui_take_open_request(void) {
    static char path[FM_PATH_MAX];

    if (pending_open_path[0] == '\0') {
        return NULL;
    }

    copy_string(path, sizeof(path), pending_open_path);
    pending_open_path[0] = '\0';
    return path;
}

static void file_manager_send_event_open(const char *path) {
    gui_app_event_t event;

    if (file_manager_event_pipe < 0 || path == NULL || path[0] == '\0') {
        return;
    }

    event.type = GUI_APP_EVENT_OPEN_FILE;
    copy_string(event.path, sizeof(event.path), path);
    if (pipe_write(file_manager_event_pipe, &event, sizeof(event)) == (int)sizeof(event)) {
        gui_wake();
    }
}

static void file_manager_process_loop(void) {
    gui_app_command_t command;

    file_manager_dirty = 1u;
    file_manager_active = 0u;

    for (;;) {
        file_manager_command_woken = 0u;
        while (pipe_read(file_manager_command_pipe, &command, sizeof(command)) == (int)sizeof(command)) {
            if (command.type == GUI_APP_CMD_CONFIGURE) {
                current_origin_x = command.x;
                current_origin_y = command.y;
                current_width = command.w;
                current_height = command.h;
                file_manager_active = (uint8_t)(command.flags != 0u);
                if (current_path[0] == '\0') {
                    file_manager_ui_open();
                }
                file_manager_dirty = 1u;
                continue;
            }

            if (command.type == GUI_APP_CMD_REFRESH) {
                file_manager_dirty = 1u;
                continue;
            }

            if (!file_manager_active) {
                continue;
            }

            if (command.type == GUI_APP_CMD_MOUSE_CLICK) {
                if (file_manager_ui_handle_click(command.x, command.y, current_width, current_height)) {
                    const char *open_path = file_manager_ui_take_open_request();
                    if (open_path != NULL) {
                        file_manager_send_event_open(open_path);
                    }
                    file_manager_dirty = 1u;
                }
            }
        }

        if (file_manager_active && file_manager_dirty && gui_terminal_surface_visible() &&
            current_width > 0u && current_height > 0u) {
            file_manager_ui_render(current_origin_x, current_origin_y, current_width, current_height);
            file_manager_dirty = 0u;
        }

        process_wait_event(&file_manager_command_woken);
    }
}

uint32_t file_manager_app_start(void) {
    if (file_manager_pid_value != 0u) {
        return file_manager_pid_value;
    }

    file_manager_command_pipe = pipe_create();
    file_manager_event_pipe = pipe_create();
    if (file_manager_command_pipe < 0 || file_manager_event_pipe < 0) {
        if (file_manager_command_pipe >= 0) {
            pipe_close(file_manager_command_pipe);
            file_manager_command_pipe = -1;
        }
        if (file_manager_event_pipe >= 0) {
            pipe_close(file_manager_event_pipe);
            file_manager_event_pipe = -1;
        }
        return 0;
    }

    file_manager_pid_value = process_create(file_manager_process_loop, FILE_MANAGER_PROCESS_PRIORITY);
    if (file_manager_pid_value == 0u) {
        pipe_close(file_manager_command_pipe);
        pipe_close(file_manager_event_pipe);
        file_manager_command_pipe = -1;
        file_manager_event_pipe = -1;
    }
    return file_manager_pid_value;
}

uint32_t file_manager_app_pid(void) {
    return file_manager_pid_value;
}

void file_manager_app_send(const gui_app_command_t *command) {
    if (command == NULL || file_manager_command_pipe < 0) {
        return;
    }

    if (pipe_write(file_manager_command_pipe, command, sizeof(*command)) == (int)sizeof(*command)) {
        file_manager_command_woken = 1u;
        if (file_manager_pid_value != 0u) process_wake(file_manager_pid_value);
    }
}

int file_manager_app_poll_event(gui_app_event_t *event) {
    if (event == NULL || file_manager_event_pipe < 0) {
        return 0;
    }

    return pipe_read(file_manager_event_pipe, event, sizeof(*event)) == (int)sizeof(*event);
}
