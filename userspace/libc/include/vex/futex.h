/* Native Vex futexes: wait on and wake a 32-bit word of the process. */
#ifndef _VEX_FUTEX_H
#define _VEX_FUTEX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sleep while *addr == expected, for at most timeout_ns nanoseconds (0: no
 * bound). Returns 0 after a wake (or a spurious wake-up), EAGAIN when *addr
 * did not hold `expected`, ETIMEDOUT, or EINTR when an event (signal) was
 * delivered. */
int vex_futex_wait(volatile uint32_t *addr, uint32_t expected, uint64_t timeout_ns);

/* Wake up to `count` threads waiting on addr; returns how many were woken. */
int vex_futex_wake(volatile uint32_t *addr, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
