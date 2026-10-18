#ifndef UNOS_COMPAT_LINUX_FUTEX_PI_H
#define UNOS_COMPAT_LINUX_FUTEX_PI_H
#include <stdint.h>
/* Linux PI word encoding stays in the personality. Native FlowSched only
 * knows explicit task dependencies and bounded wait ordering. */
int linux_futex_pi_lock(uint64_t address,int try_only);
int linux_futex_pi_unlock(uint64_t address);
void linux_futex_pi_cancel_tid(uint32_t tid);
/* Actual execution context retirement, not merely a kill/cancel request. */
void linux_futex_pi_thread_stopped(uint32_t tid);
void linux_futex_pi_cancel_range(uint64_t cr3,uint64_t address,uint64_t length);
#endif
