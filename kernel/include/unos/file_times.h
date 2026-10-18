#ifndef VEX_FILE_TIMES_H
#define VEX_FILE_TIMES_H
#include <stdint.h>
/* Backend metadata, not inferred from the time of a stat/read. */
enum { VEX_FILE_ACCESS_TIME=1, VEX_FILE_MODIFY_TIME=2,
       VEX_FILE_CHANGE_TIME=4, VEX_FILE_CREATE_TIME=8 };
typedef struct {
    uint32_t valid;
    int64_t seconds[4];
    uint32_t nanoseconds[4];
} vex_file_times_t;

/* The current CLOCK_REALTIME in nanoseconds since the epoch; 0 on success. */
int vex_realtime_ns(uint64_t *out);
#endif
