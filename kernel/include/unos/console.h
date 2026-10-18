#ifndef KYIL_CONSOLE_H
#define KYIL_CONSOLE_H
#include <stddef.h>
#include <stdint.h>
/* Lease of the existing native command service, not a PTY or app renderer. */
int console_attach(uint32_t window,uint64_t *token);
int console_read(uint32_t window,uint64_t token,char *output,size_t size,uint32_t *flags);
int console_submit(uint32_t window,uint64_t token,const char *input,size_t size);
void console_detach(uint32_t owner,uint32_t window);
void console_emit(char byte);
void console_clear(void);
void console_command_begin(void);
void console_command_ready(void);
#endif
