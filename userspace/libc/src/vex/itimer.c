/* The ITIMER_REAL interval timer (setitimer, getitimer and so alarm).
 *
 * Vex has no kernel interval timers: a library thread, started with the
 * first armed timer, sleeps on a native futex until the deadline and then
 * raises SIGALRM for the process (VEX_EVENT_ALARM). The thread blocks every
 * signal, so the alarm is taken by the process's other threads. A child of
 * fork() inherits no timer, as POSIX requires. */
#define _GNU_SOURCE
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "syscall.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int generation;
static int running;
static uint64_t deadline_ns, interval_ns; /* deadline 0: disarmed */

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static uint64_t tv_ns(const struct timeval *tv)
{
	return (uint64_t)tv->tv_sec * 1000000000u + (uint64_t)tv->tv_usec * 1000u;
}

static void ns_tv(uint64_t ns, struct timeval *tv)
{
	tv->tv_sec = ns / 1000000000u;
	tv->tv_usec = ns % 1000000000u / 1000u;
}

static void *timer_thread(void *arg)
{
	(void)arg;
	pthread_mutex_lock(&lock);
	for (;;) {
		int seen = generation;
		uint64_t wait = 0;
		if (deadline_ns) {
			uint64_t now = now_ns();
			if (now >= deadline_ns) {
				if (interval_ns) {
					/* Expirations missed while late count as one. */
					uint64_t late = now - deadline_ns;
					deadline_ns += (late / interval_ns + 1) * interval_ns;
				} else {
					deadline_ns = 0;
				}
				pthread_mutex_unlock(&lock);
				kill(getpid(), SIGALRM);
				pthread_mutex_lock(&lock);
				continue;
			}
			wait = deadline_ns - now;
		}
		pthread_mutex_unlock(&lock);
		__vex_futex_wait(&generation, seen, wait);
		pthread_mutex_lock(&lock);
	}
	return 0;
}

static void forget_in_child(void)
{
	running = 0;
	deadline_ns = 0;
	interval_ns = 0;
	pthread_mutex_init(&lock, 0);
}

int setitimer(int which, const struct itimerval *restrict new, struct itimerval *restrict old)
{
	if (which != ITIMER_REAL) {
		/* ITIMER_VIRTUAL and ITIMER_PROF need CPU-time timers the kernel
		 * does not provide. */
		errno = EINVAL;
		return -1;
	}
	if (new->it_value.tv_usec < 0 || new->it_value.tv_usec >= 1000000 ||
	    new->it_interval.tv_usec < 0 || new->it_interval.tv_usec >= 1000000 ||
	    new->it_value.tv_sec < 0 || new->it_interval.tv_sec < 0) {
		errno = EINVAL;
		return -1;
	}
	pthread_mutex_lock(&lock);
	if (old) {
		uint64_t now = now_ns();
		ns_tv(deadline_ns > now ? deadline_ns - now : 0, &old->it_value);
		if (deadline_ns && deadline_ns <= now) old->it_value.tv_usec = 1;
		ns_tv(interval_ns, &old->it_interval);
	}
	uint64_t value = tv_ns(&new->it_value);
	deadline_ns = value ? now_ns() + value : 0;
	interval_ns = tv_ns(&new->it_interval);
	if (deadline_ns && !running) {
		pthread_attr_t attr;
		sigset_t all, saved;
		pthread_t t;
		static int registered;
		if (!registered) {
			pthread_atfork(0, 0, forget_in_child);
			registered = 1;
		}
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_attr_setstacksize(&attr, 16384);
		sigfillset(&all);
		pthread_sigmask(SIG_SETMASK, &all, &saved);
		int e = pthread_create(&t, &attr, timer_thread, 0);
		pthread_sigmask(SIG_SETMASK, &saved, 0);
		if (e) {
			deadline_ns = 0;
			pthread_mutex_unlock(&lock);
			errno = e;
			return -1;
		}
		running = 1;
	}
	generation++;
	__vex_futex_wake(&generation, 1);
	pthread_mutex_unlock(&lock);
	return 0;
}

int getitimer(int which, struct itimerval *value)
{
	if (which != ITIMER_REAL) {
		errno = EINVAL;
		return -1;
	}
	pthread_mutex_lock(&lock);
	uint64_t now = now_ns();
	ns_tv(deadline_ns > now ? deadline_ns - now : 0, &value->it_value);
	ns_tv(interval_ns, &value->it_interval);
	pthread_mutex_unlock(&lock);
	return 0;
}
