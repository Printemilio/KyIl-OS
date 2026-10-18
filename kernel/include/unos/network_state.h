#ifndef UNOS_NETWORK_STATE_H
#define UNOS_NETWORK_STATE_H
#include <stdint.h>
/* Short protocol/device transactions, never a connect/read/wait lifetime. */
uint64_t network_state_lock(void);
void network_state_unlock(uint64_t flags);
void dns_thread_stopped(uint32_t tid);
#endif
