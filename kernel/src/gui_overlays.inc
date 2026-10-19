static void draw_context_menu_icon(uint64_t x, uint64_t y, int item, uint32_t accent) {
    fill_rounded_rect(x, y, 22u, 22u, 7u, pack_color(36, 24, 62));  /* dark glass icon bg */

    if (item == 0) {
        fill_rounded_rect(x + 5u, y + 6u, 12u, 9u, 2u, pack_color(50, 36, 80));
        fill_rounded_rect(x + 7u, y + 8u, 4u, 2u, 1u, accent);
        fill_rounded_rect(x + 11u, y + 13u, 5u, 1u, 1u, accent);
    } else if (item == 1) {
        fill_rounded_rect(x + 4u, y + 7u, 14u, 11u, 3u, pack_color(46, 32, 76));
        fill_rounded_rect(x + 4u, y + 6u, 8u, 4u, 2u, accent);
    } else if (item == 2) {
        fill_circle(x + 11u, y + 11u, 7u, pack_color(58, 36, 96));
        fill_circle(x + 11u, y + 11u, 4u, accent);
        fill_circle(x + 13u, y + 9u, 2u, pack_color(200, 180, 255));
    } else if (item == 3) {
        fill_circle(x + 11u, y + 11u, 6u, accent);
        fill_rounded_rect(x + 4u, y + 10u, 14u, 2u, 1u, pack_color(220, 210, 255));
    } else {
        fill_circle(x + 11u, y + 11u, 6u, pack_color(70, 48, 108));
        fill_rounded_rect(x + 10u, y + 6u, 2u, 10u, 1u, accent);
    }
}

static const char *switch_target_label(gui_switch_target_t target) {
    switch (target) {
    case GUI_SWITCH_TARGET_TERMINAL:
        return "Terminal";
    case GUI_SWITCH_TARGET_FILES:
        return "GFM";
    case GUI_SWITCH_TARGET_EDITOR:
        return "Editor";
    case GUI_SWITCH_TARGET_BROWSER:
        return "Browser";
    case GUI_SWITCH_TARGET_PAINT:
        return "VexPaint";
    default:
        return "";
    }
}

static gui_switch_target_t current_switch_target(void) {
    if (paint_active && !paint_minimized) {
        return GUI_SWITCH_TARGET_PAINT;
    }
    if (browser_active && !browser_minimized && browser_window_focused) {
        return GUI_SWITCH_TARGET_BROWSER;
    }
    if (file_manager_opened && active_pid == file_manager_pid_value) {
        return GUI_SWITCH_TARGET_FILES;
    }
    if (editor_opened && active_pid == editor_pid_value) {
        return GUI_SWITCH_TARGET_EDITOR;
    }
    if (terminal_pid_value != 0u) {
        return GUI_SWITCH_TARGET_TERMINAL;
    }
    return GUI_SWITCH_TARGET_NONE;
}

static void app_switcher_add_target(gui_switch_target_t target) {
    if (target == GUI_SWITCH_TARGET_NONE || app_switcher_count >= GUI_APP_SWITCHER_MAX) {
        return;
    }

    for (uint8_t i = 0u; i < app_switcher_count; ++i) {
        if (app_switcher_targets[i] == target) {
            return;
        }
    }

    app_switcher_targets[app_switcher_count++] = target;
}

static void app_switcher_collect(void) {
    const gui_switch_target_t current = current_switch_target();

    app_switcher_count = 0u;

    app_switcher_add_target(current);
    if (terminal_pid_value != 0u) {
        app_switcher_add_target(GUI_SWITCH_TARGET_TERMINAL);
    }
    if (file_manager_opened && file_manager_pid_value != 0u) {
        app_switcher_add_target(GUI_SWITCH_TARGET_FILES);
    }
    if (editor_opened && editor_pid_value != 0u) {
        app_switcher_add_target(GUI_SWITCH_TARGET_EDITOR);
    }
    if (browser_active) {
        app_switcher_add_target(GUI_SWITCH_TARGET_BROWSER);
    }
    if (paint_active) {
        app_switcher_add_target(GUI_SWITCH_TARGET_PAINT);
    }
}

