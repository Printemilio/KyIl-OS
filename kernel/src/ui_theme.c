#include <unos/ui_theme.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unos/fs.h>

/* Dark is the KyIlOS of the Figma frames: violet glass over a blue-to-magenta
 * gradient, #141414 windows with a violet hairline under the title bar. */
static const ui_theme_t ui_theme_dark = {
    .name = "dark",
    .wallpaper_a = 0x893597u,
    .wallpaper_b = 0x5d429bu,
    .wallpaper_c = 0x2f589cu,
    .desktop_text = 0xffffffu,
    .bar = 0x141414u,
    .bar_alpha = 122u,
    .bar_text = 0xffffffu,
    .bar_icon = 0x9c9c9cu,
    .bar_hover = 0xffffffu,
    .bar_hover_alpha = 34u,
    .window = 0x141414u,
    .panel = 0x1b1a1fu,
    .field = 0x1a191eu,
    .field_border = 0x4a3d57u,
    .hover = 0x26242cu,
    .divider = 0x483156u,
    .text = 0xf2f2f2u,
    .muted = 0x9a93a6u,
    .faint = 0x6b6676u,
    .accent = 0xa98bffu,
    .accent_soft = 0x34264au,
    .on_accent = 0xffffffu,
    .selection = 0x4b3670u,
    .success = 0x44d16eu,
    .warning = 0xe8c547u,
    .danger = 0xff5a5fu,
    .folder = 0xc99914u,
    .paper = 0xd9d9d9u,
    .control_close = 0xff5f57u,
    .control_minimize = 0xfebc2eu,
    .control_maximize = 0x28c840u,
    .control_idle = 0x3a3940u,
    .accent2 = 0x5aa3fau,
    .border_active_a = 0x9c7bffu,
    .border_active_b = 0x5aa3fau,
    .border_inactive = 0x2a2930u,
};

/* Light keeps the same hues and hierarchy: a pastel version of the gradient,
 * white glass, near-white windows and ink-coloured text. */
static const ui_theme_t ui_theme_light = {
    .name = "light",
    .wallpaper_a = 0xd9a6e3u,
    .wallpaper_b = 0xb3a6ecu,
    .wallpaper_c = 0x93b5ecu,
    .desktop_text = 0x241b3bu,
    .bar = 0xffffffu,
    .bar_alpha = 150u,
    .bar_text = 0x16141cu,
    .bar_icon = 0x4e4a58u,
    .bar_hover = 0x000000u,
    .bar_hover_alpha = 20u,
    .window = 0xf7f6fau,
    .panel = 0xefecf4u,
    .field = 0xffffffu,
    .field_border = 0xcbbfdau,
    .hover = 0xe7e2efu,
    .divider = 0xd8cce6u,
    .text = 0x18161fu,
    .muted = 0x5f5a6bu,
    .faint = 0x8e8899u,
    .accent = 0x7b45e8u,
    .accent_soft = 0xe6ddfbu,
    .on_accent = 0xffffffu,
    .selection = 0xdccff7u,
    .success = 0x1f9d55u,
    .warning = 0xb88a00u,
    .danger = 0xd93036u,
    .folder = 0xd4a21cu,
    .paper = 0xdadae2u,
    .control_close = 0xff5f57u,
    .control_minimize = 0xfebc2eu,
    .control_maximize = 0x28c840u,
    .control_idle = 0xd4d0dau,
    .accent2 = 0x2f6fe0u,
    .border_active_a = 0x8b5cf6u,
    .border_active_b = 0x3b82f6u,
    .border_inactive = 0xd9d3e2u,
};

static ui_theme_mode_t current_mode = UI_THEME_DARK;
static uint32_t generation = 1u;

const ui_theme_t *ui_theme(void) {
    return current_mode == UI_THEME_LIGHT ? &ui_theme_light : &ui_theme_dark;
}

ui_theme_mode_t ui_theme_mode(void) {
    return current_mode;
}

uint32_t ui_theme_generation(void) {
    return generation;
}

int ui_theme_set_mode(ui_theme_mode_t mode) {
    if (mode != UI_THEME_DARK && mode != UI_THEME_LIGHT) {
        return 0;
    }
    if (mode == current_mode) {
        return 0;
    }
    current_mode = mode;
    ++generation;
    (void)ui_theme_publish();
    return 1;
}

