"""Create a DISPOSABLE test image with an explicitly tagged VexFS partition.
Never operates on devices. The real Dell partition must be prepared separately.
"""
import pathlib,struct,sys
output=pathlib.Path(sys.argv[1]).resolve()
build=pathlib.Path(__file__).resolve().parent.parent/'build'
if build.resolve() not in output.parents or output.suffix!='.raw':
    raise SystemExit('Output must be a .raw FILE under repository build/')
size=64*1024*1024;base=2048;sectors=size//512-base
with output.open('wb') as f:
    f.truncate(size);mbr=bytearray(512)
    mbr[446:462]=struct.pack('<B3sB3sII',0,b'\xfe\xff\xff',0xda,b'\xfe\xff\xff',base,sectors)
    mbr[510:]=b'\x55\xaa';f.write(mbr)
    f.seek((base+2048)*512)
    f.write(struct.pack('<QQIIQ4Q',0x554e4f5346533035,4096+256*3,0,256,4096,0,0,0,0))
print(f'{output}: disposable MBR partition 1, native volume base={base}, sectors={sectors}')
