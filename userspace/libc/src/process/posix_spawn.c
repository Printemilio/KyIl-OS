#define _GNU_SOURCE
#include <spawn.h>
#include <sched.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>
#include "syscall.h"
#include "lock.h"
#include "pthread_impl.h"
#include "fdop.h"

struct args {
	int p[2];
	sigset_t oldmask;
	const char *path;
	const posix_spawn_file_actions_t *fa;
	const posix_spawnattr_t *restrict attr;
	char *const *argv, *const *envp;
	int shared;
};

/* Vex: the child is preferably a shared-memory child
 * (VEX_SYS_PROCESS_SPAWN_SHARED: it runs on its own stack inside the
 * parent's memory until execve, like clone(CLONE_VM)), which saves copying
 * the whole parent -- hundreds of milliseconds for ocamlopt. An older kernel
 * falls back to a native fork. Either way the file actions use the ordinary
 * library calls; errno-style results are negated to keep musl's error path. */
static int __sys_dup2(int old, int new)
{
	return dup2(old, new) < 0 ? -errno : new;
}

hidden pid_t __vex_fork_holding_abort_lock(void);

/* Start fn(arg) in a shared-memory child on stack_top. Returns the child's
 * pid to the parent, or -1. The child never returns here: fn ends in _exit. */
static pid_t spawn_shared(int (*fn)(void *), void *arg, void *stack_top)
{
	uint64_t r;
	uint64_t *sp = (uint64_t *)((uintptr_t)stack_top & ~(uintptr_t)15) - 2;
	sp[0] = (uint64_t)(uintptr_t)arg;
	sp[1] = (uint64_t)(uintptr_t)fn;
	__asm__ __volatile__ (
		"syscall\n\t"
		"test %%rax, %%rax\n\t"
		"jnz 1f\n\t"
		/* child: rsp = sp, 16-byte aligned once both words are popped */
		"pop %%rdi\n\t"
		"pop %%rax\n\t"
		"call *%%rax\n\t"
		"ud2\n"
		"1:"
		: "=a"(r)
		: "a"(VEX_SYSCALL_NATIVE_TAG | VEX_SYS_PROCESS_SPAWN_SHARED), "D"(sp)
		: "rcx", "r11", "memory");
	return (int64_t)r < 0 ? -1 : (pid_t)r;
}

static int sys_ret(int r)
{
	return r < 0 ? -errno : r;
}

