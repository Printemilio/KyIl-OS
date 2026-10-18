/* The native development toolchain inside Vex: dash as /bin/sh (system,
 * popen), GNU as/ld behind the cc driver, and ocamlopt compiling and linking
 * an OCaml program that then runs. Everything here executes on Vex. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "check.h"

static int write_text(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	int ok = fputs(text, f) >= 0;
	return fclose(f) == 0 && ok ? 0 : -1;
}

/* Run a shell command, capture its standard output. */
static int capture(const char *cmd, char *out, size_t size)
{
	FILE *p = popen(cmd, "r");
	size_t n = 0;
	if (!p) return -1;
	while (n + 1 < size) {
		size_t r = fread(out + n, 1, size - 1 - n, p);
		if (!r) break;
		n += r;
	}
	out[n] = 0;
	return pclose(p);
}

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void)
{
	char out[512];
	int status;
	double start;

	setenv("PATH", "/usr/bin:/bin", 1);
	setenv("TMPDIR", "/tmp", 1);

	/* dash: exit status, expansions, pipes, redirections. */
	status = system("exit 3");
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 3);
	CHECK(capture("echo $((6 * 7)); x=vex; echo \"$x-${x}\"", out, sizeof out) == 0);
	CHECK_EQ_STR(out, "42\nvex-vex\n");
	CHECK(system("echo redirected > /tmp/tc-sh.txt && test -s /tmp/tc-sh.txt") == 0);
	CHECK(capture("cat < /tmp/tc-sh.txt 2>/dev/null || read l < /tmp/tc-sh.txt; echo \"$l\"", out, sizeof out) == 0);
	CHECK(strstr(out, "redirected") != NULL);
	CHECK(capture("for i in 1 2 3; do printf '%s,' $i; done; echo", out, sizeof out) == 0);
	CHECK_EQ_STR(out, "1,2,3,\n");

	/* as + ld through the cc driver: a program written in assembly. */
	CHECK(write_text("/tmp/tc-asm.s",
		"\t.text\n\t.globl main\nmain:\n\tmov $7, %eax\n\tret\n"
		"\t.section .note.GNU-stack,\"\",@progbits\n") == 0);
	start = now_ms();
	CHECK(system("cc -c -o /tmp/tc-asm.o /tmp/tc-asm.s") == 0);
	CHECK(system("cc -o /tmp/tc-asm /tmp/tc-asm.o") == 0);
	fprintf(stderr, "  as + ld: %.0f ms\n", now_ms() - start);
	status = system("/tmp/tc-asm");
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 7);
	CHECK(system("cc /tmp/missing.c 2>/dev/null") != 0); /* no C compiler: reported */

	/* Where the time of a native build goes (informational). */
	{
		static const char *steps[][2] = {
			{ "sh -c true", "true" },
			{ "/bin/sh -c true (spawn)", "/bin/sh -c true" },
			{ "as (small file)", "as --64 -o /tmp/tc-asm2.o /tmp/tc-asm.s" },
			{ "ld (static link)", "cc -o /tmp/tc-asm2 /tmp/tc-asm2.o" },
			{ "ocamlopt -version", "ocamlopt -version >/dev/null" },
			{ "ocamlopt -version (again)", "ocamlopt -version >/dev/null" },
			{ "ocamlopt.opt -version", "OCAMLLIB=/usr/lib/ocaml /usr/libexec/ocaml/ocamlopt.opt -version >/dev/null" },
			{ "ld (again)", "cc -o /tmp/tc-asm2 /tmp/tc-asm2.o" },
			{ "ld --version", "ld --version >/dev/null" },
		};
		for (unsigned i = 0; i < sizeof steps / sizeof steps[0]; i++) {
			start = now_ms();
			status = system(steps[i][1]);
			fprintf(stderr, "  time %-26s %6.0f ms (status %d)\n", steps[i][0], now_ms() - start, status);
		}
	}

	/* ocamlopt: compile, assemble, link, run -- all native. */
	CHECK(write_text("/tmp/tc_hello.ml",
		"let () =\n"
		"  let l = List.init 10 (fun i -> i * i) in\n"
		"  Printf.printf \"hello from ocamlopt on %s: %d\\n\" Sys.os_type (List.fold_left (+) 0 l);\n"
		"  let h = Hashtbl.create 16 in\n"
		"  Hashtbl.replace h \"vex\" 1;\n"
		"  print_endline (if Hashtbl.mem h \"vex\" then \"hashtbl ok\" else \"hashtbl broken\")\n") == 0);
	start = now_ms();
	status = system("cd /tmp && ocamlopt -c tc_hello.ml");
	fprintf(stderr, "  time ocamlopt -c hello          %6.0f ms\n", now_ms() - start);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	start = now_ms();
	status = system("cd /tmp && ocamlopt tc_hello.cmx -o tc_hello");
	fprintf(stderr, "  time ocamlopt link hello        %6.0f ms\n", now_ms() - start);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	start = now_ms();
	CHECK(capture("/tmp/tc_hello", out, sizeof out) == 0);
	fprintf(stderr, "  time run tc_hello (OCaml)        %6.0f ms\n", now_ms() - start);
	start = now_ms();
	CHECK(system("/tmp/tc-asm2; true") == 0);
	fprintf(stderr, "  time run tc-asm2 (asm)          %6.0f ms\n", now_ms() - start);
	CHECK_EQ_STR(out, "hello from ocamlopt on Unix: 285\nhashtbl ok\n");

	/* Separate compilation, an interface, and the unix library. */
	CHECK(write_text("/tmp/tc_lib.mli", "val greet : string -> string\n") == 0);
	CHECK(write_text("/tmp/tc_lib.ml", "let greet s = \"hi \" ^ s\n") == 0);
	CHECK(write_text("/tmp/tc_main.ml",
		"let () = print_endline (Tc_lib.greet (string_of_int (Unix.getpid () * 0 + 5)))\n") == 0);
	start = now_ms();
	status = system("cd /tmp && ocamlopt -c tc_lib.mli tc_lib.ml && "
	                "ocamlopt -I +unix unix.cmxa tc_lib.cmx tc_main.ml -o tc_main");
	fprintf(stderr, "  ocamlopt 2 modules + unix: %.0f ms\n", now_ms() - start);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	CHECK(capture("/tmp/tc_main", out, sizeof out) == 0);
	CHECK_EQ_STR(out, "hi 5\n");

	/* A type error is reported with a failing status, not a crash. */
	CHECK(write_text("/tmp/tc_bad.ml", "let x : int = \"no\"\n") == 0);
	status = system("cd /tmp && ocamlopt -c tc_bad.ml 2>/tmp/tc_bad.err");
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 2);
	CHECK(capture("cat /tmp/tc_bad.err 2>/dev/null || echo nocat", out, sizeof out) >= 0);

	return check_report("toolchain");
}
