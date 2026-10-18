#!/usr/bin/env python3
"""Source selection for the Vex C library.

The library is musl's portable code plus the native Vex OS layer in
userspace/libc/src. A musl source is used unless
  * the Vex layer provides a file with the same path (it replaces it), or
  * it still talks to the Linux kernel (raw system calls, SYS_* numbers,
    vDSO). Such a file is left out: the Vex layer implements its functions
    on native calls, or the function is absent and a program using it fails
    to link -- never a silent Linux fallback.

usage: sources.py MUSL_DIR VEX_DIR [--report]
prints one absolute source path per line (or a report of omitted files).
"""
import os
import re
import sys

ARCH = 'x86_64'
OTHER_ARCHES = {
    'aarch64', 'arm', 'i386', 'loongarch64', 'm68k', 'microblaze', 'mips',
    'mips64', 'mipsn32', 'or1k', 'powerpc', 'powerpc64', 'riscv32', 'riscv64',
    's390x', 'sh', 'x32',
}
LINUX_ONLY = re.compile(
    r'\b__syscall\d?\s*\(|\b__syscall_cp\d?\s*\(|\bsyscall_cp\s*\(|'
    r'(?<![\w.])syscall\s*\(|\bSYS_[a-z_0-9]+\b|__vdsosym|\bsocketcall(_cp)?\s*\(|'
    r'\b__alt_socketcall\b|\b__socketcall(_cp)?\b|\bsys_wait4(_cp)?\b|'
    r'\b__sys_wait4\b|\b__procfdname\b')
ASM_SYSCALL = re.compile(r'^\s*syscall\b', re.M)

# Portable-looking musl sources whose functions the Vex layer implements
# natively (they go through Linux-only mechanisms such as prctl or /proc).
REPLACED = {
    'thread/pthread_setname_np.c',  # prctl / /proc/self/task/*/comm
    'thread/pthread_getname_np.c',
    'network/if_nameindex.c',       # netlink
}


def musl_sources(musl):
    src = os.path.join(musl, 'src')
    for top in sorted(os.listdir(src)):
        base = os.path.join(src, top)
        if not os.path.isdir(base):
            continue
        for dirpath, dirs, files in os.walk(base):
            rel = os.path.relpath(dirpath, src).replace(os.sep, '/')
            parts = rel.split('/')
            dirs[:] = sorted(d for d in dirs if d not in OTHER_ARCHES)
            if any(p in OTHER_ARCHES for p in parts):
                continue
            # musl builds one allocator: mallocng (MALLOC_DIR in its Makefile).
            if rel.startswith('malloc/oldmalloc'):
                continue
            for name in sorted(files):
                if name.endswith(('.c', '.s', '.S')):
                    yield rel + '/' + name


def arch_overrides(rel):
    """musl replaces src/dir/name.c by src/dir/ARCH/name.[csS]."""
    d, name = rel.rsplit('/', 1)
    stem = os.path.splitext(name)[0]
    return [d + '/' + ARCH + '/' + stem + ext for ext in ('.c', '.s', '.S')]


def linux_only(path):
    text = open(path, encoding='utf-8', errors='replace').read()
    if path.endswith(('.s', '.S')):
        return bool(ASM_SYSCALL.search(text))
    # Comments may mention system calls; only code counts.
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return bool(LINUX_ONLY.search(text))


def select(musl, vex):
    musl_set = set(musl_sources(musl))
    vex_src = os.path.join(vex, 'src')
    vex_set = set()
    for dirpath, dirs, files in os.walk(vex_src):
        for name in files:
            if name.endswith(('.c', '.s', '.S')):
                rel = os.path.relpath(os.path.join(dirpath, name), vex_src)
                vex_set.add(rel.replace(os.sep, '/'))

    chosen, omitted = [], []
    for rel in sorted(musl_set):
        parts = rel.split('/')
        if len(parts) == 3 and parts[1] == ARCH:
            continue  # handled through its generic counterpart below
        if rel in vex_set or rel in REPLACED:
            continue
        candidates = [r for r in arch_overrides(rel) if r in musl_set]
        actual = candidates[0] if candidates else rel
        if any(r in vex_set for r in arch_overrides(rel)):
            continue
        path = os.path.join(musl, 'src', actual)
        if linux_only(path):
            omitted.append(actual)
        else:
            chosen.append(path)
    # Architecture-only files without a generic counterpart.
    for rel in sorted(musl_set):
        parts = rel.split('/')
        if len(parts) == 3 and parts[1] == ARCH:
            stem = os.path.splitext(parts[2])[0]
            generic = [parts[0] + '/' + stem + ext for ext in ('.c', '.s', '.S')]
            if any(g in musl_set for g in generic) or rel in vex_set:
                continue
            path = os.path.join(musl, 'src', rel)
            if linux_only(path):
                omitted.append(rel)
            else:
                chosen.append(path)
    chosen += [os.path.join(vex_src, r) for r in sorted(vex_set)]
    return chosen, omitted


def main():
    musl, vex = sys.argv[1], sys.argv[2]
    chosen, omitted = select(musl, vex)
    if '--report' in sys.argv:
        for rel in omitted:
            print(rel)
    else:
        for path in chosen:
            print(path)


if __name__ == '__main__':
    main()
