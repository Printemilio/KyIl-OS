/* Files, directories and descriptors through the Vex libc. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <poll.h>
#include "check.h"

#define DIR_PATH "/tmp/vex-libc-fs"

int main(void)
{
	char path[256], buf[256];
	struct stat st;

	mkdir(DIR_PATH, 0755);
	CHECK(stat(DIR_PATH, &st) == 0 && S_ISDIR(st.st_mode));

	/* Creation with a mode, O_EXCL, write/read back, offsets. */
	snprintf(path, sizeof path, "%s/a.txt", DIR_PATH);
	unlink(path);
	umask(022);
	int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0640);
	CHECK(fd >= 0);
	CHECK(stat(path, &st) == 0);
	CHECK_EQ_INT(st.st_mode & 0777, 0640);
	CHECK(open(path, O_CREAT | O_EXCL | O_WRONLY, 0640) < 0 && errno == EEXIST);
	CHECK_EQ_INT(write(fd, "hello, vex\n", 11), 11);
	struct iovec iov[2] = { { "abc", 3 }, { "def", 3 } };
	CHECK_EQ_INT(writev(fd, iov, 2), 6);
	CHECK(close(fd) == 0);

	fd = open(path, O_RDONLY);
	CHECK(fd >= 0);
	CHECK(fstat(fd, &st) == 0 && S_ISREG(st.st_mode));
	CHECK_EQ_INT(st.st_size, 17);
	CHECK_EQ_INT(read(fd, buf, 5), 5);
	CHECK(memcmp(buf, "hello", 5) == 0);
	CHECK_EQ_INT(lseek(fd, 0, SEEK_CUR), 5);
	CHECK_EQ_INT(pread(fd, buf, 3, 11), 3);
	CHECK(memcmp(buf, "abc", 3) == 0);
	CHECK_EQ_INT(lseek(fd, 0, SEEK_CUR), 5);
	CHECK_EQ_INT(lseek(fd, -3, SEEK_END), 14);
	CHECK(close(fd) == 0);
	CHECK(read(fd, buf, 1) < 0 && errno == EBADF);

	/* stdio on files: buffered writes, fseek, getline. */
	FILE *f = fopen(path, "a+");
	CHECK(f != NULL);
	if (f) {
		CHECK(fprintf(f, "line %d\n", 2) == 7);
		CHECK(fseek(f, 0, SEEK_SET) == 0);
		char *line = NULL;
		size_t cap = 0;
		CHECK(getline(&line, &cap, f) == 11 && !strcmp(line, "hello, vex\n"));
		free(line);
		CHECK(fclose(f) == 0);
	}
	CHECK(stat(path, &st) == 0 && st.st_size == 24);

	/* Rename, missing files, access. */
	snprintf(buf, sizeof buf, "%s/b.txt", DIR_PATH);
	unlink(buf);
	CHECK(rename(path, buf) == 0);
	CHECK(stat(path, &st) < 0 && errno == ENOENT);
	CHECK(access(buf, R_OK) == 0);
	CHECK(open("/tmp/vex-libc-fs/missing", O_RDONLY) < 0 && errno == ENOENT);

	/* Directory listing. */
	DIR *d = opendir(DIR_PATH);
	CHECK(d != NULL);
	int found = 0;
	if (d) {
		struct dirent *de;
		while ((de = readdir(d)))
			if (!strcmp(de->d_name, "b.txt")) found = 1;
		CHECK(closedir(d) == 0);
	}
	CHECK(found);

	/* Working directory and relative paths. */
	char cwd[256];
	CHECK(getcwd(cwd, sizeof cwd) != NULL);
	CHECK(chdir(DIR_PATH) == 0);
	CHECK(stat("b.txt", &st) == 0);
	CHECK(getcwd(buf, sizeof buf) && !strcmp(buf, DIR_PATH));
	int dfd = open(".", O_RDONLY | O_DIRECTORY);
	CHECK(dfd >= 0);
	CHECK(fstatat(dfd, "b.txt", &st, 0) == 0 && st.st_size == 24);
	CHECK(close(dfd) == 0);
	CHECK(chdir(cwd) == 0);

	/* Pipes, dup2, poll, close-on-exec flag. */
	int p[2];
	CHECK(pipe2(p, O_CLOEXEC) == 0);
	CHECK(fcntl(p[0], F_GETFD) & FD_CLOEXEC);
	CHECK_EQ_INT(write(p[1], "xy", 2), 2);
	struct pollfd pfd = { .fd = p[0], .events = POLLIN };
	CHECK_EQ_INT(poll(&pfd, 1, 1000), 1);
	CHECK(pfd.revents & POLLIN);
	int copy = dup(p[0]);
	CHECK(copy >= 0);
	CHECK_EQ_INT(read(copy, buf, 2), 2);
	CHECK(close(copy) == 0 && close(p[0]) == 0 && close(p[1]) == 0);

	/* Removal: a non-empty directory stays, unlink refuses directories. */
	CHECK(rmdir(DIR_PATH) < 0 && errno == ENOTEMPTY);
	CHECK(unlink(DIR_PATH) < 0 && (errno == EISDIR || errno == EPERM));
	CHECK(unlink("/tmp/vex-libc-fs/b.txt") == 0);
	CHECK(remove(DIR_PATH) == 0);
	CHECK(stat(DIR_PATH, &st) < 0 && errno == ENOENT);

	return check_report("libc-fs");
}
