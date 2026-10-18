/* Native thread, futex and scheduling primitives. */
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <threads.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <vex/futex.h>
#include "pthread_impl.h"

int __vex_futex_wait(volatile void *addr, int value, uint64_t timeout_ns)
{
	vex_futex_req_t req = {
		.addr = __vex_ptr(addr),
		.op = VEX_FUTEX_WAIT,
		.value = (uint32_t)value,
		.timeout_ns = timeout_ns,
	};
	return (int)(int64_t)__vex_call1(VEX_SYS_FUTEX, __vex_ptr(&req));
}

int __vex_futex_wake(volatile void *addr, int count)
{
	vex_futex_req_t req = {
		.addr = __vex_ptr(addr),
		.op = VEX_FUTEX_WAKE,
		.value = (uint32_t)count,
	};
	return (int)(int64_t)__vex_call1(VEX_SYS_FUTEX, __vex_ptr(&req));
}

int __vex_set_tls(void *base)
{
	return (int)(int64_t)__vex_call1(VEX_SYS_THREAD_SET_TLS, __vex_ptr(base));
}

int __vex_thread_self(void)
{
	return (int)__vex_call0(VEX_SYS_THREAD_SELF);
}

int __vex_thread_start(void (*entry)(void *), void *arg, void *stack_top,
                       void *tls, volatile int *tid_out,
                       volatile int *clear_on_exit)
{
	vex_thread_start_req_t req = {
		.size = sizeof req,
		.entry = __vex_ptr(entry),
		.arg = __vex_ptr(arg),
		.stack_top = __vex_ptr(stack_top),
		.tls_base = __vex_ptr(tls),
		.tid_out = __vex_ptr(tid_out),
		.clear_on_exit = __vex_ptr(clear_on_exit),
	};
	return (int)(int64_t)__vex_call1(VEX_SYS_THREAD_START, __vex_ptr(&req));
}

_Noreturn void __vex_thread_exit(int status)
{
	for (;;) __vex_call1(VEX_SYS_THREAD_EXIT, (uint64_t)(unsigned)status);
}

_Noreturn void __vex_thread_exit_unmap(void *base, size_t length)
{
	vex_thread_exit_unmap_req_t req = {
		.base = __vex_ptr(base),
		.length = length,
	};
	/* The request lives on the stack being unmapped: the kernel copies it
	 * before acting, and never returns on success. */
	__vex_call1(VEX_SYS_THREAD_EXIT_UNMAP, __vex_ptr(&req));
	for (;;) __vex_call1(VEX_SYS_THREAD_EXIT, 0);
}

int sched_yield(void)
{
	__vex_call0(VEX_SYS_THREAD_YIELD);
	return 0;
}

void thrd_yield(void)
{
	sched_yield();
}

/* FlowSched schedules every thread in one class. POSIX scheduling is
 * reported as SCHED_OTHER at priority 0, the only value it accepts. */
int pthread_getschedparam(pthread_t t, int *restrict policy, struct sched_param *restrict param)
{
	if (!t->tid) return ESRCH;
	*policy = SCHED_OTHER;
	param->sched_priority = 0;
	return 0;
}

int pthread_setschedparam(pthread_t t, int policy, const struct sched_param *param)
{
	if (!t->tid) return ESRCH;
	if (policy != SCHED_OTHER || param->sched_priority != 0) return ENOTSUP;
	return 0;
}

int pthread_setschedprio(pthread_t t, int prio)
{
	if (!t->tid) return ESRCH;
	return prio == 0 ? 0 : ENOTSUP;
}

int sched_get_priority_max(int policy)
{
	if (policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR
	    && policy != SCHED_BATCH && policy != SCHED_IDLE) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int sched_get_priority_min(int policy)
{
	return sched_get_priority_max(policy);
}

int sched_rr_get_interval(pid_t pid, struct timespec *ts)
{
	(void)pid;
	/* FlowSched time slices follow the 1 kHz scheduler tick. */
	ts->tv_sec = 0;
	ts->tv_nsec = 1000000;
	return 0;
}

int sched_getcpu(void)
{
	errno = ENOSYS;
	return -1;
}

int sched_getaffinity(pid_t pid, size_t size, cpu_set_t *set)
{
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	(void)pid;
	if (n < 1) n = 1;
	memset(set, 0, size);
	for (long i = 0; i < n && (size_t)i < 8 * size; i++)
		((unsigned char *)set)[i / 8] |= 1u << (i % 8);
	return 0;
}

int pthread_getaffinity_np(pthread_t t, size_t size, cpu_set_t *set)
{
	(void)t;
	return sched_getaffinity(0, size, set) ? errno : 0;
}

/* FlowSched places threads itself; an affinity request cannot be honoured. */
int sched_setaffinity(pid_t pid, size_t size, const cpu_set_t *set)
{
	(void)pid; (void)size; (void)set;
	errno = ENOSYS;
	return -1;
}

int pthread_setaffinity_np(pthread_t t, size_t size, const cpu_set_t *set)
{
	(void)t; (void)size; (void)set;
	return ENOSYS;
}

/* Thread cancellation needs asynchronous delivery into blocking calls,
 * which the native event ABI does not provide for individual threads. */
int pthread_cancel(pthread_t t)
{
	(void)t;
	return ENOTSUP;
}

/* Public native futex API (<vex/futex.h>). */
int vex_futex_wait(volatile uint32_t *addr, uint32_t expected, uint64_t timeout_ns)
{
	int r = __vex_futex_wait(addr, (int)expected, timeout_ns);
	return r < 0 ? -r : 0;
}

int vex_futex_wake(volatile uint32_t *addr, uint32_t count)
{
	int r = __vex_futex_wake(addr, count > INT_MAX ? INT_MAX : (int)count);
	return r < 0 ? 0 : r;
}
