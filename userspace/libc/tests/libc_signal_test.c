/* CPU faults as signals: SIGSEGV handlers, the alternate signal stack and
 * leaving a handler with siglongjmp. */
#define _GNU_SOURCE
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "check.h"

static sigjmp_buf env;
static char alt_stack[64 * 1024];
static volatile int on_alt, fault_code, faults;
static volatile void *fault_addr;

static void on_segv(int sig, siginfo_t *si, void *uc)
{
	char probe;
	(void)uc;
	faults++;
	on_alt = &probe >= alt_stack && &probe < alt_stack + sizeof alt_stack;
	fault_addr = si->si_addr;
	fault_code = si->si_code;
	siglongjmp(env, sig);
}

static int recurse(int depth)
{
	volatile char frame[4096];
	frame[0] = (char)depth;
	return recurse(depth + 1) + frame[0];
}

int main(void)
{
	stack_t ss = { .ss_sp = alt_stack, .ss_size = sizeof alt_stack }, old;
	CHECK(sigaltstack(&ss, &old) == 0);
	CHECK(old.ss_flags & SS_DISABLE);
	CHECK(sigaltstack(NULL, &old) == 0 && old.ss_sp == alt_stack && old.ss_flags == 0);
	stack_t tiny = { .ss_sp = alt_stack, .ss_size = 16 };
	CHECK(sigaltstack(&tiny, NULL) < 0 && errno == ENOMEM);

	struct sigaction sa = { .sa_sigaction = on_segv, .sa_flags = SA_SIGINFO | SA_ONSTACK };
	CHECK(sigaction(SIGSEGV, &sa, NULL) == 0);

	/* A null-page access, recovered with siglongjmp. */
	if (!sigsetjmp(env, 1)) {
		*(volatile int *)16 = 1;
		CHECK(!"the write must fault");
	}
	CHECK_EQ_INT(faults, 1);
	CHECK(fault_addr == (void *)16);
	CHECK_EQ_INT(fault_code, SEGV_MAPERR);
	CHECK(on_alt);

	/* The handler left with siglongjmp: the next fault is still delivered. */
	if (!sigsetjmp(env, 1)) {
		*(volatile char *)(uintptr_t)8 = 0;
	}
	CHECK_EQ_INT(faults, 2);

	/* A stack overflow can only be handled on the alternate stack. */
	on_alt = 0;
	if (!sigsetjmp(env, 1)) {
		recurse(0);
	}
	CHECK_EQ_INT(faults, 3);
	CHECK(on_alt);
	fprintf(stderr, "  stack overflow caught at %p on the alternate stack\n", (void *)fault_addr);

	/* Without a handler the default action ends the process: checked by
	 * libc_process_test through SIGTERM; here SIG_DFL is just restored. */
	sa.sa_handler = SIG_DFL;
	sa.sa_flags = 0;
	CHECK(sigaction(SIGSEGV, &sa, NULL) == 0);

	return check_report("libc-signal");
}
