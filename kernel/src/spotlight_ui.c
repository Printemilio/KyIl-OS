#include <unos/spotlight_ui.h>

#include <stddef.h>
#include <stdint.h>
#include <unos/elf.h>
#include <unos/fs.h>
#include <unos/gui.h>
#include <unos/ipc.h>
#include <unos/keyboard.h>
#include <unos/plex_mono_10.h>
#include <unos/plex_mono_12.h>
#include <unos/scheduler.h>
#include <unos/shell.h>
#include <unos/spotlight.h>
#include <unos/tty.h>
#include <unos/users.h>
#include <unos/voice.h>

#define SPOTLIGHT_UI_QUERY_MAX 64u
#define SPOTLIGHT_UI_MAX_RESULTS 6u
#define SPOTLIGHT_UI_LINE_HEIGHT 46ull
#define SPOTLIGHT_UI_TEXT_XPAD 16ull
#define SPOTLIGHT_PROCESS_PRIORITY 1u
#define SPOTLIGHT_TEXT_CELL_WIDTH 8ull
#define SPOTLIGHT_TEXT_CELL_HEIGHT 16ull
#define SPOTLIGHT_BAR_HEIGHT 52ull
#define SPOTLIGHT_PANEL_GAP 14ull
#define SPOTLIGHT_PANEL_PAD 14ull
#define SPOTLIGHT_ANIM_STEPS 8u

typedef enum { ANIM_NONE = 0, ANIM_OPENING, ANIM_OPEN, ANIM_CLOSING } spotlight_anim_state_t;

static int spotlight_open;
static char spotlight_query_text[SPOTLIGHT_UI_QUERY_MAX];
static size_t spotlight_query_length;
static uint64_t overlay_x;
static uint64_t overlay_y;
static uint64_t overlay_w;
static uint64_t overlay_h;
static uint64_t previous_overlay_x;
static uint64_t previous_overlay_y;
static uint64_t previous_overlay_w;
static uint64_t previous_overlay_h;
static int spotlight_backdrop_ready;
static int shell_return_pending;
static int confirmation_pending;
static size_t selected_result_index;
static size_t visible_result_count;
static uint32_t spotlight_pid_value;
static int spotlight_command_pipe = -1;
static spotlight_anim_state_t spotlight_anim_state;
static uint64_t spotlight_anim_step;

typedef struct spotlight_rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} spotlight_rgb_t;

#define SPOTLIGHT_RGB(r, g, b) ((spotlight_rgb_t){ (uint8_t)(r), (uint8_t)(g), (uint8_t)(b) })

