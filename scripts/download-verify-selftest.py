#!/usr/bin/env python3
"""Cross-check the host test server and the guest verifier on the host.

Writes files the way download-test-server.py generates them into a scratch
directory, compiles userspace/linux_tests/download_verify.c against that
directory (use WSL/Linux) and checks its verdicts, so a mismatch between the
two pattern implementations is found before a VM run.
"""

import importlib.util
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_server():
    spec = importlib.util.spec_from_file_location(
        "download_test_server", os.path.join(ROOT, "scripts", "download-test-server.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    server = load_server()
    with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as tools:
        def write(name, data):
            with open(os.path.join(directory, name), "wb") as handle:
                handle.write(data)

        write("small.txt", server.SMALL_TEXT)
        write("small (2).txt", server.SMALL_TEXT)
        write("7-2097152.bin", server.pattern_block(0, 2097152, 7))
        write("3-1048576 (1).bin", server.pattern_block(0, 1048576, 3))
        write("5-4096.txt", server.pattern_block(0, 4096, 5))
        write("255-100000.tar.gz", server.pattern_block(0, 100000, 255))
        write("9-1000.bin", server.pattern_block(0, 999, 9) + b"\x00")  # corrupt tail
        write("other.dat", b"anything")
        write("x.vexpart", b"partial")

        binary = os.path.join(tools, "verify")
        subprocess.check_call([
            "gcc", "-O2", "-Wall", "-Wextra", "-Werror",
            '-DDOWNLOAD_DIRECTORY="%s"' % directory,
            os.path.join(ROOT, "userspace", "linux_tests", "download_verify.c"),
            "-o", binary])
        result = subprocess.run([binary], capture_output=True, text=True)
        print(result.stdout, end="")

    expected = [
        "OK name=small.txt size=1024",
        "OK name=small (2).txt size=1024",
        "OK name=7-2097152.bin size=2097152",
        "OK name=3-1048576 (1).bin size=1048576",
        "OK name=5-4096.txt size=4096",
        "OK name=255-100000.tar.gz size=100000",
        "FAIL name=9-1000.bin byte 999 is 00, expected",
        "UNCHECKED name=other.dat",
        "PARTIAL name=x.vexpart",
        "summary files=9 ok=6 failed=1 partial=1 unchecked=1",
    ]
    missing = [line for line in expected if line not in result.stdout]
    if missing or result.returncode != 1:
        print("SELFTEST FAILED, missing:", missing, "exit", result.returncode)
        return 1
    print("download verify selftest: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
