import argparse,json,sys
from PIL import Image
import numpy as np
p=argparse.ArgumentParser();p.add_argument('image',nargs='?');p.add_argument('--probe',nargs=2,type=int)
p.add_argument('--stream',action='store_true')
p.add_argument('--crop',nargs=4,type=int);p.add_argument('--compare');a=p.parse_args()
if a.stream:
    print('READY',flush=True)
    for line in sys.stdin:
        request=json.loads(line);r,g,b=Image.open(request['path']).convert('RGB').getpixel((request['x'],request['y']))
        print(json.dumps(dict(red=r>240 and g<20 and b<20,rgb=[r,g,b])),flush=True)
    raise SystemExit(0)
im=np.array(Image.open(a.image).convert('RGB'))
if a.probe:
    x,y=a.probe;c=im[y,x];print(json.dumps(dict(red=bool(c[0]>240 and c[1]<20 and c[2]<20),rgb=c.tolist())))
elif a.crop:
    x,y,w,h=a.crop;crop=im[y:y+h,x:x+w]
    colors,counts=np.unique(crop.reshape(-1,3),axis=0,return_counts=True)
    result=dict(non_gray=int(np.any(crop!=217,axis=2).sum()),colors=len(colors))
    if a.compare:
        other=np.array(Image.open(a.compare).convert('RGB'))
        result['different']=int(np.any(crop!=other[y:y+h,x:x+w],axis=2).sum())
    print(json.dumps(result))
