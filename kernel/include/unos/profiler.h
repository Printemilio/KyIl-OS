#ifndef UNOS_PROFILER_H
#define UNOS_PROFILER_H

#include <stdint.h>
#include <unos/sched_clock.h>

/*
 * Statistical profiler and latency recorder, for finding where interactive time
 * goes (see docs/browser-performance.md).
 *
 * Off by default; when off, every hook is one load and a branch. Started with
 * the shell command `prof <seconds>`, it
 *   - samples every CPU on each LAPIC timer interrupt (1 kHz): idle / user /
 *     kernel / spinning-on-a-kernel-gate, with thread, RIP and queue depth;
 *   - measures how long each kernel gate is held and who holds it;
 *   - records latency histograms posted by the GUI and compositor;
 * then writes a report ("PROF-..." lines) to the serial log, which
 * scripts/profile-report.py symbolizes and aggregates on the host.
 */

typedef struct interrupt_frame interrupt_frame_t;

extern volatile uint8_t profiler_active;

enum profiler_latency_kind {
    PROF_LAT_GUI_TICK,        /* duration of one GUI loop iteration */
    PROF_LAT_GUI_GAP,         /* time between two GUI loop iterations */
    PROF_LAT_MOUSE_QUEUE,     /* mouse IRQ to its consumption by the GUI */
    PROF_LAT_WEB_PRESENT_GAP, /* time between two web-view frames presented */
    PROF_LAT_PRESENT_COST,    /* cost of presenting one web-view frame */
    PROF_LAT_KIND_COUNT
};

/* Called from the LAPIC timer interrupt of every CPU. */
void profiler_sample(const interrupt_frame_t *frame);
/* Called by kernel gates when profiling: the final release of a gate held for
 * `held_ns`, acquired at `holder_pc`. */
void profiler_gate_release(const void *gate, uint64_t held_ns, uint64_t holder_pc);
void profiler_latency(enum profiler_latency_kind kind, uint64_t ns);
/* One finished system call: `key` is (personality << 16) | number. */
void profiler_syscall(uint32_t key, uint64_t ns);

/* Start sampling for `seconds`; the report is written by profiler_poll() once
 * the window has elapsed. Returns 0, or -1 when already running. */
int profiler_start(uint32_t seconds);
/* Cheap; call from a thread that may print (the GUI loop). */
void profiler_poll(void);
/* Write the report now and stop. */
void profiler_report(void);

/* Timing of a named stretch of kernel code, reported with the system calls as
 * personality 4. Open one with a cleanup attribute so every return is covered:
 *   profiler_span_t span __attribute__((cleanup(profiler_span_end))) = profiler_span_begin(ID);
 * `span.id` may be changed once the path taken is known. */
typedef struct profiler_span { uint64_t start_ns; uint32_t id; } profiler_span_t;
static inline profiler_span_t profiler_span_begin(uint32_t id) {
    return (profiler_span_t){profiler_active ? sched_clock_ns() : 0u, id};
}
static inline void profiler_span_end(profiler_span_t *span) {
    if (span->start_ns != 0u && profiler_active) {
        profiler_syscall((4u << 16) | span->id, sched_clock_ns() - span->start_ns);
    }
}

#endif
