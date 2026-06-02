#include "gui_private.h"

#include <stddef.h>
#include <stdint.h>
#include <unos/gui.h>
#include <unos/keyboard.h>
#include <unos/scheduler.h>
#include <unos/tty.h>
#include <unos/vex_logo.h>
#include <unos/vex_legacy_compat.h>

static size_t browser_string_length(const char *text) {
    size_t length = 0u;
    while (text != NULL && text[length] != '\0') {
        ++length;
    }
    return length;
}

static int browser_starts_with(const char *text, const char *prefix) {
    size_t i = 0u;
    while (prefix != NULL && prefix[i] != '\0') {
        if (text == NULL || text[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static int browser_contains_char(const char *text, char needle) {
    for (size_t i = 0u; text != NULL && text[i] != '\0'; ++i) {
        if (text[i] == needle) {
            return 1;
        }
    }
    return 0;
}

static void browser_copy_string(char *dst, size_t capacity, const char *src) {
    size_t i = 0u;
    if (dst == NULL || capacity == 0u) {
        return;
    }
    while (src != NULL && src[i] != '\0' && i + 1u < capacity) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void browser_append_string(char *dst, size_t capacity, const char *src) {
    size_t length = browser_string_length(dst);
    size_t i = 0u;
    while (src != NULL && src[i] != '\0' && length + 1u < capacity) {
        dst[length++] = src[i++];
    }
    dst[length] = '\0';
}

static void browser_normalize_submission(const char *address, char *out, size_t capacity) {
    char trimmed[GUI_BROWSER_ADDRESS_MAX];
    size_t start = 0u;
    size_t end;
    size_t out_len = 0u;

    browser_copy_string(trimmed, sizeof(trimmed), address);
    while (trimmed[start] == ' ') {
        ++start;
    }
    end = browser_string_length(trimmed);
    while (end > start && trimmed[end - 1u] == ' ') {
        trimmed[--end] = '\0';
    }

    if (trimmed[start] == '\0') {
        out[0] = '\0';
        return;
    }

    if (browser_starts_with(trimmed + start, "http://") ||
        browser_starts_with(trimmed + start, "https://") ||
        browser_starts_with(trimmed + start, "vex://") ||
        browser_starts_with(trimmed + start, "about:") ||
        browser_starts_with(trimmed + start, "file://")) {
        browser_copy_string(out, capacity, trimmed + start);
        return;
    }

    if (browser_contains_char(trimmed + start, ' ')) {
        browser_copy_string(out, capacity, "https://duckduckgo.com/?q=");
        out_len = browser_string_length(out);
        for (size_t i = start; trimmed[i] != '\0' && out_len + 1u < capacity; ++i) {
            out[out_len++] = trimmed[i] == ' ' ? '+' : trimmed[i];
        }
        out[out_len] = '\0';
        return;
    }

    if (browser_starts_with(trimmed + start, "localhost") ||
        browser_starts_with(trimmed + start, "127.") ||
        browser_starts_with(trimmed + start, "[::1]")) {
        browser_copy_string(out, capacity, "http://");
        browser_append_string(out, capacity, trimmed + start);
        return;
    }

    browser_copy_string(out, capacity, "https://");
    browser_append_string(out, capacity, trimmed + start);
}

void browser_shell_set_address(const char *address) {
    browser_address_len = 0u;
    if (address == NULL) {
        browser_address[0] = '\0';
        return;
    }

    while (address[browser_address_len] != '\0' && browser_address_len + 1u < sizeof(browser_address)) {
        browser_address[browser_address_len] = address[browser_address_len];
        ++browser_address_len;
    }
    browser_address[browser_address_len] = '\0';
}

void draw_browser_loading_state(void) {
    const uint64_t x = gui_content_x();
    const uint64_t y = gui_content_y();
    const uint64_t w = gui_content_width();
    const uint64_t h = gui_content_height();
    const uint32_t bg = pack_color(12, 10, 22);
    const uint32_t panel = pack_color(22, 16, 40);
    const uint32_t border = pack_color(100, 56, 180);
    const uint32_t text = pack_color(228, 220, 248);
    const uint32_t muted = pack_color(150, 130, 185);
    const uint64_t panel_w = w > 280u ? (w > 760u ? 760u : w - 48u) : w;
    const uint64_t panel_h = 112u;
    const uint64_t panel_x = x + (w > panel_w ? (w - panel_w) / 2u : 0u);
    const uint64_t panel_y = y + (h > 220u ? (h - panel_h) / 3u : 28u);

    if (!browser_active || browser_minimized || w == 0u || h == 0u) {
        return;
    }

    fill_rect(x, y, w, h, bg);
    fill_rounded_rect(panel_x - 2u, panel_y - 2u, panel_w + 4u, panel_h + 4u, 12u, border);
    fill_rounded_rect(panel_x, panel_y, panel_w, panel_h, 10u, panel);
    tty_draw_text_transparent_at(panel_x + 24u, panel_y + 28u, "Chargement...", text);
    draw_browser_text_clipped(panel_x + 24u, panel_y + 58u,
        browser_loading_text[0] == '\0' ? "Waiting for WebKit frame" : browser_loading_text,
        muted,
        panel_w > 48u ? panel_w - 48u : 0u);
    compositor_mark_dirty(x, y, w, h);
}

void draw_browser_shell(void) {
    const uint64_t x = browser_win_x + 8u;
    const uint64_t y = browser_win_y + GUI_TITLE_HEIGHT + 6u;
    const uint64_t w = browser_win_w > 16u ? browser_win_w - 16u : browser_win_w;
    const uint32_t shell = pack_color(28, 20, 48);
    const uint32_t tab = pack_color(48, 32, 80);
    const uint32_t bar = pack_color(36, 24, 62);
    const uint32_t address = browser_address_focused ? pack_color(52, 34, 88) : pack_color(42, 28, 74);
    const uint32_t text = pack_color(236, 228, 255);
    const uint32_t muted = pack_color(160, 142, 200);
    const uint64_t address_x = x + 100u;
    const uint64_t address_w = w > 148u ? w - 148u : 80u;

    if (!browser_active || browser_minimized || w == 0u) {
        return;
    }

    fill_rect(x, y, w, GUI_BROWSER_SHELL_HEIGHT, shell);
    fill_rounded_rect(x, y + 3u, 210u, 30u, 7u, tab);
    tty_draw_text_transparent_at(x + 58u, y + 14u, browser_tab_count > 1u ? "New-page 2" : "New-page", text);
    tty_draw_text_transparent_at(x + 188u, y + 14u, "x", muted);
    tty_draw_text_transparent_at(x + 224u, y + 14u, "+", muted);

    fill_rounded_rect(x, y + 36u, w, 34u, 5u, bar);
    tty_draw_text_transparent_at(x + 14u, y + 48u, "<", text);
    tty_draw_text_transparent_at(x + 40u, y + 48u, ">", text);
    tty_draw_text_transparent_at(x + 66u, y + 48u, "C", text);

    fill_rounded_rect(address_x, y + 40u, address_w, 26u, 13u, pack_color(148, 92, 220));
    fill_rounded_rect(address_x + 1u, y + 41u, address_w - 2u, 24u, 12u, address);
    draw_browser_text_clipped(address_x + 16u, y + 49u,
        browser_address_len == 0u ? "Search or enter address" : browser_address,
        browser_address_len == 0u ? muted : text,
        address_w > 76u ? address_w - 76u : 0u);
    if (browser_address_focused) {
        const uint64_t cursor_px = address_x + 18u + browser_address_len * 8u;
        if (cursor_px < address_x + address_w - 12u) {
            fill_rect(cursor_px, y + 47u, 1u, 12u, text);
        }
    }

    fill_circle(address_x + address_w - 22u, y + 53u, 9u, pack_color(140, 72, 220));
    tty_draw_text_transparent_at(x + w - 32u, y + 48u, "...", text);
    compositor_mark_dirty(x, y, w, GUI_BROWSER_SHELL_HEIGHT);
}

void draw_browser_text_clipped(uint64_t x, uint64_t y, const char *text, uint32_t color, uint64_t max_width) {
    char clipped[96];
    uint64_t max_chars;
    uint64_t index = 0u;

    if (text == NULL || max_width < 8u) {
        return;
    }

    max_chars = max_width / 8u;
    if (max_chars >= sizeof(clipped)) {
        max_chars = sizeof(clipped) - 1u;
    }
    if (max_chars == 0u) {
        return;
    }

    while (text[index] != '\0' && index < max_chars) {
        clipped[index] = text[index];
        ++index;
    }
    if (text[index] != '\0' && index > 3u) {
        clipped[index - 3u] = '.';
        clipped[index - 2u] = '.';
        clipped[index - 1u] = '.';
    }
    clipped[index] = '\0';
    tty_draw_text_transparent_at(x, y, clipped, color);
}

uint64_t gui_content_x(void) {
    if (browser_active) {
        return browser_win_x + 8u;
    }
    return terminal_win_x + 8u;
}

uint64_t gui_content_y(void) {
    if (browser_active) {
        return browser_win_y + GUI_TITLE_HEIGHT + 6u + GUI_BROWSER_SHELL_HEIGHT;
    }
    return terminal_win_y + GUI_TITLE_HEIGHT + 6u;
}

uint64_t gui_content_width(void) {
    if (browser_active) {
        if (browser_minimized) {
            return 0u;
        }
        return browser_win_w > 16u ? browser_win_w - 16u : browser_win_w;
    }
    return terminal_win_w > 16u ? terminal_win_w - 16u : terminal_win_w;
}

uint64_t gui_content_height(void) {
    if (browser_active) {
        if (browser_minimized) {
            return 0u;
        }
        return browser_win_h > GUI_TITLE_HEIGHT + GUI_BROWSER_SHELL_HEIGHT + 12u
            ? browser_win_h - GUI_TITLE_HEIGHT - GUI_BROWSER_SHELL_HEIGHT - 12u
            : 0u;
    }
    return terminal_win_h > GUI_TITLE_HEIGHT + 12u ? terminal_win_h - GUI_TITLE_HEIGHT - 12u : terminal_win_h;
}

void draw_browser_native_home(void) {
    const uint64_t x = gui_content_x();
    const uint64_t y = gui_content_y();
    const uint64_t w = gui_content_width();
    const uint64_t h = gui_content_height();
    const uint32_t bg = pack_color(12, 10, 22);
    const uint32_t text = pack_color(228, 220, 248);
    const uint32_t muted = pack_color(150, 130, 185);
    const uint32_t field = pack_color(24, 18, 46);
    const uint32_t ring = pack_color(120, 58, 200);
    const uint32_t ring_2 = pack_color(70, 38, 140);
    uint64_t title_x;
    uint64_t title_y;
    uint64_t search_w;
    uint64_t search_x;
    uint64_t search_y;

    if (!browser_active || browser_minimized || w == 0u || h == 0u) {
        return;
    }

    fill_rect(x, y, w, h, bg);
    title_y = y + (h > 360u ? (h * 28u) / 100u : 118u);
    title_x = x + (w > 260u ? (w - 260u) / 2u : 24u);
    tty_draw_text_transparent_at(title_x, title_y, "Vex - Browser", text);

    search_w = w > 320u ? (w * 66u) / 100u : w - 32u;
    if (search_w > 720u) {
        search_w = 720u;
    }
    search_x = x + (w > search_w ? (w - search_w) / 2u : 16u);
    search_y = title_y + 52u;

    fill_rounded_rect(search_x - 1u, search_y - 1u, search_w + 2u, 38u, 19u, ring);
    fill_rect(search_x, search_y + 35u, search_w, 2u, ring_2);
    fill_rounded_rect(search_x, search_y, search_w, 36u, 18u, field);
    tty_draw_text_transparent_at(search_x + 22u, search_y + 14u, "Search or enter address", muted);
    gui_draw_vex_logo(search_x + search_w - 34u, search_y + 5u, 26u);
    tty_draw_text_transparent_at(search_x + search_w - 170u, search_y + 50u, "Ctrl + Space to talk", muted);
    tty_draw_text_transparent_at(x + 22u, y + h - 28u, "Powered by WebKit", muted);
    compositor_mark_dirty(x, y, w, h);
}

void draw_browser_native_results(void) {
    const uint64_t x = gui_content_x();
    const uint64_t y = gui_content_y();
    const uint64_t w = gui_content_width();
    const uint64_t h = gui_content_height();
    const uint32_t panel = pack_color(22, 16, 40);
    const uint32_t panel_hi = pack_color(32, 22, 58);
    const uint32_t border = pack_color(100, 56, 180);
    const uint32_t accent = pack_color(140, 60, 220);
    const uint32_t text = pack_color(228, 220, 248);
    const uint32_t muted = pack_color(150, 130, 185);
    const uint32_t link = pack_color(160, 100, 255);
    const uint32_t chip = pack_color(40, 28, 70);
    const uint32_t icon_bg = pack_color(36, 24, 62);
    const uint32_t icon_fg = pack_color(160, 80, 255);
    const uint32_t count = ladybird_browser_results_count();
    uint64_t page_x;
    uint64_t page_w;
    uint64_t row_y;
    uint64_t row_h = 84u;
    uint64_t gap = 14u;

    if (!browser_active || browser_minimized || w == 0u || h == 0u) {
        return;
    }

    for (uint64_t row = 0u; row < h; ++row) {
        const uint32_t shade = (uint32_t)(row * 10u / (h == 0u ? 1u : h));
        fill_rect(x, y + row, w, 1u, pack_color((uint8_t)(12u + shade / 3u), (uint8_t)(10u + shade / 3u), (uint8_t)(22u + shade / 2u)));
    }

    page_x = x + (w > 96u ? 72u : 18u);
    page_w = w > 144u ? w - 144u : (w > 24u ? w - 24u : w);
    draw_browser_text_clipped(page_x, y + 42u, "Search", pack_color(130, 44, 220), 220u);
    draw_browser_text_clipped(page_x, y + 72u, "Live results", muted, 120u);
    draw_browser_text_clipped(page_x + 132u, y + 72u, ladybird_browser_results_query(), text, page_w > 160u ? page_w - 160u : 0u);

    row_y = y + 120u;
    for (uint32_t i = 0u; i < count && i < 6u; ++i) {
        const uint64_t card_y = row_y + (uint64_t)i * (row_h + gap);
        if (card_y + row_h > y + h - 16u) {
            break;
        }
        fill_rounded_rect(page_x - 2u, card_y - 2u, page_w + 4u, row_h + 4u, 10u, border);
        fill_rounded_rect(page_x + 2u, card_y + 2u, page_w - 4u, row_h - 4u, 9u, i == 0u ? panel_hi : panel);
        fill_rounded_rect(page_x + 22u, card_y + 20u, 34u, 34u, 8u, icon_bg);
        fill_circle(page_x + 39u, card_y + 37u, 8u, icon_fg);
        fill_rounded_rect(page_x + page_w - 112u, card_y + 24u, 82u, 24u, 9u, chip);
        draw_browser_text_clipped(page_x + page_w - 88u, card_y + 32u, "Open", muted, 48u);
        draw_browser_text_clipped(page_x + 72u, card_y + 18u, ladybird_browser_result_title(i), text, page_w > 208u ? page_w - 208u : 0u);
        draw_browser_text_clipped(page_x + 72u, card_y + 40u, ladybird_browser_result_url(i), link, page_w > 208u ? page_w - 208u : 0u);
        draw_browser_text_clipped(page_x + 72u, card_y + 62u, ladybird_browser_result_snippet(i), muted, page_w > 208u ? page_w - 208u : 0u);
        if (i == 0u) {
            fill_rounded_rect(page_x - 2u, card_y + 12u, 4u, row_h - 24u, 2u, accent);
        }
    }

    if (count == 0u) {
        fill_rounded_rect(page_x - 2u, row_y - 2u, page_w + 4u, 86u, 10u, border);
        fill_rounded_rect(page_x + 2u, row_y + 2u, page_w - 4u, 78u, 9u, panel);
        draw_browser_text_clipped(page_x + 28u, row_y + 24u, "No result", text, page_w > 56u ? page_w - 56u : 0u);
        draw_browser_text_clipped(page_x + 28u, row_y + 48u, "The network answered, but no usable link was extracted.", muted, page_w > 56u ? page_w - 56u : 0u);
    }

    compositor_mark_dirty(x, y, w, h);
}

void browser_submit_address_to_web(const char *address) {
    char pending[GUI_BROWSER_ADDRESS_MAX];

    if (address == NULL || address[0] == '\0') {
        return;
    }

    browser_normalize_submission(address, pending, sizeof(pending));
    if (pending[0] == '\0') {
        return;
    }

    ladybird_debug_log("GUI submit: push\n");
    browser_shell_set_address(pending);
    browser_address_focused = 0u;
    keyboard_focus_release();
    browser_native_home_visible = 0u;
    browser_loading_visible = 1u;
    browser_copy_string(browser_loading_text, sizeof(browser_loading_text), pending);
    ladybird_browser_results_hide();
    browser_results_drawn = 0u;
    (void)ladybird_browser_command_push(pending);
    ladybird_browser_yield_to_process();
    ladybird_debug_log("GUI submit: draw\n");
    render_active_app();
    gui_flush_scene_and_cursor();
    ladybird_debug_log("GUI submit: done\n");
}

static void append_u64_decimal(char *buffer, size_t capacity, size_t *length, uint64_t value) {
    char digits[20];
    size_t count = 0u;

    if (buffer == NULL || length == NULL || capacity == 0u) {
        return;
    }
    if (value == 0u) {
        if (*length + 1u < capacity) {
            buffer[(*length)++] = '0';
            buffer[*length] = '\0';
        }
        return;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count > 0u && *length + 1u < capacity) {
        buffer[(*length)++] = digits[--count];
    }
    buffer[*length] = '\0';
}

void browser_submit_content_click(uint64_t x, uint64_t y) {
    char command[48];
    size_t length = 0u;

    command[0] = '\0';
    command[length++] = 'c';
    command[length++] = 'l';
    command[length++] = 'i';
    command[length++] = 'c';
    command[length++] = 'k';
    command[length++] = ':';
    command[length] = '\0';
    append_u64_decimal(command, sizeof(command), &length, x);
    if (length + 1u < sizeof(command)) {
        command[length++] = ':';
        command[length] = '\0';
    }
    append_u64_decimal(command, sizeof(command), &length, y);
    (void)ladybird_browser_command_push(command);
    ladybird_browser_yield_to_process();
}

void browser_shell_handle_key(char key) {
    if (!browser_active || !browser_address_focused || key == 0) {
        return;
    }

    if (key == '\n' || key == '\r') {
        browser_results_drawn = 0u;
        if (browser_address_len == 0u) {
            browser_address_focused = 0u;
            keyboard_focus_release();
            browser_shell_set_address("vex://home");
            browser_native_home_visible = 1u;
            browser_loading_visible = 0u;
            browser_loading_text[0] = '\0';
            (void)ladybird_browser_command_push("vex://home");
            render_active_app();
            gui_flush_scene_and_cursor();
            return;
        }
        browser_submit_address_to_web(browser_address);
        return;
    }

    if (key == '\b') {
        if (browser_address_len > 0u) {
            browser_address[--browser_address_len] = '\0';
        }
        draw_browser_shell();
        gui_flush_scene_and_cursor();
        return;
    }

    if (key >= 32 && key <= 126 && browser_address_len + 1u < sizeof(browser_address)) {
        browser_results_drawn = 0u;
        browser_address[browser_address_len++] = key;
        browser_address[browser_address_len] = '\0';
        draw_browser_shell();
        gui_flush_scene_and_cursor();
    }
}

gui_window_control_t browser_control_hit(uint64_t x, uint64_t y) {
    if (!browser_active || browser_minimized) {
        return GUI_WINDOW_CONTROL_NONE;
    }

    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + 16u)) {
        return GUI_WINDOW_CONTROL_CLOSE;
    }
    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + 32u)) {
        return GUI_WINDOW_CONTROL_MINIMIZE;
    }
    if (window_control_circle_hit_at(x, y, browser_win_y, browser_win_x + 48u)) {
        return GUI_WINDOW_CONTROL_MAXIMIZE;
    }

    return GUI_WINDOW_CONTROL_NONE;
}

void set_browser_minimized(uint8_t minimized) {
    if (gui_framebuffer == NULL || !browser_active || browser_minimized == minimized) {
        return;
    }

    if (minimized) {
        browser_minimized = 1u;
        if (browser_previous_paint && paint_active) {
            paint_minimized = 0u;
            active_pid = 0u;
        } else if (active_pid == 0u) {
            active_pid = browser_previous_pid != 0u ? browser_previous_pid : terminal_pid_value;
        }
        render_active_app();
        draw_taskbar_static();
        return;
    }

    browser_minimized = 0u;
    if (browser_previous_paint && paint_active) {
        paint_minimized = 1u;
    }
    if (paint_previous_browser && paint_active) {
        paint_minimized = 1u;
        paint_drawing = 0u;
    }
    render_active_app();
    draw_taskbar_static();
}

int browser_handle_shell_click(uint64_t x, uint64_t y) {
    const uint64_t content_x = gui_content_x();
    const uint64_t content_y = gui_content_y();
    const uint64_t content_w = gui_content_width();
    const uint64_t content_h = gui_content_height();
    const uint64_t shell_x = browser_win_x + 8u;
    const uint64_t shell_y = browser_win_y + GUI_TITLE_HEIGHT + 6u;
    const uint64_t shell_w = browser_win_w > 16u ? browser_win_w - 16u : browser_win_w;
    const uint64_t address_x = shell_x + 100u;
    const uint64_t address_w = shell_w > 148u ? shell_w - 148u : 80u;
    uint64_t rx;
    uint64_t ry;

    if (!browser_active || browser_minimized || content_w == 0u || content_h == 0u) {
        return 0;
    }

    if (y >= shell_y + 3u && y < shell_y + 33u && x >= shell_x + 218u && x < shell_x + 242u) {
        browser_tab_count++;
        browser_address_focused = 0u;
        browser_shell_set_address("vex://home");
        browser_native_home_visible = 1u;
        browser_loading_visible = 0u;
        browser_loading_text[0] = '\0';
        (void)ladybird_browser_command_push("vex://home");
        render_active_app();
        gui_flush_scene_and_cursor();
        return 1;
    }

    if (y >= shell_y + 40u && y < shell_y + 66u && x >= address_x && x < address_x + address_w) {
        browser_address_focused = 1u;
        keyboard_focus_capture_current();
        draw_browser_shell();
        gui_flush_scene_and_cursor();
        return 1;
    }

    if (x >= content_x && y >= content_y && x < content_x + content_w && y < content_y + content_h) {
        if (ladybird_browser_results_visible()) {
            const uint64_t page_x = content_x + (content_w > 96u ? 72u : 18u);
            const uint64_t page_w = content_w > 144u ? content_w - 144u : (content_w > 24u ? content_w - 24u : content_w);
            const uint64_t row_y = content_y + 120u;
            const uint64_t row_h = 84u;
            const uint64_t gap = 14u;
            const uint32_t count = ladybird_browser_results_count();
            if (x >= page_x && x < page_x + page_w && y >= row_y) {
                const uint64_t offset = y - row_y;
                const uint32_t index = (uint32_t)(offset / (row_h + gap));
                const uint64_t local_y = offset % (row_h + gap);
                if (index < count && local_y < row_h) {
                    const char *url = ladybird_browser_result_url(index);
                    if (url != NULL && url[0] != '\0') {
                        ladybird_browser_results_hide();
                        browser_results_drawn = 0u;
                        browser_submit_address_to_web(url);
                        return 1;
                    }
                }
            }
            return 1;
        }
        rx = ((x - content_x) * 1800u) / content_w;
        ry = ((y - content_y) * 860u) / content_h;
        if (rx >= 560u && rx < 1240u && ry >= 290u && ry < 345u) {
            browser_address_focused = 1u;
            keyboard_focus_capture_current();
            draw_browser_shell();
            gui_flush_scene_and_cursor();
            return 1;
        }
        browser_submit_content_click(rx, ry);
        return 1;
    }

    return 0;
}

void gui_browser_begin(void) {
    if (gui_framebuffer == NULL) {
        return;
    }

    if (!browser_active && !terminal_minimized && active_pid == terminal_pid_value) {
        save_terminal_snapshot();
    }

    browser_previous_paint = (paint_active && !paint_minimized) ? 1u : 0u;
    if (browser_previous_paint) {
        paint_minimized = 1u;
        paint_drawing = 0u;
    }
    browser_previous_pid = active_pid;
    active_pid = 0u;
    browser_active = 1u;
    browser_minimized = 0u;
    browser_address_focused = 0u;
    browser_native_home_visible = 1u;
    browser_loading_visible = 0u;
    browser_loading_text[0] = '\0';
    browser_tab_count = browser_tab_count == 0u ? 1u : browser_tab_count;
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

void gui_browser_end(void) {
    uint32_t browser_pid;

    if (gui_framebuffer == NULL || !browser_active) {
        return;
    }

    browser_pid = ladybird_browser_process_pid();
    if (browser_pid != 0u) {
        process_kill(browser_pid);
    }
    browser_active = 0u;
    browser_minimized = 0u;
    if (browser_previous_paint && paint_active) {
        paint_minimized = 0u;
        active_pid = 0u;
    } else {
        terminal_minimized = 0u;
        active_pid = browser_previous_pid != 0u ? browser_previous_pid : terminal_pid_value;
    }
    browser_previous_paint = 0u;
    browser_address_focused = 0u;
    browser_native_home_visible = 0u;
    browser_loading_visible = 0u;
    browser_loading_text[0] = '\0';
    browser_results_drawn = 0u;
    render_active_app();
    draw_taskbar_static();
    send_app_config(terminal_pid_value, active_pid == terminal_pid_value, 0u);
    gui_flush_scene_and_cursor();
}

void gui_browser_results_changed(void) {
    if (!browser_active || browser_minimized) {
        return;
    }
    browser_native_home_visible = 0u;
    browser_loading_visible = 0u;
    browser_loading_text[0] = '\0';
    browser_results_drawn = 0u;
}

void gui_browser_frame_ready(void) {
    browser_loading_visible = 0u;
    browser_loading_text[0] = '\0';
}