static int child(void *args_vp)
{
	int i, ret;
	struct sigaction sa = {0};
	struct args *args = args_vp;
	int p = args->p[1];
	const posix_spawn_file_actions_t *fa = args->fa;
	const posix_spawnattr_t *restrict attr = args->attr;
	sigset_t hset;

	close(args->p[0]);

	/* All signal dispositions must be either SIG_DFL or SIG_IGN
	 * before signals are unblocked. Otherwise a signal handler
	 * from the parent might get run in the child while sharing
	 * memory, with unpredictable and dangerous results. To
	 * reduce overhead, sigaction has tracked for us which signals
	 * potentially have a signal handler. */
	/* A shared-memory child must not touch the library's disposition table,
	 * which is the parent's: execve resets caught signals by itself. */
	if (args->shared) sigemptyset(&hset);
	else __get_handler_set(&hset);
	for (i=1; i<_NSIG && !args->shared; i++) {
		if ((attr->__flags & POSIX_SPAWN_SETSIGDEF)
		     && sigismember(&attr->__def, i)) {
			sa.sa_handler = SIG_DFL;
		} else if (sigismember(&hset, i)) {
			if (i-32<3U) {
				sa.sa_handler = SIG_IGN;
			} else {
				__libc_sigaction(i, 0, &sa);
				if (sa.sa_handler==SIG_IGN) continue;
				sa.sa_handler = SIG_DFL;
			}
		} else {
			continue;
		}
		__libc_sigaction(i, &sa, 0);
	}

	if (attr->__flags & POSIX_SPAWN_SETSID)
		if ((ret=sys_ret(setsid())) < 0)
			goto fail;

	if (attr->__flags & POSIX_SPAWN_SETPGROUP)
		if ((ret=sys_ret(setpgid(0, attr->__pgrp))))
			goto fail;

	if (attr->__flags & POSIX_SPAWN_RESETIDS)
		if ((ret=sys_ret(setgid(getgid()))) ||
		    (ret=sys_ret(setuid(getuid()))) )
			goto fail;

	if (fa && fa->__actions) {
		struct fdop *op;
		int fd;
		for (op = fa->__actions; op->next; op = op->next);
		for (; op; op = op->prev) {
			/* It's possible that a file operation would clobber
			 * the pipe fd used for synchronizing with the
			 * parent. To avoid that, we dup the pipe onto
			 * an unoccupied fd. */
			if (op->fd == p) {
				ret = sys_ret(dup(p));
				if (ret < 0) goto fail;
				close(p);
				p = ret;
			}
			switch(op->cmd) {
			case FDOP_CLOSE:
				close(op->fd);
				break;
			case FDOP_DUP2:
				fd = op->srcfd;
				if (fd == p) {
					ret = -EBADF;
					goto fail;
				}
				if (fd != op->fd) {
					if ((ret=__sys_dup2(fd, op->fd))<0)
						goto fail;
				} else {
					ret = sys_ret(fcntl(fd, F_GETFD));
					ret = sys_ret(fcntl(fd, F_SETFD,
					              ret & ~FD_CLOEXEC));
					if (ret<0)
						goto fail;
				}
				break;
			case FDOP_OPEN:
				fd = __sys_open(op->path, op->oflag, op->mode);
				if ((ret=fd) < 0) goto fail;
				if (fd != op->fd) {
					if ((ret=__sys_dup2(fd, op->fd))<0)
						goto fail;
					close(fd);
				}
				break;
			case FDOP_CHDIR:
				ret = sys_ret(chdir(op->path));
				if (ret<0) goto fail;
				break;
			case FDOP_FCHDIR:
				ret = sys_ret(fchdir(op->fd));
				if (ret<0) goto fail;
				break;
			}
		}
	}

	/* Close-on-exec flag may have been lost if we moved the pipe
	 * to a different fd. We don't use F_DUPFD_CLOEXEC above because
	 * it would fail on older kernels and atomicity is not needed --
	 * in this process there are no threads or signal handlers. */
	fcntl(p, F_SETFD, FD_CLOEXEC);

	pthread_sigmask(SIG_SETMASK, (attr->__flags & POSIX_SPAWN_SETSIGMASK)
		? &attr->__mask : &args->oldmask, 0);

	int (*exec)(const char *, char *const *, char *const *) =
		attr->__fn ? (int (*)())attr->__fn : execve;

	exec(args->path, args->argv, args->envp);
	ret = -errno;

fail:
	/* Since sizeof errno < PIPE_BUF, the write is atomic. */
	ret = -ret;
	if (ret) {
		int r;
		do r = write(p, &ret, sizeof ret);
		while (r<0 && errno!=EPIPE);
	}
	_exit(127);
}


int posix_spawn(pid_t *restrict res, const char *restrict path,
	const posix_spawn_file_actions_t *fa,
	const posix_spawnattr_t *restrict attr,
	char *const argv[restrict], char *const envp[restrict])
{
	pid_t pid;
	int ec=0, cs;
	struct args args;

	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);

	args.path = path;
	args.fa = fa;
	args.attr = attr ? attr : &(const posix_spawnattr_t){0};
	args.argv = argv;
	args.envp = envp;
	pthread_sigmask(SIG_BLOCK, SIGALL_SET, &args.oldmask);

	/* The lock guards both against seeing a SIGABRT disposition change
	 * by abort and against leaking the pipe fd to fork-without-exec. */
	LOCK(__abort_lock);

	if (pipe2(args.p, O_CLOEXEC)) {
		UNLOCK(__abort_lock);
		ec = errno;
		goto fail;
	}

	/* The child's stack; it is no longer used once the pipe reports exec
	 * (closed by O_CLOEXEC) or the child has been waited for. */
	enum { SPAWN_STACK = 64 << 10 };
	char *stack = malloc(SPAWN_STACK);
	args.shared = stack != 0;
	pid = args.shared ? spawn_shared(child, &args, stack + SPAWN_STACK) : -1;
	if (pid < 0) {
		args.shared = 0;
		pid = __vex_fork_holding_abort_lock();
		if (!pid) child(&args);
	}
	if (pid < 0) pid = -errno;
	close(args.p[1]);
	UNLOCK(__abort_lock);

	if (pid > 0) {
		if (read(args.p[0], &ec, sizeof ec) != sizeof ec) ec = 0;
		else waitpid(pid, &(int){0}, 0);
	} else {
		ec = -pid;
	}

	close(args.p[0]);
	free(stack);

	if (!ec && res) *res = pid;

fail:
	pthread_sigmask(SIG_SETMASK, &args.oldmask, 0);
	pthread_setcancelstate(cs, 0);

	return ec;
}