static size_t append_token(char *out, size_t used, size_t capacity, const char *name, uint32_t color) {
    int written;

    if (used >= capacity) {
        return used;
    }
    written = snprintf(out + used, capacity - used, "%s=#%06x\n", name, (unsigned)(color & 0xffffffu));
    return written > 0 ? used + (size_t)written : used;
}

/* The file every application reads. One "name=#rrggbb" per line, plus the
 * mode and the two glass alphas. Suchu resolves `color: text` against it. */
int ui_theme_publish(void) {
    const ui_theme_t *t = ui_theme();
    char text[1800];
    size_t used = 0u;
    int written = snprintf(text, sizeof(text),
        "# KyIlOS system theme. Written by the desktop; read by every application.\n"
        "mode=%s\nbar_alpha=%u\nbar_hover_alpha=%u\n",
        t->name, (unsigned)t->bar_alpha, (unsigned)t->bar_hover_alpha);

    if (written <= 0) {
        return -1;
    }
    used = (size_t)written;
    used = append_token(text, used, sizeof(text), "wallpaper_a", t->wallpaper_a);
    used = append_token(text, used, sizeof(text), "wallpaper_b", t->wallpaper_b);
    used = append_token(text, used, sizeof(text), "wallpaper_c", t->wallpaper_c);
    used = append_token(text, used, sizeof(text), "desktop_text", t->desktop_text);
    used = append_token(text, used, sizeof(text), "bar", t->bar);
    used = append_token(text, used, sizeof(text), "bar_text", t->bar_text);
    used = append_token(text, used, sizeof(text), "bar_icon", t->bar_icon);
    used = append_token(text, used, sizeof(text), "window", t->window);
    used = append_token(text, used, sizeof(text), "panel", t->panel);
    used = append_token(text, used, sizeof(text), "field", t->field);
    used = append_token(text, used, sizeof(text), "field_border", t->field_border);
    used = append_token(text, used, sizeof(text), "hover", t->hover);
    used = append_token(text, used, sizeof(text), "divider", t->divider);
    used = append_token(text, used, sizeof(text), "text", t->text);
    used = append_token(text, used, sizeof(text), "muted", t->muted);
    used = append_token(text, used, sizeof(text), "faint", t->faint);
    used = append_token(text, used, sizeof(text), "accent", t->accent);
    used = append_token(text, used, sizeof(text), "accent_soft", t->accent_soft);
    used = append_token(text, used, sizeof(text), "on_accent", t->on_accent);
    used = append_token(text, used, sizeof(text), "selection", t->selection);
    used = append_token(text, used, sizeof(text), "success", t->success);
    used = append_token(text, used, sizeof(text), "warning", t->warning);
    used = append_token(text, used, sizeof(text), "danger", t->danger);
    used = append_token(text, used, sizeof(text), "folder", t->folder);
    used = append_token(text, used, sizeof(text), "paper", t->paper);
    used = append_token(text, used, sizeof(text), "control_close", t->control_close);
    used = append_token(text, used, sizeof(text), "control_minimize", t->control_minimize);
    used = append_token(text, used, sizeof(text), "control_maximize", t->control_maximize);
    used = append_token(text, used, sizeof(text), "control_idle", t->control_idle);
    used = append_token(text, used, sizeof(text), "accent2", t->accent2);
    used = append_token(text, used, sizeof(text), "border_active_a", t->border_active_a);
    used = append_token(text, used, sizeof(text), "border_active_b", t->border_active_b);
    used = append_token(text, used, sizeof(text), "border_inactive", t->border_inactive);
    if (used >= sizeof(text)) {
        return -1;
    }
    if (fs_write(UI_THEME_PATH, text, used) != 0) {
        return -1;
    }
    (void)fs_set_mode(UI_THEME_PATH, 0644u);
    return 0;
}

void ui_theme_init(void) {
    ora_view_t lease __attribute__((cleanup(ora_view_release))) = {0};
    void *data = NULL;
    size_t size = 0u;

    if (fs_read_view(UI_THEME_PATH, &data, &size, &lease) == 0 && data != NULL) {
        static const char key[] = "mode=light";
        const char *text = data;
        for (size_t i = 0u; i + sizeof(key) - 1u <= size; ++i) {
            if ((i == 0u || text[i - 1u] == '\n') &&
                memcmp(text + i, key, sizeof(key) - 1u) == 0) {
                current_mode = UI_THEME_LIGHT;
                break;
            }
        }
    }
    (void)ui_theme_publish();
}