static void app_switcher_apply(gui_switch_target_t target) {
    switch (target) {
    case GUI_SWITCH_TARGET_TERMINAL:
        switch_to_pid(terminal_pid_value);
        break;
    case GUI_SWITCH_TARGET_FILES:
        switch_to_pid(file_manager_pid_value);
        break;
    case GUI_SWITCH_TARGET_EDITOR:
        switch_to_pid(editor_pid_value);
        break;
    case GUI_SWITCH_TARGET_BROWSER:
        if (browser_active) {
            browser_minimized = 0u;
            browser_window_focused=1u;keyboard_focus_capture_current();
            if (paint_active) {
                paint_minimized = 1u;
                paint_drawing = 0u;
            }
            active_pid = 0u;
            render_active_app();
            draw_taskbar_static();
        }
        break;
    case GUI_SWITCH_TARGET_PAINT:
        if (paint_active) {
            paint_minimized = 0u;
            if (browser_active) {
                browser_minimized = 1u;
            }
            active_pid = 0u;
            render_active_app();
            draw_taskbar_static();
        }
        break;
    default:
        break;
    }
}

static void app_switcher_commit(void) {
    gui_switch_target_t target;

    if (!app_switcher_visible || app_switcher_count == 0u || app_switcher_selected >= app_switcher_count) {
        app_switcher_visible = 0u;
        return;
    }

    target = app_switcher_targets[app_switcher_selected];
    app_switcher_visible = 0u;
    app_switcher_apply(target);
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

static void close_paint_view(uint8_t keep_open) {
    paint_drawing = 0u;
    paint_minimized = 0u;

    if (keep_open == 0u) {
        paint_active = 0u;
    }

    if (paint_previous_browser && browser_active) {
        browser_minimized = 0u;
        active_pid = 0u;
    } else {
        active_pid = paint_previous_pid != 0u ? paint_previous_pid : terminal_pid_value;
    }

    paint_previous_browser = 0u;
    render_active_app();
    draw_taskbar_static();
}

static void app_switcher_cycle(int reverse) {
    gui_switch_target_t current;
    uint64_t now;

    app_switcher_collect();
    if (app_switcher_count < 2u) {
        return;
    }

    now = irq_timer_ticks();
    if (!app_switcher_visible) {
        current = current_switch_target();
        app_switcher_selected = 0u;
        for (uint8_t i = 0u; i < app_switcher_count; ++i) {
            if (app_switcher_targets[i] == current) {
                app_switcher_selected = i;
                break;
            }
        }
        app_switcher_visible = 1u;
        app_switcher_anim_start_tick = now;
        app_switcher_anim_step = 0u;
    }

    if (reverse) {
        app_switcher_selected = app_switcher_selected == 0u ? (uint8_t)(app_switcher_count - 1u) : (uint8_t)(app_switcher_selected - 1u);
    } else {
        app_switcher_selected = (uint8_t)((app_switcher_selected + 1u) % app_switcher_count);
    }

    app_switcher_deadline_tick = now + GUI_APP_SWITCHER_TIMEOUT_TICKS;
    render_active_app();
    draw_taskbar_static();
    gui_flush_scene_and_cursor();
}

static void draw_app_switcher_overlay(void) {
    uint64_t panel_w;
    uint64_t panel_h;
    uint64_t panel_x;
    uint64_t panel_y;
    uint64_t row_y;
    uint64_t now;
    uint64_t elapsed;
    uint64_t offset_y = 0u;
    const uint32_t panel      = pack_color(22, 20, 36);   /* deep glass */
    const uint32_t panel_head = pack_color(30, 28, 48);   /* elevated header */
    const uint32_t border     = pack_color(52, 47, 80);   /* subtle border */
    const uint32_t text_col   = pack_color(212, 206, 236); /* warm white */
    const uint32_t muted      = pack_color(108, 100, 138); /* muted text */
    const uint32_t accent     = pack_color(96, 86, 152);   /* soft accent */

    if (!app_switcher_visible || gui_framebuffer == NULL || app_switcher_count == 0u) {
        return;
    }

    if (vex_lvgl_ui_ready()) {
        const char *labels[GUI_APP_SWITCHER_MAX];

        now = irq_timer_ticks();
        elapsed = now > app_switcher_anim_start_tick ? now - app_switcher_anim_start_tick : 0u;
        if (elapsed < GUI_APP_SWITCHER_ANIM_TICKS) {
            offset_y = GUI_APP_SWITCHER_ANIM_TICKS - elapsed;
            app_switcher_anim_step = (uint8_t)elapsed;
        } else {
            app_switcher_anim_step = (uint8_t)GUI_APP_SWITCHER_ANIM_TICKS;
        }
        for (uint8_t i = 0u; i < app_switcher_count; ++i) {
            labels[i] = switch_target_label(app_switcher_targets[i]);
        }
        vex_lvgl_ui_draw_app_switcher(labels, app_switcher_count,
            app_switcher_selected, (int32_t)offset_y);
        return;
    }

    now = irq_timer_ticks();
    elapsed = now > app_switcher_anim_start_tick ? now - app_switcher_anim_start_tick : 0u;
    if (elapsed < GUI_APP_SWITCHER_ANIM_TICKS) {
        offset_y = GUI_APP_SWITCHER_ANIM_TICKS - elapsed;
        app_switcher_anim_step = (uint8_t)elapsed;
    } else {
        app_switcher_anim_step = (uint8_t)GUI_APP_SWITCHER_ANIM_TICKS;
    }

    panel_w = 280u;
    panel_h = 56u + (uint64_t)app_switcher_count * 34u;
    panel_x = gui_framebuffer->width > panel_w ? (gui_framebuffer->width - panel_w) / 2u : 0u;
    panel_y = gui_framebuffer->height > panel_h ? (gui_framebuffer->height - panel_h) / 2u : 0u;
    if (offset_y < panel_y) {
        panel_y -= offset_y;
    }

    fill_rounded_rect(panel_x + 4u, panel_y + 6u, panel_w + 2u, panel_h + 2u, 17u, dim_color(pack_color(20, 12, 40), 80u));
    fill_rounded_rect(panel_x + 2u, panel_y + 3u, panel_w + 1u, panel_h + 1u, 17u, dim_color(pack_color(60, 30, 120), 50u));
    fill_rounded_rect(panel_x, panel_y, panel_w, panel_h, 16u, border);
    fill_rounded_rect(panel_x + 1u, panel_y + 1u, panel_w - 2u, panel_h - 2u, 15u, panel);
    fill_rounded_rect(panel_x + 1u, panel_y + 1u, panel_w - 2u, 38u, 15u, panel_head);
    fill_rect(panel_x + 16u, panel_y + 1u, panel_w > 32u ? panel_w - 32u : panel_w, 1u, pack_color(58, 52, 86));
    tty_draw_text_transparent_at(panel_x + 18u, panel_y + 14u, "Switcher", text_col);
    tty_draw_text_transparent_at(panel_x + panel_w - 82u, panel_y + 14u, "Alt+Tab", muted);

    row_y = panel_y + 48u;
    for (uint8_t i = 0u; i < app_switcher_count; ++i) {
        const uint8_t selected = i == app_switcher_selected;
        const uint32_t row_bg = selected ? pack_color(44, 40, 68) : pack_color(28, 26, 44);
        fill_rounded_rect(panel_x + 10u, row_y, panel_w - 20u, 28u, 9u, row_bg);
        if (selected) {
            fill_rounded_rect(panel_x + 10u, row_y, 3u, 28u, 2u, accent);
        }
        fill_circle(panel_x + 24u, row_y + 14u, 6u, selected ? accent : pack_color(52, 46, 78));
        tty_draw_text_transparent_at(panel_x + 42u, row_y + 7u, switch_target_label(app_switcher_targets[i]), text_col);
        row_y += 34u;
    }
}

/* Row under the pointer while the menu is open (-1: none). */
static int context_menu_hover = -1;

static void draw_context_menu(void) {
    const ui_theme_t *theme = ui_theme();
    const uint32_t menu   = theme_px(theme->window);
    const uint32_t border = theme_px(theme->field_border);
    const uint32_t sep    = theme_px(theme->divider);
    const uint32_t text   = theme_px(theme->text);
    const uint32_t accent = theme_px(theme->accent);
    const uint64_t menu_w = GUI_CONTEXT_MENU_WIDTH;
    const uint64_t menu_h = GUI_CONTEXT_MENU_HEIGHT;
    const uint64_t x = context_menu_x;
    const uint64_t y = context_menu_y;
    /* Applications | appearance | power */
    const char *labels[] = { "Terminal", "Files", "Browser", "Paint", "Editor",
        ui_theme_mode() == UI_THEME_DARK ? "Light mode" : "Dark mode", "Shutdown", "Reboot" };

    if (vex_lvgl_ui_ready()) {
        vex_lvgl_ui_set_context_hover(context_menu_hover);
        vex_lvgl_ui_draw_context_menu((uint32_t)x, (uint32_t)y,
            labels, NULL, 8u);
        context_menu_visible = 1;
        return;
    }
    fill_rounded_rect(x, y, menu_w, menu_h, 7u, border);
    fill_rounded_rect(x + 1u, y + 1u, menu_w - 2u, menu_h - 2u, 6u, menu);

    for (int i = 0; i < 8; ++i) {
        const uint64_t row_y = y + 8u + (uint64_t)i * GUI_CONTEXT_MENU_ROW_HEIGHT;
        if (i == 5 || i == 6) {
            fill_rect(x + 14u, row_y - 4u, menu_w - 28u, 1u, sep);
        }
        fill_rounded_rect(x + GUI_CONTEXT_MENU_PAD, row_y,
                          menu_w - GUI_CONTEXT_MENU_PAD * 2u,
                          GUI_CONTEXT_MENU_ROW_HEIGHT, 0u, menu);
        draw_context_menu_icon(x + 16u, row_y + 8u, i, accent);
        tty_draw_text_transparent_at(x + 48u, row_y + 11u, labels[i], text);
    }
    context_menu_visible = 1;
}

static void open_context_menu(void) {
    const uint64_t max_x = gui_framebuffer->width > GUI_CONTEXT_MENU_WIDTH
        ? gui_framebuffer->width - GUI_CONTEXT_MENU_WIDTH : 0u;
    const uint64_t max_y = gui_framebuffer->height > GUI_CONTEXT_MENU_HEIGHT
        ? gui_framebuffer->height - GUI_CONTEXT_MENU_HEIGHT : 0u;
    context_menu_x = cursor_x < max_x ? cursor_x : max_x;
    context_menu_y = cursor_y < max_y ? cursor_y : max_y;
    context_menu_hover = -1;
    draw_context_menu();
}

static void hide_context_menu(void) {
    const uint64_t x = context_menu_x;
    const uint64_t y = context_menu_y;

    if (!context_menu_visible || gui_framebuffer == NULL) {
        return;
    }

    context_menu_visible = 0;
    vex_lvgl_ui_hide_context_menu();
    draw_wallpaper_rect(x > 4u ? x - 4u : 0u, y, GUI_CONTEXT_MENU_WIDTH + 12u, GUI_CONTEXT_MENU_HEIGHT + 12u);
    render_active_app();
    draw_taskbar_static();
}

static int context_menu_item_at(uint64_t x, uint64_t y) {
    if (!context_menu_visible) {
        return -1;
    }

    if (x < context_menu_x || x >= context_menu_x + GUI_CONTEXT_MENU_WIDTH ||
        y < context_menu_y || y >= context_menu_y + GUI_CONTEXT_MENU_HEIGHT) {
        return -1;
    }

    if (y < context_menu_y + 8u) {
        return -1;
    }

    {
        const uint64_t item = (y - context_menu_y - 8u) / GUI_CONTEXT_MENU_ROW_HEIGHT;
        if (item >= 8u) {
            return -1;
        }

        return (int)item;
    }
}
