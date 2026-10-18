/* POSIX descriptor, path and identity functions on native Vex calls. */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <sys/uio.h>
#include "syscall.h"
#include "at.h"

ssize_t read(int fd, void *buf, size_t count)
{
	vex_fd_io_req_t req = { .fd = fd, .buffer = buf, .count = count };
	return __vex_legacy_ret(__vex_call1(VEX_SYS_READFD, __vex_ptr(&req)));
}

ssize_t write(int fd, const void *buf, size_t count)
{
	vex_fd_io_req_t req = { .fd = fd, .buffer = (void *)buf, .count = count };
	return __vex_legacy_ret(__vex_call1(VEX_SYS_WRITEFD, __vex_ptr(&req)));
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}
	vex_rw_offset_req_t req = { .fd = fd, .buffer = buf, .count = count, .offset = offset };
	return __vex_legacy_ret(__vex_call1(VEX_SYS_PREAD, __vex_ptr(&req)));
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}
	vex_rw_offset_req_t req = { .fd = fd, .buffer = (void *)buf, .count = count, .offset = offset };
	return __vex_legacy_ret(__vex_call1(VEX_SYS_PWRITE, __vex_ptr(&req)));
}

/* Vector I/O. A gathered write is staged in one buffer when it is small,
 * so that it reaches a pipe or socket as a single write, as POSIX requires
 * for writes up to PIPE_BUF. */
static ssize_t vector_total(const struct iovec *iov, int n)
{
	size_t total = 0;
	if (n < 0 || n > IOV_MAX) {
		errno = EINVAL;
		return -1;
	}
	for (int i = 0; i < n; i++) {
		if (iov[i].iov_len > SSIZE_MAX - total) {
			errno = EINVAL;
			return -1;
		}
		total += iov[i].iov_len;
	}
	return (ssize_t)total;
}

static ssize_t vector_io(int fd, const struct iovec *iov, int n, off_t off, int writing, int positional)
{
	ssize_t total = vector_total(iov, n), done = 0;
	if (total < 0) return -1;
	if (writing && total <= 65536) {
		char stage[total ? total : 1], *p = stage;
		for (int i = 0; i < n; i++) {
			memcpy(p, iov[i].iov_base, iov[i].iov_len);
			p += iov[i].iov_len;
		}
		return positional ? pwrite(fd, stage, total, off) : write(fd, stage, total);
	}
	for (int i = 0; i < n; i++) {
		ssize_t r;
		if (!iov[i].iov_len) continue;
		if (writing)
			r = positional ? pwrite(fd, iov[i].iov_base, iov[i].iov_len, off + done)
			               : write(fd, iov[i].iov_base, iov[i].iov_len);
		else
			r = positional ? pread(fd, iov[i].iov_base, iov[i].iov_len, off + done)
			               : read(fd, iov[i].iov_base, iov[i].iov_len);
		if (r < 0) return done ? done : -1;
		done += r;
		if ((size_t)r < iov[i].iov_len) break;
	}
	return done;
}

ssize_t readv(int fd, const struct iovec *iov, int n) { return vector_io(fd, iov, n, 0, 0, 0); }
ssize_t writev(int fd, const struct iovec *iov, int n) { return vector_io(fd, iov, n, 0, 1, 0); }
ssize_t preadv(int fd, const struct iovec *iov, int n, off_t off) { return vector_io(fd, iov, n, off, 0, 1); }
ssize_t pwritev(int fd, const struct iovec *iov, int n, off_t off) { return vector_io(fd, iov, n, off, 1, 1); }

int __close(int fd)
{
	int r = (int)__vex_legacy_ret(__vex_call1(VEX_SYS_CLOSE, (uint64_t)(unsigned)fd));
	/* POSIX leaves the descriptor state unspecified after EINTR; Vex
	 * always releases it, so EINTR is not an error the caller must retry. */
	if (r < 0 && errno == EINTR) r = 0;
	return r;
}
weak_alias(__close, close);

off_t __lseek(int fd, off_t offset, int whence)
{
	return (off_t)__vex_legacy_ret(__vex_call3(VEX_SYS_LSEEK, (uint64_t)(unsigned)fd,
		(uint64_t)offset, (uint64_t)(unsigned)whence));
}
weak_alias(__lseek, lseek);

int dup(int fd)
{
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_DUP, (uint64_t)(unsigned)fd));
}

int __dup3(int old, int new, int flags)
{
	if (old == new || flags & ~O_CLOEXEC) {
		errno = EINVAL;
		return -1;
	}
	if (__vex_legacy_ret(__vex_call2(VEX_SYS_DUP2, (uint64_t)(unsigned)old,
	    (uint64_t)(unsigned)new)) < 0) return -1;
	if (flags & O_CLOEXEC) fcntl(new, F_SETFD, FD_CLOEXEC);
	return new;
}
weak_alias(__dup3, dup3);

