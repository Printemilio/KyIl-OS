/* Paths relative to a directory descriptor, resolved by the kernel. */
#ifndef VEX_LIBC_AT_H
#define VEX_LIBC_AT_H

#include <fcntl.h>
#include <limits.h>
#include "syscall.h"

/* The absolute form of `path` relative to `dirfd` in `out` (PATH_MAX bytes).
 * Returns `path` itself when no resolution is needed (absolute path or
 * AT_FDCWD), `out` after a resolution, or 0 with errno set. */
static inline const char *__vex_at(int dirfd, const char *path, char *out)
{
	if (!path) {
		errno = EFAULT;
		return 0;
	}
	if (path[0] == '/' || dirfd == AT_FDCWD) return path;
	if (!path[0]) {
		errno = ENOENT;
		return 0;
	}
	vex_resolve_at_req_t req = {
		.dirfd = dirfd,
		.path = path,
		.buffer = out,
		.size = PATH_MAX,
	};
	if (__vex_ret(__vex_call1(VEX_SYS_RESOLVE_AT, __vex_ptr(&req))) < 0) return 0;
	return out;
}

#endif
