#!/usr/bin/env python3
"""One-time authoring helper. Runtime/build consume the resulting JSON descriptions.
Hardware sequences live in these packages, never in Driver Engine/native delegates.
"""
import json,pathlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
def ins(op,**kw):return dict(op=op,**kw)
def label(s):return {'label':s}
def c(r,v):return ins('const',dst=r,imm=v)
def rd(r,off):return ins('read',dst=r,b=1,aux=0,imm=off)
def wr(r,off):return ins('write',a=r,b=1,aux=0,imm=off)
def write(off,value):return [c(31,value),wr(31,off)]
def alu(op,d,a,b):return ins(op,dst=d,a=a,b=b)
def bcd(r):return [c(30,16),alu('div',17,r,30),c(30,10),alu('mul',17,17,30),c(30,16),alu('mod',r,r,30),alu('add',r,r,17)]
clock=[label('again'),rd(1,10),c(30,128),alu('and',2,1,30),ins('jnz',a=2,target='again'),
       rd(13,0),rd(12,2),rd(11,4),rd(10,7),rd(9,8),rd(8,9),rd(1,11),
       rd(2,10),c(30,128),alu('and',2,2,30),ins('jnz',a=2,target='again'),rd(3,0),
       alu('eq',2,3,13),ins('jz',a=2,target='again'),c(30,4),alu('and',2,1,30),ins('jnz',a=2,target='binary')]
for r in (13,12,10,9,8):clock+=bcd(r)
clock += [c(30,128),alu('and',18,11,30),c(30,127),alu('and',11,11,30)]+bcd(11)+[alu('or',11,11,18),label('binary'),
          c(30,2000),alu('add',8,8,30),c(30,128),alu('and',18,11,30),c(30,127),alu('and',11,11,30),
          c(30,2),alu('and',2,1,30),ins('jnz',a=2,target='done'),c(30,12),alu('mod',11,11,30),
          ins('jz',a=18,target='done'),alu('add',11,11,30),label('done'),ins('ret',imm=0)]
cmos={'format':1,'name':'cmos-calendar','platform':'rtc.cmos','bus':'platform','class':'clock','priority':100,
      'features':['base','indexed'],'resources':[{'kind':'indexed-pio','length':16,'rights':['read'],'widths':1}],
      'entries':{'probe':[rd(1,13),c(30,128),alu('and',1,1,30),ins('jnz',a=1,target='valid'),ins('ret',imm=-12),label('valid'),ins('ret',imm=0)],
                 'init':[ins('ret',imm=0)],'stop':[ins('ret',imm=0)],'read':clock},
      'info':{'hardware':'MC146818/DS12887 compatible calendar; firmware-configured, read only',
              'source':'https://www.analog.com/media/en/technical-documentation/data-sheets/DS12885-DS12C887A.pdf',
              'protocol':'UIP/seconds coherence; BCD or binary, 12h AM/PM or 24h; years 2000..2099'}}
uart={'format':1,'name':'uart-16550','platform':'uart.com2','bus':'platform','class':'character','priority':100,
      'features':['base','pio','irq'],'irq':3,'resources':[{'kind':'pio','length':8,'rights':['read','write'],'widths':1}],
      'entries':{
       'probe':[rd(1,7),c(2,85),wr(2,7),rd(3,7),wr(1,7),alu('eq',4,2,3),ins('jnz',a=4,target='valid'),ins('ret',imm=-12),label('valid'),ins('ret',imm=0)],
       'init':write(1,0)+write(3,131)+write(0,1)+write(1,0)+write(3,3)+write(2,7)+write(4,11)+write(1,1)+[ins('ret',imm=0)],
       'stop':write(1,0)+write(4,0)+[rd(1,2),ins('ret',imm=0)],
       'write':[label('ready'),rd(1,5),c(30,32),alu('and',1,1,30),ins('jz',a=1,target='ready'),wr(0,0),ins('ret',imm=0)],
       'irq':[rd(1,2),label('drain'),rd(2,5),c(30,1),alu('and',2,2,30),ins('jz',a=2,target='done'),rd(3,0),ins('emit',a=3),ins('jmp',target='drain'),label('done'),ins('ret',imm=0)],
       'control':[ins('jz',a=0,target='normal')]+write(4,27)+[ins('ret',imm=0),label('normal')]+write(4,11)+[ins('ret',imm=0)],
       'diagnostic':write(3,131)+[rd(8,0)]+write(3,3)+[ins('ret',imm=0)]},
      'info':{'hardware':'16550 UART, COM2 0x2f8 IRQ3, 115200 8N1, FIFO, RX interrupt',
              'source':'https://www.ti.com/lit/gpn/TL16C550D','control':'0 normal, 1 hardware loopback; diagnostic reads baud divisor'}}
for name,source in [('cmos',cmos),('uart',uart)]:
    (ROOT/'drivers'/f'{name}.driver.json').write_text(json.dumps(source,indent=2)+'\n')
# Different actual hardware configuration, same bytecode primitives/kernel.
variant=json.loads(json.dumps(uart));variant['name']='uart-16550-57600'
# init DIV DLL instruction: pair after LCR 0x83 (index 4).
variant['entries']['init'][4]['imm']=2
variant['info']['hardware']='16550 UART, COM2 0x2f8 IRQ3, 57600 8N1'
(ROOT/'drivers/uart-slow.driver.json').write_text(json.dumps(variant,indent=2)+'\n')
