"""Build the native reader regression against exact current ISO payloads."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parent.parent
work = root / "build/suchu-browser"
work.mkdir(parents=True, exist_ok=True)
lines = []
for index, name in enumerate(("/usr/lib/ocaml/libasmrun.a", "/usr/lib/gio/modules/libgiognutls.so")):
    data = (root / "build/iso_root" / name.lstrip("/")).read_bytes()
    hash_value = 1469598103934665603
    for byte in data:
        hash_value = ((hash_value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    lines.extend((f'#define ISO_FILE_{index} "{name}"',
                  f'#define ISO_SIZE_{index} {len(data)}u',
                  f'#define ISO_HASH_{index} UINT64_C(0x{hash_value:016x})'))
(work / "iso_reader_expected.h").write_text("\n".join(lines) + "\n")
subprocess.run([str(root / "toolchain/bin/x86_64-unknown-vexos-gcc"), "-O2", "-pthread",
               "-I", str(work), str(root / "userspace/libc/tests/iso_smp_reader_test.c"),
               "-o", str(root / "usr/native-tests/iso_smp_reader_test")], check=True, cwd=root)
