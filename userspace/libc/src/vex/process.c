/* Process creation, replacement, waiting and termination on native calls. */
#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "syscall.h"
#include "at.h"

_Noreturn void _Exit(int status)
{
	for (;;) __vex_call1(VEX_SYS_PROCESS_EXIT_GROUP, (uint64_t)(unsigned)(status & 0xff));
}

int execve(const char *path, char *const argv[], char *const envp[])
{
	vex_process_exec_req_t req = { .path = path, .argv = argv, .envp = envp };
	/* Returns only on failure. */
	return (int)__vex_legacy_ret(__vex_call1(VEX_SYS_PROCESS_EXEC, __vex_ptr(&req)));
}

int fexecve(int fd, char *const argv[], char *const envp[])
{
	char buf[PATH_MAX];
	const char *path = __vex_at(fd, ".", buf);
	return path ? execve(path, argv, envp) : -1;
}

pid_t vfork(void)
{
	/* Vex has no address-space sharing child: vfork is a fork. */
	return fork();
}

/* The native wait status is the exit status, or 128 + the signal number
 * for a process ended by an event's default action. */
static int posix_status(int raw)
{
	if (raw >= 128 && raw < 128 + _NSIG) return raw - 128;
	return (raw & 0xff) << 8;
}

pid_t wait4(pid_t pid, int *status, int options, struct rusage *ru)
{
	int raw = 0;
	if (options & ~(WNOHANG | WUNTRACED | WCONTINUED)) {
		errno = EINVAL;
		return -1;
	}
	if (pid == 0 || (pid < -1 && -pid == getpgrp())) {
		/* Children stay in their parent's group (Vex has no setpgid to
		 * move them), so the caller's group holds all of its children. */
		pid = -1;
	} else if (pid < -1) {
		errno = ECHILD;
		return -1;
	}
	vex_process_wait_req_t req = {
		.pid = pid,
		.status = &raw,
		.flags = (uint32_t)(options & WNOHANG),
	};
	long r = __vex_legacy_ret(__vex_call1(VEX_SYS_PROCESS_WAIT, __vex_ptr(&req)));
	if (r > 0 && status) *status = posix_status(raw);
	if (r >= 0 && ru) memset(ru, 0, sizeof *ru);
	return (pid_t)r;
}

pid_t waitpid(pid_t pid, int *status, int options)
{
	return wait4(pid, status, options, 0);
}

int waitid(idtype_t type, id_t id, siginfo_t *info, int options)
{
	int status;
	pid_t pid;
	if (!(options & WEXITED) || options & ~(WEXITED | WNOHANG | WNOWAIT | WSTOPPED | WCONTINUED)) {
		errno = EINVAL;
		return -1;
	}
	if (options & WNOWAIT) {
		/* Peeking without reaping is not a native operation. */
		errno = ENOTSUP;
		return -1;
	}
	switch (type) {
	case P_ALL: pid = -1; break;
	case P_PID: pid = (pid_t)id; break;
	default:
		errno = EINVAL;
		return -1;
	}
	pid = wait4(pid, &status, options & WNOHANG, 0);
	if (pid < 0) return -1;
	memset(info, 0, sizeof *info);
	if (!pid) return 0;
	info->si_signo = SIGCHLD;
	info->si_pid = pid;
	if (WIFEXITED(status)) {
		info->si_code = CLD_EXITED;
		info->si_status = WEXITSTATUS(status);
	} else {
		info->si_code = CLD_KILLED;
		info->si_status = WTERMSIG(status);
	}
	return 0;
}
