#ifndef KYIL_BOOT_CONSOLE_H
#define KYIL_BOOT_CONSOLE_H
#include <stdint.h>
/* Direct framebuffer diagnostic path: no heap, GUI, scheduler, serial or timer. */
void boot_console_init(void);
void boot_console_puts(const char *text);
void boot_console_hex(uint64_t value);
void boot_console_stage(const char *name,int completed);
void boot_console_finish(void);
int boot_console_active(void);
__attribute__((noreturn)) void boot_console_fail(const char *reason);
#endif
