"""Analyze recorded VM counters. Never generates benchmark measurements."""
import re,json,csv,pathlib,statistics,argparse
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/'build/paint-comparison'
def quantile(xs,p):
    xs=sorted(xs);at=(len(xs)-1)*p;lo=int(at);hi=min(lo+1,len(xs)-1)
    return xs[lo]+(xs[hi]-xs[lo])*(at-lo)
parser=argparse.ArgumentParser()
parser.add_argument('--cpu-count',type=int,choices=(1,4),action='append',help='Only analyze these runs; avoid mixing archived versions.')
args=parser.parse_args()
results=[];frame_rows=[]
for n in (args.cpu_count or (1,4)):
    path=OUT/f'raw/cpu{n}/all.log'
    if not path.exists():continue
    sessions=[];s=None
    for line in path.read_text(errors='replace').splitlines():
        m=re.search(r'PAINTBENCH RUNNER (\w+) instances=(\d+) ns=(\d+) baseline_pages=(\d+)',line)
        if m:s=dict(kind=m[1],instances=int(m[2]),start=int(m[3]),baseline=int(m[4]),ready=[],frames=[]);sessions.append(s)
        if not s:continue
        m=re.search(r'PAINTBENCH READY \w+ ns=(\d+) cpu=(\d+) pages=(\d+)',line)
        if m:s['ready'].append(dict(ns=int(m[1]),cpu=int(m[2]),pages=int(m[3])))
        m=re.search(r'PAINTBENCH IDLE \w+ wall_ns=(\d+) cpu_ns=(\d+) pages=(\d+)',line)
        if m:s['idle_wall']=int(m[1]);s['idle_cpu']=int(m[2])
        m=re.search(r'PAINTBENCH SUMMARY \w+ count=(\d+) checksum=(\d+)',line)
        if m:s['count']=int(m[1]);s['checksum']=int(m[2])
        if 'PAINTBENCH FRAME ' in line:
            a=line.split();r=dict(phase=int(a[3]),begin=int(a[4]),end=int(a[5]),cpu=int(a[6]),present=int(a[7]),pages=int(a[8]))
            if len(a)>9:r['cpu_end']=int(a[9])
            s['frames'].append(r)
        m=re.search(r'PAINTBENCH EXIT \w+ peak_pages=(\d+) after_pages=(\d+) status=(\d+)',line)
        if m:s.update(peak=int(m[1]),after=int(m[2]),status=int(m[3]))
    visible=json.loads((OUT/f'raw/cpu{n}/visible.json').read_text())
    for kind in ('lvgl','suchu'):
        runs=[s for s in sessions if s['kind']==kind and s['instances']==1 and 'checksum'in s]
        if not runs:continue
        for run in runs:
            assert run['count']==3584 and run['checksum']==543696415,(n,kind,run.get('checksum'))
            assert run['status']==0
        draws=[];presents=[];intervals=[];occupancy=[];render_cpu=[];fps=[]
        for index,run in enumerate(runs):
            rows=[r for r in run['frames'] if 2<=r['phase']<=5]
            assert len(rows)==64,(n,kind,len(rows))
            draws.extend((r['end']-r['begin'])/1e6 for r in rows)
            presents.extend(r['present']/1e6 for r in rows)
            intervals.extend((b['end']-a['end'])/1e6 for a,b in zip(rows,rows[1:]))
            span=rows[-1]['end']-rows[0]['begin'];fps.append(64*1e9/span)
            if 'cpu_end'in rows[0]:occupancy.append(100*(rows[-1]['cpu_end']-rows[0]['cpu_end']+rows[0]['cpu'])/span)
            render_cpu.append(sum(r['cpu'] for r in rows)/1e6)
            for r in rows:frame_rows.append(dict(cpu_count=n,kind=kind,repetition=index+1,**r))
        pair=next(s for s in sessions if s['kind']==kind and s['instances']==2)
        memory_idle=[(s['ready'][0]['pages']-s['baseline'])*4/1024 for s in runs]
        memory_draw=[(max(r['pages'] for r in s['frames'])-s['baseline'])*4/1024 for s in runs]
        results.append(dict(cpu_count=n,kind=kind,repetitions=len(runs),
            ram_idle_mib=statistics.median(memory_idle),ram_drawing_mib=statistics.median(memory_draw),
            sampled_peak_mib=statistics.median((s['peak']-s['baseline'])*4/1024 for s in runs),
            second_increment_mib=(pair['ready'][1]['pages']-pair['ready'][0]['pages'])*4/1024,
            pair_total_mib=(pair['ready'][1]['pages']-pair['baseline'])*4/1024,
            idle_cpu_pct=statistics.median(100*s['idle_cpu']/s['idle_wall'] for s in runs),
            load_one_core_pct=statistics.median(occupancy) if occupancy else None,
            startup_ms=statistics.median((s['ready'][0]['ns']-s['start'])/1e6 for s in runs),
            render_ms=dict(p50=quantile(draws,.5),p95=quantile(draws,.95),p99=quantile(draws,.99)),
            present_ms=dict(p50=quantile(presents,.5),p95=quantile(presents,.95),p99=quantile(presents,.99)),
            interval_ms=dict(p50=quantile(intervals,.5),p95=quantile(intervals,.95),p99=quantile(intervals,.99)),
            fps=statistics.median(fps),render_cpu_ms=statistics.median(render_cpu),
            visible_upper_ms=statistics.median(s['visible_upper_ms'] for s in visible if s['kind']==kind)))
OUT.mkdir(exist_ok=True)
(OUT/'results.json').write_text(json.dumps(results,indent=2))
if frame_rows:
    with (OUT/'frames.csv').open('w',newline='')as f:
        w=csv.DictWriter(f,fieldnames=list(frame_rows[0]));w.writeheader();w.writerows(frame_rows)
print(json.dumps(results,indent=2))
