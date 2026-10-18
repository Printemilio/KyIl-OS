#ifndef UNOS_RTC_CLOCK_CALIBRATION_H
#define UNOS_RTC_CLOCK_CALIBRATION_H
#include <stdint.h>
/* Bootstrap only: caller exclusively owns CMOS index/data accesses. */
typedef struct {
    void *context;
    uint8_t (*read)(void *, uint8_t);
    uint64_t (*cycles)(void *);
    uint32_t poll_budget;
} rtc_clock_io_t;
typedef struct {
    uint64_t frequency, interval[2], lower_hz, upper_hz;
    uint32_t polls, rejected_uip, rejected_torn, rejected_invalid;
    uint32_t discontinuities, inconsistent_intervals;
    uint8_t edge_seconds[3];
} rtc_clock_result_t;
/* 0: measured, -1: bounded acquisition failed, -2: nonmonotonic TSC. */
int rtc_clock_measure(const rtc_clock_io_t *, rtc_clock_result_t *);
#endif
