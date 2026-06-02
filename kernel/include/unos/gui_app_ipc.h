#ifndef UNOS_GUI_APP_IPC_H
#define UNOS_GUI_APP_IPC_H

#include <stdint.h>

#define GUI_APP_IPC_PATH_MAX 128u

typedef enum gui_app_command_type {
    GUI_APP_CMD_CONFIGURE = 1,
    GUI_APP_CMD_MOUSE_CLICK = 2,
    GUI_APP_CMD_KEY = 3,
    GUI_APP_CMD_OPEN_FILE = 4,
    GUI_APP_CMD_REFRESH = 5,
    GUI_APP_CMD_TOGGLE = 6,
} gui_app_command_type_t;

typedef struct gui_app_command {
    uint32_t type;
    uint32_t flags;
    uint64_t x;
    uint64_t y;
    uint64_t w;
    uint64_t h;
    char key;
    char path[GUI_APP_IPC_PATH_MAX];
} gui_app_command_t;

typedef enum gui_app_event_type {
    GUI_APP_EVENT_NONE = 0,
    GUI_APP_EVENT_OPEN_FILE = 1,
} gui_app_event_type_t;

typedef struct gui_app_event {
    uint32_t type;
    char path[GUI_APP_IPC_PATH_MAX];
} gui_app_event_t;

#endif
