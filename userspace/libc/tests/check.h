/* Minimal assertion helpers for the Vex libc conformance tests. Results go
 * to stderr, which the kernel mirrors to the serial log. */
#ifndef VEX_LIBC_TEST_CHECK_H
#define VEX_LIBC_TEST_CHECK_H

#include <stdio.h>
#include <string.h>
#include <errno.h>

static int check_failures;
static int check_count;

#define CHECK(cond) do { \
	check_count++; \
	if (!(cond)) { \
		check_failures++; \
		fprintf(stderr, "  FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, #cond, \
			errno, strerror(errno)); \
	} \
} while (0)

#define CHECK_EQ_INT(a, b) do { \
	long long check_a = (long long)(a), check_b = (long long)(b); \
	check_count++; \
	if (check_a != check_b) { \
		check_failures++; \
		fprintf(stderr, "  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, \
			#a, #b, check_a, check_b); \
	} \
} while (0)

#define CHECK_EQ_STR(a, b) do { \
	const char *check_a = (a), *check_b = (b); \
	check_count++; \
	if (!check_a || !check_b || strcmp(check_a, check_b)) { \
		check_failures++; \
		fprintf(stderr, "  FAIL %s:%d: %s == \"%s\" (got \"%s\")\n", __FILE__, __LINE__, \
			#a, check_b ? check_b : "(null)", check_a ? check_a : "(null)"); \
	} \
} while (0)

static int check_report(const char *name)
{
	fprintf(stderr, "%s: %s (%d checks, %d failed)\n", name,
		check_failures ? "FAIL" : "PASS", check_count, check_failures);
	return check_failures ? 1 : 0;
}

#endif
