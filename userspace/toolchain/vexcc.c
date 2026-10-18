/* vexcc: the native toolchain driver of Vex.
 *
 * Installed as /usr/bin/x86_64-unknown-vexos-gcc and /usr/bin/cc, it gives
 * native programs (ocamlopt first) the command line they expect from a C
 * compiler driver, on top of the native GNU assembler and linker:
 *
 *   cc -c [-o out.o] file.s       assemble with as(1)
 *   cc [-o out] objs... [-L..] [-l..]   link a static native executable
 *
 * The link adds what every Vex program needs, in the order GCC uses: the
 * startup objects (crt1.o carries the .note.vexos ABI note), the program,
 * libc and libgcc, then the closing objects. There is no C compiler on Vex
 * yet: a .c (or preprocessed .S) input is reported, not silently skipped.
 *
 * Environment: VEX_TOOLCHAIN_BIN (default /usr/bin) and VEX_TOOLCHAIN_LIB
 * (default /usr/lib) locate as/ld and the startup objects and libraries. */
#define _GNU_SOURCE
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

typedef struct {
	char **v;
	int n, cap;
} argv_t;

static void push(argv_t *a, const char *s)
{
	if (a->n + 2 > a->cap) {
		a->cap = a->cap ? a->cap * 2 : 32;
		a->v = realloc(a->v, sizeof *a->v * a->cap);
		if (!a->v) { perror("cc"); exit(2); }
	}
	a->v[a->n++] = strdup(s);
	a->v[a->n] = NULL;
}

static char *join(const char *dir, const char *name)
{
	char *s = malloc(strlen(dir) + strlen(name) + 2);
	if (!s) { perror("cc"); exit(2); }
	sprintf(s, "%s/%s", dir, name);
	return s;
}

static int ends_with(const char *s, const char *suffix)
{
	size_t n = strlen(s), m = strlen(suffix);
	return n >= m && !strcmp(s + n - m, suffix);
}

static int verbose;

static int run(argv_t *a)
{
	pid_t pid;
	int status;
	if (verbose) {
		for (int i = 0; i < a->n; i++) fprintf(stderr, "%s%s", i ? " " : "", a->v[i]);
		fputc('\n', stderr);
	}
	int err = posix_spawn(&pid, a->v[0], NULL, NULL, a->v, environ);
	if (err) {
		fprintf(stderr, "cc: cannot run %s: %s\n", a->v[0], strerror(err));
		return 1;
	}
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR) { perror("cc: waitpid"); return 1; }
	if (WIFEXITED(status)) return WEXITSTATUS(status);
	fprintf(stderr, "cc: %s killed by signal %d\n", a->v[0], WTERMSIG(status));
	return 1;
}

/* Split "-Wl,a,b" / "-Wa,a,b" into separate arguments. */
static void push_split(argv_t *a, const char *list)
{
	char *copy = strdup(list), *save = NULL;
	for (char *t = strtok_r(copy, ",", &save); t; t = strtok_r(NULL, ",", &save)) push(a, t);
	free(copy);
}

