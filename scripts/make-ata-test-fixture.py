"""Create a NEW disposable VM disk. Never accepts a physical device."""
import argparse, hashlib, pathlib
args=argparse.ArgumentParser()
args.add_argument('path',type=pathlib.Path)
opt=args.parse_args()
path=opt.path.resolve()
root=pathlib.Path(__file__).resolve().parent.parent/'build'/'d630-tests'
if not path.is_relative_to(root.resolve()):
    raise SystemExit('Fixture must stay inside build/d630-tests')
path.parent.mkdir(parents=True,exist_ok=True)
with path.open('xb') as out:
    block=bytes((i*51+7)&255 for i in range(65536))
    for _ in range(256):out.write(block)
with path.open('r+b') as out:
    out.write(b'KYIL-ATA-TEST-1\0')
print(f'{path}: SHA256={hashlib.sha256(path.read_bytes()).hexdigest()}')
