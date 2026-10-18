"""Symbolize static native Paint samples using the exact captured executable."""
import bisect,collections,pathlib,re,subprocess,sys
log=pathlib.Path(sys.argv[1]).read_text(errors='replace')
binary=sys.argv[2]
nm=subprocess.check_output(['C:/Program Files/LLVM/bin/llvm-nm.exe','--numeric-sort',binary],text=True)
symbols=[]
for line in nm.splitlines():
    m=re.match(r'([0-9a-fA-F]+) [tT] (.*)',line)
    if m:symbols.append((int(m[1],16),m[2]))
addresses=[s[0] for s in symbols]
pids={int(m[1]) for m in re.finditer(r'^PROF-TASK .*pid=(\d+).*name=kyil-paint$',log,re.M)}
counts=collections.Counter()
for line in log.splitlines():
    if not line.startswith('PROF-SAMPLE '):continue
    fields=dict(re.findall(r'(\w+)=([^ ]*)',line))
    if fields.get('mode')!='user' or int(fields.get('pid','0')) not in pids:continue
    rip=int(fields['rip'],16);index=bisect.bisect_right(addresses,rip)-1
    counts[symbols[index][1] if index>=0 else '?']+=int(fields['n'])
total=sum(counts.values())
print(f'Paint userspace samples: {total}; PID(s): {sorted(pids)}')
for name,count in counts.most_common(25):print(f'{count:5} {100*count/total:6.2f}% {name}')
