#ifndef UNOS_SHELL_H
#define UNOS_SHELL_H

#include <stdint.h>
#include <unos/gui_app_ipc.h>

void shell_init(void);
void shell_run(void);
uint32_t shell_pid(void);
void shell_app_send(const gui_app_command_t *command);
void shell_execute_line(const char *line);
void shell_submit_line(const char *line);

#endif
