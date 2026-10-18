/* File metadata on native Vex calls. */
#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include "syscall.h"
#include "at.h"

hidden mode_t __vex_umask_apply(mode_t mode);

static void fill_stat(struct stat *st, const vex_stat_ex_t *x)
{
	memset(st, 0, sizeof *st);
	st->st_dev = x->dev;
	st->st_ino = x->ino;
	st->st_mode = x->mode;
	st->st_nlink = x->nlink;
	st->st_uid = x->uid;
	st->st_gid = x->gid;
	st->st_rdev = x->rdev;
	st->st_size = x->size;
	st->st_blksize = x->blksize;
	st->st_blocks = x->blocks;
	st->st_atim.tv_sec = x->atime_sec;
	st->st_atim.tv_nsec = x->atime_nsec;
	st->st_mtim.tv_sec = x->mtime_sec;
	st->st_mtim.tv_nsec = x->mtime_nsec;
	st->st_ctim.tv_sec = x->ctime_sec;
	st->st_ctim.tv_nsec = x->ctime_nsec;
}

int __fstatat(int dirfd, const char *restrict path, struct stat *restrict st, int flag)
{
	char buf[PATH_MAX];
	vex_stat_ex_t x;
	vex_stat_ex_req_t req = { .out = &x };

	if (flag & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH | AT_NO_AUTOMOUNT)) {
		errno = EINVAL;
		return -1;
	}
	if ((flag & AT_EMPTY_PATH) && path && !*path) {
		req.kind = VEX_STAT_EX_FD;
		req.fd = dirfd;
	} else {
		if (!(path = __vex_at(dirfd, path, buf))) return -1;
		req.kind = (flag & AT_SYMLINK_NOFOLLOW) ? VEX_STAT_EX_LPATH : VEX_STAT_EX_PATH;
		req.path = path;
	}
	if (__vex_ret(__vex_call1(VEX_SYS_STAT_EX, __vex_ptr(&req))) < 0) return -1;
	fill_stat(st, &x);
	return 0;
}
weak_alias(__fstatat, fstatat);

int mkdirat(int dirfd, const char *path, mode_t mode)
{
	char buf[PATH_MAX];
	if (!(path = __vex_at(dirfd, path, buf))) return -1;
	vex_mkdir_req_t req = { .path = path, .mode = __vex_umask_apply(mode) };
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_MKDIR, __vex_ptr(&req)));
}

int mkdir(const char *path, mode_t mode)
{
	return mkdirat(AT_FDCWD, path, mode);
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flag)
{
	char buf[PATH_MAX];
	if (flag & ~AT_SYMLINK_NOFOLLOW) {
		errno = EINVAL;
		return -1;
	}
	if (flag & AT_SYMLINK_NOFOLLOW) {
		/* Link permissions are not meaningful on VexFS. */
		errno = EOPNOTSUPP;
		return -1;
	}
	if (!(path = __vex_at(dirfd, path, buf))) return -1;
	return (int)__vex_legacy_ret(__vex_call2(VEX_SYS_CHMOD, __vex_ptr(path),
		(uint64_t)(mode & 07777)));
}

int chmod(const char *path, mode_t mode)
{
	return fchmodat(AT_FDCWD, path, mode, 0);
}

/* VexFS stores neither device nodes nor FIFOs. */
int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
	(void)dirfd; (void)path; (void)mode; (void)dev;
	errno = EPERM;
	return -1;
}

int mknod(const char *path, mode_t mode, dev_t dev)
{
	return mknodat(AT_FDCWD, path, mode, dev);
}

int fchmod(int fd, mode_t mode)
{
	vex_fchmod_req_t req = { .fd = fd, .mode = mode & 07777 };
	return (int)__vex_ret(__vex_call1(VEX_SYS_FCHMOD, __vex_ptr(&req)));
}

/* UTIME_NOW is resolved here, against the same CLOCK_REALTIME that
   time() and clock_gettime() report; UTIME_OMIT leaves a time alone. */
int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
	char buf[PATH_MAX];
	struct timespec now = { 0 };
	vex_set_times_req_t req = { .fd = dirfd };
	int i;

	if (flags & ~AT_SYMLINK_NOFOLLOW) {
		errno = EINVAL;
		return -1;
	}
	if (!times || times[0].tv_nsec == UTIME_NOW || times[1].tv_nsec == UTIME_NOW)
		clock_gettime(CLOCK_REALTIME, &now);
	for (i = 0; i < 2; i++) {
		struct timespec t = times ? times[i] : (struct timespec){ .tv_nsec = UTIME_NOW };
		if (t.tv_nsec == UTIME_OMIT) continue;
		if (t.tv_nsec == UTIME_NOW) t = now;
		else if (t.tv_nsec < 0 || t.tv_nsec >= 1000000000L) {
			errno = EINVAL;
			return -1;
		}
		req.seconds[i] = t.tv_sec;
		req.nanoseconds[i] = (uint32_t)t.tv_nsec;
		req.valid |= i == 0 ? VEX_STAT_EX_HAS_ATIME : VEX_STAT_EX_HAS_MTIME;
	}
	if (path) {
		if (!(path = __vex_at(dirfd, path, buf))) return -1;
		req.path = path;
		if (flags & AT_SYMLINK_NOFOLLOW) req.flags = VEX_SET_TIMES_NOFOLLOW;
	} else if (flags) {
		errno = EINVAL;
		return -1;
	}
	return (int)__vex_ret(__vex_call1(VEX_SYS_SET_TIMES, __vex_ptr(&req)));
}