static size_t string_length(const char *str) {
    size_t length = 0;

    while (str != NULL && str[length] != '\0') {
        ++length;
    }

    return length;
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

static int string_equals(const char *left, const char *right) {
    size_t i = 0;

    while (left != NULL && right != NULL && left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }

    return left != NULL && right != NULL && left[i] == '\0' && right[i] == '\0';
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

static int is_printable(char key) {
    return key >= 32 && key <= 126;
}

static void set_query_text(const char *text) {
    spotlight_query_length = 0;
    spotlight_query_text[0] = '\0';

    while (text != NULL &&
           text[spotlight_query_length] != '\0' &&
           spotlight_query_length + 1u < sizeof(spotlight_query_text)) {
        spotlight_query_text[spotlight_query_length] = text[spotlight_query_length];
        ++spotlight_query_length;
    }

    spotlight_query_text[spotlight_query_length] = '\0';
}

static uint32_t blend_rgb(spotlight_rgb_t bg, spotlight_rgb_t fg, uint8_t alpha) {
    const uint8_t inv = (uint8_t)(255u - alpha);

    return gui_color_rgb(
        (uint8_t)(((uint32_t)bg.r * inv + (uint32_t)fg.r * alpha) / 255u),
        (uint8_t)(((uint32_t)bg.g * inv + (uint32_t)fg.g * alpha) / 255u),
        (uint8_t)(((uint32_t)bg.b * inv + (uint32_t)fg.b * alpha) / 255u)
    );
}

static void draw_text_clipped(uint64_t x, uint64_t y, uint64_t max_width, const char *text, spotlight_rgb_t fg, spotlight_rgb_t bg) {
    char clipped[96];
    size_t limit = max_width / SPOTLIGHT_TEXT_CELL_WIDTH;
    size_t i = 0;

    if (text == NULL || max_width < SPOTLIGHT_TEXT_CELL_WIDTH) {
        return;
    }

    if (limit >= sizeof(clipped)) {
        limit = sizeof(clipped) - 1u;
    }

    while (i < limit && text[i] != '\0') {
        clipped[i] = text[i];
        ++i;
    }
    clipped[i] = '\0';

    for (size_t ci = 0; clipped[ci] != '\0'; ++ci) {
        uint8_t c = (uint8_t)clipped[ci];
        if (c < PLEX_MONO_12_FIRST || c > PLEX_MONO_12_LAST) {
            c = (uint8_t)'?';
        }

        const size_t glyph_index = (size_t)(c - PLEX_MONO_12_FIRST) * PLEX_MONO_12_WIDTH * PLEX_MONO_12_HEIGHT;
        const uint64_t glyph_x = x + ci * PLEX_MONO_12_WIDTH;

        for (uint64_t gy = 0; gy < PLEX_MONO_12_HEIGHT; ++gy) {
            for (uint64_t gx = 0; gx < PLEX_MONO_12_WIDTH; ++gx) {
                const uint8_t alpha = plex_mono_12_alpha[glyph_index + gy * PLEX_MONO_12_WIDTH + gx];
                if (alpha != 0u) {
                    gui_text_put_pixel(glyph_x + gx, y + gy, blend_rgb(bg, fg, alpha));
                }
            }
        }
    }
}

static void draw_small_text_clipped(uint64_t x, uint64_t y, uint64_t max_width, const char *text, spotlight_rgb_t fg, spotlight_rgb_t bg) {
    char clipped[112];
    size_t limit = max_width / PLEX_MONO_10_WIDTH;
    size_t i = 0;

    if (text == NULL || max_width < PLEX_MONO_10_WIDTH) {
        return;
    }

    if (limit >= sizeof(clipped)) {
        limit = sizeof(clipped) - 1u;
    }

    while (i < limit && text[i] != '\0') {
        clipped[i] = text[i];
        ++i;
    }
    clipped[i] = '\0';

    for (size_t ci = 0; clipped[ci] != '\0'; ++ci) {
        uint8_t c = (uint8_t)clipped[ci];
        if (c < PLEX_MONO_10_FIRST || c > PLEX_MONO_10_LAST) {
            c = (uint8_t)'?';
        }

        const size_t glyph_index = (size_t)(c - PLEX_MONO_10_FIRST) * PLEX_MONO_10_WIDTH * PLEX_MONO_10_HEIGHT;
        const uint64_t glyph_x = x + ci * PLEX_MONO_10_WIDTH;

        for (uint64_t gy = 0; gy < PLEX_MONO_10_HEIGHT; ++gy) {
            for (uint64_t gx = 0; gx < PLEX_MONO_10_WIDTH; ++gx) {
                const uint8_t alpha = plex_mono_10_alpha[glyph_index + gy * PLEX_MONO_10_WIDTH + gx];
                if (alpha != 0u) {
                    gui_text_put_pixel(glyph_x + gx, y + gy, blend_rgb(bg, fg, alpha));
                }
            }
        }
    }
}

static void compute_overlay_rect(size_t result_count) {
    const uint64_t width = gui_width();
    const uint64_t height = gui_height();
    const uint64_t rows = result_count > SPOTLIGHT_UI_MAX_RESULTS ? SPOTLIGHT_UI_MAX_RESULTS : result_count;
    uint64_t wanted_w = (width * 58u) / 100u;
    const uint64_t wanted_h = SPOTLIGHT_BAR_HEIGHT + (rows == 0 ? 34u : SPOTLIGHT_PANEL_GAP + SPOTLIGHT_PANEL_PAD * 2u + rows * SPOTLIGHT_UI_LINE_HEIGHT);

    if (wanted_w > 1040u) {
        wanted_w = 1040u;
    }
    if (wanted_w < 560u) {
        wanted_w = width > 64u ? width - 64u : width;
    }

    overlay_w = wanted_w;
    overlay_h = wanted_h;

    overlay_x = width > overlay_w ? (width - overlay_w) / 2u : 0;
    overlay_y = height > overlay_h ? (height * 28u) / 100u : 0;
}

static void hide_overlay_now(void) {
    spotlight_open = 0;
    spotlight_backdrop_ready = 0;
    confirmation_pending = 0;
    selected_result_index = 0;
    visible_result_count = 0;
    previous_overlay_w = 0;
    previous_overlay_h = 0;
    spotlight_query_text[0] = '\0';
    spotlight_query_length = 0;
    gui_restore_after_overlay();
}

static void draw_result(size_t index, const spotlight_result_view_t *result, uint64_t y) {
    const spotlight_rgb_t text_rgb    = SPOTLIGHT_RGB(228, 220, 248);
    const spotlight_rgb_t muted_rgb   = SPOTLIGHT_RGB(150, 130, 185);
    const spotlight_rgb_t selected_rgb = SPOTLIGHT_RGB(52, 30, 96);
    const spotlight_rgb_t panel_rgb   = SPOTLIGHT_RGB(22, 16, 40);
    const spotlight_rgb_t badge_rgb   = SPOTLIGHT_RGB(30, 22, 52);
    const uint32_t panel    = gui_color_rgb(22, 16, 40);   /* dark glass row */
    const uint32_t selected = gui_color_rgb(52, 30, 96);   /* neon selected row */
    const uint32_t app_color  = gui_color_rgb(160, 80, 255);
    const uint32_t file_color = gui_color_rgb(44, 148, 220);
    const uint32_t cmd_color  = gui_color_rgb(224, 112, 32);
    const uint32_t accent = result->kind == SPOTLIGHT_RESULT_APP
        ? app_color
        : (result->kind == SPOTLIGHT_RESULT_COMMAND ? cmd_color : file_color);
    const int selected_row = index == selected_result_index;
    const spotlight_rgb_t row_bg_rgb = selected_row ? selected_rgb : panel_rgb;
    const uint64_t row_x = overlay_x + 16u;
    const uint64_t row_w = overlay_w > 32u ? overlay_w - 32u : overlay_w;
    const uint64_t row_h = SPOTLIGHT_UI_LINE_HEIGHT - 6u;
    const uint64_t row_y = y;
    const uint64_t arrow_x = overlay_x + 6u;
    const uint64_t icon_x = row_x + 34u;
    const uint64_t icon_y = row_y + row_h / 2u;
    const uint64_t text_x = row_x + 58u;
    const uint64_t title_y = row_y + 6u;
    const uint64_t subtitle_y = row_y + 23u;
    const uint64_t badge_w = 86u;
    const uint64_t badge_h = 22u;
    const uint64_t badge_x = row_x + row_w - 96u;
    const uint64_t badge_y = row_y + (row_h - badge_h) / 2u;
    if (selected_row) {
        draw_text_clipped(arrow_x, row_y + 10u, 8u, ">", SPOTLIGHT_RGB(160, 80, 255), SPOTLIGHT_RGB(22, 16, 40));
        gui_fill_rounded_rect(row_x, row_y, row_w, row_h, 10u, gui_color_rgb(168, 116, 230));
        gui_fill_rounded_rect(row_x + 1u, row_y + 1u, row_w - 2u, row_h - 2u, 9u, selected);
        gui_fill_rounded_rect(row_x + 1u, row_y + 1u, 3u, row_h - 2u, 2u, gui_color_rgb(136, 48, 224));
    } else {
        gui_fill_rounded_rect(row_x, row_y, row_w, row_h, 9u, panel);
    }

    gui_fill_rounded_rect(icon_x - 15u, icon_y - 15u, 30u, 30u, 6u, gui_color_rgb(36, 24, 64));
    if (result->kind == SPOTLIGHT_RESULT_APP) {
        gui_fill_circle(icon_x, icon_y, 9u, gui_color_rgb(60, 36, 100));
        gui_fill_circle(icon_x, icon_y, 6u, accent);
        gui_fill_circle(icon_x + 2u, icon_y - 2u, 2u, gui_color_rgb(210, 190, 255));
    } else if (result->kind == SPOTLIGHT_RESULT_COMMAND) {
        gui_fill_rounded_rect(icon_x - 9u, icon_y - 7u, 18u, 14u, 3u, gui_color_rgb(50, 34, 84));
        gui_fill_rounded_rect(icon_x - 6u, icon_y - 4u, 4u, 2u, 1u, accent);
        gui_fill_rounded_rect(icon_x - 1u, icon_y + 3u, 7u, 2u, 1u, accent);
    } else {
        gui_fill_rounded_rect(icon_x - 9u, icon_y - 7u, 18u, 14u, 3u, gui_color_rgb(38, 34, 70));
        gui_fill_rounded_rect(icon_x - 8u, icon_y - 7u, 8u, 4u, 2u, accent);
        gui_fill_rounded_rect(icon_x - 6u, icon_y - 2u, 12u, 2u, 1u, gui_color_rgb(44, 148, 220));
    }
    draw_text_clipped(text_x, title_y, row_w > 188u ? row_w - 188u : row_w, result->title, text_rgb, row_bg_rgb);

    if (result->description != NULL && result->description[0] != '\0') {
        draw_small_text_clipped(text_x, subtitle_y, row_w > 198u ? row_w - 198u : row_w, result->description, muted_rgb, row_bg_rgb);
    } else {
        draw_small_text_clipped(text_x, subtitle_y, row_w > 198u ? row_w - 198u : row_w, result->path, muted_rgb, row_bg_rgb);
    }

    if (result->category != NULL && result->category[0] != '\0' && row_w > 150u) {
        gui_fill_rounded_rect(badge_x, badge_y, badge_w, badge_h, 5u, gui_color_rgb(30, 22, 52));
        draw_text_clipped(badge_x + 18u, badge_y + 3u, badge_w - 24u, result->category, muted_rgb, badge_rgb);
    }
}

static void draw_orb(uint64_t cx, uint64_t cy) {
    gui_draw_vex_logo(cx - 13u, cy - 13u, 26u);
}

/* Wipe-from-top animation: reveals overlay top-to-bottom.
 * percent=0 → fully hidden, percent=100 → fully visible.
 * Restores the blurred backdrop over the not-yet-revealed portion. */
static void draw_transition_frame(uint64_t percent) {
    const uint64_t pad = 12u;
    const uint64_t full_h = overlay_h + pad * 2u;
    const uint64_t revealed_h = (full_h * percent) / 100u;
    const uint64_t cover_y = (overlay_y > pad ? overlay_y - pad : 0u) + revealed_h;
    const uint64_t cover_h = full_h > revealed_h ? full_h - revealed_h : 0u;

    if (cover_h > 0u && overlay_w > 0u) {
        gui_restore_blurred_rect(
            overlay_x > pad ? overlay_x - pad : 0u,
            cover_y,
            overlay_w + pad * 2u,
            cover_h
        );
    }
}

static void animate_open(void) {
    gui_restore_after_overlay();
    gui_blur_desktop_background();
    spotlight_backdrop_ready = 1;
    previous_overlay_w = 0u;
    previous_overlay_h = 0u;

    draw_transition_frame(100u);
}

static void animate_close(void) {
    /* Closing is now handled frame-by-frame in spotlight_ui_tick(). */
    (void)spotlight_backdrop_ready;
}

static void draw_overlay(void) {
    const size_t count = spotlight_query(spotlight_query_text);
    const uint32_t shell      = gui_color_rgb(38, 24, 70);   /* visible violet glass */
    const uint32_t shell_soft = gui_color_rgb(48, 30, 84);   /* lighter inner */
    const uint32_t purple_l   = gui_color_rgb(148, 78, 240); /* vivid neon border left */
    const uint32_t purple_r   = gui_color_rgb(196, 64, 218); /* vivid border right */
    const spotlight_rgb_t text_rgb      = SPOTLIGHT_RGB(236, 228, 255);
    const spotlight_rgb_t muted_rgb     = SPOTLIGHT_RGB(160, 142, 200);
    const spotlight_rgb_t shell_soft_rgb = SPOTLIGHT_RGB(48, 30, 84);
    const spotlight_rgb_t black_rgb     = SPOTLIGHT_RGB(20, 14, 38);
    const spotlight_rgb_t panel_rgb     = SPOTLIGHT_RGB(32, 22, 58);
    const uint32_t panel        = gui_color_rgb(32, 22, 58);   /* visible dark glass panel */
    const uint32_t panel_border = gui_color_rgb(148, 78, 240); /* vivid neon border */

    if (gui_width() == 0 || gui_height() == 0) {
        return;
    }

    if (!spotlight_backdrop_ready) {
        gui_restore_after_overlay();
        gui_blur_desktop_background();
        spotlight_backdrop_ready = 1;
    } else if (previous_overlay_w != 0u && previous_overlay_h != 0u) {
        gui_restore_blurred_rect(previous_overlay_x, previous_overlay_y, previous_overlay_w, previous_overlay_h);
    }

    compute_overlay_rect(count);
    visible_result_count = count > SPOTLIGHT_UI_MAX_RESULTS ? SPOTLIGHT_UI_MAX_RESULTS : count;
    if (selected_result_index >= visible_result_count) {
        selected_result_index = visible_result_count == 0u ? 0u : visible_result_count - 1u;
    }

    const uint64_t border_x = overlay_x > 0u ? overlay_x - 1u : overlay_x;
    const uint64_t border_y = overlay_y > 0u ? overlay_y - 1u : overlay_y;
    /* Drop shadow: 2 dark layers, not bright rings */
    gui_fill_rounded_rect(overlay_x > 10u ? overlay_x - 10u : 0u, overlay_y > 10u ? overlay_y - 10u : 0u, overlay_w + 20u, SPOTLIGHT_BAR_HEIGHT + 20u, 34u, gui_color_rgb(14, 8, 28));
    gui_fill_rounded_rect(overlay_x > 5u ? overlay_x - 5u : 0u, overlay_y > 5u ? overlay_y - 5u : 0u, overlay_w + 10u, SPOTLIGHT_BAR_HEIGHT + 10u, 31u, gui_color_rgb(36, 18, 68));
    gui_fill_rounded_rect_horizontal_gradient(border_x, border_y, overlay_w + 2u, SPOTLIGHT_BAR_HEIGHT + 2u, 27u, purple_l, purple_r);
    gui_fill_rounded_rect_horizontal_gradient(overlay_x, overlay_y, overlay_w, SPOTLIGHT_BAR_HEIGHT, 26u, shell, gui_color_rgb(14, 10, 30));
    gui_fill_rounded_rect_horizontal_gradient(overlay_x + 3u, overlay_y + 3u, overlay_w - 6u, SPOTLIGHT_BAR_HEIGHT - 6u, 23u, shell_soft, gui_color_rgb(18, 12, 36));
    /* Glass top-edge highlight */
    gui_fill_rect(overlay_x + 26u, overlay_y + 1u, overlay_w > 52u ? overlay_w - 52u : overlay_w, 1u, gui_color_rgb(80, 56, 130));
    /* Separator line bottom */
    gui_fill_rect(overlay_x + 26u, overlay_y + SPOTLIGHT_BAR_HEIGHT - 8u, overlay_w > 52u ? overlay_w - 52u : overlay_w, 1u, gui_color_rgb(70, 48, 110));

    if (spotlight_query_text[0] == '\0') {
        draw_text_clipped(overlay_x + 34u, overlay_y + (SPOTLIGHT_BAR_HEIGHT - SPOTLIGHT_TEXT_CELL_HEIGHT) / 2u, overlay_w - 104u, "Search apps, files, commands ...", muted_rgb, shell_soft_rgb);
    } else {
        draw_text_clipped(overlay_x + 34u, overlay_y + (SPOTLIGHT_BAR_HEIGHT - SPOTLIGHT_TEXT_CELL_HEIGHT) / 2u, overlay_w - 104u, spotlight_query_text, text_rgb, shell_soft_rgb);
    }

    draw_orb(overlay_x + overlay_w - 32u, overlay_y + 26u);
    if (count == 0) {
        draw_text_clipped(overlay_x + overlay_w - 174u, overlay_y + 64u, 174u, "Ctrl + Space to talk", muted_rgb, black_rgb);
    }

    if (count == 0) {
        previous_overlay_x = overlay_x > 12u ? overlay_x - 12u : 0u;
        previous_overlay_y = overlay_y > 12u ? overlay_y - 12u : 0u;
        previous_overlay_w = overlay_w + 24u;
        previous_overlay_h = 112u;
        gui_redraw_cursor_overlay();
        return;
    }

    const size_t visible = visible_result_count;
    const uint64_t panel_y = overlay_y + SPOTLIGHT_BAR_HEIGHT + SPOTLIGHT_PANEL_GAP;
    const uint64_t panel_h = SPOTLIGHT_PANEL_PAD * 2u + visible * SPOTLIGHT_UI_LINE_HEIGHT;
    /* Results panel drop shadow: dark, not bright ring */
    gui_fill_rounded_rect(overlay_x > 5u ? overlay_x - 5u : 0u, panel_y > 5u ? panel_y - 5u : 0u, overlay_w + 10u, panel_h + 10u, 16u, gui_color_rgb(16, 8, 30));
    gui_fill_rounded_rect(overlay_x, panel_y, overlay_w, panel_h, 14u, panel_border);
    gui_fill_rounded_rect(overlay_x + 1u, panel_y + 1u, overlay_w - 2u, panel_h - 2u, 13u, panel);
    if (confirmation_pending) {
        draw_text_clipped(overlay_x + 24u, panel_y + panel_h - 22u, overlay_w - 48u, "Enter confirme l'action, Esc annule", SPOTLIGHT_RGB(200, 112, 28), panel_rgb);
    }

    uint64_t y = panel_y + SPOTLIGHT_PANEL_PAD;
    for (size_t i = 0; i < visible; ++i) {
        const spotlight_result_view_t *result = spotlight_result(i);
        if (result != NULL) {
            draw_result(i, result, y);
            y += SPOTLIGHT_UI_LINE_HEIGHT;
        }
    }

    previous_overlay_x = overlay_x > 12u ? overlay_x - 12u : 0u;
    previous_overlay_y = overlay_y > 12u ? overlay_y - 12u : 0u;
    previous_overlay_w = overlay_w + 24u;
    previous_overlay_h = overlay_h + 24u;

    if (spotlight_anim_state == ANIM_OPENING || spotlight_anim_state == ANIM_CLOSING) {
        const uint64_t pct = (spotlight_anim_step * 100u) / SPOTLIGHT_ANIM_STEPS;
        draw_transition_frame(pct);
    }

    gui_redraw_cursor_overlay();
}

static int result_is_program(const spotlight_result_view_t *result) {
    char prefix[64];

    copy_string(prefix, sizeof(prefix), "/home/");
    append_string(prefix, sizeof(prefix), user_current_username());
    append_string(prefix, sizeof(prefix), "/bin/");

    return result != NULL && result->program != 0 &&
        (starts_with(result->path, prefix) || starts_with(result->path, "/home/root/packages/"));
}

static void run_path_via_loader(const char *path) {
    gui_focus_terminal_for_launch();
    if (elf_load(path) != 0) {
        tty_puts("spotlight: run failed\n");
    }
}

static void open_selected_result(void) {
    const spotlight_result_view_t *result = spotlight_result(selected_result_index);
    char path[128];
    spotlight_action_t action = SPOTLIGHT_ACTION_NONE;
    int has_result = 0;
    int program = 0;
    uint8_t dangerous = 0u;

    if (result != NULL) {
        copy_string(path, sizeof(path), result->path);
        action = result->action;
        program = result_is_program(result);
        dangerous = result->dangerous;
        has_result = 1;
    }

    if (has_result && dangerous != 0u && !confirmation_pending) {
        confirmation_pending = 1;
        draw_overlay();
        return;
    }

    hide_overlay_now();

    if (!has_result) {
        return;
    }

    spotlight_record_selection(selected_result_index);

    if (action == SPOTLIGHT_ACTION_OPEN_TERMINAL) {
        gui_focus_terminal_for_launch();
        return;
    }

    if (action == SPOTLIGHT_ACTION_OPEN_FILES) {
        gui_open_file_manager();
        return;
    }

    if (action == SPOTLIGHT_ACTION_OPEN_EDITOR) {
        gui_open_editor();
        return;
    }

    if (action == SPOTLIGHT_ACTION_OPEN_BROWSER) {
        if (elf_spawn("/home/root/bin/webkit") != 0) {
            tty_puts("spotlight: browser launch failed\n");
        }
        return;
    }

    if (action == SPOTLIGHT_ACTION_RUN_SYSINFO ||
        action == SPOTLIGHT_ACTION_RUN_PS ||
        action == SPOTLIGHT_ACTION_RUN_LOG) {
        gui_focus_terminal_for_launch();
        if (action == SPOTLIGHT_ACTION_RUN_SYSINFO) {
            shell_submit_line("sysinfo");
        } else if (action == SPOTLIGHT_ACTION_RUN_PS) {
            shell_submit_line("ps");
        } else {
            shell_submit_line("log");
        }
        return;
    }

    if (program) {
        if (string_equals(path, "/home/root/bin/webkit")) {
            if (elf_spawn(path) != 0) {
                tty_puts("spotlight: run failed\n");
            }
            return;
        }

        run_path_via_loader(path);
        return;
    }

    if (action == SPOTLIGHT_ACTION_RUN_PROGRAM && path[0] == '/') {
        run_path_via_loader(path);
        return;
    }

    if (path[0] == '/' &&
        (starts_with(path, "/home/root/packages/") || starts_with(path, "/home/root/bin/"))) {
        run_path_via_loader(path);
        return;
    }

    if (path[0] == '/') {
        gui_open_editor_path(path);
        return;
    }

    if (fs_exists(path)) {
        gui_open_editor_path(path);
        return;
    }

    tty_puts("spotlight: open failed\n");
}

void spotlight_ui_toggle(void) {
    if (spotlight_open) {
        spotlight_ui_close();
        return;
    }

    spotlight_open = 1;
    spotlight_backdrop_ready = 0;
    previous_overlay_w = 0;
    previous_overlay_h = 0;
    shell_return_pending = 0;
    confirmation_pending = 0;
    spotlight_query_text[0] = '\0';
    spotlight_query_length = 0;
    selected_result_index = 0;
    visible_result_count = 0;
    spotlight_anim_state = ANIM_OPENING;
    spotlight_anim_step = 0u;
    animate_open();
    draw_overlay();   /* first frame: step=0 → fully covered → invisible */
}

void spotlight_ui_close(void) {
    if (!spotlight_open) {
        return;
    }

    if (spotlight_anim_state == ANIM_CLOSING) {
        return;   /* already closing */
    }

    /* When interrupting an opening anim, reverse from the current step.
     * Otherwise start from the fully-open position. */
    if (spotlight_anim_state != ANIM_OPENING) {
        spotlight_anim_step = SPOTLIGHT_ANIM_STEPS;
    }
    spotlight_anim_state = ANIM_CLOSING;
    animate_close();
    draw_overlay();   /* first closing frame */
}

int spotlight_ui_is_open(void) {
    return spotlight_open;
}

int spotlight_ui_take_shell_return(void) {
    const int pending = shell_return_pending;
    shell_return_pending = 0;
    return pending;
}

void spotlight_ui_handle_key(char key) {
    if (!spotlight_open) {
        return;
    }

    if (key == 27) {
        if (confirmation_pending) {
            confirmation_pending = 0;
            draw_overlay();
            return;
        }
        spotlight_ui_close();
        return;
    }

    if (key == '\n') {
        open_selected_result();
        shell_return_pending = 1;
        return;
    }

    if (key == KEYBOARD_KEY_UP) {
        if (visible_result_count != 0u) {
            selected_result_index = selected_result_index == 0u ? visible_result_count - 1u : selected_result_index - 1u;
            confirmation_pending = 0;
            draw_overlay();
        }
        return;
    }

    if (key == KEYBOARD_KEY_DOWN) {
        if (visible_result_count != 0u) {
            selected_result_index = (selected_result_index + 1u) % visible_result_count;
            confirmation_pending = 0;
            draw_overlay();
        }
        return;
    }

    if (key == '\b') {
        if (spotlight_query_length > 0) {
            spotlight_query_text[--spotlight_query_length] = '\0';
        }
        selected_result_index = 0;
        confirmation_pending = 0;
        draw_overlay();
        return;
    }

    if (is_printable(key) && spotlight_query_length + 1u < sizeof(spotlight_query_text)) {
        spotlight_query_text[spotlight_query_length++] = key;
        spotlight_query_text[spotlight_query_length] = '\0';
        selected_result_index = 0;
        confirmation_pending = 0;
        draw_overlay();
    }
}

void spotlight_ui_handle_click(uint64_t x, uint64_t y) {
    const size_t count = spotlight_query(spotlight_query_text);
    const size_t visible = count > SPOTLIGHT_UI_MAX_RESULTS ? SPOTLIGHT_UI_MAX_RESULTS : count;

    if (!spotlight_open) {
        return;
    }

    compute_overlay_rect(count);

    const uint64_t panel_y = overlay_y + SPOTLIGHT_BAR_HEIGHT + SPOTLIGHT_PANEL_GAP;
    const uint64_t panel_h = SPOTLIGHT_PANEL_PAD * 2u + visible * SPOTLIGHT_UI_LINE_HEIGHT;

    if (visible == 0u) {
        if (x < overlay_x || x >= overlay_x + overlay_w ||
            y < overlay_y || y >= overlay_y + SPOTLIGHT_BAR_HEIGHT + 80u) {
            spotlight_ui_close();
        }
        return;
    }

    if (x < overlay_x || x >= overlay_x + overlay_w || y < panel_y || y >= panel_y + panel_h) {
        if (y < overlay_y || y >= overlay_y + SPOTLIGHT_BAR_HEIGHT || x < overlay_x || x >= overlay_x + overlay_w) {
            spotlight_ui_close();
        }
        return;
    }

    if (y < panel_y + SPOTLIGHT_PANEL_PAD) {
        return;
    }

    const size_t index = (size_t)((y - panel_y - SPOTLIGHT_PANEL_PAD) / SPOTLIGHT_UI_LINE_HEIGHT);
    if (index >= visible) {
        return;
    }

    selected_result_index = index;
    visible_result_count = visible;
    open_selected_result();
    shell_return_pending = 1;
}

void spotlight_ui_voice_listen(void) {
    char heard[SPOTLIGHT_UI_QUERY_MAX];
    voice_command_t command;

    if (!spotlight_open) {
        spotlight_open = 1;
        spotlight_backdrop_ready = 0;
        previous_overlay_w = 0;
        previous_overlay_h = 0;
        shell_return_pending = 0;
    }

    if (voice_listen_once(heard, sizeof(heard)) != 0) {
        /* Recognition failed — show spotlight empty, wait for user to type */
        draw_overlay();
        return;
    }

    /* Success: show what was heard, then try to execute a command */
    set_query_text(heard);
    selected_result_index = 0;
    draw_overlay();

    command = voice_command_from_text(heard);
    if (command == VOICE_COMMAND_OPEN_TERMINAL) {
        hide_overlay_now();
        gui_focus_terminal_for_launch();
        shell_return_pending = 1;
        return;
    }

    if (command == VOICE_COMMAND_OPEN_BROWSER) {
        hide_overlay_now();
        if (elf_spawn("/home/root/bin/webkit") != 0) {
            tty_puts("voice: browser launch failed\n");
        }
        shell_return_pending = 1;
    }
}

void spotlight_ui_tick(void) {
    if (spotlight_anim_state == ANIM_OPENING) {
        if (spotlight_anim_step < SPOTLIGHT_ANIM_STEPS) {
            ++spotlight_anim_step;
            draw_overlay();
        } else {
            spotlight_anim_state = ANIM_OPEN;
        }
    } else if (spotlight_anim_state == ANIM_CLOSING) {
        if (spotlight_anim_step > 0u) {
            --spotlight_anim_step;
            draw_overlay();
        } else {
            spotlight_anim_state = ANIM_NONE;
            hide_overlay_now();
        }
    }
}

static void spotlight_process_loop(void) {
    gui_app_command_t command;

    for (;;) {
        while (pipe_read(spotlight_command_pipe, &command, sizeof(command)) == (int)sizeof(command)) {
            if (command.type == GUI_APP_CMD_TOGGLE) {
                spotlight_ui_toggle();
                continue;
            }

            if (command.type == GUI_APP_CMD_KEY) {
                spotlight_ui_handle_key(command.key);
            }
        }

        process_yield();
    }
}

uint32_t spotlight_app_start(void) {
    if (spotlight_pid_value != 0u) {
        return spotlight_pid_value;
    }

    spotlight_command_pipe = pipe_create();
    if (spotlight_command_pipe < 0) {
        spotlight_command_pipe = -1;
        return 0;
    }

    spotlight_pid_value = process_create(spotlight_process_loop, SPOTLIGHT_PROCESS_PRIORITY);
    if (spotlight_pid_value == 0u) {
        pipe_close(spotlight_command_pipe);
        spotlight_command_pipe = -1;
    }

    return spotlight_pid_value;
}

uint32_t spotlight_app_pid(void) {
    return spotlight_pid_value;
}

void spotlight_app_send(const gui_app_command_t *command) {
    if (command == NULL || spotlight_command_pipe < 0) {
        return;
    }

    (void)pipe_write(spotlight_command_pipe, command, sizeof(*command));
}
