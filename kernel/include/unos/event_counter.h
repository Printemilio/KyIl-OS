#ifndef UNOS_EVENT_COUNTER_H
#define UNOS_EVENT_COUNTER_H

#include <stddef.h>
#include <stdint.h>

/* Native VexOS event counter.  The capability is an opaque, non-sequential
 * token; Linux eventfd descriptors are merely a compatibility view over it. */
#ifndef VEX_EVENT_COUNTER_SEMAPHORE
#define VEX_EVENT_COUNTER_SEMAPHORE 0x1u
#endif
#ifndef VEX_EVENT_COUNTER_NONBLOCK
#define VEX_EVENT_COUNTER_NONBLOCK  0x2u
#endif

int vex_event_counter_create(uint64_t initial, uint32_t flags,
                             uint64_t *capability_out);
int vex_event_counter_create_owned(uint32_t owner_pid, uint64_t initial,
                                   uint32_t flags, uint64_t *capability_out);
int vex_event_counter_authorize_owner(uint32_t owner_pid,
                                      uint64_t capability);
int vex_event_counter_release_owned(uint32_t owner_pid,
                                    uint64_t capability);
int vex_event_counter_signal(uint64_t capability, uint64_t value);
int vex_event_counter_read(uint64_t capability, uint64_t *value_out,
                           uint32_t flags);
int vex_event_counter_read_timed(uint64_t capability, uint64_t *value_out,
                                 uint32_t flags, uint64_t timeout_ticks);
int vex_event_counter_poll(uint64_t capability, short events, short *revents);
int vex_event_counter_retain(uint64_t capability);
int vex_event_counter_release(uint64_t capability);
void vex_event_counter_cancel_pid(uint32_t pid);
/* Internal: caller guarantees that this TID can no longer execute its stack. */
void vex_event_counter_thread_stopped(uint32_t tid);
void vex_event_counter_reap_pid(uint32_t pid);

#endif
