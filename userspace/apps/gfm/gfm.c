/*
 * GFM — Graphical File Manager of Vex-OS.
 *
 * Controller: owns the current directory, history, selection and clipboard,
 * turns user intent from gfm_view into operations of gfm_fs, and keeps the
 * view in sync with the file system (explicit refresh after every change and
 * a cheap periodic check, so files that appear while GFM is open, such as
 * browser downloads, show up without a manual refresh).
 */

#include <stddef.h>
#include <stdint.h>

#include <unos/vex_legacy_compat.h>

#include "gfm_fs.h"
#include "gfm_view.h"
#include "vex_runtime.h"

#define WINDOW_WIDTH 1080u
#define WINDOW_HEIGHT 640u
#define HISTORY_DEPTH 16u
#define DOUBLE_CLICK_MS 450u
#define POLL_INTERVAL_MS 1000u
#define TYPE_AHEAD_RESET_MS 1000u
#define DOWNLOADS_PATH "/home/root/Downloads"

#define KEY_UP ((char)0x11)
#define KEY_LEFT ((char)0x14)
#define KEY_RIGHT ((char)0x15)
#define KEY_DOWN ((char)0x17)
#define KEY_CTRL_C ((char)0x03)
#define KEY_CTRL_V ((char)0x16)
#define KEY_ESCAPE ((char)0x1b)

typedef struct clipboard {
    char path[GFM_PATH_MAX];
    int valid;
    int cut;
} clipboard_t;

static vex_lvgl_app_t gfm_app;
static gfm_listing_t listing;
static char cwd[GFM_PATH_MAX];
static char back_stack[HISTORY_DEPTH][GFM_PATH_MAX];
static size_t back_count;
static char forward_stack[HISTORY_DEPTH][GFM_PATH_MAX];
static size_t forward_count;
static int selected = -1;
static clipboard_t clipboard;
static int info_wanted;
static uint64_t last_click_ms;
static int last_click_index = -1;
static uint64_t last_poll_ms;
static char type_ahead[GFM_NAME_LIMIT];
static size_t type_ahead_length;
static uint64_t type_ahead_ms;
static int running = 1;

/* ── small helpers ───────────────────────────────────────────────────────── */

