/* Clocks and sleeping on native Vex calls. */
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <threads.h>
#include <unistd.h>
#include <string.h>
#include "syscall.h"

static void ns_to_timespec(uint64_t ns, struct timespec *ts)
{
	ts->tv_sec = ns / 1000000000u;
	ts->tv_nsec = ns % 1000000000u;
}

int __clock_gettime(clockid_t clk, struct timespec *ts)
{
	switch (clk) {
	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID:
		ns_to_timespec(__vex_call1(VEX_SYS_CPU_TIME,
			clk == CLOCK_PROCESS_CPUTIME_ID ? VEX_CPU_TIME_PROCESS : VEX_CPU_TIME_THREAD), ts);
		return 0;
	default: {
		vex_clock_gettime_req_t req = { .clock_id = clk, .ts = ts };
		return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_CLOCK_GETTIME, __vex_ptr(&req)));
	}
	}
}
weak_alias(__clock_gettime, clock_gettime);

int clock_getres(clockid_t clk, struct timespec *ts)
{
	long ns;
	switch (clk) {
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
	case CLOCK_BOOTTIME:
	case CLOCK_REALTIME:
	case CLOCK_REALTIME_COARSE:
	case CLOCK_MONOTONIC_COARSE:
		/* One nanosecond clock behind all of them (see the kernel). */
		ns = 1;
		break;
	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID:
		ns = 10000000;
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	if (ts) {
		ts->tv_sec = 0;
		ts->tv_nsec = ns;
	}
	return 0;
}

int clock_settime(clockid_t clk, const struct timespec *ts)
{
	(void)ts;
	errno = clk == CLOCK_REALTIME ? EPERM : EINVAL;
	return -1;
}

int clock_getcpuclockid(pid_t pid, clockid_t *clk)
{
	if (pid && pid != getpid()) return EPERM;
	*clk = CLOCK_PROCESS_CPUTIME_ID;
	return 0;
}

/* Relative sleep: 0 or an errno value (musl's nanosleep() and
 * clock_nanosleep() are built on __clock_nanosleep). */
static int native_sleep(const struct timespec *req, struct timespec *rem)
{
	if (req->tv_sec < 0 || (unsigned long)req->tv_nsec >= 1000000000u) return EINVAL;
	vex_nanosleep_req_t r = { .requested = req, .remaining = rem };
	if (__vex_call1(VEX_SYS_NANOSLEEP, __vex_ptr(&r)) == (uint64_t)-1)
		return __vex_last_error();
	return 0;
}

int __clock_nanosleep(clockid_t clk, int flags, const struct timespec *req, struct timespec *rem)
{
	struct timespec now, rel;
	if (clk == CLOCK_THREAD_CPUTIME_ID || clk == CLOCK_PROCESS_CPUTIME_ID) return EINVAL;
	if (clk != CLOCK_REALTIME && clk != CLOCK_MONOTONIC && clk != CLOCK_BOOTTIME)
		return EINVAL;
	if (!(flags & TIMER_ABSTIME))
		return native_sleep(req, rem);
	if (__clock_gettime(clk, &now)) return errno;
	rel.tv_sec = req->tv_sec - now.tv_sec;
	rel.tv_nsec = req->tv_nsec - now.tv_nsec;
	if (rel.tv_nsec < 0) {
		rel.tv_sec--;
		rel.tv_nsec += 1000000000;
	}
	if (rel.tv_sec < 0) return 0;
	return native_sleep(&rel, 0);
}
weak_alias(__clock_nanosleep, clock_nanosleep);

clock_t times(struct tms *tms)
{
	struct timespec cpu, mono;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu);
	clock_gettime(CLOCK_MONOTONIC, &mono);
	long hz = sysconf(_SC_CLK_TCK);
	if (tms) {
		/* Vex does not split user and system time. */
		tms->tms_utime = cpu.tv_sec * hz + cpu.tv_nsec / (1000000000 / hz);
		tms->tms_stime = 0;
		tms->tms_cutime = 0;
		tms->tms_cstime = 0;
	}
	return mono.tv_sec * hz + mono.tv_nsec / (1000000000 / hz);
}

int getrusage(int who, struct rusage *ru)
{
	struct timespec cpu;
	if (who == RUSAGE_CHILDREN) {
		/* Children's usage is not reported by the kernel. */
		memset(ru, 0, sizeof *ru);
		return 0;
	}
	if (who != RUSAGE_SELF && who != RUSAGE_THREAD) {
		errno = EINVAL;
		return -1;
	}
	clock_gettime(who == RUSAGE_SELF ? CLOCK_PROCESS_CPUTIME_ID : CLOCK_THREAD_CPUTIME_ID, &cpu);
	memset(ru, 0, sizeof *ru);
	ru->ru_utime.tv_sec = cpu.tv_sec;
	ru->ru_utime.tv_usec = cpu.tv_nsec / 1000;
	return 0;
}
