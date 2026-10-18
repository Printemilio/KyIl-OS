/* Suchu on Vex: the interpreter (`suchu run`), the translation to OCaml
 * (`suchu build`) and native compilation (`suchu comp`, through ocamlopt, as
 * and ld running on Vex). A compiled program must print exactly what the
 * interpreter prints for the same source -- the upstream differential check. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "check.h"

#define SHARE "/usr/share/suchu"

static char out_a[16384], out_b[16384];

/* Run a command, capture stdout+stderr; returns the exit status (or -1). */
static int capture(const char *cmd, char *out, size_t size)
{
	char full[1024];
	snprintf(full, sizeof full, "%s 2>&1", cmd);
	FILE *p = popen(full, "r");
	size_t n = 0;
	if (!p) return -1;
	while (n + 1 < size) {
		size_t r = fread(out + n, 1, size - 1 - n, p);
		if (!r) break;
		n += r;
	}
	out[n] = 0;
	int status = pclose(p);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* run vs comp on one source: same output, same status. */
static void differential(const char *dir, const char *name)
{
	char cmd[512], binary[256];
	double start = now_ms();
	snprintf(cmd, sizeof cmd, "cd %s && suchu run %s.suchu", dir, name);
	int run_status = capture(cmd, out_a, sizeof out_a);
	snprintf(binary, sizeof binary, "/tmp/suchu-%s", name);
	snprintf(cmd, sizeof cmd, "cd %s && suchu comp %s.suchu -o %s", dir, name, binary);
	int comp_status = capture(cmd, out_b, sizeof out_b);
	CHECK_EQ_INT(comp_status, 0);
	if (comp_status != 0) {
		fprintf(stderr, "  comp %s:\n%s\n", name, out_b);
		return;
	}
	snprintf(cmd, sizeof cmd, "cd %s && %s", dir, binary);
	int bin_status = capture(cmd, out_b, sizeof out_b);
	CHECK_EQ_INT(bin_status, run_status);
	CHECK(strcmp(out_a, out_b) == 0);
	if (strcmp(out_a, out_b) != 0)
		fprintf(stderr, "  %s differs:\n--- run\n%s--- comp\n%s---\n", name, out_a, out_b);
	fprintf(stderr, "  differential %-14s ok=%d status=%d (%.0f ms)\n", name,
		!strcmp(out_a, out_b) && bin_status == run_status, run_status, now_ms() - start);
}

int main(void)
{
	double start;
	setenv("PATH", "/usr/bin:/bin", 1);
	setenv("TMPDIR", "/tmp", 1);

	/* The interpreter. */
	start = now_ms();
	CHECK_EQ_INT(capture("suchu run " SHARE "/examples/basic.suchu", out_a, sizeof out_a), 0);
	fprintf(stderr, "  suchu run basic: %.0f ms\n", now_ms() - start);
	CHECK(strstr(out_a, "5! = 120") != NULL);
	CHECK(strstr(out_a, "Title: hello world from Suchu") != NULL);
	fputs(out_a, stderr);

	CHECK_EQ_INT(capture("cd " SHARE "/examples && suchu run modular_app.suchu", out_a, sizeof out_a), 0);
	CHECK(strstr(out_a, "Area:") != NULL);
	fputs(out_a, stderr);

	/* Errors keep their statuses: 2 for a parse error, 3 at run time. */
	FILE *f = fopen("/tmp/suchu-bad.suchu", "w");
	CHECK(f != NULL);
	if (f) { fputs("x = (1 + ;\n", f); fclose(f); }
	CHECK_EQ_INT(capture("suchu run /tmp/suchu-bad.suchu", out_a, sizeof out_a), 2);
	f = fopen("/tmp/suchu-boom.suchu", "w");
	if (f) { fputs("print(5 % 0);\n", f); fclose(f); }
	CHECK_EQ_INT(capture("suchu run /tmp/suchu-boom.suchu", out_a, sizeof out_a), 3);

	/* Translation to OCaml. */
	CHECK_EQ_INT(capture("suchu build " SHARE "/examples/loops.suchu -o /tmp/suchu-loops.ml", out_a, sizeof out_a), 0);
	struct stat st;
	CHECK(stat("/tmp/suchu-loops.ml", &st) == 0 && st.st_size > 0);

	/* Native compilation, checked against the interpreter. */
	differential(SHARE "/examples", "basic");
	differential(SHARE "/examples", "modular_app");
	differential(SHARE "/differential", "closures");
	differential(SHARE "/differential", "strings");
	differential(SHARE "/differential", "control");
	differential(SHARE "/differential", "function_exceptions");

	/* A window program links raylib, its bindings and the GUI backend on
	 * Vex. It is not run: the test boot has no desktop to open it on. */
	start = now_ms();
	int gui_status = capture("suchu comp " SHARE "/examples/gui/counter.suchu -o /tmp/suchu-counter",
		out_a, sizeof out_a);
	CHECK_EQ_INT(gui_status, 0);
	if (gui_status != 0) fprintf(stderr, "  comp counter:\n%s\n", out_a);
	CHECK(stat("/tmp/suchu-counter", &st) == 0 && st.st_size > 1000000);
	fprintf(stderr, "  suchu comp gui/counter: %.0f ms, %lld bytes\n", now_ms() - start,
		(long long)st.st_size);
	/* http is refused at compile time, with a message rather than a link error. */
	f = fopen("/tmp/suchu-http.suchu", "w");
	if (f) { fputs("import http;\nprint(1);\n", f); fclose(f); }
	CHECK(capture("suchu comp /tmp/suchu-http.suchu -o /tmp/suchu-http", out_a, sizeof out_a) != 0);
	CHECK(strstr(out_a, "http") != NULL);

	return check_report("suchu");
}
