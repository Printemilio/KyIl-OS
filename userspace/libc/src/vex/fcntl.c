/* open(), fcntl() and file-mode creation mask on native Vex calls. */
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include "syscall.h"
#include "at.h"

/* The creation mask belongs to the process. Vex has no kernel umask, so
 * the library applies it to every mode it passes for a new file. */
static mode_t creation_mask = 022;

mode_t umask(mode_t mask)
{
	mode_t old = creation_mask;
	creation_mask = mask & 0777;
	return old;
}

hidden mode_t __vex_umask_apply(mode_t mode)
{
	return mode & ~creation_mask & 07777;
}

int openat(int dirfd, const char *path, int flags, ...)
{
	mode_t mode = 0;
	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	if ((flags & O_TMPFILE) == O_TMPFILE) {
		/* VexFS cannot hold an unnamed file. */
		errno = EOPNOTSUPP;
		return -1;
	}
	vex_open_ex_req_t req = {
		.dirfd = dirfd,
		.flags = (uint32_t)flags,
		.mode = (uint32_t)__vex_umask_apply(mode),
		.path = path,
	};
	return (int)__vex_ret(__vex_call1(VEX_SYS_OPEN_EX, __vex_ptr(&req)));
}

int open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	return openat(AT_FDCWD, path, flags, mode);
}

int fcntl(int fd, int cmd, ...)
{
	unsigned long arg;
	va_list ap;
	va_start(ap, cmd);
	arg = va_arg(ap, unsigned long);
	va_end(ap);

	switch (cmd) {
	case F_DUPFD:
	case F_DUPFD_CLOEXEC:
	case F_GETFD:
	case F_SETFD:
	case F_GETFL:
	case F_SETFL:
		return (int)__vex_legacy_ret(__vex_call3(VEX_SYS_FCNTL, (uint64_t)(unsigned)fd,
			(uint64_t)(unsigned)cmd, (uint64_t)(unsigned)arg));
	case F_GETOWN:
	case F_SETOWN:
	case F_GETOWN_EX:
	case F_SETOWN_EX:
	case F_SETSIG:
	case F_GETSIG:
		/* Asynchronous I/O signals do not exist on Vex. */
		errno = EINVAL;
		return -1;
	default:
		/* Record locks (F_GETLK, F_SETLK, F_OFD_*) have no native
		 * equivalent yet: report the absence rather than pretend. */
		errno = EINVAL;
		return -1;
	}
}

int posix_fadvise(int fd, off_t base, off_t len, int advice)
{
	(void)base;
	if (len < 0 || advice < 0 || advice > POSIX_FADV_NOREUSE) return EINVAL;
	/* Advice only; a valid descriptor is all that can be checked. */
	return fcntl(fd, F_GETFD) < 0 ? errno : 0;
}

int posix_fallocate(int fd, off_t base, off_t len)
{
	struct stat st;
	if (base < 0 || len <= 0) return EINVAL;
	if (fstat(fd, &st)) return errno;
	if (!S_ISREG(st.st_mode)) return ENODEV;
	if (base + len <= st.st_size) return 0;
	/* VexFS stores a file contiguously; growing it reserves the space. */
	return ftruncate(fd, base + len) ? errno : 0;
}
