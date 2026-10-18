#!/usr/bin/env python3
"""Compile a declarative .driver JSON source, not native CPU code.
No device protocol in this compiler. Driver Engine is the authoritative validator.
"""
import argparse,hashlib,json,pathlib,struct
OPS=['ret','const','mov','add','sub','mul','div','mod','and','or','xor','shl','shr','jmp','jz','jnz','eq','lt','read','write','load','store','emit','dma']
ENTRIES=['probe','init','stop','read','write','irq','control','diagnostic']
KINDS={'pio':1,'mmio':2,'indexed-pio':3,'dma':4}
FEATURES={'base':1,'pio':2,'mmio':4,'indexed':8,'buffer':16,'irq':32,'dma':64}
CLASSES={'clock':1,'character':2,'network':3}
def number(v):return int(v,0) if isinstance(v,str) else int(v)
def name(s,n):
    b=s.encode('ascii');assert 0<len(b)<n and all(chr(x).isalnum() or chr(x) in '_.-' for x in b);return b.ljust(n,b'\0')
def compress(data):
    out=bytearray();i=0
    while i<len(data):
        run=1
        while i+run<len(data) and run<128 and data[i+run]==data[i]:run+=1
        if run>=3:out.extend((128+run-1,data[i]));i+=run;continue
        start=i;i+=run
        while i<len(data) and i-start<128:
            run=1
            while i+run<len(data) and run<3 and data[i+run]==data[i]:run+=1
            if run>=3:break
            i+=min(run,128-(i-start))
        literal=data[start:i];out.append(len(literal)-1);out.extend(literal)
    return bytes(out)
def pack(source):
    assert source['format']==1
    minor=number(source.get('minor',0));assert minor in (0,1)
    engine=number(source.get('engine_min',1));assert engine in (1,2) and (engine!=2 or minor==1)
    program=[];labels={};entries=[0xffffffff]*8
    for entry,body in source['entries'].items():
        assert entry in ENTRIES;entries[ENTRIES.index(entry)]=len(program)
        for instruction in body:
            if 'label' in instruction:
                key=entry+':'+instruction['label'];assert key not in labels;labels[key]=len(program)
            else:program.append((entry,instruction))
    code=bytearray()
    for entry,c in program:
        op=OPS.index(c['op']);imm=number(c.get('imm',0))
        if 'target' in c:imm=labels[entry+':'+c['target']]
        dst=number(c.get('dst',0));a=number(c.get('a',0));b=number(c.get('b',0));aux=number(c.get('aux',0))
        assert 0<=dst<32 and 0<=a<32 and 0<=b<32
        code.extend(struct.pack('<BBBBIQ',op,dst,a,b,aux,imm&0xffffffffffffffff))
    features=sum(FEATURES[f] for f in set(source['features']))
    resources=bytearray()
    for r in source['resources']:
        resources.extend(struct.pack('<IIQIIII',KINDS[r['kind']],number(r.get('selector',0)),number(r.get('offset',0)),
            number(r['length']),sum({'read':1,'write':2}[x] for x in set(r['rights'])),number(r['widths']),0))
    assert len(resources)<=128;resources.extend(bytes(128-len(resources)))
    manifest=name(source['name'],32)+name(source['platform'],32)
    manifest+=struct.pack('<8I',{'platform':1,'pci':2}[source['bus']],CLASSES[source['class']],number(source.get('vendor',0)),
        number(source.get('device',0)),number(source.get('pci_class',0)),features,engine,number(source.get('priority',0)))
    manifest+=struct.pack('<8I',*entries)+struct.pack('<4I',len(program),len(source['resources']),number(source.get('scratch',0)),number(source.get('irq',0xffffffff)))+resources
    assert len(manifest)==272 and 0<len(program)<=2048
    members=[('manifest',1,manifest),('program',2,bytes(code)),('info',3,json.dumps(source.get('info',{}),sort_keys=True).encode())]
    payload=bytearray(struct.pack('<II',len(members),0));offset=8+32*len(members)
    for member,kind,data in members:payload.extend(name(member,16)+struct.pack('<4I',offset,len(data),kind,0));offset+=len(data)
    for member,kind,data in members:payload.extend(data)
    assert len(payload)<=65536
    compressed=compress(payload)
    archive=struct.pack('<8sHH5I32s16s',b'KYDRIVER',1,minor,80,1,len(compressed),len(payload),0,hashlib.sha256(payload).digest(),bytes(16))+compressed
    assert len(archive)<=65536
    return archive
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('output');args=p.parse_args()
    archive=pack(json.loads(pathlib.Path(args.source).read_text()));out=pathlib.Path(args.output);out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(archive)
    print(f'{out}: {len(archive)} bytes sha256={hashlib.sha256(archive).hexdigest()}')
