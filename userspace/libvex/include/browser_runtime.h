#ifndef VEX_BROWSER_RUNTIME_H
#define VEX_BROWSER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include <unos/vex_web_compat.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef vex_web_display_surface_t browser_display_surface_t;

int browser_display_open(uint32_t width, uint32_t height);
void browser_display_resize(uint32_t width, uint32_t height);
browser_display_surface_t* browser_display_surface(void);
void browser_display_present(void);
void browser_display_close(void);

int browser_command_read(char* buffer, size_t size);
void browser_results_begin(const char* query);
void browser_results_add(const char* title, const char* url, const char* snippet);
void browser_results_show(void);
void browser_results_hide(void);

void browser_debug_log(const char* text);
void browser_yield(void);
uint32_t browser_process_pid(void);

int browser_process_spawn(const char* path, uint32_t* pid_out);
int browser_process_wait(int32_t pid, int* status, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif
