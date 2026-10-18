"""Build real AMD64 PE API-adapter DLLs with JMP-through-IAT exports.

No success stubs: each export imports a specifically implemented userspace
KyIlOS Windows function. Source registration is validated by the VM census.
Original guest PEs are not rewritten/converted. There is no kernel Win32 ABI.
"""
import importlib.util,pathlib,struct
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pegraph',root/'scripts/audit-pe-graph.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
def align(n,a):return (n+a-1)&-a
def dll(module,names,base):
    text=bytearray();data=bytearray();exports=bytearray()
    eb=0x1000+align(16*len(names),4096)
    def put(buf,blob,a=1):
        buf.extend(b'\0'*(align(len(buf),a)-len(buf)));at=len(buf);buf.extend(blob);return at
    # Separate RO metadata and RW import slots. Every thunk preserves the
    # caller's Microsoft x64 registers/shadow space via a tail jump.
    exports.extend(b'\0'*40);module_name=put(exports,module.encode()+b'\0')
    symbol_rvas=[0x1000+i*16 for i in range(len(names))]
    funcs=put(exports,struct.pack('<'+'I'*len(names),*symbol_rvas),4)
    name_table=put(exports,b'\0'*(4*len(names)),4);ord_table=put(exports,struct.pack('<'+'H'*len(names),*range(len(names))),2)
    for i,name in enumerate(names):struct.pack_into('<I',exports,name_table+i*4,eb+put(exports,name.encode()+b'\0'))
    struct.pack_into('<IIHHIIIIIII',exports,0,0,0,0,0,eb+module_name,1,len(names),len(names),eb+funcs,eb+name_table,eb+ord_table)
    db=eb+align(len(exports),4096)
    # Import every binding through the private, native userspace provider.
    data.extend(b'\0'*40);provider=put(data,b'kyilwin-provider.dll\0')
    intab=put(data,b'\0'*(8*(len(names)+1)),8);iat=put(data,b'\0'*(8*(len(names)+1)),8)
    for i,name in enumerate(names):
        value=db+put(data,b'\0\0'+(module+'$'+name).encode()+b'\0',2)
        struct.pack_into('<Q',data,intab+i*8,value);struct.pack_into('<Q',data,iat+i*8,value)
        code=b'\xff\x25'+struct.pack('<i',db+iat+i*8-(symbol_rvas[i]+6))
        text.extend(code+b'\x90'*(16-len(code)))
    struct.pack_into('<IIIII',data,0,db+intab,0,0,db+provider,db+iat)
    # Empty ABSOLUTE relocation block: thunks and import metadata are RVA/RIP
    # relative, so the DLL can move without patching any code/data VA.
    reloc=struct.pack('<II',0,8)
    rb=db+align(len(data),4096)
    sections=[(b'.text',0x1000,text,0x60000020),(b'.edata',eb,exports,0x40000040),
              (b'.idata',db,data,0xc0000040),(b'.reloc',rb,reloc,0x42000040)]
    header=bytearray(512);header[:2]=b'MZ';struct.pack_into('<I',header,60,128);header[128:132]=b'PE\0\0'
    struct.pack_into('<HHIIIHH',header,132,0x8664,4,0,0,0,240,0x2022)
    o=152;struct.pack_into('<H',header,o,0x20b);struct.pack_into('<I',header,o+20,0x1000)
    struct.pack_into('<Q',header,o+24,base);struct.pack_into('<II',header,o+32,4096,512)
    struct.pack_into('<HH',header,o+40,6,0);struct.pack_into('<HH',header,o+48,6,0)
    struct.pack_into('<II',header,o+56,rb+4096,512);struct.pack_into('<HH',header,o+68,3,0x140)
    struct.pack_into('<QQQQ',header,o+72,0x100000,4096,0x100000,4096);struct.pack_into('<I',header,o+108,16)
    struct.pack_into('<II',header,o+112,eb,len(exports));struct.pack_into('<II',header,o+120,db,40)
    struct.pack_into('<II',header,o+112+5*8,rb,len(reloc));struct.pack_into('<II',header,o+112+12*8,db+iat,8*(len(names)+1))
    # Four section records need a 1024-byte header (392+160 >512).
    header.extend(b'\0'*512);struct.pack_into('<I',header,o+60,1024)
    raw=1024;body=bytearray()
    for i,(name,rva,blob,flags) in enumerate(sections):
        s=o+240+i*40;header[s:s+8]=name.ljust(8,b'\0');rs=align(len(blob),512)
        struct.pack_into('<IIII',header,s+8,len(blob),rva,rs,raw);struct.pack_into('<I',header,s+36,flags)
        body.extend(blob);body.extend(b'\0'*(rs-len(blob)));raw+=rs
    return header+body
directory=root/'usr/lib/kyil-windows';directory.mkdir(parents=True,exist_ok=True)
for i,(module,bindings) in enumerate(sorted(g.registered(root).items())):
    names=sorted(bindings);image=dll(module,names,0x180000000+i*0x100000)
    path=directory/module;path.write_bytes(image)
    parsed=g.PE(path)
    assert set(names)<=set(parsed.exports())
    assert len(parsed.imports()[0]['symbols'])==len(names)
    parsed.close();print(f'{module}: {len(names)} real IAT-tailcall exports, {len(image)} bytes')
