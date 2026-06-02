#ifndef UNOS_VEX_WEB_TYPES_H
#define UNOS_VEX_WEB_TYPES_H

#include <stddef.h>
#include <stdint.h>

/*
 * Forced first in browser-side probe builds. Keep the ABI-shaped POSIX types
 * here so host libc typedefs do not leak into Vex-targeted browser tooling.
 */
#ifndef __vexos__
#define __vexos__ 1
#endif

#ifndef __serenity__
#define __serenity__ 1
#endif

typedef long long vex_off_t;
typedef int vex_pid_t;
typedef unsigned int vex_uid_t;
typedef unsigned int vex_gid_t;
typedef unsigned int vex_mode_t;
typedef long vex_clock_t;

#ifndef _OFF_T_DECLARED
#define _OFF_T_DECLARED
typedef vex_off_t off_t;
#endif

#ifndef _PID_T_DECLARED
#define _PID_T_DECLARED
typedef vex_pid_t pid_t;
#endif

#ifndef _UID_T_DECLARED
#define _UID_T_DECLARED
typedef vex_uid_t uid_t;
#endif

#ifndef _GID_T_DECLARED
#define _GID_T_DECLARED
typedef vex_gid_t gid_t;
#endif

#ifndef _MODE_T_DECLARED
#define _MODE_T_DECLARED
typedef vex_mode_t mode_t;
#endif

#ifndef _CLOCK_T_DECLARED
#define _CLOCK_T_DECLARED
typedef vex_clock_t clock_t;
#endif

#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
    long long tv_sec;
    long tv_usec;
};
#endif

#ifndef _TIMESPEC_DEFINED
#define _TIMESPEC_DEFINED
struct timespec {
    long long tv_sec;
    long tv_nsec;
};
#endif

#endif
