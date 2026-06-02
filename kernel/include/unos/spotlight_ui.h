#ifndef UNOS_SPOTLIGHT_UI_H
#define UNOS_SPOTLIGHT_UI_H

#include <stdint.h>
#include <unos/gui_app_ipc.h>

void spotlight_ui_toggle(void);
void spotlight_ui_close(void);
int spotlight_ui_is_open(void);
void spotlight_ui_tick(void);
void spotlight_ui_handle_key(char key);
void spotlight_ui_handle_click(uint64_t x, uint64_t y);
void spotlight_ui_voice_listen(void);
int spotlight_ui_take_shell_return(void);
uint32_t spotlight_app_start(void);
uint32_t spotlight_app_pid(void);
void spotlight_app_send(const gui_app_command_t *command);

#endif
