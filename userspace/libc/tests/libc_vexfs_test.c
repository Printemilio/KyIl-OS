/* VexFS v2 through the Vex libc: a growable entry table, long paths, file
 * times, fchmod, directory renames, and the memory-only /tmp namespace,
 * including renames that cross between /tmp and durable storage. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "check.h"

#define MANY 300

static int write_file(const char *path, const char *text)
{
	int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0) return -1;
	size_t n = strlen(text);
	int ok = write(fd, text, n) == (ssize_t)n;
	return close(fd) == 0 && ok ? 0 : -1;
}

static int read_equals(const char *path, const char *text)
{
	char buf[512];
	int fd = open(path, O_RDONLY);
	if (fd < 0) return 0;
	ssize_t n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if (n < 0) return 0;
	buf[n] = 0;
	return !strcmp(buf, text);
}

static int count_entries(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *de;
	int n = 0;
	if (!d) return -1;
	while ((de = readdir(d)))
		if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) n++;
	closedir(d);
	return n;
}

/* More files than the old fixed 64-entry table could hold. */
static void test_many(const char *base)
{
	char dir[256], path[320], text[64];
	int created = 0;

	snprintf(dir, sizeof dir, "%s/many", base);
	CHECK(mkdir(dir, 0755) == 0);
	for (int i = 0; i < MANY; i++) {
		snprintf(path, sizeof path, "%s/file-%03d", dir, i);
		snprintf(text, sizeof text, "contents of %d\n", i);
		if (write_file(path, text) == 0) created++;
	}
	CHECK_EQ_INT(created, MANY);
	CHECK_EQ_INT(count_entries(dir), MANY);
	int intact = 0;
	for (int i = 0; i < MANY; i += 7) {
		snprintf(path, sizeof path, "%s/file-%03d", dir, i);
		snprintf(text, sizeof text, "contents of %d\n", i);
		intact += read_equals(path, text);
	}
	CHECK_EQ_INT(intact, (MANY + 6) / 7);
	for (int i = 0; i < MANY; i++) {
		snprintf(path, sizeof path, "%s/file-%03d", dir, i);
		unlink(path);
	}
	CHECK_EQ_INT(count_entries(dir), 0);
	CHECK(rmdir(dir) == 0);
}

/* Paths far beyond the old 128-byte limit, both as names and as depth. */
static void test_long_paths(const char *base)
{
	char path[512], name[201];
	struct stat st;

	memset(name, 'n', 200);
	name[200] = 0;
	snprintf(path, sizeof path, "%s/%s", base, name);
	CHECK(write_file(path, "long name\n") == 0);
	CHECK(read_equals(path, "long name\n"));
	CHECK(stat(path, &st) == 0 && st.st_size == 10);
	CHECK(unlink(path) == 0);

	size_t used = (size_t)snprintf(path, sizeof path, "%s", base);
	int depth = 0;
	while (used + 21 < 420) {
		used += (size_t)snprintf(path + used, sizeof path - used, "/directory-level-%02d", depth++);
		if (mkdir(path, 0755) != 0) break;
	}
	CHECK(used >= 400);
	CHECK(stat(path, &st) == 0 && S_ISDIR(st.st_mode));
	strcat(path, "/leaf.txt");
	CHECK(write_file(path, "deep\n") == 0);
	CHECK(read_equals(path, "deep\n"));
	CHECK(unlink(path) == 0);
	/* Remove the chain from the deepest level up. */
	for (char *slash; depth-- > 0;) {
		slash = strrchr(path, '/');
		*slash = 0;
		CHECK(rmdir(path) == 0);
	}
}

