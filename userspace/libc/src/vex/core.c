/* Error reporting and raw descriptor helpers of the Vex OS layer. */
#include <errno.h>
#include <fcntl.h>
#include "syscall.h"

int __vex_last_error(void)
{
	int e = (int)__vex_call0(VEX_SYS_LAST_ERROR);
	/* A historical call that failed without recording a reason. */
	return e > 0 && e < 4096 ? e : EIO;
}

/* Raw helpers used by musl internals (see src/internal/syscall.h): they
 * return a negated errno instead of touching errno. */
static long raw(uint64_t r)
{
	if (r == (uint64_t)-1) return -__vex_last_error();
	return (long)r;
}

int __vex_sys_open(const char *path, int flags, int mode)
{
	vex_open_ex_req_t req = {
		.dirfd = VEX_AT_FDCWD,
		.flags = (uint32_t)flags,
		.mode = (uint32_t)mode,
		.path = path,
	};
	return (int)(int64_t)__vex_call1(VEX_SYS_OPEN_EX, __vex_ptr(&req));
}

int __vex_sys_close(int fd)
{
	return (int)raw(__vex_call1(VEX_SYS_CLOSE, (uint64_t)(unsigned)fd));
}

long __vex_sys_read(int fd, void *buf, size_t len)
{
	vex_fd_io_req_t req = { .fd = fd, .buffer = buf, .count = len };
	return raw(__vex_call1(VEX_SYS_READFD, __vex_ptr(&req)));
}

long __vex_sys_write(int fd, const void *buf, size_t len)
{
	vex_fd_io_req_t req = { .fd = fd, .buffer = (void *)buf, .count = len };
	return raw(__vex_call1(VEX_SYS_WRITEFD, __vex_ptr(&req)));
}

int __vex_sys_fcntl(int fd, int cmd, long arg)
{
	return (int)raw(__vex_call3(VEX_SYS_FCNTL, (uint64_t)(unsigned)fd,
		(uint64_t)(unsigned)cmd, (uint64_t)arg));
}
