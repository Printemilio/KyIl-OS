import pathlib,json,struct,hashlib,re,argparse
import numpy as np
from PIL import Image
root=pathlib.Path(__file__).resolve().parents[1];out=root/'build/paint-comparison'
def code_lines(path):
    s=(root/path).read_text();s=re.sub(r'/\*.*?\*/|\(\*.*?\*\)|//[^\n]*','',s,flags=re.S)
    return sum(bool(line.strip())for line in s.splitlines())
def elf(path):
    b=(root/path).read_bytes();off=struct.unpack_from('<Q',b,32)[0]
    size,n=struct.unpack_from('<HH',b,54);loads=[];dynamic=False
    for i in range(n):
        typ,flags,po,va,pa,fs,ms,align=struct.unpack_from('<IIQQQQQQ',b,off+i*size)
        if typ==1:loads.append((fs,ms))
        if typ==2:dynamic=True
    return dict(bytes=len(b),load_file_bytes=sum(x[0]for x in loads),load_virtual_bytes=sum(x[1]for x in loads),dynamic=dynamic,sha256=hashlib.sha256(b).hexdigest())
metadata=dict(executables={name:elf(path)for name,path in [('lvgl','vex_paint'),('lvgl_profile','usr/native-tests/paint_lvgl_bench'),('suchu','usr/bin/kyil-paint')]},
    code={path:code_lines(path)for path in [
        'userspace/apps/vex_paint.c','userspace/suchu/examples/paint.suchu','userspace/suchu/examples/paint_model.suchu',
        'userspace/libvex/src/vex_bench.c','userspace/apps/paint_bench_runner.c','userspace/suchu/examples/paint_test.suchu',
        'scripts/run-paint-comparison.ps1','scripts/test-paint-functions.ps1']})
parser=argparse.ArgumentParser()
parser.add_argument('--cpu-count',type=int,choices=(1,4),action='append')
args=parser.parse_args()
visual=[]
for n in (args.cpu_count or (1,4)):
    base=out/f'raw/cpu{n}'
    if not (base/'visible.json').exists():continue
    positions=json.loads((base/'visible.json').read_text())
    for t in (1,2,3):
        crops=[]
        for kind in ('lvgl','suchu'):
            p=next(p for p in positions if p['kind']==kind and p['trial']==t)
            im=np.array(Image.open(base/f'{kind}-{t}-drawn.png').convert('RGB'))
            x,y=p['window_x']+8,p['window_y']+51
            crop=im[y:y+720,x:x+1200];assert crop.shape==(720,1200,3)
            crops.append(crop)
        # Exclude toolbar and its typography. Compare actual rendered document.
        a,b=crops;mask=np.ones((720,1200),dtype=bool);mask[386:714,10:54]=False
        exact=np.all(a==b,axis=2)
        black_a=np.all(a==0,axis=2);black_b=np.all(b==0,axis=2)
        union=(black_a|black_b)&mask;intersection=(black_a&black_b)&mask
        visual.append(dict(cpu=n,trial=t,exact_document_pct=float(100*exact[mask].mean()),
            black_iou=float(intersection.sum()/union.sum()),black_pixels_lvgl=int(black_a[mask].sum()),black_pixels_suchu=int(black_b[mask].sum())))
metadata['visual']=visual
(out/'metadata.json').write_text(json.dumps(metadata,indent=2));print(json.dumps(metadata,indent=2))
