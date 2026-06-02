#ifndef UNOS_TEXT_EDITOR_UI_H
#define UNOS_TEXT_EDITOR_UI_H

#include <stdint.h>
#include <unos/gui_app_ipc.h>

void text_editor_ui_new_document(void);
void text_editor_ui_open(const char *path);
void text_editor_ui_render(uint64_t x, uint64_t y, uint64_t w, uint64_t h);
int text_editor_ui_handle_key(char key);
int text_editor_ui_is_open(void);
const char *text_editor_ui_title(void);
uint32_t text_editor_app_start(void);
uint32_t text_editor_app_pid(void);
void text_editor_app_send(const gui_app_command_t *command);

#endif
