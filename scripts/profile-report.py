#!/usr/bin/env python3
"""Summarize the PROF-* lines the kernel `prof <seconds>` command writes to the serial log.

    python scripts/profile-report.py build/prof-baseline2.log [--kernel build/vex-os]

Kernel addresses are symbolized with llvm-symbolizer/llvm-nm; gate addresses are
resolved to the static variable they live at (fs_gate, memory_gate, ...).
"""
import argparse
import bisect
import collections
import re
import subprocess
import sys

LLVM = "C:/Program Files/LLVM/bin/"


def symbolize(binary, addresses):
    """Map kernel code addresses to 'function file:line'."""
    unique = sorted(set(addresses))
    if not unique:
        return {}
    out = subprocess.run([LLVM + "llvm-symbolizer.exe", "-f", "-e", binary] + [hex(a) for a in unique],
                         capture_output=True, text=True, check=True).stdout.split("\n\n")
    result = {}
    for address, block in zip(unique, out):
        lines = [l for l in block.strip().split("\n") if l]
        name = lines[0] if lines else "?"
        where = lines[1].rsplit("\\", 1)[-1].rsplit("/", 1)[-1] if len(lines) > 1 else ""
        result[address] = f"{name} {where}".strip()
    return result


def data_symbols(binary):
    out = subprocess.run([LLVM + "llvm-nm.exe", "-n", binary], capture_output=True, text=True, check=True).stdout
    table = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "bBdDrR":
            table.append((int(parts[0], 16), parts[2]))
    return table


def gate_name(table, keys, address):
    if address >= 0xFFFFFFFF80000000:
        index = bisect.bisect_right(keys, address) - 1
        if index >= 0:
            return table[index][1] if address == table[index][0] else f"{table[index][1]}+{address - table[index][0]:#x}"
    return f"heap:{address:#x}"


def load_segments(binary):
    """PT_LOAD (file offset, vaddr, filesz) of an ELF file."""
    out = subprocess.run([LLVM + "llvm-objdump.exe", "-p", binary], capture_output=True, text=True).stdout
    return [(int(m[1], 16), int(m[2], 16), int(m[3], 16)) for m in re.finditer(
        r"LOAD off\s+(0x[0-9a-f]+) vaddr (0x[0-9a-f]+) paddr \S+ align \S+\s+filesz (0x[0-9a-f]+)", out)]


