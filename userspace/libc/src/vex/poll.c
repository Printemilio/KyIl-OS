/* Descriptor readiness (poll, select and their masked forms) on VEX_SYS_POLL. */
#include <poll.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include "syscall.h"

int poll(struct pollfd *fds, nfds_t n, int timeout)
{
	vex_poll_req_t req = { .fds = fds, .count = n, .timeout = timeout < 0 ? -1 : timeout };
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_POLL, __vex_ptr(&req)));
}

static int timeout_ms(const struct timespec *ts)
{
	if (!ts) return -1;
	if (ts->tv_sec < 0 || (unsigned long)ts->tv_nsec >= 1000000000u) return -2;
	if (ts->tv_sec >= INT_MAX / 1000) return INT_MAX;
	/* Round up: a wait must not end before the requested time. */
	return (int)(ts->tv_sec * 1000 + (ts->tv_nsec + 999999) / 1000000);
}

int ppoll(struct pollfd *fds, nfds_t n, const struct timespec *to, const sigset_t *mask)
{
	sigset_t old;
	int ms = timeout_ms(to);
	if (ms == -2) {
		errno = EINVAL;
		return -1;
	}
	if (mask) pthread_sigmask(SIG_SETMASK, mask, &old);
	int r = poll(fds, n, ms);
	int e = errno;
	if (mask) pthread_sigmask(SIG_SETMASK, &old, 0);
	errno = e;
	return r;
}

/* select() is poll() over the descriptors named in the three sets. */
int pselect(int n, fd_set *restrict rfds, fd_set *restrict wfds, fd_set *restrict efds,
	const struct timespec *restrict ts, const sigset_t *restrict mask)
{
	struct pollfd pfd[FD_SETSIZE];
	int count = 0, ready = 0, ms = timeout_ms(ts);

	if (n < 0 || n > FD_SETSIZE || ms == -2) {
		errno = EINVAL;
		return -1;
	}
	for (int fd = 0; fd < n; fd++) {
		short ev = 0;
		if (rfds && FD_ISSET(fd, rfds)) ev |= POLLIN;
		if (wfds && FD_ISSET(fd, wfds)) ev |= POLLOUT;
		if (efds && FD_ISSET(fd, efds)) ev |= POLLPRI;
		if (ev) pfd[count++] = (struct pollfd){ .fd = fd, .events = ev };
	}
	int r = ppoll(pfd, count, ts, mask);
	if (r < 0) return -1;
	for (int i = 0; i < count; i++)
		if (pfd[i].revents & POLLNVAL) {
			errno = EBADF;
			return -1;
		}
	if (rfds) FD_ZERO(rfds);
	if (wfds) FD_ZERO(wfds);
	if (efds) FD_ZERO(efds);
	for (int i = 0; i < count; i++) {
		short re = pfd[i].revents, ev = pfd[i].events;
		int fd = pfd[i].fd;
		if ((ev & POLLIN) && (re & (POLLIN | POLLHUP | POLLERR))) { FD_SET(fd, rfds); ready++; }
		if ((ev & POLLOUT) && (re & (POLLOUT | POLLERR))) { FD_SET(fd, wfds); ready++; }
		if ((ev & POLLPRI) && (re & POLLPRI)) { FD_SET(fd, efds); ready++; }
	}
	return ready;
}

int select(int n, fd_set *restrict rfds, fd_set *restrict wfds, fd_set *restrict efds, struct timeval *restrict tv)
{
	struct timespec ts, *tsp = 0;
	if (tv) {
		if (tv->tv_sec < 0 || tv->tv_usec < 0) {
			errno = EINVAL;
			return -1;
		}
		ts.tv_sec = tv->tv_sec + tv->tv_usec / 1000000;
		ts.tv_nsec = tv->tv_usec % 1000000 * 1000;
		tsp = &ts;
	}
	return pselect(n, rfds, wfds, efds, tsp, 0);
}
