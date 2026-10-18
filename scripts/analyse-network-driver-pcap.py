"""Summarize actual Ethernet/IPv4 capture, including DHCP and TLS records."""
import collections,json,pathlib,socket,struct,sys
raw=pathlib.Path(sys.argv[1]).read_bytes()
if raw[:4] not in (b'\xd4\xc3\xb2\xa1',b'\xa1\xb2\xc3\xd4'):raise ValueError('Expected classic PCAP')
endian='<' if raw[:4]==b'\xd4\xc3\xb2\xa1' else '>'
counts=collections.Counter();dhcp=[];dns=[];tcp_flows=collections.Counter();offset=24
while offset+16<=len(raw):
    seconds,micros,n,original=struct.unpack_from(endian+'4I',raw,offset);offset+=16
    if offset+n>len(raw):break
    p=raw[offset:offset+n];offset+=n;counts['frames']+=1
    if len(p)<14:continue
    if p[12:14]==b'\x08\x06':counts['arp']+=1;continue
    if p[12:14]!=b'\x08\x00' or len(p)<34:continue
    header=(p[14]&15)*4
    if header<20 or len(p)<14+header:continue
    src=socket.inet_ntoa(p[26:30]);dst=socket.inet_ntoa(p[30:34]);t=p[14+header:]
    if p[23]==17 and len(t)>=8:
        sport,dport,length,checksum=struct.unpack_from('!4H',t);data=t[8:length];counts['udp']+=1
        if 53 in (sport,dport) and len(data)>=12:
            ident,flags=struct.unpack_from('!HH',data);dns.append(dict(src=src,dst=dst,id=ident,response=bool(flags&0x8000),rcode=flags&15))
        if {sport,dport}=={67,68} and len(data)>=240:
            i=240;kind=None
            while i<len(data):
                key=data[i];i+=1
                if key==255:break
                if key==0:continue
                if i>=len(data):break
                size=data[i];i+=1;value=data[i:i+size];i+=size
                if key==53 and value:kind=value[0]
            dhcp.append(dict(type=kind,src=src,dst=dst,xid=data[4:8].hex(),address=socket.inet_ntoa(data[16:20])))
    if p[23]==6 and len(t)>=20:
        sport,dport=struct.unpack_from('!HH',t);counts['tcp']+=1
        tcp_flows[f'{src}:{sport}->{dst}:{dport}']+=1
        if t[13]&2:counts['tcp_syn']+=1
        data=t[(t[12]>>4)*4:]
        if 443 in (sport,dport) and len(data)>=5 and data[0] in (20,21,22,23) and data[1]==3:
            counts['tls_records']+=1
report=dict(counters=dict(counts),dhcp=dhcp,dns=dns,tcp_flows=dict(tcp_flows))
print(json.dumps(report,indent=2))
if not any(x['type']==5 for x in dhcp) or not any(x['response'] and not x['rcode'] for x in dns) or not counts['tls_records']:
    sys.exit(1)