int dup2(int old, int new)
{
	if (old == new) {
		/* A valid descriptor is returned unchanged. */
		if (fcntl(old, F_GETFD) < 0) return -1;
		return new;
	}
	return (int)__vex_legacy_ret(__vex_call2(VEX_SYS_DUP2, (uint64_t)(unsigned)old,
		(uint64_t)(unsigned)new));
}

int pipe2(int fd[2], int flags)
{
	if (flags & ~(O_CLOEXEC | O_NONBLOCK)) {
		errno = EINVAL;
		return -1;
	}
	vex_pipe_req_t req = { .pipefd = fd };
	if (__vex_legacy_ret(__vex_call1(VEX_SYS_PIPE, __vex_ptr(&req))) < 0) return -1;
	for (int i = 0; i < 2; i++) {
		if (flags & O_CLOEXEC) fcntl(fd[i], F_SETFD, FD_CLOEXEC);
		if (flags & O_NONBLOCK) fcntl(fd[i], F_SETFL, O_NONBLOCK);
	}
	return 0;
}

int pipe(int fd[2])
{
	return pipe2(fd, 0);
}

int faccessat(int dirfd, const char *path, int mode, int flags)
{
	char buf[PATH_MAX];
	if (flags & ~(AT_EACCESS | AT_SYMLINK_NOFOLLOW)) {
		errno = EINVAL;
		return -1;
	}
	if (!(path = __vex_at(dirfd, path, buf))) return -1;
	/* Vex runs a process with one identity: real == effective. */
	return (int)__vex_legacy_ret(__vex_call2(VEX_SYS_ACCESS, __vex_ptr(path),
		(uint64_t)(unsigned)mode));
}

int access(const char *path, int mode)
{
	return faccessat(AT_FDCWD, path, mode, 0);
}

int chdir(const char *path)
{
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_CHDIR, __vex_ptr(path)));
}

int fchdir(int fd)
{
	char buf[PATH_MAX];
	const char *path = __vex_at(fd, ".", buf);
	return path ? chdir(path) : -1;
}

char *getcwd(char *buf, size_t size)
{
	char tmp[buf ? 1 : PATH_MAX];
	if (!buf) {
		buf = tmp;
		size = sizeof tmp;
	} else if (!size) {
		errno = EINVAL;
		return 0;
	}
	vex_getcwd_req_t req = { .buffer = buf, .size = size };
	if (!__vex_call1(VEX_SYS_GETCWD, __vex_ptr(&req))) {
		int e = __vex_last_error();
		errno = e == EIO ? ERANGE : e;
		return 0;
	}
	return buf == tmp ? strdup(buf) : buf;
}

int unlinkat(int dirfd, const char *path, int flags)
{
	char buf[PATH_MAX];
	if (flags & ~AT_REMOVEDIR) {
		errno = EINVAL;
		return -1;
	}
	if (!(path = __vex_at(dirfd, path, buf))) return -1;
	if (flags & AT_REMOVEDIR)
		return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_RMDIR, __vex_ptr(path)));
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_UNLINK, __vex_ptr(path)));
}

int unlink(const char *path)
{
	return unlinkat(AT_FDCWD, path, 0);
}

int rmdir(const char *path)
{
	return unlinkat(AT_FDCWD, path, AT_REMOVEDIR);
}

int renameat(int oldfd, const char *old, int newfd, const char *new)
{
	char obuf[PATH_MAX], nbuf[PATH_MAX];
	if (!(old = __vex_at(oldfd, old, obuf)) || !(new = __vex_at(newfd, new, nbuf)))
		return -1;
	vex_rename_req_t req = { .old_path = old, .new_path = new };
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_RENAME, __vex_ptr(&req)));
}

int symlinkat(const char *target, int dirfd, const char *link)
{
	char buf[PATH_MAX];
	if (!(link = __vex_at(dirfd, link, buf))) return -1;
	vex_symlink_req_t req = { .target = target, .link_path = link };
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_SYMLINK, __vex_ptr(&req)));
}

int symlink(const char *target, const char *link)
{
	return symlinkat(target, AT_FDCWD, link);
}

ssize_t readlinkat(int dirfd, const char *restrict path, char *restrict out, size_t size)
{
	char buf[PATH_MAX];
	char one;
	if (!size) {
		out = &one;
		size = 1;
	}
	if (!(path = __vex_at(dirfd, path, buf))) return -1;
	vex_readlink_req_t req = { .path = path, .buffer = out, .size = size };
	ssize_t r = __vex_legacy_ret(__vex_call1(VEX_SYS_READLINK, __vex_ptr(&req)));
	if (out == &one && r > 0) r = 0;
	return r;
}

