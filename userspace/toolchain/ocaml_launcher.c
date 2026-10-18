/* /usr/bin/ocamlopt (and friends): start the native OCaml tool installed in
 * /usr/libexec/ocaml with the Vex standard library location.
 *
 * The compiler is linked with the compiler-libs of the cross toolchain, whose
 * configured standard library path is the build machine's. OCaml lets
 * OCAMLLIB override it at run time; this launcher sets it (unless the caller
 * already chose one) so that `ocamlopt` works from any program or shell. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OCAML_LIBDIR "/usr/lib/ocaml"
#define OCAML_LIBEXEC "/usr/libexec/ocaml"

int main(int argc, char **argv)
{
	const char *name = strrchr(argv[0], '/');
	char path[256];

	name = name ? name + 1 : argv[0];
	if (setenv("OCAMLLIB", OCAML_LIBDIR, 0) != 0) {
		perror("ocaml launcher: setenv");
		return 2;
	}
	/* ocamlopt -> ocamlopt.opt; an explicit .opt name maps to itself. */
	if (snprintf(path, sizeof path, "%s/%s%s", OCAML_LIBEXEC, name,
	             strstr(name, ".opt") ? "" : ".opt") >= (int)sizeof path) {
		fprintf(stderr, "ocaml launcher: name too long\n");
		return 2;
	}
	(void)argc;
	execv(path, argv);
	fprintf(stderr, "ocaml launcher: cannot run %s: %s\n", path, strerror(errno));
	return 127;
}
