#ifndef UNOS_DEBUG_LOG_H
#define UNOS_DEBUG_LOG_H

#include <stddef.h>
#include <stdint.h>

/* Verbose syscall/futex investigations are opt-in: UART I/O is synchronous
 * and incurs hypervisor exits. Keep errors and fatal diagnostics enabled. */
#ifndef VEX_COMPAT_TRACE
#define VEX_COMPAT_TRACE 0
#endif
#ifndef VEX_FPU_TRACE
#define VEX_FPU_TRACE 0
#endif
#ifndef VEX_DESKTOP_TRACE
#define VEX_DESKTOP_TRACE 0
#endif

void debug_log_init(void);
void debug_log_puts(const char *text);
/* Counted writes preserve stderr and allow UART FIFO batching across bytes. */
void debug_log_write(const void *bytes, size_t count);
size_t debug_log_copy_recent(char *buffer, size_t capacity, uint32_t *seq_inout);

#endif
