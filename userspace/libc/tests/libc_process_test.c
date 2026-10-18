/* Processes and signals: fork, exec, wait, posix_spawn, pipes, signals. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>
#include "check.h"

extern char **environ;

static volatile sig_atomic_t usr1_seen, usr2_seen;

static void on_usr1(int sig)
{
	usr1_seen = sig;
}

static void on_usr2(int sig, siginfo_t *si, void *uc)
{
	(void)uc;
	usr2_seen = si->si_signo == sig ? sig : -1;
}

/* Child roles, selected by argv[1] when the test re-executes itself. */
static int child_main(int argc, char **argv)
{
	if (!strcmp(argv[1], "exit")) return atoi(argv[2]);
	if (!strcmp(argv[1], "env")) {
		const char *v = getenv("VEX_CHILD_VALUE");
		return v && !strcmp(v, "42") ? 0 : 9;
	}
	if (!strcmp(argv[1], "echo")) {
		/* stdout is a pipe set up by the parent. */
		for (int i = 2; i < argc; i++) printf("%s%s", argv[i], i + 1 < argc ? " " : "\n");
		return 0;
	}
	if (!strcmp(argv[1], "wait-term")) {
		/* Default action of SIGTERM ends the process. */
		for (;;) pause();
	}
	return 99;
}

int main(int argc, char **argv)
{
	int status;
	if (argc > 1) return child_main(argc, argv);

	/* fork and exit status. */
	pid_t pid = fork();
	CHECK(pid >= 0);
	if (pid == 0) _exit(7);
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 7);
	CHECK(waitpid(-1, &status, WNOHANG) < 0 && errno == ECHILD);

	/* exec with arguments and environment. */
	pid = fork();
	if (pid == 0) {
		char *args[] = { argv[0], "exit", "23", NULL };
		execve(argv[0], args, environ);
		_exit(98);
	}
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 23);

	setenv("VEX_CHILD_VALUE", "42", 1);
	char *env_args[] = { argv[0], "env", NULL };
	CHECK(posix_spawn(&pid, argv[0], NULL, NULL, env_args, environ) == 0);
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);

	/* A pipe from a spawned child's stdout. */
	int p[2];
	CHECK(pipe(p) == 0);
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, p[1], 1);
	posix_spawn_file_actions_addclose(&fa, p[0]);
	char *echo_args[] = { argv[0], "echo", "native", "vex", NULL };
	CHECK(posix_spawn(&pid, argv[0], &fa, NULL, echo_args, environ) == 0);
	close(p[1]);
	char buf[64] = {0};
	ssize_t n, got = 0;
	while ((n = read(p[0], buf + got, sizeof buf - 1 - got)) > 0) got += n;
	close(p[0]);
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK_EQ_STR(buf, "native vex\n");
	posix_spawn_file_actions_destroy(&fa);

	/* Missing program: posix_spawn reports the exec error. */
	char *missing[] = { "/no/such/program", NULL };
	CHECK(posix_spawn(&pid, "/no/such/program", NULL, NULL, missing, environ) == ENOENT);

	/* posix_spawn from a process that has had threads: libc locks are now
	 * real, and spawning must not deadlock on them (regression). */
	pthread_t t;
	CHECK(pthread_create(&t, NULL, (void *(*)(void *))getpid, NULL) == 0);
	CHECK(pthread_join(t, NULL) == 0);
	char *threaded_args[] = { argv[0], "exit", "5", NULL };
	CHECK(posix_spawn(&pid, argv[0], NULL, NULL, threaded_args, environ) == 0);
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 5);
	pid = fork();
	if (pid == 0) _exit(6);
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 6);

	/* Signal handlers: native events delivered to this process. */
	struct sigaction sa = { .sa_handler = on_usr1 };
	CHECK(sigaction(SIGUSR1, &sa, NULL) == 0);
	CHECK(kill(getpid(), SIGUSR1) == 0);
	for (int i = 0; i < 100 && !usr1_seen; i++) usleep(1000);
	CHECK_EQ_INT(usr1_seen, SIGUSR1);

	struct sigaction sa2 = { .sa_sigaction = on_usr2, .sa_flags = SA_SIGINFO };
	CHECK(sigaction(SIGUSR2, &sa2, NULL) == 0);
	CHECK(raise(SIGUSR2) == 0);
	for (int i = 0; i < 100 && !usr2_seen; i++) usleep(1000);
	CHECK_EQ_INT(usr2_seen, SIGUSR2);

	/* A blocked signal waits for its unblocking. */
	sigset_t set, old;
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	usr1_seen = 0;
	CHECK(sigprocmask(SIG_BLOCK, &set, &old) == 0);
	CHECK(raise(SIGUSR1) == 0);
	usleep(5000);
	CHECK_EQ_INT(usr1_seen, 0);
	CHECK(sigprocmask(SIG_SETMASK, &old, NULL) == 0);
	for (int i = 0; i < 100 && !usr1_seen; i++) usleep(1000);
	CHECK_EQ_INT(usr1_seen, SIGUSR1);

	/* An ignored signal is ignored; an in-process signal without a
	 * native class still reaches its handler. */
	signal(SIGUSR1, SIG_IGN);
	CHECK(kill(getpid(), SIGUSR1) == 0);
	usleep(2000);
	usr1_seen = 0;
	CHECK(signal(SIGTRAP, on_usr1) != SIG_ERR);
	CHECK(raise(SIGTRAP) == 0);
	CHECK_EQ_INT(usr1_seen, SIGTRAP);

	/* SIGTERM's default action ends another process, reported as such. */
	char *term_args[] = { argv[0], "wait-term", NULL };
	CHECK(posix_spawn(&pid, argv[0], NULL, NULL, term_args, environ) == 0);
	usleep(20000);
	CHECK(kill(pid, SIGTERM) == 0);
	CHECK(waitpid(pid, &status, 0) == pid);
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

	return check_report("libc-process");
}