def user_functions(samples, rootfs, lines=False):
    """Symbolize user samples of file-backed modules found under `rootfs`."""
    import os
    result = collections.Counter()
    wanted = collections.defaultdict(list)          # module path -> [(rip, base, file offset, n)]
    for s in samples:
        if s["mode"] != "user":
            continue
        path = s.get("path", "")
        if not path:
            result[("(anonymous/JIT)", f"region {s.get('base', '?')}")] += int(s["n"])
            continue
        wanted[path].append((int(s["rip"], 16), int(s["base"], 16), int(s["off"], 16), int(s["n"])))
    for path, items in wanted.items():
        binary = os.path.join(rootfs, path.lstrip("/"))
        if not os.path.exists(binary):
            for _, _, _, n in items:
                result[(path, "(file not found)")] += n
            continue
        segments = load_segments(binary)
        addresses = []
        for rip, base, off, n in items:
            file_offset = off + (rip - base)
            vaddr = file_offset
            for seg_off, seg_vaddr, filesz in segments:
                if seg_off <= file_offset < seg_off + filesz:
                    vaddr = seg_vaddr + (file_offset - seg_off)
                    break
            addresses.append((vaddr, n))
        unique = sorted({a for a, _ in addresses})
        out = subprocess.run([LLVM + "llvm-symbolizer.exe", "-f", "-C", "-e", binary] + [hex(a) for a in unique],
                             capture_output=True, text=True).stdout.split("\n\n")
        def label(block):
            parts = block.strip().split(chr(10))
            if not parts or not parts[0]:
                return "?"
            if lines and len(parts) > 1:
                return parts[0] + " @" + parts[1].replace(chr(92), "/").rsplit("/", 1)[-1]
            return parts[0]
        names = {a: label(blk) for a, blk in zip(unique, out)}
        for vaddr, n in addresses:
            result[(os.path.basename(path), names.get(vaddr, "?"))] += n
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--kernel", default="build/vex-os")
    parser.add_argument("--top", type=int, default=12)
    parser.add_argument("--rootfs", default="build/iso_root")
    parser.add_argument("--lines", action="store_true", help="split user functions by source line")
    args = parser.parse_args()

    tasks, series, samples, latency, gates = {}, collections.defaultdict(lambda: [0] * 5), [], [], []
    for line in open(args.log, errors="replace"):
        fields = dict(re.findall(r"(\w+)=(\S*)", line))
        if line.startswith("PROF-") and not line.startswith(("PROF-TASK", "PROF-LAT", "PROF-BEGIN", "PROF-END")):
            if any(not re.fullmatch(r"[0-9a-f]*", v) for k, v in fields.items() if k not in ("mode", "name", "path")):
                continue   # line garbled by other serial output
        try:
            if line.startswith("PROF-TASK"):
                tasks[int(fields["tid"])] = (int(fields["pid"]), fields.get("name", ""))
            elif line.startswith("PROF-SERIES"):
                row = series[int(fields["sec"])]
                for i, key in enumerate(("idle", "user", "kernel", "spin")):
                    row[i] += int(fields[key])
        except (KeyError, ValueError):
            continue   # line garbled by other serial output
        if line.startswith(("PROF-TASK", "PROF-SERIES")):
            continue
        if False:
            pass
        elif line.startswith("PROF-SAMPLE"):
            if all(k in fields and fields[k] for k in ("n", "rip", "mode", "tid", "pid")) and fields["n"].isdigit() and fields["tid"].isdigit():   # skip lines garbled by other serial output
                samples.append(fields)
        elif line.startswith("PROF-LAT"):
            latency.append(fields)
        elif line.startswith("PROF-GATE"):
            gates.append(fields)

    total = sum(int(s["n"]) for s in samples)
    by_mode = collections.Counter()
    for s in samples:
        by_mode[s["mode"]] += int(s["n"])
    print("== CPU time by mode (ms, all CPUs)")
    for mode, n in by_mode.most_common():
        print(f"  {mode:7} {n:8} {100 * n / max(total, 1):5.1f}%")

    table = data_symbols(args.kernel)
    keys = [a for a, _ in table]

    print("\n== Time spinning on kernel gates (ms of CPU time, by gate)")
    spin = collections.Counter()
    for s in samples:
        if s["mode"] == "spin" and s.get("gate"):
            spin[int(s["gate"], 16)] += int(s["n"])
    for gate, n in spin.most_common(args.top):
        print(f"  {n:8}  {gate_name(table, keys, gate)}")

    print("\n== Gate hold times (total held ms, acquisitions, longest hold ms)")
    for g in sorted(gates, key=lambda g: -int(g["total_ns"]))[:args.top]:
        address = int(g["gate"], 16)
        print(f"  {int(g['total_ns']) / 1e6:9.1f} ms {int(g['count']):8} acq  max {int(g['max_ns']) / 1e6:7.2f} ms  "
              f"{gate_name(table, keys, address)}")

    kernel = [s for s in samples if s["mode"] == "kernel"]
    names = symbolize(args.kernel, [int(s["rip"], 16) for s in kernel])
    print("\n== Where kernel time goes (ms, by function; includes spinning)")
    fn = collections.Counter()
    for s in kernel:
        fn[names[int(s["rip"], 16)].split(" ")[0]] += int(s["n"])
    for name, n in fn.most_common(args.top * 2):
        print(f"  {n:8}  {name}")

    print("\n== What the CPU holding a contended gate is doing (ms, by function)")
    held = collections.Counter()
    for s in kernel:
        if int(s.get("gate", "0") or "0", 16):
            held[(gate_name(table, keys, int(s["gate"], 16)), names[int(s["rip"], 16)])] += int(s["n"])
    for (gate, where), n in held.most_common(args.top * 2):
        print(f"  {n:8}  {gate:14} {where}")

    print("\n== User time by thread (ms)")
    users = collections.Counter()
    for s in samples:
        if s["mode"] == "user":
            tid = int(s["tid"])
            users[(s["pid"], tid, tasks.get(tid, (0, "?"))[1])] += int(s["n"])
    for (pid, tid, name), n in users.most_common(args.top):
        print(f"  {n:8}  pid {pid} tid {tid} {name}")

    print("\n== User time by module and function (ms)")
    functions = user_functions(samples, args.rootfs, args.lines)
    for (module, function), n in functions.most_common(args.top * 3):
        print(f"  {n:8}  {module:30} {function[:110]}")
    by_module = collections.Counter()
    for (module, _), n in functions.items():
        by_module[module] += n
    print("\n== User time by module (ms)")
    for module, n in by_module.most_common(args.top):
        print(f"  {n:8}  {module}")

    syscalls = []
    for line in open(args.log, errors="replace"):
        m = re.fullmatch(r"PROF-SYSCALL key=([0-9a-f]+) count=(\d+) total_ns=(\d+) max_ns=(\d+)\s*", line)
        if m:
            syscalls.append((int(m[1], 16), int(m[2]), int(m[3]), int(m[4])))
    names = {1: "linux", 2: "native", 3: "other", 4: "span"}
    if syscalls:
        print("\n== System calls by total wall time (blocking included): count, total ms, mean us, max ms")
        for key, count, total, longest in sorted(syscalls, key=lambda r: -r[2])[:args.top * 2]:
            print(f"  {names.get(key >> 16, '?'):6} #{key & 0xffff:<4} {count:9} {total / 1e6:10.1f} ms  "
                  f"mean {total / count / 1e3:9.1f} us  max {longest / 1e6:8.2f} ms")
        print("\n== System calls by count")
        for key, count, total, longest in sorted(syscalls, key=lambda r: -r[1])[:args.top]:
            print(f"  {names.get(key >> 16, '?'):6} #{key & 0xffff:<4} {count:9} calls  mean {total / count / 1e3:9.1f} us")

    print("\n== Latencies (ms)")
    for l in latency:
        count = int(l["count"])
        if count:
            print(f"  {l['kind']:16} n={count:6} mean={int(l['sum_ns']) / count / 1e6:8.3f} max={int(l['max_ns']) / 1e6:9.3f}")

    print("\n== Per second  idle%/user/kernel/spin (ms)")
    row = []
    for sec in sorted(series):
        i, u, k, sp, _ = series[sec]
        row.append(f"{sec}:{100 * i // max(i + u + k + sp, 1)}/{u}/{k}/{sp}")
    for start in range(0, len(row), 8):
        print("  " + "  ".join(row[start:start + 8]))


if __name__ == "__main__":
    sys.exit(main())