static size_t text_length(const char *text) {
    size_t length = 0u;

    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void copy_text(char *destination, size_t capacity, const char *source) {
    size_t i = 0u;

    if (capacity == 0u) {
        return;
    }
    while (source[i] != '\0' && i + 1u < capacity) {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
}

static int text_equal(const char *a, const char *b) {
    size_t i = 0u;

    while (a[i] != '\0' && a[i] == b[i]) {
        ++i;
    }
    return a[i] == b[i];
}

static void append_text(char *buffer, size_t capacity, const char *text) {
    size_t length = text_length(buffer);
    size_t i = 0u;

    while (text[i] != '\0' && length + 1u < capacity) {
        buffer[length++] = text[i++];
    }
    buffer[length] = '\0';
}

static void append_number(char *buffer, size_t capacity, uint64_t value) {
    char digits[21];
    size_t count = 0u;
    char text[22];
    size_t i;

    if (value == 0u) {
        digits[count++] = '0';
    }
    while (value != 0u) {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    for (i = 0u; i < count; ++i) {
        text[i] = digits[count - 1u - i];
    }
    text[count] = '\0';
    append_text(buffer, capacity, text);
}

static uint64_t now_ms(void) {
    struct timespec now;

    if (clock_gettime(1, &now) != 0) {
        return 0u;
    }
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static const gfm_entry_t *selected_entry(void) {
    if (selected < 0 || (size_t)selected >= listing.count) {
        return 0;
    }
    return &listing.entries[selected];
}

static void selected_path(char *out, size_t capacity) {
    const gfm_entry_t *entry = selected_entry();

    out[0] = '\0';
    if (entry != 0) {
        (void)gfm_path_join(cwd, entry->name, out, capacity);
    }
}

static void say(const char *message) {
    gfm_view_set_status(message, 0);
}

static void complain(const char *what, gfm_status_t status) {
    char text[160];

    copy_text(text, sizeof(text), what);
    append_text(text, sizeof(text), ": ");
    append_text(text, sizeof(text), gfm_status_text(status));
    gfm_view_set_status(text, 1);
}

/* ── view synchronisation ────────────────────────────────────────────────── */

static void update_summary(void) {
    char text[64];
    const gfm_entry_t *entry = selected_entry();

    text[0] = '\0';
    append_number(text, sizeof(text), listing.count);
    append_text(text, sizeof(text), listing.count == 1u ? " item" : " items");
    if (listing.truncated) {
        append_text(text, sizeof(text), "+");
    }
    if (entry != 0) {
        append_text(text, sizeof(text), " - 1 selected");
    }
    gfm_view_set_summary(text);
}

static void fill_info(gfm_view_info_t *info) {
    gfm_entry_t folder;
    const gfm_entry_t *entry = selected_entry();
    char path[GFM_PATH_MAX];
    gfm_sys_stat_t raw;
    size_t count = 0u;

    if (entry == 0) {
        /* Nothing selected: describe the folder being shown. */
        if (gfm_stat(cwd, &folder) != GFM_OK) {
            copy_text(folder.name, sizeof(folder.name), cwd);
            folder.type = GFM_TYPE_DIRECTORY;
            folder.size = 0u;
            folder.mode = 0u;
        }
        entry = &folder;
        copy_text(path, sizeof(path), cwd);
        if (text_equal(cwd, "/")) {
            copy_text(folder.name, sizeof(folder.name), "/");
        }
    } else {
        selected_path(path, sizeof(path));
    }

    copy_text(info->name, sizeof(info->name), entry->name);
    copy_text(info->kind, sizeof(info->kind),
              entry->type == GFM_TYPE_DIRECTORY ? "Folder"
              : gfm_is_transient_download(entry->name) ? "Download in progress"
              : (entry->mode & 0111u) != 0u ? "Program" : "File");
    if (entry->type == GFM_TYPE_DIRECTORY) {
        copy_text(info->size, sizeof(info->size), "-");
        (void)gfm_count_descendants(path, &count);
        info->contents[0] = '\0';
        append_number(info->contents, sizeof(info->contents), count);
        append_text(info->contents, sizeof(info->contents), count == 1u ? " item" : " items");
    } else {
        gfm_format_size(entry->size, info->size, sizeof(info->size));
        copy_text(info->contents, sizeof(info->contents), "-");
    }
    gfm_format_mode(entry->mode, info->permissions, sizeof(info->permissions));
    info->owner[0] = '\0';
    if (gfm_sys_stat(path, &raw) == 0) {
        append_text(info->owner, sizeof(info->owner), "user ");
        append_number(info->owner, sizeof(info->owner), raw.uid);
    }
    gfm_path_parent(path, info->location, sizeof(info->location));
    info->can_open = selected_entry() != 0;
    info->can_toggle_executable = selected_entry() != 0 && entry->type != GFM_TYPE_DIRECTORY;
    info->executable = entry->type != GFM_TYPE_DIRECTORY && (entry->mode & 0111u) != 0u;
}

static void update_enabled(void) {
    gfm_view_enabled_t state;
    const gfm_entry_t *entry = selected_entry();
    char parent[GFM_PATH_MAX];
    size_t i;

    for (i = 0u; i < GFM_ACTION_COUNT; ++i) {
        state.flags[i] = 1u;
    }
    gfm_path_parent(cwd, parent, sizeof(parent));
    state.flags[GFM_ACTION_BACK] = back_count > 0u;
    state.flags[GFM_ACTION_FORWARD] = forward_count > 0u;
    state.flags[GFM_ACTION_UP] = !text_equal(parent, cwd);
    state.flags[GFM_ACTION_OPEN] = entry != 0;
    state.flags[GFM_ACTION_RENAME] = entry != 0;
    state.flags[GFM_ACTION_COPY] = entry != 0;
    state.flags[GFM_ACTION_CUT] = entry != 0;
    state.flags[GFM_ACTION_DELETE] = entry != 0;
    state.flags[GFM_ACTION_PASTE] = clipboard.valid;
    state.flags[GFM_ACTION_TOGGLE_EXECUTABLE] = entry != 0 && entry->type != GFM_TYPE_DIRECTORY;
    gfm_view_set_enabled(&state);
}

static void sync_selection(void) {
    gfm_view_select(&listing, selected);
    update_summary();
    update_enabled();
    if (info_wanted) {
        gfm_view_info_t info;

        fill_info(&info);
        gfm_view_show_info(&info);
    }
}

static void show_listing(void) {
    gfm_view_show_directory(cwd, &listing, selected);
    update_summary();
    update_enabled();
    if (info_wanted) {
        gfm_view_info_t info;

        fill_info(&info);
        gfm_view_show_info(&info);
    }
}

static int find_entry(const char *name) {
    size_t i;

    for (i = 0u; i < listing.count; ++i) {
        if (text_equal(listing.entries[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

/* Reload the current directory, keeping the selection by name when possible. */
static gfm_status_t reload(const char *select_name) {
    char keep[GFM_SYS_NAME_MAX];
    gfm_status_t status;
    const gfm_entry_t *entry = selected_entry();

    keep[0] = '\0';
    if (select_name != 0) {
        copy_text(keep, sizeof(keep), select_name);
    } else if (entry != 0) {
        copy_text(keep, sizeof(keep), entry->name);
    }
    status = gfm_list(cwd, &listing);
    if (status != GFM_OK) {
        listing.count = 0u;
        selected = -1;
        show_listing();
        return status;
    }
    selected = keep[0] != '\0' ? find_entry(keep) : -1;
    show_listing();
    last_poll_ms = now_ms();
    return GFM_OK;
}

/* ── navigation ──────────────────────────────────────────────────────────── */

static void push_history(char stack[][GFM_PATH_MAX], size_t *count, const char *path) {
    size_t i;

    if (*count == HISTORY_DEPTH) {
        for (i = 1u; i < HISTORY_DEPTH; ++i) {
            copy_text(stack[i - 1u], GFM_PATH_MAX, stack[i]);
        }
        --*count;
    }
    copy_text(stack[*count], GFM_PATH_MAX, path);
    ++*count;
}

/* Open `target` (already resolved). `record` pushes the current folder on the
 * back stack and clears forward history. */
static int enter_directory(const char *target, int record) {
    static gfm_listing_t probe;
    gfm_status_t status = gfm_list(target, &probe);

    if (status != GFM_OK) {
        complain(target, status);
        gfm_view_show_directory(cwd, &listing, selected);   /* restores the path field */
        return -1;
    }
    if (record && !text_equal(cwd, target)) {
        push_history(back_stack, &back_count, cwd);
        forward_count = 0u;
    }
    copy_text(cwd, sizeof(cwd), target);
    listing = probe;
    selected = -1;
    show_listing();
    last_poll_ms = now_ms();
    {
        char text[GFM_PATH_MAX + 16];

        text[0] = '\0';
        append_text(text, sizeof(text), cwd);
        say(text);
    }
    return 0;
}

static void navigate_input(const char *input) {
    char resolved[GFM_PATH_MAX];
    gfm_status_t status = gfm_path_resolve(cwd, input, resolved, sizeof(resolved));

    if (status != GFM_OK) {
        complain(input, status);
        gfm_view_show_directory(cwd, &listing, selected);
        return;
    }
    (void)enter_directory(resolved, 1);
}

static void go_back(void) {
    char target[GFM_PATH_MAX];

    if (back_count == 0u) {
        return;
    }
    copy_text(target, sizeof(target), back_stack[back_count - 1u]);
    --back_count;
    push_history(forward_stack, &forward_count, cwd);
    if (enter_directory(target, 0) != 0) {
        /* The folder vanished: drop the entry and stay where we are. */
        --forward_count;
    }
    update_enabled();
}

static void go_forward(void) {
    char target[GFM_PATH_MAX];

    if (forward_count == 0u) {
        return;
    }
    copy_text(target, sizeof(target), forward_stack[forward_count - 1u]);
    --forward_count;
    push_history(back_stack, &back_count, cwd);
    if (enter_directory(target, 0) != 0) {
        --back_count;
    }
    update_enabled();
}

static void go_up(void) {
    char parent[GFM_PATH_MAX];
    char child[GFM_SYS_NAME_MAX];

    gfm_path_parent(cwd, parent, sizeof(parent));
    if (text_equal(parent, cwd)) {
        return;
    }
    copy_text(child, sizeof(child), gfm_path_basename(cwd));
    if (enter_directory(parent, 1) == 0) {
        selected = find_entry(child);   /* leave the folder we came from highlighted */
        sync_selection();
    }
}

static void go_place(const char *path) {
    gfm_sys_stat_t info;

    /* Downloads is where Vex-Browser saves files: make sure it can be opened
     * even before the first download created it. */
    if (text_equal(path, DOWNLOADS_PATH) && gfm_sys_stat(path, &info) != 0) {
        (void)gfm_sys_mkdir(path, 0755u);
    }
    (void)enter_directory(path, 1);
}

/* ── opening ─────────────────────────────────────────────────────────────── */

static void open_selected(void) {
    const gfm_entry_t *entry = selected_entry();
    char path[GFM_PATH_MAX];

    if (entry == 0) {
        return;
    }
    selected_path(path, sizeof(path));
    if (entry->type == GFM_TYPE_DIRECTORY) {
        (void)enter_directory(path, 1);
        return;
    }
    if (gfm_is_transient_download(entry->name)) {
        say("This file is still being downloaded.");
        return;
    }
    if ((entry->mode & 0111u) == 0u && gfm_looks_binary(path)) {
        gfm_view_set_status("No application opens this binary file. Use \"Allow running\" in Info to run it.", 1);
        return;
    }
    if (vex_open_path(path, (uint8_t)entry->type) != 0) {
        gfm_view_set_status("Could not open the file.", 1);
        return;
    }
    {
        char text[160];

        text[0] = '\0';
        append_text(text, sizeof(text), (entry->mode & 0111u) != 0u ? "Started " : "Opened ");
        append_text(text, sizeof(text), entry->name);
        say(text);
    }
}

/* ── operations ──────────────────────────────────────────────────────────── */

static void begin_new_folder(void) {
    char name[GFM_SYS_NAME_MAX];
    unsigned n = 1u;

    copy_text(name, sizeof(name), "New folder");
    while (gfm_name_exists(cwd, name) && n < 100u) {
        ++n;
        copy_text(name, sizeof(name), "New folder ");
        append_number(name, sizeof(name), n);
    }
    gfm_view_open_prompt(GFM_PROMPT_NEW_FOLDER, "Folder name", name, "Create");
}

static void begin_rename(void) {
    const gfm_entry_t *entry = selected_entry();

    if (entry != 0) {
        gfm_view_open_prompt(GFM_PROMPT_RENAME, "New name", entry->name, "Rename");
    }
}

static void begin_delete(void) {
    const gfm_entry_t *entry = selected_entry();
    char title[160];
    char path[GFM_PATH_MAX];

    if (entry == 0) {
        return;
    }
    title[0] = '\0';
    append_text(title, sizeof(title), "Delete \"");
    append_text(title, sizeof(title), entry->name);
    append_text(title, sizeof(title), "\"");
    if (entry->type == GFM_TYPE_DIRECTORY) {
        size_t count = 0u;

        selected_path(path, sizeof(path));
        if (gfm_count_descendants(path, &count) == GFM_OK && count > 0u) {
            append_text(title, sizeof(title), " and its ");
            append_number(title, sizeof(title), count);
            append_text(title, sizeof(title), count == 1u ? " item" : " items");
        }
    }
    append_text(title, sizeof(title), "? This cannot be undone.");
    gfm_view_open_prompt(GFM_PROMPT_CONFIRM_DELETE, title, "", "Delete");
}

static void accept_prompt(void) {
    const gfm_prompt_kind_t kind = gfm_view_prompt_kind();
    char text[GFM_SYS_NAME_MAX];
    char result[GFM_PATH_MAX];
    char path[GFM_PATH_MAX];
    gfm_status_t status = GFM_OK;

    copy_text(text, sizeof(text), gfm_view_prompt_text());
    selected_path(path, sizeof(path));
    result[0] = '\0';
    if (kind == GFM_PROMPT_NEW_FOLDER) {
        status = gfm_make_directory(cwd, text, result, sizeof(result));
    } else if (kind == GFM_PROMPT_RENAME) {
        status = gfm_rename(path, text, result, sizeof(result));
    } else if (kind == GFM_PROMPT_CONFIRM_DELETE) {
        status = gfm_delete(path);
    } else {
        return;
    }
    if (status != GFM_OK) {
        /* Keep the prompt open so the name can be corrected. */
        complain(kind == GFM_PROMPT_NEW_FOLDER ? "Cannot create folder"
                 : kind == GFM_PROMPT_RENAME ? "Cannot rename" : "Cannot delete", status);
        if (kind == GFM_PROMPT_CONFIRM_DELETE) {
            gfm_view_close_prompt();
            (void)reload(0);
        }
        return;
    }
    gfm_view_close_prompt();
    (void)reload(kind == GFM_PROMPT_CONFIRM_DELETE ? "" : gfm_path_basename(result));
    say(kind == GFM_PROMPT_NEW_FOLDER ? "Folder created."
        : kind == GFM_PROMPT_RENAME ? "Renamed." : "Deleted.");
}

static void remember_for_paste(int cut) {
    const gfm_entry_t *entry = selected_entry();

    if (entry == 0) {
        return;
    }
    selected_path(clipboard.path, sizeof(clipboard.path));
    clipboard.valid = 1;
    clipboard.cut = cut;
    update_enabled();
    {
        char text[160];

        text[0] = '\0';
        append_text(text, sizeof(text), cut ? "Cut \"" : "Copied \"");
        append_text(text, sizeof(text), entry->name);
        append_text(text, sizeof(text), "\" - open the destination and choose Paste.");
        say(text);
    }
}

static void paste(void) {
    char result[GFM_PATH_MAX];
    gfm_status_t status;

    if (!clipboard.valid) {
        return;
    }
    status = clipboard.cut ? gfm_move(clipboard.path, cwd, result, sizeof(result))
                           : gfm_copy(clipboard.path, cwd, result, sizeof(result));
    if (status != GFM_OK) {
        complain(clipboard.cut ? "Cannot move" : "Cannot copy", status);
        return;
    }
    if (clipboard.cut) {
        clipboard.valid = 0;
    }
    (void)reload(gfm_path_basename(result));
    say(clipboard.cut ? "Moved." : "Pasted.");
    update_enabled();
}

static void toggle_executable(void) {
    const gfm_entry_t *entry = selected_entry();
    char path[GFM_PATH_MAX];
    gfm_status_t status;

    if (entry == 0 || entry->type == GFM_TYPE_DIRECTORY) {
        return;
    }
    selected_path(path, sizeof(path));
    status = gfm_set_executable(path, (entry->mode & 0111u) == 0u);
    if (status != GFM_OK) {
        complain("Cannot change permissions", status);
        return;
    }
    (void)reload(0);
    say("Permissions updated.");
}

static void toggle_info(void) {
    info_wanted = !info_wanted;
    if (info_wanted) {
        gfm_view_info_t info;

        fill_info(&info);
        gfm_view_show_info(&info);
    } else {
        gfm_view_show_info(0);
    }
}

/* ── view callbacks ──────────────────────────────────────────────────────── */

static void on_action(gfm_action_t action, void *context) {
    (void)context;
    switch (action) {
    case GFM_ACTION_BACK: go_back(); break;
    case GFM_ACTION_FORWARD: go_forward(); break;
    case GFM_ACTION_UP: go_up(); break;
    case GFM_ACTION_REFRESH:
        if (reload(0) == GFM_OK) {
            say("Refreshed.");
        } else {
            gfm_view_set_status("This folder is no longer available.", 1);
        }
        break;
    case GFM_ACTION_NEW_FOLDER: begin_new_folder(); break;
    case GFM_ACTION_RENAME: begin_rename(); break;
    case GFM_ACTION_COPY: remember_for_paste(0); break;
    case GFM_ACTION_CUT: remember_for_paste(1); break;
    case GFM_ACTION_PASTE: paste(); break;
    case GFM_ACTION_DELETE: begin_delete(); break;
    case GFM_ACTION_OPEN: open_selected(); break;
    case GFM_ACTION_INFO: toggle_info(); break;
    case GFM_ACTION_TOGGLE_EXECUTABLE: toggle_executable(); break;
    case GFM_ACTION_PROMPT_ACCEPT: accept_prompt(); break;
    case GFM_ACTION_PROMPT_CANCEL:
        gfm_view_close_prompt();
        say("Cancelled.");
        break;
    case GFM_ACTION_COUNT: break;
    }
}

static void on_row_clicked(size_t index, void *context) {
    const uint64_t now = now_ms();

    (void)context;
    if (index >= listing.count) {
        return;
    }
    if ((int)index == last_click_index && now - last_click_ms <= DOUBLE_CLICK_MS) {
        selected = (int)index;
        sync_selection();
        last_click_index = -1;
        open_selected();
        return;
    }
    last_click_index = (int)index;
    last_click_ms = now;
    selected = (int)index;
    sync_selection();
}

static void on_place_clicked(const char *path, void *context) {
    (void)context;
    go_place(path);
}

static void on_path_submitted(const char *text, void *context) {
    (void)context;
    gfm_view_release_text_focus();
    navigate_input(text);
}

/* ── keyboard ────────────────────────────────────────────────────────────── */

static void move_selection(int delta) {
    int next;

    if (listing.count == 0u) {
        return;
    }
    next = selected < 0 ? (delta > 0 ? 0 : (int)listing.count - 1) : selected + delta;
    if (next < 0) {
        next = 0;
    }
    if ((size_t)next >= listing.count) {
        next = (int)listing.count - 1;
    }
    selected = next;
    sync_selection();
}

/* Typing a few letters jumps to the first entry with that prefix. */
static void jump_to_prefix(char key) {
    const uint64_t now = now_ms();
    size_t i;
    size_t j;

    if (now - type_ahead_ms > TYPE_AHEAD_RESET_MS) {
        type_ahead_length = 0u;
    }
    type_ahead_ms = now;
    if (type_ahead_length + 1u < sizeof(type_ahead)) {
        type_ahead[type_ahead_length++] = lower(key);
        type_ahead[type_ahead_length] = '\0';
    }
    for (i = 0u; i < listing.count; ++i) {
        const char *name = listing.entries[i].name;

        for (j = 0u; j < type_ahead_length && name[j] != '\0' && lower(name[j]) == type_ahead[j]; ++j) {
        }
        if (j == type_ahead_length) {
            selected = (int)i;
            sync_selection();
            return;
        }
    }
}

static void handle_key(char key) {
    const gfm_prompt_kind_t prompt = gfm_view_prompt_kind();

    if (key == KEY_ESCAPE) {
        if (prompt != GFM_PROMPT_NONE) {
            on_action(GFM_ACTION_PROMPT_CANCEL, 0);
        } else {
            gfm_view_release_text_focus();
        }
        return;
    }
    if (prompt == GFM_PROMPT_CONFIRM_DELETE) {
        if (key == '\n' || key == '\r') {
            accept_prompt();
        }
        return;
    }
    if (key == KEY_UP) {
        move_selection(-1);
    } else if (key == KEY_DOWN) {
        move_selection(1);
    } else if (key == KEY_LEFT) {
        go_back();
    } else if (key == KEY_RIGHT) {
        go_forward();
    } else if (key == '\n' || key == '\r') {
        open_selected();
    } else if (key == '\b' || key == 127) {
        go_up();
    } else if (key == KEY_CTRL_C) {
        remember_for_paste(0);
    } else if (key == KEY_CTRL_V) {
        paste();
    } else if ((unsigned char)key >= 0x20u && (unsigned char)key < 0x7fu) {
        jump_to_prefix(key);
    }
}

/* ── main loop ───────────────────────────────────────────────────────────── */

static void poll_directory(void) {
    static gfm_listing_t probe;
    const uint64_t now = now_ms();

    if (now - last_poll_ms < POLL_INTERVAL_MS || gfm_view_text_focused()) {
        return;
    }
    last_poll_ms = now;
    if (gfm_list(cwd, &probe) != GFM_OK) {
        if (listing.count != 0u) {
            listing.count = 0u;
            selected = -1;
            show_listing();
            gfm_view_set_status("This folder is no longer available.", 1);
        }
        return;
    }
    if (probe.fingerprint != listing.fingerprint || probe.count != listing.count) {
        (void)reload(0);
    }
}

static void choose_initial_path(char *out, size_t capacity) {
    char context[GFM_PATH_MAX];
    gfm_listing_t *probe = &listing;

    if (vex_open_context(VEX_OPEN_CONTEXT_FILES, context, sizeof(context)) == 0 &&
        gfm_path_resolve("/", context, out, capacity) == GFM_OK &&
        gfm_list(out, probe) == GFM_OK) {
        return;
    }
    copy_text(out, capacity, "/home/root");
    if (gfm_list(out, probe) != GFM_OK) {
        copy_text(out, capacity, "/");
    }
}

int main(void) {
    static const gfm_view_callbacks_t callbacks = {
        on_action, on_row_clicked, on_place_clicked, on_path_submitted, 0
    };
    vex_app_window_event_t event;

    choose_initial_path(cwd, sizeof(cwd));
    if (vex_lvgl_app_open(&gfm_app, "GFM", WINDOW_WIDTH, WINDOW_HEIGHT,
                          VEX_APP_WINDOW_FLAG_RESIZABLE) != 0) {
        return 1;
    }
    gfm_view_build(&gfm_app, &callbacks);
    (void)reload(0);
    say("GFM - double-click a folder to open it.");
    vex_lvgl_app_render(&gfm_app);

    while (running) {
        if (vex_app_window_poll(&gfm_app.window, &event) == 0 && event.type != VEX_APP_EVENT_NONE) {
            if (event.type == VEX_APP_EVENT_CLOSE) {
                break;
            }
            if (event.type == VEX_APP_EVENT_KEY && event.key != 0 && !gfm_view_text_focused()) {
                handle_key(event.key);
                vex_lvgl_app_render(&gfm_app);
            } else if (event.type == VEX_APP_EVENT_KEY && event.key == KEY_ESCAPE) {
                handle_key(event.key);
                vex_lvgl_app_render(&gfm_app);
            } else if (event.type == VEX_APP_EVENT_CONFIGURE) {
                const int accepted = vex_lvgl_app_handle_event(&gfm_app, &event);
                if (accepted) {
                    vex_lvgl_app_render(&gfm_app);
                }
            } else {
                (void)vex_lvgl_app_handle_event(&gfm_app, &event);
            }
            continue;
        }
        poll_directory();
        (void)vex_syscall1(VEX_SYS_USLEEP, 8000u);
    }
    vex_lvgl_app_close(&gfm_app);
    return 0;
}
