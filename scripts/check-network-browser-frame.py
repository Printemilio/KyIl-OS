"""Check real captured viewport pixels; never generate or alter a frame."""
import json,sys
from PIL import Image
import numpy as np
image=np.asarray(Image.open(sys.argv[1]).convert('RGB'))
viewport=image[225:905,54:1160]
light=(viewport.min(axis=2)>205).mean()
dark=int((viewport.max(axis=2)<160).sum())
colors=len(np.unique(viewport.reshape(-1,3),axis=0))
ok=bool(light>0.6 and dark>1500 and colors>30)
print(json.dumps(dict(content_visible=ok,light_fraction=float(light),dark_pixels=dark,colors=colors)))
sys.exit(0 if ok else 1)
