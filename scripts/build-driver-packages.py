#!/usr/bin/env python3
import hashlib,importlib.util,pathlib,struct
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('driver_pack',ROOT/'scripts/pack-driver.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
import json
entries=[]
for source in sorted((ROOT/'drivers').glob('*.driver.json')):
    archive=module.pack(json.loads(source.read_text()))
    name=source.name.removesuffix('.json');path='/drivers/'+name
    for directory in (ROOT/'drivers',ROOT/'build/iso_root/drivers'):
        directory.mkdir(parents=True,exist_ok=True);(directory/name).write_bytes(archive)
    flags=1 if name in ('cmos.driver','e1000.driver') else 0
    entries.append(path.encode().ljust(192,b'\0')+hashlib.sha256(archive).digest()+struct.pack('<I',flags)+bytes(28))
    print(path,hashlib.sha256(archive).hexdigest(),len(archive))
catalog=b'KYTRUST1'+struct.pack('<II',len(entries),0)+b''.join(entries)
for directory in (ROOT/'drivers',ROOT/'build/iso_root/drivers'):(directory/'boot.trust').write_bytes(catalog)
