import importlib.util,json,pathlib,sys
root=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pegraph',root/'scripts/audit-pe-graph.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
table=module.registered(root)
rows=[(dll,name) for dll,names in sorted(table.items()) for name in sorted(names)]
out=root/'userspace/windows/generated_export_census.h'
out.write_text('/* Generated candidates. VM resolves actual compiled exports; not support claims. */\n'
    'static const struct {const char *dll,*name;} export_candidates[]={\n'+
    ''.join('{'+json.dumps(dll)+','+json.dumps(name)+'},\n' for dll,name in rows)+'};\n',encoding='utf8')
print(f'Generated {len(rows)} candidate bindings')
