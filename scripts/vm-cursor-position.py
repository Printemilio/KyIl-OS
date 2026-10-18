"""Read real cursor / checked Web Worker pixels from VM screenshots.
Host test helper only. Does not modify or fabricate guest state.
"""
import json, sys
import numpy as np
from PIL import Image

def inspect(path, worker=False):
    pixels=np.asarray(Image.open(path).convert("RGB"))
    if worker:
        green=int(np.count_nonzero(np.all(pixels==(32,208,128),axis=2)))
        return dict(passed=green>50,pixels=green)
    white=(230,236,248);shadow=(4,4,8)
    shape=[0x8000,0xc000,0xe000,0xf000,0xf800,0xfc00,0xfe00,0xff00,
           0xff80,0xf800,0xd800,0x8c00,0x0c00,0x0600,0x0600,0]
    ys,xs=np.where(np.all(pixels==white,axis=2))
    h,w=pixels.shape[:2]
    valid=(ys<=h-17)&(xs<=w-17);ys,xs=ys[valid],xs[valid]
    for y in range(17):
        for x in range(17):
            color=None
            if x<16 and y<16 and shape[y]&(0x8000>>x):color=white
            elif x>0 and y>0 and shape[y-1]&(0x8000>>(x-1)):color=shadow
            if color is not None:
                valid=np.all(pixels[ys+y,xs+x]==color,axis=1)
                ys,xs=ys[valid],xs[valid]
    if len(xs)!=1:raise ValueError(f"Expected one real Vex cursor, found {len(xs)}")
    return dict(x=int(xs[0]),y=int(ys[0]),width=w,height=h)

if "--stream" in sys.argv:
    for line in sys.stdin:
        try:print(json.dumps(inspect(line.strip())),flush=True)
        except Exception as exc:print(json.dumps(dict(error=str(exc))),flush=True)
else:
    print(json.dumps(inspect(sys.argv[1],"--worker-result" in sys.argv)))
