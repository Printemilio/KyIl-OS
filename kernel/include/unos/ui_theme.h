#ifndef UNOS_UI_THEME_H
#define UNOS_UI_THEME_H

#include <stdint.h>

/* The system palette.
 *
 * One table is the whole visual language of KyIlOS: the desktop shell draws
 * with it, and it is published to UI_THEME_PATH so that every Suchu
 * application resolves the same names ('background: window; color: text;').
 * Colours are 0xRRGGBB; alphas are 0..255. */

#define UI_THEME_PATH "/etc/kyil/theme.conf"

typedef enum ui_theme_mode {
    UI_THEME_DARK = 0,
    UI_THEME_LIGHT = 1,
} ui_theme_mode_t;

typedef struct ui_theme {
    const char *name;

    /* Desktop */
    uint32_t wallpaper_a;      /* top-right corner of the diagonal gradient */
    uint32_t wallpaper_b;      /* centre */
    uint32_t wallpaper_c;      /* bottom-left corner */
    uint32_t desktop_text;     /* wordmark and icon labels */

    /* Top bar (frosted glass over the wallpaper) */
    uint32_t bar;
    uint8_t bar_alpha;
    uint32_t bar_text;
    uint32_t bar_icon;
    uint32_t bar_hover;
    uint8_t bar_hover_alpha;

    /* Windows and controls */
    uint32_t window;           /* window body and title bar */
    uint32_t panel;            /* raised areas: sidebars, toolbars, menus */
    uint32_t field;            /* text inputs */
    uint32_t field_border;
    uint32_t hover;
    uint32_t divider;          /* title bar separator, tab strips */
    uint32_t text;
    uint32_t muted;            /* secondary text */
    uint32_t faint;            /* hints, placeholders */
    uint32_t accent;
    uint32_t accent_soft;      /* button backgrounds */
    uint32_t on_accent;
    uint32_t selection;
    uint32_t success;
    uint32_t warning;
    uint32_t danger;
    uint32_t folder;
    uint32_t paper;
    uint32_t control_close;
    uint32_t control_minimize;
    uint32_t control_maximize;
    uint32_t control_idle;     /* traffic lights of a window in the background */
    uint32_t accent2;          /* the blue end of the brand gradient */
    /* Hyprland-style frame: the focused window wears a 2px gradient ring
     * (border_active_a -> border_active_b), the others a 1px hairline. */
    uint32_t border_active_a;
    uint32_t border_active_b;
    uint32_t border_inactive;
} ui_theme_t;

const ui_theme_t *ui_theme(void);
ui_theme_mode_t ui_theme_mode(void);
/* Changes the palette and republishes it. Returns 1 when the mode changed. */
int ui_theme_set_mode(ui_theme_mode_t mode);
/* Bumped on every change, so caches derived from the palette can notice. */
uint32_t ui_theme_generation(void);
/* Reads a previously chosen mode from UI_THEME_PATH, then publishes. */
void ui_theme_init(void);
int ui_theme_publish(void);

#endif
