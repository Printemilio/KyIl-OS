#!/usr/bin/env python3
"""Replay a host dune build for Vex.

    ocaml-cross-replay.py LOG TREE MAP...

LOG is the stderr of `dune build --display verbose --cache=disabled` run in
TREE (a dune project whose _build already holds the host build). Every
command that compiles or archives a *library* is run again in TREE, in the
original order, with the host compilers replaced by the Vex cross toolchain:

    ocamlc.opt / ocamlopt.opt / ocamlmklib  ->  $VEX_OCAML/bin/...
    gcc                                     ->  x86_64-unknown-vexos-gcc

Host-only work is not replayed: linking executables (generators, type
discovery programs), running them (their outputs are target independent for
x86-64 System V and are already in _build), ocamldep, and -shared plugins.

MAP entries are FROM=TO path substitutions applied to every command, e.g. a
host opam library directory to the replayed directory of the same library.
"""
import os
import re
import subprocess
import sys

VEX_OCAML = os.environ.get("VEX_OCAML", os.path.expanduser("~/.local/vexos/ocaml-vexos"))
CROSS = {
    "ocamlc.opt": os.path.join(VEX_OCAML, "bin", "ocamlc.opt"),
    "ocamlopt.opt": os.path.join(VEX_OCAML, "bin", "ocamlopt.opt"),
    "ocamlmklib": os.path.join(VEX_OCAML, "bin", "ocamlmklib"),
    "gcc": "x86_64-unknown-vexos-gcc",
}
LINE = re.compile(r"^Running\[\d+\]: \(cd (\S+) && (\S+)(.*)\)(.*)$")


def main():
    log, tree, maps = sys.argv[1], sys.argv[2], sys.argv[3:]
    subs = sorted((m.split("=", 1) for m in maps), key=lambda p: -len(p[0]))
    replayed = skipped = 0
    for raw in open(log, encoding="utf-8", errors="replace"):
        m = LINE.match(raw.rstrip("\n"))
        if not m:
            continue
        cwd, prog, args, redirect = m.groups()
        tool = os.path.basename(prog)
        if tool not in CROSS:
            skipped += 1  # generators, ocamldep, shell actions
            continue
        words = args.split()
        out = words[words.index("-o") + 1] if "-o" in words else ""
        linking = "-c" not in words and "-a" not in words and tool != "ocamlmklib"
        if (out.endswith((".exe", ".bc", ".cmxs")) or "-shared" in words or
                (linking and tool != "gcc") or ".eobjs/" in args):  # executables' modules
            skipped += 1
            continue
        if tool == "gcc" and "-c" not in words:
            skipped += 1
            continue
        for src, dst in subs:
            args = args.replace(src, dst)
        if tool == "ocamlmklib":
            args = " -custom" + args  # static stubs only: Vex has no dlopen
        command = "cd %s && %s%s%s" % (cwd, CROSS[tool], args, redirect)
        result = subprocess.run(["bash", "-c", command], cwd=tree)
        if result.returncode != 0:
            sys.exit("replay failed (%d): %s" % (result.returncode, command))
        replayed += 1
    print("replay: %d commands for Vex, %d host-only skipped" % (replayed, skipped))


if __name__ == "__main__":
    main()