static void test_times(const char *base)
{
	char path[320];
	struct stat st;
	struct timespec before, after;

	snprintf(path, sizeof path, "%s/times.txt", base);
	clock_gettime(CLOCK_REALTIME, &before);
	CHECK(write_file(path, "t") == 0);
	clock_gettime(CLOCK_REALTIME, &after);
	CHECK(stat(path, &st) == 0);
	/* Creation stamps all three times with the realtime clock. */
	CHECK(st.st_mtim.tv_sec >= before.tv_sec && st.st_mtim.tv_sec <= after.tv_sec + 1);
	CHECK(st.st_ctim.tv_sec >= before.tv_sec && st.st_atim.tv_sec >= before.tv_sec);

	struct timespec set[2] = { { 1000000000, 123456789 }, { 1200000000, 987654321 } };
	CHECK(utimensat(AT_FDCWD, path, set, 0) == 0);
	CHECK(stat(path, &st) == 0);
	CHECK_EQ_INT(st.st_atim.tv_sec, 1000000000);
	CHECK_EQ_INT(st.st_atim.tv_nsec, 123456789);
	CHECK_EQ_INT(st.st_mtim.tv_sec, 1200000000);
	CHECK_EQ_INT(st.st_mtim.tv_nsec, 987654321);
	CHECK(st.st_ctim.tv_sec >= before.tv_sec);

	/* UTIME_OMIT keeps a time, UTIME_NOW takes the current one. */
	struct timespec partial[2] = { { 0, UTIME_OMIT }, { 0, UTIME_NOW } };
	CHECK(utimensat(AT_FDCWD, path, partial, 0) == 0);
	CHECK(stat(path, &st) == 0);
	CHECK_EQ_INT(st.st_atim.tv_sec, 1000000000);
	CHECK(st.st_mtim.tv_sec >= before.tv_sec);

	/* Through a descriptor with buffered data: the write must not
	   overtake the times set afterwards. */
	int fd = open(path, O_WRONLY | O_APPEND);
	CHECK(fd >= 0);
	CHECK_EQ_INT(write(fd, "more", 4), 4);
	CHECK(futimens(fd, set) == 0);
	CHECK(close(fd) == 0);
	CHECK(stat(path, &st) == 0);
	CHECK_EQ_INT(st.st_mtim.tv_sec, 1200000000);
	CHECK_EQ_INT(st.st_size, 5);

	struct utimbuf ub = { .actime = 1300000000, .modtime = 1300000001 };
	CHECK(utime(path, &ub) == 0);
	CHECK(stat(path, &st) == 0 && st.st_mtim.tv_sec == 1300000001);
	struct timeval tv[2] = { { 1400000000, 5 }, { 1400000001, 7 } };
	CHECK(utimes(path, tv) == 0);
	CHECK(stat(path, &st) == 0 && st.st_mtim.tv_sec == 1400000001 && st.st_mtim.tv_nsec == 7000);

	struct timespec bad[2] = { { 0, 1000000000 }, { 0, 0 } };
	CHECK(utimensat(AT_FDCWD, path, bad, 0) < 0 && errno == EINVAL);
	CHECK(utimensat(AT_FDCWD, "/tmp/vex-vexfs-missing", NULL, 0) < 0 && errno == ENOENT);

	/* fchmod changes the mode and the change time. */
	fd = open(path, O_RDONLY);
	CHECK(fd >= 0);
	CHECK(fchmod(fd, 0600) == 0);
	CHECK(fstat(fd, &st) == 0 && (st.st_mode & 0777) == 0600);
	CHECK(close(fd) == 0);
	CHECK(fchmod(fd, 0600) < 0 && errno == EBADF);
	CHECK(unlink(path) == 0);
}

/* Renaming a directory moves everything below it. */
static void test_directory_rename(const char *base)
{
	char a[320], b[320], path[400];
	struct stat st;

	snprintf(a, sizeof a, "%s/dir-a", base);
	snprintf(b, sizeof b, "%s/dir-b", base);
	CHECK(mkdir(a, 0755) == 0);
	snprintf(path, sizeof path, "%s/sub", a);
	CHECK(mkdir(path, 0755) == 0);
	snprintf(path, sizeof path, "%s/sub/x.txt", a);
	CHECK(write_file(path, "x\n") == 0);
	snprintf(path, sizeof path, "%s/y.txt", a);
	CHECK(write_file(path, "y\n") == 0);

	CHECK(rename(a, b) == 0);
	CHECK(stat(a, &st) < 0 && errno == ENOENT);
	CHECK(stat(b, &st) == 0 && S_ISDIR(st.st_mode));
	snprintf(path, sizeof path, "%s/sub/x.txt", b);
	CHECK(read_equals(path, "x\n"));
	snprintf(path, sizeof path, "%s/y.txt", b);
	CHECK(read_equals(path, "y\n"));
	snprintf(path, sizeof path, "%s/sub/inner", b);
	CHECK(rename(b, path) < 0); /* not into itself */

	snprintf(path, sizeof path, "%s/sub/x.txt", b);
	CHECK(unlink(path) == 0);
	snprintf(path, sizeof path, "%s/sub", b);
	CHECK(rmdir(path) == 0);
	snprintf(path, sizeof path, "%s/y.txt", b);
	CHECK(unlink(path) == 0);
	CHECK(rmdir(b) == 0);
}

