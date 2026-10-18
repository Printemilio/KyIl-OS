/* File ownership, root directory, supplementary groups and thread names. */
#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <grp.h>
#include <string.h>
#include <sys/stat.h>
#include "pthread_impl.h"

/* Changing a file's owner needs a privilege Vex grants through elevation,
 * not through chown(): a request that changes nothing succeeds, any other
 * is refused, as for an unprivileged POSIX process. */
int fchownat(int dirfd, const char *path, uid_t uid, gid_t gid, int flag)
{
	struct stat st;
	if (flag & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) {
		errno = EINVAL;
		return -1;
	}
	if (fstatat(dirfd, path, &st, flag)) return -1;
	if ((uid == (uid_t)-1 || uid == st.st_uid) && (gid == (gid_t)-1 || gid == st.st_gid))
		return 0;
	errno = EPERM;
	return -1;
}

int chown(const char *path, uid_t uid, gid_t gid)
{
	return fchownat(AT_FDCWD, path, uid, gid, 0);
}

int lchown(const char *path, uid_t uid, gid_t gid)
{
	return fchownat(AT_FDCWD, path, uid, gid, AT_SYMLINK_NOFOLLOW);
}

int fchown(int fd, uid_t uid, gid_t gid)
{
	return fchownat(fd, "", uid, gid, AT_EMPTY_PATH);
}

/* Every process sees the one VexFS namespace. */
int chroot(const char *path)
{
	(void)path;
	errno = EPERM;
	return -1;
}

/* A Vex user is its own single group (see getgroups). */
int setgroups(size_t count, const gid_t list[])
{
	if (count == 0 || (count == 1 && list[0] == getgid())) return 0;
	errno = EPERM;
	return -1;
}

int pthread_setname_np(pthread_t t, const char *name)
{
	vex_thread_name_req_t req = {
		.tid = (uint32_t)t->tid, .op = VEX_THREAD_NAME_SET,
		.name = (char *)name, .size = strlen(name) + 1,
	};
	if (strlen(name) >= 16) return ERANGE; /* the POSIX-visible limit (Linux's) */
	long r = (long)(int64_t)__vex_call1(VEX_SYS_THREAD_NAME, __vex_ptr(&req));
	return r < 0 ? (int)-r : 0;
}

int pthread_getname_np(pthread_t t, char *name, size_t len)
{
	vex_thread_name_req_t req = {
		.tid = (uint32_t)t->tid, .op = VEX_THREAD_NAME_GET, .name = name, .size = len,
	};
	if (!len) return ERANGE;
	long r = (long)(int64_t)__vex_call1(VEX_SYS_THREAD_NAME, __vex_ptr(&req));
	return r < 0 ? (int)-r : 0;
}
