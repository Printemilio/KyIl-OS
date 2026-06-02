#ifndef UNOS_DEBUG_LOG_H
#define UNOS_DEBUG_LOG_H

#include <stddef.h>
#include <stdint.h>

void debug_log_init(void);
void debug_log_puts(const char *text);
size_t debug_log_copy_recent(char *buffer, size_t capacity, uint32_t *seq_inout);

#endif
