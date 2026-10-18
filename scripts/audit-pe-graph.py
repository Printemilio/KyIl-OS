"""Read-only PE dependency graph. No Windows DLL is executed or staged.

Reference DLL availability and a registered KyIlOS export are NOT proof of a
fully implemented contract. Dynamic resolver targets remain runtime work.
"""
import argparse, collections, hashlib, json, mmap, os, pathlib, re, struct

class PE:
    def __del__(self):
        if hasattr(self,'b'):self.b.close()
        if hasattr(self,'f'):self.f.close()
    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.f = self.path.open('rb'); self.b = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        if self.b[:2] != b'MZ': raise ValueError('not MZ')
        p = self.u32(60)
        if self.b[p:p+4] != b'PE\0\0': raise ValueError('not PE')
        self.machine = self.u16(p+4); n = self.u16(p+6); optsize = self.u16(p+20); o = p+24
        magic = self.u16(o); self.width = 8 if magic == 0x20b else 4 if magic == 0x10b else 0
        if not self.width: raise ValueError('bad optional header')
        self.base = self.u64(o+24) if self.width == 8 else self.u32(o+28)
        self.headers = self.u32(o+60)
        dc = self.u32(o+(108 if self.width == 8 else 92)); d = o+(112 if self.width == 8 else 96)
        self.dirs = [self.unpack('<II', d+i*8) if i < min(dc,16) else (0,0) for i in range(16)]
        self.sections=[]
        for i in range(n):
            s=o+optsize+i*40
            self.sections.append((self.b[s:s+8].rstrip(b'\0').decode('ascii','replace'),self.u32(s+12),self.u32(s+8),self.u32(s+20),self.u32(s+16)))
    def close(self): self.b.close(); self.f.close()
    def unpack(self, fmt, at):
        size=struct.calcsize(fmt)
        if at < 0 or at > len(self.b)-size: raise ValueError('file bounds')
        return struct.unpack_from(fmt,self.b,at)
    def u16(self,a): return self.unpack('<H',a)[0]
    def u32(self,a): return self.unpack('<I',a)[0]
    def u64(self,a): return self.unpack('<Q',a)[0]
    def off(self,r,n=1):
        if r < self.headers and r+n <= self.headers and r+n <= len(self.b): return r
        for _,va,vs,raw,rs in self.sections:
            if va <= r and r-va+n <= rs and raw+r-va+n <= len(self.b): return raw+r-va
        raise ValueError('unbacked RVA '+hex(r))
    def string(self,r):
        a=self.off(r); end=self.b.find(b'\0',a,min(a+65536,len(self.b)))
        if end < 0: raise ValueError('unterminated string')
        self.off(r,end-a+1)
        return self.b[a:end].decode('ascii','strict')
    def imports(self,delay=False):
        r,size=self.dirs[13 if delay else 1]; out=[]
        if not size:return out
        step=32 if delay else 20
        for i in range(size//step):
            a=self.off(r+i*step,step); words=self.unpack('<'+'I'*(step//4),a)
            if not any(words): return out
            if delay:
                attr,name,_,iat,intab,_,_,_=words
                if attr & ~1: raise ValueError('delay attributes')
                if not attr: name-=self.base; intab-=self.base; iat-=self.base
            else: intab,_,_,name,iat=words
            intab=intab or iat; module=self.string(name).lower(); symbols=[]
            for k in range(100000):
                a=self.off(intab+k*self.width,self.width); v=self.u64(a) if self.width==8 else self.u32(a)
                if not v:break
                ordinal=v>>(self.width*8-1)
                symbols.append('#'+str(v&65535) if ordinal else self.string(v+2))
            else: raise ValueError('thunk bound')
            out.append({'module':module,'symbols':symbols,'kind':'delay' if delay else 'static'})
        raise ValueError('unterminated descriptors')
    def exports(self):
        r,size=self.dirs[0]; out={}
        if not size:return out
        a=self.off(r,40); first,nf,nn,funcs,names,ords=self.unpack('<IIIIII',a+16)
        if max(nf,nn)>1000000:raise ValueError('export bound')
        targets=[]
        for i in range(nf):
            target=self.u32(self.off(funcs+i*4,4))
            value=self.string(target) if r<=target<r+size else {'rva':target}
            targets.append(value);out['#'+str(first+i)]=value
        for i in range(nn):
            idx=self.u16(self.off(ords+i*2,2))
            if idx>=nf:raise ValueError('export ordinal')
            out[self.string(self.u32(self.off(names+i*4,4)))]=targets[idx]
        return out

def api_sets(path):
    p=PE(path)
    try:
        sec=next(s for s in p.sections if s[0]=='.apiset'); raw,size=sec[3],sec[4]
        b=p.b[raw:raw+size]
        version,total,_,count,entry,_,_=struct.unpack_from('<7I',b)
        if version!=6 or total>len(b) or count>100000:raise ValueError('unsupported API set schema')
        def text(at,n):
            if at+n>total or n%2:raise ValueError('API set string')
            return b[at:at+n].decode('utf-16-le').lower()
        result={}
        for i in range(count):
            _,no,nl,_,vo,vc=struct.unpack_from('<6I',b,entry+i*24)
            values=[]
            for j in range(vc):
                _,ao,al,ho,hl=struct.unpack_from('<5I',b,vo+j*20)
                values.append({'alias':text(ao,al),'host':text(ho,hl)})
            result[text(no,nl)+'.dll']=values
        return result
    finally:p.close()

def registered(root):
    # Conservative source registration evidence. Report partial coverage, not
    # "all imports implemented". Every symbol has its exact adapter source.
    groups={}; all_exports={}
    for path in (root/'userspace/windows').glob('*.c'):
        text=path.read_text(encoding='utf-8'); names=set(re.findall(r'\b(?:E|EXPORT)\(([A-Za-z_]\w*)\)',text))-{'f'}
        # Direct special aliases without an E macro are recorded too.
        names.update(re.findall(r'wr_equal\(name,"([A-Za-z_]\w*)",0\)',text))
        groups[path.stem]=names
        for name in names:all_exports.setdefault(name,[]).append(str(path.relative_to(root)))
    builtins={}
    version_names=groups.get('version',set());nt_names=groups.get('nt_core',set())|groups.get('registry',set())
    rng_names=groups.get('rng',set())
    multimedia_names=groups.get('multimedia',set())
    shell_names=groups.get('shell_api',set())
    advapi_names=groups.get('advapi',set())|groups.get('security_data',set())
    for dll in ('kernel32.dll','kernelbase.dll'):
        builtins[dll]={n:src for n,src in all_exports.items() if n not in version_names|nt_names|rng_names|multimedia_names|advapi_names|shell_names}
    if rng_names:builtins['bcryptprimitives.dll']={n:all_exports[n] for n in rng_names}
    if multimedia_names:builtins['winmm.dll']={n:all_exports[n] for n in multimedia_names}
    if shell_names:builtins['shell32.dll']={n:all_exports[n] for n in shell_names}
    if advapi_names:builtins['advapi32.dll']={n:all_exports[n] for n in advapi_names}
    builtins['ntdll.dll']={n:all_exports[n] for stem in ('heap','exceptions') for n in groups.get(stem,())}
    builtins['ntdll.dll'].update({n:all_exports[n] for n in nt_names})
    builtins['ntdll.dll'].update({n:all_exports[n] for n in groups.get('unwind',set())})
    builtins['ntdll.dll'].update({n:all_exports[n] for n in groups.get('seh',set())})
    if version_names:builtins['version.dll']={n:all_exports[n] for n in version_names}
    return builtins

def category(name,module):
    for group,pattern in [
        ('process-thread','Process|Thread|Startup|JobObject|Toolhelp'),
        ('loader','Library|Module|ProcAddress|Dll|Ldr'),
        ('memory','Virtual|Heap|Mapping|MapView|UnmapView|Memory|Alloc'),
        ('file-path','File|Directory|Path|Volume|FindFirst|FindNext|Drive'),
        ('ipc-io','Pipe|Completion|Overlapped|DeviceIo|CancelIo'),
        ('synchronization','Wait|Event|Mutex|Semaphore|Critical|SRW|Condition|Interlocked'),
        ('time-system','Time|Tick|Performance|System|Processor|Computer'),
        ('text-environment','String|Locale|Char|Environment|CommandLine|NLS|CodePage'),
        ('security-registry','Token|Security|Access|Privilege|Sid|Reg|Crypt'),
        ('ui','Window|Message|Cursor|Icon|Monitor|Dpi|DC|Menu'),
        ('network','Socket|WSA|Inet|Addr|Host|Net')]:
        if re.search(pattern,name):return group
    if module in ('user32.dll','gdi32.dll','dwmapi.dll'):return 'ui'
    return 'other'

def audit(roots,dirs,repo,schema,limit,census=None,reference_dirs=()):
    native=registered(repo); queue=collections.deque((pathlib.Path(p),'requested') for p in roots)
    if census:
        actual={}
        for dll,name,status in re.findall(r'windows-export\|([^|\r\n]+)\|([^|\r\n]+)\|(resolved|absent)',pathlib.Path(census).read_text(encoding='utf8',errors='replace')):
            if status=='resolved':actual.setdefault(dll,{})[name]=native.get(dll,{}).get(name,['compiled resolver'])
        if not actual:raise ValueError('census contained no actual resolved bindings')
        native=actual
    nodes={}; edges=[]; errors=[]; missing=collections.defaultdict(set); apis={}; dynamics=[]
    indexes=[]
    export_cache={}
    references=[pathlib.Path(d).resolve() for d in reference_dirs]
    # Backward-compatible host metadata input. A directory merely containing
    # "windows" in its name is NOT a host OS library (e.g. windows-apps/Chrome).
    if os.environ.get('SystemRoot'):references.append((pathlib.Path(os.environ['SystemRoot'])/'System32').resolve())
    def is_reference(path):
        absolute=path.resolve()
        return any(absolute==directory or directory in absolute.parents for directory in references)
    for directory in dirs:
        d=pathlib.Path(directory)
        indexes.append({p.name.lower():p for p in d.iterdir() if p.is_file()})
    def locate(name,parent):
        candidate=parent.parent/name
        if candidate.is_file():return candidate
        for ix in indexes:
            if name in ix:return ix[name]
        return None
    while queue:
        path,role=queue.popleft(); key=str(path.resolve()).lower()
        if key in nodes:continue
        if len(nodes)>=limit:raise ValueError('module bound exceeded; increase --max-modules explicitly')
        try:
            p=PE(path); imports=p.imports()+p.imports(True); exports=p.exports()
            node={'path':str(path),'role':role,'machine':hex(p.machine),'imports':imports,'exports':exports,
                  'reference_only':role=='reference','size':len(p.b)}
            p.close();nodes[key]=node
        except (OSError,ValueError,struct.error,UnicodeError) as e:
            errors.append({'path':str(path),'error':str(e)});continue
        for imp in imports:
            dll=imp['module']; host=dll
            if dll.startswith(('api-ms-','ext-ms-')):
                vals=schema.get(dll,[]); chosen=next((v for v in vals if v['alias']==path.name.lower()),None) or next((v for v in vals if not v['alias']),None)
                host=chosen['host'] if chosen else dll;apis[dll]={'reference_host':host,'values':vals,'native_mapping':False}
            found=locate(host,path)
            supplied={}
            if found and host not in native and not is_reference(found):
                fk=str(found.resolve()).lower()
                if fk not in export_cache:
                    reference=PE(found)
                    try:export_cache[fk]=reference.exports()
                    finally:reference.close()
                supplied=export_cache[fk]
            for symbol in imp['symbols']:
                status='registered-partial' if symbol in native.get(host,{}) and host==dll else 'missing-native-contract'
                if dll=='kyilwin-provider.dll' and '$' in symbol:
                    provider,export=symbol.split('$',1)
                    if export in native.get(provider,{}):status='registered-partial'
                target=supplied.get(symbol)
                if status=='missing-native-contract' and target and (not isinstance(target,dict) or target.get('rva')):
                    status='provided-pe-forwarder' if isinstance(target,str) else 'provided-pe-symbol'
                edge={'from':str(path),'kind':imp['kind'],'dll':dll,'host_reference':host,'symbol':symbol,'status':status,
                      'adapter':native.get(host,{}).get(symbol,[]),'family':category(symbol,host)}
                edges.append(edge)
                if status=='missing-native-contract':missing[edge['family']].add((dll,symbol))
                if symbol in ('GetProcAddress','LoadLibraryA','LoadLibraryW','LoadLibraryExA','LoadLibraryExW','LdrGetProcedureAddress','LdrLoadDll'):
                    dynamics.append({'caller':str(path),'resolver':symbol,'targets':'runtime discovery required'})
            if found:queue.append((found,'reference' if is_reference(found) else 'package'))
        for symbol,target in exports.items():
            if isinstance(target,str) and '.' in target:
                dll,forward=target.rsplit('.',1); dll=dll.lower()+('.dll' if not dll.lower().endswith('.dll') else '')
                edges.append({'from':str(path),'kind':'forwarder','dll':dll,'symbol':forward,'export':symbol,'status':'reference-forwarder'})
                found=locate(dll,path)
                if found:queue.append((found,'reference' if is_reference(found) else 'package'))
    requested={n['path'] for n in nodes.values() if n['role']=='requested'}
    package={n['path'] for n in nodes.values() if not n['reference_only']}
    def contracts(paths,kind):
        return sorted({(e['dll'],e['symbol']) for e in edges if e['from'] in paths and e['kind']==kind and e.get('status')=='missing-native-contract'})
    return {'schema_version':1,'roots':list(roots),'nodes':list(nodes.values()),'edges':edges,'parse_errors':errors,
            'priority_missing':{label:[{'dll':d,'symbol':s} for d,s in contracts(paths,kind)]
                                for label,paths,kind in [('requested-static',requested,'static'),('package-static',package,'static'),('package-delay',package,'delay')]},
            'api_sets':apis,'dynamic_resolvers':dynamics,'native_registrations':native,
            'missing_families':{f:[{'dll':d,'symbol':s} for d,s in sorted(v)] for f,v in sorted(missing.items())},
            'caveats':['reference Windows DLLs are NOT KyIlOS implementations or staged payloads',
                       'registered exports have partial contracts; this graph does not prove execution',
                       'API-set reference mappings are not yet native support',
                       'dynamic resolver targets and ordinal API use require execution traces']}

def main():
    a=argparse.ArgumentParser();a.add_argument('pe',nargs='+');a.add_argument('--search',action='append',default=[])
    a.add_argument('--reference-search',action='append',default=[],help='Host/reference metadata only, never an implementation')
    a.add_argument('--repo',default=str(pathlib.Path(__file__).resolve().parents[1]));a.add_argument('--api-schema')
    a.add_argument('--out',required=True);a.add_argument('--max-modules',type=int,default=2048);a.add_argument('--census');x=a.parse_args()
    graph=audit(x.pe,x.search+x.reference_search,pathlib.Path(x.repo),api_sets(x.api_schema) if x.api_schema else {},x.max_modules,x.census,x.reference_search)
    out=pathlib.Path(x.out);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(graph,indent=2),encoding='utf-8')
    lines=['# PE contract inventory','',f'Modules: {len(graph["nodes"])}; edges: {len(graph["edges"])}; parse errors: {len(graph["parse_errors"])}','',
           'Reference-only host DLLs are not native support. Registered adapters remain partial.','', '| Family | Missing unique contracts |','|---|---:|']
    for label,items in graph['priority_missing'].items():lines.append(f'| {label} | {len(items)} |')
    for family,items in graph['missing_families'].items():lines.append(f'| {family} | {len(items)} |')
    for family,items in graph['missing_families'].items():
        lines.extend(['',f'## {family}','']+[f'- `{i["dll"]}!{i["symbol"]}`' for i in items])
    out.with_suffix('.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(json.dumps({'modules':len(graph['nodes']),'edges':len(graph['edges']),'api_sets':len(graph['api_sets']),
                      'errors':len(graph['parse_errors']),'missing':{k:len(v) for k,v in graph['missing_families'].items()}}))
if __name__=='__main__':main()
