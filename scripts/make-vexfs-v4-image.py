#!/usr/bin/env python3
"""Write a raw disk image holding a VexFS version 4 volume.

Version 4 is the layout used before the growable table (fixed 64 entries,
128-byte paths, table right after the header at LBA 2048). The kernel
migrates it to version 5 when it mounts the disk; the native tests check
that the files below survive the migration and that the /tmp entry, which
version 5 keeps in memory only, is dropped.

usage: make-vexfs-v4-image.py OUTPUT.raw [SIZE_MB]
"""
import struct
import sys

SECTOR = 512
TABLE_LBA = 2048
DATA_LBA = 4096
MAX_ENTRIES = 64
MAX_PATH = 128
ACL_ENTRIES = 4
MAGIC_V4 = 0x554e4f5346533034  # "UNOSFS04"
REGULAR, DIRECTORY = 1, 2

# path, contents, mode
FILES = [
    ('/home/root/vexfs-v4/legacy.txt', b'written by VexFS v4\n', 0o644),
    ('/home/root/vexfs-v4/.ladybird_dir', b'\x01', 0o755),
    ('/tmp/vexfs-v4-stale.txt', b'stale\n', 0o644),
]


def fnv1a64(text):
    h = 14695981039346656037
    for b in text.encode():
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def entry(path, lba, size, mode):
    # fs_v4_entry_t: hash, offset_lba, size, used, protected, type, reserved0,
    # uid, gid, mode, path[128], link_target[128], acl[4] {u32 uid, u8, u8, pad}
    record = struct.pack('<QQQBBBBIII', fnv1a64(path), lba, size, 1, 0, REGULAR, 0, 0, 0, mode)
    record += path.encode().ljust(MAX_PATH, b'\0') + b'\0' * MAX_PATH
    record += b'\0' * (8 * ACL_ENTRIES)
    return record


def main():
    out = sys.argv[1]
    size_mb = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    image = bytearray(size_mb * 1024 * 1024)
    entries = b''
    lba = DATA_LBA
    for path, data, mode in FILES:
        entries += entry(path, lba, len(data), mode)
        image[lba * SECTOR:lba * SECTOR + len(data)] = data
        lba += (len(data) + SECTOR - 1) // SECTOR
    assert len(entries) == 328 * len(FILES)
    header = struct.pack('<QQII', MAGIC_V4, lba, len(FILES), 0)
    base = TABLE_LBA * SECTOR
    image[base:base + len(header)] = header
    image[base + SECTOR:base + SECTOR + len(entries)] = entries
    with open(out, 'wb') as f:
        f.write(image)


if __name__ == '__main__':
    main()
