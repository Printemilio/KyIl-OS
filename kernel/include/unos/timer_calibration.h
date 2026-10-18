#ifndef UNOS_TIMER_CALIBRATION_H
#define UNOS_TIMER_CALIBRATION_H
#include <stdint.h>
/* Bootstrap caller owns PIT2 exclusively and masks interrupts. */
typedef struct {
    void *context;
    uint8_t (*input)(void *, uint16_t);
    void (*output)(void *, uint16_t, uint8_t);
    uint32_t (*apic_read)(void *, uint32_t);
    void (*apic_write)(void *, uint32_t, uint32_t);
    uint64_t (*cycles)(void *);
    uint64_t watchdog_cycles; /* calibrated TSC Hz, one second */
} timer_calibration_io_t;
typedef enum {
    TIMER_EXIT_CHECKS, TIMER_EXIT_PIT_LOAD_WATCHDOG, TIMER_EXIT_APIC_LOAD_WATCHDOG,
    TIMER_EXIT_WINDOW_WATCHDOG, TIMER_EXIT_TERMINAL_WATCHDOG, TIMER_EXIT_VERIFIED
} timer_calibration_exit_t;
typedef enum {
    TIMER_CAL_OK, TIMER_CAL_NO_CLOCK, TIMER_CAL_PIT_PROGRAM,
    TIMER_CAL_PIT_LOAD_TIMEOUT, TIMER_CAL_PIT_COUNT, TIMER_CAL_GATE,
    TIMER_CAL_APIC_PROGRAM, TIMER_CAL_APIC_LOAD_TIMEOUT, TIMER_CAL_PIT_STOPPED,
    TIMER_CAL_WINDOW_TIMEOUT, TIMER_CAL_EARLY_TERMINAL, TIMER_CAL_TERMINAL_TIMEOUT,
    TIMER_CAL_OUT_MISMATCH, TIMER_CAL_PIT_REVERSED, TIMER_CAL_APIC_STOPPED,
    TIMER_CAL_APIC_EXPIRED, TIMER_CAL_APIC_REVERSED, TIMER_CAL_TSC_REVERSED,
    TIMER_CAL_RATE_ZERO, TIMER_CAL_RATE_OVERFLOW, TIMER_CAL_REFERENCE_WINDOW
} timer_calibration_error_t;
typedef struct {
    uint8_t status, port, low, high;
    uint16_t pit;
    uint32_t apic_before, apic_after, apic;
    uint64_t tsc_before, tsc_after, tsc;
} timer_calibration_sample_t;
typedef struct {
    timer_calibration_error_t error;
    timer_calibration_exit_t exit_reason;
    timer_calibration_sample_t start, end;
    uint8_t port_before, port_armed, status_armed, start_valid, end_valid;
    uint8_t terminal_port, terminal_status, terminal_low, terminal_high;
    uint16_t reload, target_ticks, pit_loaded, terminal_pit;
    uint32_t apic_div, apic_lvt, reference_ticks, apic_ticks, count_per_ms;
    uint64_t cycles_elapsed, watchdog_cycles, terminal_tsc, setup_tsc, exit_tsc;
    uint64_t pit_load_iterations, apic_load_iterations, sample_iterations, terminal_iterations;
    uint64_t rate_numerator, rate_denominator, rate_raw;
} timer_calibration_result_t;
timer_calibration_error_t timer_calibration_rate(uint32_t apic_ticks,
    uint32_t reference_ticks, uint64_t *numerator, uint64_t *denominator, uint64_t *rate);
timer_calibration_error_t timer_calibration_measure(
    const timer_calibration_io_t *, timer_calibration_result_t *);
const char *timer_calibration_error_name(timer_calibration_error_t);
const char *timer_calibration_exit_name(timer_calibration_exit_t);
#endif
