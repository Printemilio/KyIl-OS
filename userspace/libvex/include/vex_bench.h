#pragma once
#include <stdint.h>
#include <stddef.h>
int vex_bench_format(char *out,size_t capacity,const char *format,...);
/* Opt-in measurement only; no Paint policy in this bridge. */
void vex_bench_start(const char *label);
void vex_bench_begin(void);
void vex_bench_end(void);
void vex_bench_present_notify(int after);
void vex_bench_tag(unsigned phase);
void vex_bench_run(void);
void vex_bench_idle(void);
void vex_bench_report(unsigned count,uint64_t checksum);