ssize_t readlink(const char *restrict path, char *restrict out, size_t size)
{
	return readlinkat(AT_FDCWD, path, out, size);
}

/* VexFS names each file once: there are no hard links. */
int linkat(int oldfd, const char *old, int newfd, const char *new, int flags)
{
	(void)oldfd; (void)old; (void)newfd; (void)new; (void)flags;
	errno = EPERM;
	return -1;
}

int link(const char *old, const char *new)
{
	return linkat(AT_FDCWD, old, AT_FDCWD, new, 0);
}

int ftruncate(int fd, off_t length)
{
	if (length < 0) {
		errno = EINVAL;
		return -1;
	}
	return (int)__vex_legacy_ret(__vex_call2(VEX_SYS_FTRUNCATE, (uint64_t)(unsigned)fd,
		(uint64_t)length));
}

int truncate(const char *path, off_t length)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) return -1;
	int r = ftruncate(fd, length);
	int e = errno;
	if (close(fd) < 0 && !r) return -1;
	errno = e;
	return r;
}

int fsync(int fd)
{
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_FSYNC, (uint64_t)(unsigned)fd));
}

int fdatasync(int fd)
{
	return fsync(fd);
}

/* VexFS writes a descriptor's contents through on close or fsync; there is
 * no other cache to flush. */
void sync(void)
{
}

int isatty(int fd)
{
	uint64_t r = __vex_call1(VEX_SYS_ISATTY, (uint64_t)(unsigned)fd);
	if (r == 1) return 1;
	errno = r == (uint64_t)-1 ? __vex_last_error() : ENOTTY;
	return 0;
}

pid_t getpid(void)
{
	return (pid_t)__vex_call0(VEX_SYS_GETPID);
}

pid_t getppid(void)
{
	return (pid_t)__vex_call0(VEX_SYS_GETPPID);
}

uid_t getuid(void)
{
	return (uid_t)__vex_call0(VEX_SYS_GETUID);
}

uid_t geteuid(void)
{
	return (uid_t)__vex_call0(VEX_SYS_GETEUID);
}

/* A Vex user is its own group. */
gid_t getgid(void)
{
	return (gid_t)getuid();
}

gid_t getegid(void)
{
	return (gid_t)geteuid();
}

int getgroups(int count, gid_t list[])
{
	if (count < 0) {
		errno = EINVAL;
		return -1;
	}
	if (count == 0) return 1;
	list[0] = getegid();
	return 1;
}

/* Identity changes go through Vex elevation, not set*id(): only the
 * current identity can be "set". */
static int same_id(unsigned id, unsigned current)
{
	if (id == (unsigned)-1 || id == current) return 0;
	errno = EPERM;
	return -1;
}

int setuid(uid_t uid) { return same_id(uid, getuid()); }
int seteuid(uid_t uid) { return same_id(uid, geteuid()); }
int setgid(gid_t gid) { return same_id(gid, getgid()); }
int setegid(gid_t gid) { return same_id(gid, getegid()); }
int setreuid(uid_t r, uid_t e) { return same_id(r, getuid()) ? -1 : same_id(e, geteuid()); }
int setregid(gid_t r, gid_t e) { return same_id(r, getgid()) ? -1 : same_id(e, getegid()); }
int setresuid(uid_t r, uid_t e, uid_t s) { return setreuid(r, e) ? -1 : same_id(s, geteuid()); }
int setresgid(gid_t r, gid_t e, gid_t s) { return setregid(r, e) ? -1 : same_id(s, getegid()); }

/* Vex has no sessions or process groups: every process leads its own. */
pid_t getpgid(pid_t pid)
{
	return pid ? pid : getpid();
}

pid_t getpgrp(void)
{
	return getpid();
}

pid_t getsid(pid_t pid)
{
	return pid ? pid : getpid();
}

pid_t setsid(void)
{
	return getpid();
}

int setpgid(pid_t pid, pid_t pgid)
{
	pid_t self = getpid();
	if ((pid && pid != self) || (pgid && pgid != (pid ? pid : self))) {
		errno = EPERM;
		return -1;
	}
	return 0;
}

int pause(void)
{
	/* Sleeps until a native event is delivered (the wait returns -EINTR). */
	static volatile int never;
	for (;;) {
		if (__vex_futex_wait(&never, 0, 0) == -EINTR) {
			errno = EINTR;
			return -1;
		}
	}
}
