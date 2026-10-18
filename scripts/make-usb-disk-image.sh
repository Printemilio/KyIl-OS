#!/usr/bin/env bash
# A real USB-HDD image, not an ISO renamed to evade an image detector.
set -euo pipefail
cd "$(dirname "$0")/.."
name=${1:-kyilos-legacy-usb.img}
[[ "$name" =~ ^[a-zA-Z0-9._-]+\.img$ ]] || { echo 'Invalid image filename' >&2; exit 1; }
for tool in python3 mformat mmd mcopy; do command -v "$tool" >/dev/null; done
stage=build/portable_iso_root
image="build/$name"
# Files are already inside system.boot; the boot volume only needs the loader,
# kernel and archive. No second copy of all application files on the USB volume.
python3 - "$stage" "$image" <<'PY'
import pathlib,struct,sys
root,image=map(pathlib.Path,sys.argv[1:])
payloads=[root/'system.boot',root/'boot/vex-os',root/'boot/limine/limine-bios.sys',*list((root/'EFI/BOOT').glob('*.EFI'))]
size=sum(p.stat().st_size for p in payloads)
align=64*1024*1024
length=max(256*1024*1024,((size+64*1024*1024+1048576+align-1)//align)*align)
start=2048;sectors=length//512-start
if sectors>0xffffffff:raise SystemExit('Image exceeds MBR capacity')
mbr=bytearray(512)
# An active FAT32 ESP is discoverable by removable-media UEFI firmware and
# Limine BIOS alike. CHS saturated; the stage uses BIOS extended LBA reads.
mbr[446:462]=struct.pack('<B3sB3sII',0x80,b'\xfe\xff\xff',0xef,b'\xfe\xff\xff',start,sectors)
mbr[510:]=b'\x55\xaa'
with image.open('wb') as out:out.truncate(length);out.write(mbr)
print(f'USB-HDD: MBR, active FAT32 ESP, start={start}, sectors={sectors}, bytes={length}')
PY
sectors=$(python3 - "$image" <<'PY'
import pathlib,sys
print(pathlib.Path(sys.argv[1]).stat().st_size//512-2048)
PY
)
volume="$image@@1048576"
mformat -i "$volume" -F -H 2048 -T "$sectors" -v KYILOS ::
mmd -i "$volume" ::/boot ::/boot/limine ::/EFI ::/EFI/BOOT
mcopy -i "$volume" "$stage/system.boot" ::/system.boot
mcopy -i "$volume" "$stage/boot/vex-os" ::/boot/vex-os
mcopy -i "$volume" "$stage/boot/limine/limine-bios.sys" ::/boot/limine/limine-bios.sys
mcopy -i "$volume" "$stage/limine.conf" ::/limine.conf
mcopy -i "$volume" "$stage/limine.conf" ::/boot/limine/limine.conf
mcopy -i "$volume" "$stage/limine.conf" ::/EFI/BOOT/limine.conf
for file in "$stage"/EFI/BOOT/*.EFI; do mcopy -i "$volume" "$file" ::/EFI/BOOT/; done
# Native host tool installs the actual MBR code + embedded stage 2 in the gap.
# This is the same pinned Limine version as the ISO and kernel protocol.
./vendor/limine/limine.exe bios-install "$(wslpath -w "$(realpath "$image")")"
sha256sum "$image"
