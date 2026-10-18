import importlib.util,pathlib,tempfile,unittest,struct,shutil
root=pathlib.Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('graph',root/'scripts/audit-pe-graph.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class GraphTests(unittest.TestCase):
    def test_real_process_static_imports(self):
        p=m.PE(root/'usr/pe-tests/windows-process-parent.exe')
        try:
            names={name for entry in p.imports() for name in entry['symbols']}
            self.assertIn('CreateProcessW',names);self.assertIn('CreatePipe',names);self.assertEqual(p.machine,0x8664)
        finally:p.close()
    def test_provider_is_real_pe_importing_implementations(self):
        p=m.PE(root/'usr/lib/kyil-windows/kernel32.dll')
        try:
            exports=p.exports();self.assertIn('CreateProcessW',exports);self.assertGreater(exports['CreateProcessW']['rva'],0)
            imports=p.imports();self.assertEqual(imports[0]['module'],'kyilwin-provider.dll')
            self.assertIn('kernel32.dll$CreateProcessW',imports[0]['symbols'])
        finally:p.close()
    def test_bad_header_bounds(self):
        with tempfile.TemporaryDirectory(dir=root/'build/windows') as directory:
            data=bytearray(64);data[:2]=b'MZ';struct.pack_into('<I',data,60,0xffffffff)
            path=pathlib.Path(directory)/'invalid.exe';path.write_bytes(data)
            with self.assertRaises(ValueError):m.PE(path)
    def test_alias_registrations_are_found(self):
        exports=m.registered(root);self.assertIn('AddVectoredExceptionHandler',exports['kernel32.dll'])
        self.assertIn('RtlAddVectoredExceptionHandler',exports['ntdll.dll'])
        self.assertIn('NtCreateKey',exports['ntdll.dll'])
        self.assertNotIn('NtCreateKey',exports['kernel32.dll'])
        self.assertIn('CommandLineToArgvW',exports['shell32.dll'])
        self.assertNotIn('CommandLineToArgvW',exports['kernel32.dll'])
    def test_supplied_pe_exports_are_not_called_missing(self):
        fixture=root/'usr/pe-tests/windows-tls-smoke.exe'
        graph=m.audit([str(fixture)],[str(root/'usr/pe-tests')],root,{},100)
        supplied=[e for e in graph['edges'] if e.get('status')=='provided-pe-symbol']
        self.assertTrue(supplied)
    def test_windows_named_package_is_not_host_reference(self):
        with tempfile.TemporaryDirectory(prefix='windows-payload-',dir=root/'build/windows') as directory:
            path=pathlib.Path(directory)
            for file in ('windows-tls-smoke.exe','kyil-tls-one.dll','kyil-tls-two.dll'):shutil.copy(root/'usr/pe-tests'/file,path/file)
            graph=m.audit([str(path/'windows-tls-smoke.exe')],[str(path)],root,{},100)
            self.assertTrue(any(e.get('status')=='provided-pe-symbol' for e in graph['edges']))
            self.assertTrue(all(not n['reference_only'] for n in graph['nodes']))
            reference=m.audit([str(path/'windows-tls-smoke.exe')],[str(path)],root,{},100,reference_dirs=[str(path)])
            self.assertTrue(any(n['reference_only'] for n in reference['nodes']))
            self.assertFalse(any(e.get('status')=='provided-pe-symbol' for e in reference['edges']))
if __name__=='__main__':unittest.main()
