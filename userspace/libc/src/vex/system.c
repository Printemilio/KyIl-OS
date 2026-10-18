/* System information, limits and terminal control on native Vex calls. */
#define _GNU_SOURCE
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <termios.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "syscall.h"

int uname(struct utsname *uts)
{
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_UNAME, __vex_ptr(uts)));
}

int __lsysinfo(struct sysinfo *info)
{
	vex_bulk_result_t r;
	vex_bulk_req_t req = { .mask = VEX_BULK_MEMORY | VEX_BULK_CLOCK | VEX_BULK_SCHEDULER, .result = &r };
	memset(&r, 0, sizeof r);
	if (__vex_legacy_ret(__vex_call1(VEX_SYS_BULK, __vex_ptr(&req))) < 0) return -1;
	memset(info, 0, sizeof *info);
	info->uptime = (long)(r.monotonic_ms / 1000);
	info->totalram = r.total_pages;
	info->freeram = r.free_pages;
	info->procs = (unsigned short)(r.process_count > 0xffff ? 0xffff : r.process_count);
	info->mem_unit = 4096;
	return 0;
}
weak_alias(__lsysinfo, sysinfo);

ssize_t getrandom(void *buf, size_t len, unsigned flags)
{
	vex_getrandom_req_t req = { .buffer = buf, .length = len, .flags = flags };
	return __vex_legacy_ret(__vex_call1(VEX_SYS_GETRANDOM, __vex_ptr(&req)));
}

/* Resource limits: Vex enforces none of these per process, so each limit
 * reads as unlimited except those the system fixes. Lowering a limit is
 * accepted only when it would not have to be enforced. */
static struct rlimit fixed_limit(int resource)
{
	switch (resource) {
	case RLIMIT_NOFILE: {
		/* Asked of the kernel directly: musl's sysconf(_SC_OPEN_MAX) is
		   itself answered from getrlimit(RLIMIT_NOFILE). */
		long n = (long)__vex_call1(VEX_SYS_SYSCONF, _SC_OPEN_MAX);
		if (n <= 0) n = 256;
		return (struct rlimit){ n, n };
	}
	case RLIMIT_STACK:
		/* The main thread's stack reservation, set by the ELF loader. */
		return (struct rlimit){ 8 * 1024 * 1024, 8 * 1024 * 1024 };
	case RLIMIT_CORE:
		return (struct rlimit){ 0, 0 };
	default:
		return (struct rlimit){ RLIM_INFINITY, RLIM_INFINITY };
	}
}

int getrlimit(int resource, struct rlimit *rlim)
{
	if (resource < 0 || resource >= RLIM_NLIMITS) {
		errno = EINVAL;
		return -1;
	}
	*rlim = fixed_limit(resource);
	return 0;
}

int setrlimit(int resource, const struct rlimit *rlim)
{
	struct rlimit cur;
	if (getrlimit(resource, &cur)) return -1;
	if (rlim->rlim_cur > rlim->rlim_max) {
		errno = EINVAL;
		return -1;
	}
	if (rlim->rlim_cur == cur.rlim_cur && rlim->rlim_max == cur.rlim_max) return 0;
	errno = EPERM;
	return -1;
}

int prlimit(pid_t pid, int resource, const struct rlimit *new, struct rlimit *old)
{
	if (pid && pid != getpid()) {
		errno = EPERM;
		return -1;
	}
	if (old && getrlimit(resource, old)) return -1;
	return new ? setrlimit(resource, new) : 0;
}

int getpriority(int which, id_t who)
{
	(void)which; (void)who;
	return 0;
}

int setpriority(int which, id_t who, int prio)
{
	(void)which; (void)who;
	if (prio) {
		errno = EPERM;
		return -1;
	}
	return 0;
}

int ioctl(int fd, int req, ...)
{
	void *arg;
	va_list ap;
	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	vex_ioctl_req_t r = { .fd = fd, .request = (unsigned long)req, .arg = arg };
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_IOCTL, __vex_ptr(&r)));
}

int tcgetwinsize(int fd, struct winsize *wsz)
{
	return ioctl(fd, TIOCGWINSZ, wsz);
}

int tcsetwinsize(int fd, const struct winsize *wsz)
{
	return ioctl(fd, TIOCSWINSZ, wsz);
}

int tcdrain(int fd)
{
	/* Terminal output is written synchronously. */
	return isatty(fd) ? 0 : -1;
}
