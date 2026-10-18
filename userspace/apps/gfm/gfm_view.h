#ifndef GFM_VIEW_H
#define GFM_VIEW_H

/*
 * GFM's LVGL view: widgets, layout and styling only. It owns no file-system
 * state; the controller (gfm.c) pushes what to show and receives user intent
 * through gfm_view_callbacks_t.
 */

#include <stddef.h>
#include <stdint.h>

#include "gfm_fs.h"
#include "vex_lvgl.h"

typedef enum gfm_action {
    GFM_ACTION_BACK,
    GFM_ACTION_FORWARD,
    GFM_ACTION_UP,
    GFM_ACTION_REFRESH,
    GFM_ACTION_NEW_FOLDER,
    GFM_ACTION_RENAME,
    GFM_ACTION_COPY,
    GFM_ACTION_CUT,
    GFM_ACTION_PASTE,
    GFM_ACTION_DELETE,
    GFM_ACTION_OPEN,
    GFM_ACTION_INFO,
    GFM_ACTION_TOGGLE_EXECUTABLE,
    GFM_ACTION_PROMPT_ACCEPT,
    GFM_ACTION_PROMPT_CANCEL,
    GFM_ACTION_COUNT
} gfm_action_t;

typedef struct gfm_view_callbacks {
    void (*action)(gfm_action_t action, void *context);
    void (*row_clicked)(size_t index, void *context);
    void (*place_clicked)(const char *path, void *context);
    /* The user pressed Enter in the path field. */
    void (*path_submitted)(const char *text, void *context);
    void *context;
} gfm_view_callbacks_t;

/* What the toolbar and navigation buttons may do right now. */
typedef struct gfm_view_enabled {
    uint8_t flags[GFM_ACTION_COUNT];
} gfm_view_enabled_t;

typedef struct gfm_view_info {
    char name[GFM_SYS_NAME_MAX];
    char kind[24];
    char size[32];
    char permissions[16];
    char owner[16];
    char location[GFM_PATH_MAX];
    char contents[32];
    uint8_t can_open;
    uint8_t can_toggle_executable;
    uint8_t executable;
} gfm_view_info_t;

typedef enum gfm_prompt_kind {
    GFM_PROMPT_NONE,
    GFM_PROMPT_NEW_FOLDER,
    GFM_PROMPT_RENAME,
    GFM_PROMPT_CONFIRM_DELETE
} gfm_prompt_kind_t;

void gfm_view_build(vex_lvgl_app_t *app, const gfm_view_callbacks_t *callbacks);

/* Show a directory. `selected` is an index into the listing, or -1. */
void gfm_view_show_directory(const char *path, const gfm_listing_t *listing, int selected);
void gfm_view_select(const gfm_listing_t *listing, int selected);
void gfm_view_set_enabled(const gfm_view_enabled_t *enabled);
void gfm_view_set_places(const char *current_path);

/* A message in the status bar; errors are drawn in red. */
void gfm_view_set_status(const char *message, int is_error);
void gfm_view_set_summary(const char *summary);

void gfm_view_show_info(const gfm_view_info_t *info);   /* NULL hides the panel */
int gfm_view_info_visible(void);

void gfm_view_open_prompt(gfm_prompt_kind_t kind, const char *title, const char *initial_text,
                          const char *accept_label);
void gfm_view_close_prompt(void);
const char *gfm_view_prompt_text(void);
gfm_prompt_kind_t gfm_view_prompt_kind(void);

/* A text field has the keyboard: keys go to LVGL instead of the controller. */
int gfm_view_text_focused(void);
void gfm_view_release_text_focus(void);

#endif