int main(int argc, char **argv)
{
	const char *bin = getenv("VEX_TOOLCHAIN_BIN");
	const char *lib = getenv("VEX_TOOLCHAIN_LIB");
	const char *out = NULL;
	argv_t inputs = { 0 }, ldopts = { 0 }, asopts = { 0 }, libs = { 0 };
	int compile = 0, nostdlib = 0;

	if (!bin || !*bin) bin = "/usr/bin";
	if (!lib || !*lib) lib = "/usr/lib";

	for (int i = 1; i < argc; i++) {
		const char *s = argv[i];
		if (!strcmp(s, "-c")) compile = 1;
		else if (!strcmp(s, "-o") && i + 1 < argc) out = argv[++i];
		else if (!strncmp(s, "-o", 2) && s[2]) out = s + 2;
		else if (!strcmp(s, "-v")) verbose = 1;
		else if (!strncmp(s, "-Wl,", 4)) push_split(&ldopts, s + 4);
		else if (!strncmp(s, "-Wa,", 4)) push_split(&asopts, s + 4);
		else if (!strcmp(s, "-L") && i + 1 < argc) { push(&ldopts, "-L"); push(&ldopts, argv[++i]); }
		else if (!strncmp(s, "-L", 2)) push(&ldopts, s);
		else if (!strcmp(s, "-l") && i + 1 < argc) { char b[256]; snprintf(b, sizeof b, "-l%s", argv[++i]); push(&libs, b); }
		else if (!strncmp(s, "-l", 2)) push(&libs, s);
		else if (!strcmp(s, "-nostdlib") || !strcmp(s, "-nostartfiles")) nostdlib = 1;
		else if (!strcmp(s, "-I") || !strcmp(s, "-D") || !strcmp(s, "-U") || !strcmp(s, "-include")) i++;
		/* Code generation and preprocessor options mean nothing to an
		   assembler-only toolchain; static non-PIE is the only link mode. */
		else if (s[0] == '-' && strchr("OgfDUIWmp", s[1])) ;
		else if (!strcmp(s, "-static") || !strcmp(s, "-no-pie") || !strcmp(s, "-pthread")) ;
		else if (!strcmp(s, "-shared") || !strcmp(s, "-pie") || !strcmp(s, "-rdynamic")) {
			fprintf(stderr, "cc: %s is not supported (Vex programs are static)\n", s);
			return 1;
		} else if (s[0] == '-' && s[1]) {
			fprintf(stderr, "cc: unknown option %s\n", s);
			return 1;
		} else push(&inputs, s);
	}
	if (!inputs.n) { fprintf(stderr, "cc: no input files\n"); return 1; }
	for (int i = 0; i < inputs.n; i++)
		if (ends_with(inputs.v[i], ".c") || ends_with(inputs.v[i], ".S") ||
		    ends_with(inputs.v[i], ".cpp") || ends_with(inputs.v[i], ".cc")) {
			fprintf(stderr, "cc: %s: there is no C compiler on Vex yet; "
			        "only assembly (.s) and objects can be built natively\n", inputs.v[i]);
			return 1;
		}

	if (compile) {
		/* One assembly file per invocation, as GCC's -c -o requires. */
		if (out && inputs.n != 1) { fprintf(stderr, "cc: -o with -c needs one input\n"); return 1; }
		for (int i = 0; i < inputs.n; i++) {
			argv_t a = { 0 };
			push(&a, join(bin, "as"));
			push(&a, "--64");
			for (int j = 0; j < asopts.n; j++) push(&a, asopts.v[j]);
			push(&a, "-o");
			if (out) push(&a, out);
			else {
				char *o = strdup(inputs.v[i]), *dot = strrchr(o, '.');
				if (dot) strcpy(dot, ".o");
				push(&a, o);
			}
			push(&a, inputs.v[i]);
			int r = run(&a);
			if (r) return r;
		}
		return 0;
	}

	argv_t a = { 0 };
	push(&a, join(bin, "ld"));
	push(&a, "-m"); push(&a, "elf_x86_64");
	push(&a, "-static");
	push(&a, "-z"); push(&a, "noexecstack");
	push(&a, "-z"); push(&a, "max-page-size=0x1000");
	push(&a, "--build-id=none");
	push(&a, "-o"); push(&a, out ? out : "a.out");
	if (!nostdlib) {
		push(&a, join(lib, "crt1.o"));
		push(&a, join(lib, "crti.o"));
		push(&a, join(lib, "crtbeginT.o"));
	}
	for (int i = 0; i < ldopts.n; i++) push(&a, ldopts.v[i]);
	{
		char *dir = malloc(strlen(lib) + 3);
		if (!dir) { perror("cc"); return 2; }
		sprintf(dir, "-L%s", lib);
		push(&a, dir);
	}
	for (int i = 0; i < inputs.n; i++) push(&a, inputs.v[i]);
	for (int i = 0; i < libs.n; i++) push(&a, libs.v[i]);
	if (!nostdlib) {
		push(&a, "--start-group");
		push(&a, "-lc");
		push(&a, join(lib, "libgcc.a"));
		push(&a, "--end-group");
		push(&a, join(lib, "crtend.o"));
		push(&a, join(lib, "crtn.o"));
	}
	return run(&a);
}