/* A file moved out of /tmp becomes durable, and back again. */
static void test_cross_namespace(const char *base)
{
	char durable[320];
	const char *volatile_path = "/tmp/vex-vexfs-cross.txt";

	snprintf(durable, sizeof durable, "%s/cross.txt", base);
	unlink(volatile_path);
	unlink(durable);
	CHECK(write_file(volatile_path, "crossing\n") == 0);
	CHECK(rename(volatile_path, durable) == 0);
	CHECK(access(volatile_path, F_OK) < 0 && errno == ENOENT);
	CHECK(read_equals(durable, "crossing\n"));
	CHECK(rename(durable, volatile_path) == 0);
	CHECK(read_equals(volatile_path, "crossing\n"));
	CHECK(access(durable, F_OK) < 0 && errno == ENOENT);
	CHECK(unlink(volatile_path) == 0);
}

int main(void)
{
	char base[256];
	const char *home = getenv("HOME");

	/* Durable storage: the caller's home directory. */
	if (!home || !*home) home = getuid() == 0 ? "/home/root" : "/tmp";
	mkdir(home, 0755);
	snprintf(base, sizeof base, "%s/vex-vexfs-test", home);
	fprintf(stderr, "  durable base %s\n", base);
	CHECK(mkdir(base, 0755) == 0 || errno == EEXIST);

	/* Across boots: the durable stamp written by the previous run (with a
	   long name, past the old table limits) is still there, and the /tmp
	   marker it left is gone, since /tmp starts empty at every boot. */
	{
		char stamp[400], text[64];
		int runs = 0;
		snprintf(stamp, sizeof stamp, "%s/vex-vexfs-persist-%0200d", home, 0);
		int fd = open(stamp, O_RDONLY);
		if (fd >= 0) {
			ssize_t n = read(fd, text, sizeof text - 1);
			close(fd);
			text[n > 0 ? n : 0] = 0;
			runs = atoi(text);
			CHECK(runs > 0);
			CHECK(access("/tmp/vex-vexfs-boot-marker", F_OK) < 0 && errno == ENOENT);
		}
		fprintf(stderr, "  previous runs on this disk: %d\n", runs);
		snprintf(text, sizeof text, "%d\n", runs + 1);
		CHECK(write_file(stamp, text) == 0);
		CHECK(write_file("/tmp/vex-vexfs-boot-marker", "boot\n") == 0);
	}

	/* A disk made by scripts/make-vexfs-v4-image.py: its files were
	   migrated to version 5, and its /tmp entry was dropped. */
	if (access("/home/root/vexfs-v4/legacy.txt", F_OK) == 0) {
		struct stat st;
		CHECK(read_equals("/home/root/vexfs-v4/legacy.txt", "written by VexFS v4\n"));
		CHECK(stat("/home/root/vexfs-v4", &st) == 0 && S_ISDIR(st.st_mode));
		CHECK(access("/tmp/vexfs-v4-stale.txt", F_OK) < 0 && errno == ENOENT);
		fprintf(stderr, "  version 4 volume migrated\n");
	}

	test_many("/tmp");
	test_many(base);
	test_long_paths("/tmp");
	test_long_paths(base);
	test_times("/tmp");
	test_times(base);
	test_directory_rename("/tmp");
	test_directory_rename(base);
	test_cross_namespace(base);

	CHECK(rmdir(base) == 0);
	return check_report("libc-vexfs");
}
