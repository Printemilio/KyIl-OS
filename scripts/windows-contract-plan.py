"""Track the precise 94-import VM frontier independently of broader PE graphs."""
import argparse, collections, json, pathlib, re
families=[
 ('registry','NtCreateKey|NtDeleteKey|NtOpenKey|NtQueryValue|NtSetValue|RtlFormatCurrentUserKeyPath','confined prefix root, durable VexFS values, shared transaction/locks','large',2),
 ('exception-unwind','RtlCapture|RtlLookup|RtlUnwind|RtlVirtualUnwind|RtlPcToFileHeader|RaiseException|UnhandledException|WerRegister','PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH','large',2),
 ('memory','Virtual|ProcessMemory|MappedFile|WorkingSet|DiscardVirtual','process_memory inventory, VMM, usercopy, native mapping/protection transactions','medium/large',1),
 ('thread-runtime','Fls|ThreadContext|ThreadPriority|ThreadInformation|SuspendThread|QueryThreadCycle|SwitchToThread|SleepEx','native contexts/accounting, per-thread TLS and stop/wake lifecycle','medium/large',3),
 ('process-job','JobObject|ProcessMitigation','process capabilities, supervision/lifetime, loader CFG and native W^X','large',3),
 ('synchronization','WaitEx|SList','native futex/thread waits, typed handles, atomic queues','medium/large',3),
 ('file-io','Find|File|PathName|DriveType|NamedPipe','root capabilities, FD inventory, file locks/metadata, native IPC/IOCP','medium/large',2),
 ('console','Console|StdHandle','native console resource and process-owned file capabilities','medium',4),
 ('heap','Heap|LocalFree','existing process heap, native allocation and object lifetime','medium',3),
 ('system-nls','Locale|Format|CPInfo|ComputerName|SystemInfo|Processor|ProductInfo|Version|ConditionMask|Time|StringType|LCMap|lstrcmp','native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy','medium/large',4),
]
def classify(name):
    for family,pattern,native,cost,priority in families:
        if re.search(pattern,name):return family,native,cost,priority
    return 'runtime','existing userspace loader and native resource contracts','medium/large',4
def main():
    p=argparse.ArgumentParser();p.add_argument('--baseline',required=True);p.add_argument('--census');p.add_argument('--out',required=True);a=p.parse_args()
    text=pathlib.Path(a.baseline).read_text(encoding='utf8',errors='replace')
    original=re.findall(r'windows: missing import ([^!\r\n]+)!([^ ]+) caller=chrome_elf.dll',text)
    if len(original)!=94 or len(set(original))!=94:raise ValueError('Baseline must contain exactly 94 distinct chrome_elf.dll static contracts')
    actual=set()
    if a.census:
        live=pathlib.Path(a.census).read_text(encoding='utf8',errors='replace')
        actual={(dll.lower(),symbol) for dll,symbol in re.findall(r'windows-export\|([^|\r\n]+)\|([^|\r\n]+)\|resolved',live)}
        if not actual:raise ValueError('No compiled resolver evidence in census')
    rows=[]
    for dll,symbol in original:
        family,native,cost,priority=classify(symbol)
        rows.append(dict(dll=dll,symbol=symbol,importer='chrome_elf.dll',kind='static',family=family,native_dependencies=native,
                         complexity=cost,priority=priority,state='resolved-partial-contract' if (dll.lower(),symbol) in actual else 'absent-export'))
    for row in rows:row['associated_contracts']=[r['symbol'] for r in rows if r['family']==row['family'] and r!=row]
    rows.sort(key=lambda r:(r['priority'],r['family'],r['symbol']))
    out=pathlib.Path(a.out);out.parent.mkdir(parents=True,exist_ok=True)
    remaining=sum(r['state']=='absent-export' for r in rows)
    out.write_text(json.dumps(dict(baseline_count=94,remaining=remaining,baseline=a.baseline,census=a.census,contracts=rows,
        caveat='A resolved export is not a complete or tested contract. Static imports of chrome.exe/chrome.dll, delay imports and dynamic targets are separate.'),indent=2),encoding='utf8')
    lines=['# Exact chrome_elf.dll static frontier','',f'Baseline: 94; still absent: {remaining}.',
        'Resolved means compiled binding only; limitations and execution tests remain separate.','',
        '| DLL / symbol | Importer | Family | Native dependencies | Complexity | Priority | State |','|---|---|---|---|---|---:|---|']
    for r in rows:lines.append('| '+ ' | '.join([f'`{r["dll"]}!{r["symbol"]}`',r['importer'],r['family'],r['native_dependencies'],r['complexity'],str(r['priority']),r['state']])+' |')
    out.with_suffix('.md').write_text('\n'.join(lines)+'\n',encoding='utf8');print(f'exact baseline=94; absent={remaining}')
if __name__=='__main__':main()
