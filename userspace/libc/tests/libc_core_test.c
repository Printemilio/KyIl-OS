/* Portable libc behaviour: formatting, conversions, math, memory, time. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <locale.h>
#include <setjmp.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include "check.h"

static int compare_ints(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;
	return (x > y) - (x < y);
}

static jmp_buf jump;

static void jump_back(int value)
{
	longjmp(jump, value);
}

static __thread int tls_value = 42;

int main(int argc, char **argv)
{
	char buf[256];

	/* Startup: argv and environment come from the kernel's initial stack. */
	CHECK(argc >= 1);
	CHECK(argv[0] && argv[0][0]);
	CHECK(argv[argc] == NULL);
	CHECK_EQ_INT(tls_value, 42);
	tls_value = 7;
	CHECK_EQ_INT(tls_value, 7);

	/* Formatting and conversions, the C locale. */
	snprintf(buf, sizeof buf, "%d|%5.2f|%s|%x|%" PRIu64 "|%e", -12, 3.14159, "vex", 255u,
		(uint64_t)18446744073709551615ull, 1e-300);
	CHECK_EQ_STR(buf, "-12| 3.14|vex|ff|18446744073709551615|1.000000e-300");
	snprintf(buf, sizeof buf, "%.17g", 0.1);
	CHECK_EQ_STR(buf, "0.10000000000000001");
	CHECK(strtod("0.10000000000000001", NULL) == 0.1);
	CHECK(strtod("1e308", NULL) == 1e308);
	CHECK(isinf(strtod("1e400", NULL)));
	CHECK_EQ_INT(strtol("-0x7fffffff", NULL, 16), -0x7fffffffL);
	CHECK_EQ_INT(strtoull("18446744073709551615", NULL, 10) == 18446744073709551615ull, 1);
	locale_t c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	CHECK(c != (locale_t)0);
	CHECK(strtod_l("2.5", NULL, c) == 2.5);
	freelocale(c);

	/* Math. */
	CHECK(fabs(sin(1.0) - 0.8414709848078965) < 1e-15);
	CHECK(fabs(pow(2.0, 0.5) - sqrt(2.0)) < 1e-15);
	CHECK(fabs(exp(log(10.0)) - 10.0) < 1e-13);
	CHECK(fmod(10.5, 3.0) == 1.5);
	CHECK(fabs(expm1(1e-10) - 1e-10) < 1e-20);
	CHECK(fabs(hypot(3.0, 4.0) - 5.0) < 1e-15);
	CHECK(nextafter(1.0, 2.0) > 1.0);
	CHECK(fma(2.0, 3.0, 1.0) == 7.0);
	CHECK(round(2.5) == 3.0 && round(-2.5) == -3.0);
	CHECK(isnan(nan("")));

	/* Memory. */
	void *blocks[512];
	for (int i = 0; i < 512; i++) {
		blocks[i] = malloc((size_t)(i * 37 % 4096 + 1));
		CHECK(blocks[i] != NULL);
		memset(blocks[i], i & 0xff, (size_t)(i * 37 % 4096 + 1));
	}
	for (int i = 0; i < 512; i += 2) free(blocks[i]);
	for (int i = 1; i < 512; i += 2) {
		unsigned char *p = blocks[i];
		CHECK(p[0] == (unsigned char)(i & 0xff));
		blocks[i] = realloc(blocks[i], 100000);
		CHECK(blocks[i] != NULL && ((unsigned char *)blocks[i])[0] == (unsigned char)(i & 0xff));
		free(blocks[i]);
	}
	void *big = malloc(64u << 20);
	CHECK(big != NULL);
	if (big) {
		memset(big, 1, 64u << 20);
		free(big);
	}
	void *aligned = NULL;
	CHECK(posix_memalign(&aligned, 4096, 12345) == 0 && ((uintptr_t)aligned & 4095) == 0);
	free(aligned);

	/* Anonymous mappings: reserve, commit with MAP_FIXED, protect. */
	unsigned char *res = mmap(NULL, 1 << 20, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	CHECK(res != MAP_FAILED);
	if (res != MAP_FAILED) {
		unsigned char *com = mmap(res + 65536, 65536, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
		CHECK(com == res + 65536);
		if (com == res + 65536) {
			com[0] = 1;
			com[65535] = 2;
			CHECK(com[0] + com[65535] == 3);
		}
		CHECK(mprotect(res + 65536, 65536, PROT_READ) == 0);
		CHECK(munmap(res, 1 << 20) == 0);
	}

	/* Sorting and searching. */
	int values[1000];
	for (int i = 0; i < 1000; i++) values[i] = (i * 7919) % 1000;
	qsort(values, 1000, sizeof values[0], compare_ints);
	int sorted = 1;
	for (int i = 0; i < 1000; i++) sorted &= values[i] == i;
	CHECK(sorted);

	/* setjmp/longjmp. */
	int r = setjmp(jump);
	if (!r) jump_back(5);
	CHECK_EQ_INT(r, 5);

	/* Environment. */
	CHECK(setenv("VEX_LIBC_TEST", "value", 1) == 0);
	CHECK_EQ_STR(getenv("VEX_LIBC_TEST"), "value");
	CHECK(unsetenv("VEX_LIBC_TEST") == 0 && getenv("VEX_LIBC_TEST") == NULL);

	/* Clocks. */
	struct timespec a, b, cpu;
	CHECK(clock_gettime(CLOCK_MONOTONIC, &a) == 0);
	struct timespec nap = { 0, 20 * 1000 * 1000 };
	CHECK(nanosleep(&nap, NULL) == 0);
	CHECK(clock_gettime(CLOCK_MONOTONIC, &b) == 0);
	long long elapsed = (b.tv_sec - a.tv_sec) * 1000000000ll + (b.tv_nsec - a.tv_nsec);
	CHECK(elapsed >= 20000000ll);
	CHECK(time(NULL) > 1600000000);
	CHECK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu) == 0);
	struct tm tm;
	time_t epoch = 86400 * 365;
	CHECK(gmtime_r(&epoch, &tm) != NULL && tm.tm_year == 71 && tm.tm_yday == 0);
	strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
	CHECK_EQ_STR(buf, "1971-01-01 00:00:00");

	/* System description. */
	struct utsname u;
	CHECK(uname(&u) == 0 && u.sysname[0]);
	CHECK(sysconf(_SC_PAGESIZE) == 4096);
	CHECK(sysconf(_SC_NPROCESSORS_ONLN) >= 1);
	fprintf(stderr, "  cpus=%ld sysname=%s\n", sysconf(_SC_NPROCESSORS_ONLN), u.sysname);

	/* stdio buffering through a pipe-less descriptor: stdout to the TTY. */
	CHECK(printf("libc-core: stdout works\n") > 0);
	CHECK(fflush(stdout) == 0);

	/* sysconf and getrlimit answer each other's questions without
	   recursing (binutils asks for _SC_OPEN_MAX at startup). */
	{
		struct rlimit rl;
		long open_max = sysconf(_SC_OPEN_MAX);
		CHECK(open_max > 0);
		CHECK(getrlimit(RLIMIT_NOFILE, &rl) == 0 && (long)rl.rlim_cur == open_max);
		CHECK(getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur >= 8 * 1024 * 1024);
		CHECK(getdtablesize() == open_max);
	}

	return check_report("libc-core");
}
