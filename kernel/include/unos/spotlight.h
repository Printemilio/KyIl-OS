#ifndef UNOS_SPOTLIGHT_H
#define UNOS_SPOTLIGHT_H

#include <stddef.h>
#include <stdint.h>

typedef enum spotlight_result_kind {
    SPOTLIGHT_RESULT_FILE,
    SPOTLIGHT_RESULT_APP,
    SPOTLIGHT_RESULT_COMMAND
} spotlight_result_kind_t;

typedef enum spotlight_action {
    SPOTLIGHT_ACTION_OPEN_PATH,
    SPOTLIGHT_ACTION_RUN_PROGRAM,
    SPOTLIGHT_ACTION_OPEN_TERMINAL,
    SPOTLIGHT_ACTION_OPEN_FILES,
    SPOTLIGHT_ACTION_OPEN_EDITOR,
    SPOTLIGHT_ACTION_OPEN_BROWSER,
    SPOTLIGHT_ACTION_RUN_SYSINFO,
    SPOTLIGHT_ACTION_RUN_PS,
    SPOTLIGHT_ACTION_RUN_LOG,
    SPOTLIGHT_ACTION_NONE
} spotlight_action_t;

typedef struct spotlight_result_view {
    const char *path;
    const char *title;
    const char *description;
    const char *category;
    spotlight_result_kind_t kind;
    spotlight_action_t action;
    uint8_t program;
    uint8_t dangerous;
} spotlight_result_view_t;

void spotlight_search(const char *query);
void spotlight_index_build(void);
void spotlight_index_add(const char *path, size_t size);
void spotlight_index_remove(const char *path);
size_t spotlight_query(const char *query);
const spotlight_result_view_t *spotlight_result(size_t index);
void spotlight_record_selection(size_t index);

#endif
