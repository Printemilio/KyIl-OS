#ifndef UNOS_SCHED_CLOCK_H
#define UNOS_SCHED_CLOCK_H
#include <stdint.h>
/* BSP clock calibrated against crystal ratio or RTC, never a guessed frequency.
 * SMP activation must establish per-CPU offsets before using this timebase. */
void sched_clock_calibrate(void);
uint64_t sched_clock_ns(void);
uint64_t sched_clock_cycles(void);
uint64_t sched_clock_frequency(void);
#endif
