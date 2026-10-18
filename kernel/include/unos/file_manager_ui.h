#ifndef UNOS_FILE_MANAGER_UI_H
#define UNOS_FILE_MANAGER_UI_H

#include <stdint.h>
#include <unos/gui_app_ipc.h>

void file_manager_ui_open(void);
void file_manager_ui_open_path(const char *path);
void file_manager_ui_render(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
int file_manager_ui_handle_click(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
const char *file_manager_ui_take_open_request(void);
uint32_t file_manager_app_start(void);
uint32_t file_manager_app_pid(void);
void file_manager_app_send(const gui_app_command_t *command);
int file_manager_app_poll_event(gui_app_event_t *event);

#endif
