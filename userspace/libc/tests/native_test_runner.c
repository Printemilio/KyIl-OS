/* Runs every native test program found next to it and reports to the
 * serial log (stderr). Started by the kernel in the auto_native_test boot
 * mode; it is itself a Vex libc program (readdir, posix_spawn, waitpid). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>

extern char **environ;

static int by_name(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "/usr/native-tests";
	char *names[128];
	int count = 0, failed = 0;
	DIR *d = opendir(dir);

	if (!d) {
		perror("native-tests: opendir");
		return 2;
	}
	struct dirent *de;
	while ((de = readdir(d)) && count < 128) {
		if (de->d_name[0] == '.' || strstr(de->d_name, "runner")) continue;
		names[count++] = strdup(de->d_name);
	}
	closedir(d);
	qsort(names, count, sizeof names[0], by_name);

	fprintf(stderr, "native-tests: %d programs in %s\n", count, dir);
	for (int i = 0; i < count; i++) {
		char path[512];
		pid_t pid;
		int status = -1;
		struct timespec a, b;
		snprintf(path, sizeof path, "%s/%s", dir, names[i]);
		char *args[] = { path, NULL };
		clock_gettime(CLOCK_MONOTONIC, &a);
		int e = posix_spawn(&pid, path, NULL, NULL, args, environ);
		if (e) {
			fprintf(stderr, "native-tests: %s: spawn failed (%s)\n", names[i], strerror(e));
			failed++;
			continue;
		}
		if (waitpid(pid, &status, 0) != pid) status = -1;
		clock_gettime(CLOCK_MONOTONIC, &b);
		long ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
		int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
		if (!ok) failed++;
		if (WIFSIGNALED(status))
			fprintf(stderr, "native-tests: %s: FAIL (signal %d, %ld ms)\n", names[i], WTERMSIG(status), ms);
		else
			fprintf(stderr, "native-tests: %s: %s (exit %d, %ld ms)\n", names[i],
				ok ? "ok" : "FAIL", WEXITSTATUS(status), ms);
	}
	fprintf(stderr, "native-tests: done, %d passed, %d failed\n", count - failed, failed);
	return failed ? 1 : 0;
}
