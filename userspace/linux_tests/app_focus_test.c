#include "../../kernel/src/app_surface.c"
extern int puts(const char *);
extern void abort(void);
static unsigned captures;
static unsigned desktop_redraws, compositions, flushes, completions;
static int present_result;
static vex_rect_t last_damage;
void keyboard_focus_capture_current(void) { ++captures; }
void render_active_app(void) { ++desktop_redraws; }
void draw_taskbar_static(void) { ++desktop_redraws; }
void gui_compositor_restore_foreground(const vex_rect_t *damage) {
    ++compositions; last_damage = *damage;
}
void gui_flush_scene_and_cursor(void) { ++flushes; }
int compositor_surface_present(uint32_t owner, compositor_surface_handle_t handle,
        const compositor_surface_present_info_t *present, uint64_t *serial) {
    (void)owner; (void)handle; (void)present; *serial = 10; return present_result;
}
int compositor_surface_complete(compositor_surface_identity_t identity, uint64_t serial) {
    (void)identity; if (serial != 10 || flushes != 1) abort(); ++completions; return 0;
}
#define CHECK(x) do { if (!(x)) abort(); } while (0)
int main(void) {
    app_surface_t *s = &app_surfaces[0];
    CHECK(!app_surface_has_keyboard_focus());
    s->window_id = 42; s->visible = 1;
    app_surface_focus(42);
    CHECK(app_surface_has_keyboard_focus() && captures == 1);
    app_surface_focus(42);
    CHECK(captures == 2); /* Also reclaim an already focused native window. */
    app_surface_blur();
    CHECK(!app_surface_has_keyboard_focus());
    app_surface_focus(42);
    CHECK(captures == 3);
    s->minimized = 1; CHECK(!app_surface_has_keyboard_focus());
    s->minimized = 0; s->close_pending = 1;
    CHECK(!app_surface_has_keyboard_focus());
    app_surface_focus(42); CHECK(captures == 3);
    s->close_pending = 0; s->visible = 0;
    CHECK(!app_surface_has_keyboard_focus());
    app_surface_focused_window_id = 99;
    CHECK(!app_surface_has_keyboard_focus());
    CHECK(app_surface_present(NULL, NULL) == -1);
    s->gpu_surface = 1; s->x = 46; s->y = 94; s->w = 776; s->h = 520;
    present_result = COMPOSITOR_SURFACE_ERROR_INVALID;
    CHECK(app_surface_present(s, NULL) == -1 && !compositions && !flushes && !completions);
    present_result = COMPOSITOR_SURFACE_OK;
    CHECK(app_surface_present(s, NULL) == 0);
    CHECK(compositions == 1 && flushes == 1 && completions == 1 && !desktop_redraws);
    CHECK(last_damage.x == 46 && last_damage.y == 94 && last_damage.w == 776 && last_damage.h == 520);
    puts("native app focus and presentation isolation: PASS");
    return 0;
}
