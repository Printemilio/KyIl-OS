"""Actual USB mass-storage firmware boot; no IDE/SATA/CD boot device exists."""
import argparse,json,pathlib,socket,subprocess,time,shutil,hashlib

p=argparse.ArgumentParser()
p.add_argument('image',type=pathlib.Path)
p.add_argument('--firmware',choices=['bios','uefi'],default='bios')
p.add_argument('--timeout',type=int,default=240)
p.add_argument('--cpus',type=int,choices=range(1,65),default=2)
p.add_argument('--optical-control',action='store_true',help='UEFI-only control run from ISO; NEVER a USB-HDD boot proof')
p.add_argument('--modern-map',action='store_true',help='regression: modern PC reserves a very high MMIO/hotplug hole')
p.add_argument('--boot-capture',action='store_true',help='capture actual text trace before desktop takeover')
a=p.parse_args()
if a.optical_control and a.firmware!='uefi':p.error('Optical control is UEFI-only; BIOS validation must use USB-HDD')
root=pathlib.Path(__file__).resolve().parent.parent
image=a.image.resolve()
with image.open('rb') as source:
    digest=hashlib.file_digest(source,'sha256').hexdigest()
directory=root/'build/usb-boot-tests'/f'{image.stem}-{a.firmware}{"-modern-map" if a.modern_map else ""}{"-optical-control" if a.optical_control else ""}'
directory.mkdir(parents=True,exist_ok=True)
(directory/'result.json').write_text(json.dumps({'status':'RUNNING','sha256':digest,'firmware':a.firmware,'cpus':a.cpus},indent=2))
serial=directory/'serial.log';serial.write_text('')
with socket.socket() as port_socket:
    port_socket.bind(('127.0.0.1',0));port=port_socket.getsockname()[1]
# KVM availability is established by the actual launch. Only accelerator-init
# failures allow a retry with TCG; firmware/kernel failures are never hidden.
# Legacy PC has no modern large hotplug-memory aperture. The kernel currently
# maps through the highest reserved PFN as well as RAM; modern pc machine types
# reserve 1 TiB and expose that unrelated VMM limitation before desktop startup.
machine='pc' if a.modern_map else 'pc-i440fx-2.0'
common=['qemu-system-x86_64','-machine',machine,'-global','i440FX-pcihost.pci-hole64-size=0','-m','2048','-smp',str(a.cpus),
        '-display','none','-vga','std','-nic','none','-serial',f'file:{serial}',
        '-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off',
        '-no-reboot']
if a.optical_control:
    common+=['-drive',f'if=none,id=bootcd,file={image},format=raw,readonly=on,media=cdrom',
             '-device','ide-cd,drive=bootcd,bootindex=1']
else:
    common+=['-device','usb-ehci,id=ehci',
             '-drive',f'if=none,id=usbdrive,file={image},format=raw,readonly=on',
             '-device','usb-storage,id=bootusb,bus=ehci.0,drive=usbdrive,removable=on,bootindex=1']
if a.firmware=='uefi':
    var=directory/'OVMF_VARS.fd'
    shutil.copyfile('/usr/share/OVMF/OVMF_VARS_4M.fd',var)
    common+=['-drive','if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd',
             '-drive',f'if=pflash,format=raw,unit=1,file={var}']
# D630 generation: 36-bit physical addresses and 4-level paging. The host CPU
# may otherwise expose LA57, which this kernel does not currently implement.
command=common+['-accel','kvm','-cpu','host,phys-bits=36,-la57']
err=(directory/'qemu.stderr').open('w+')
process=subprocess.Popen(command,stdout=subprocess.DEVNULL,stderr=err)
time.sleep(1)
if process.poll() is not None:
    err.flush();err.seek(0);message=err.read()
    if 'KVM' in message or 'kvm' in message:
        command=common+['-accel','tcg','-cpu','max,phys-bits=36,-la57']
        err.close();err=(directory/'qemu.stderr').open('w+')
        process=subprocess.Popen(command,stdout=subprocess.DEVNULL,stderr=err)
start=time.monotonic();channel=None;reader=None;boot_captured=False
def qmp(name,arguments=None):
    request={'execute':name}
    if arguments is not None:request['arguments']=arguments
    channel.sendall((json.dumps(request)+'\r\n').encode())
    while True:
        reply=json.loads(reader.readline())
        if 'error' in reply:raise RuntimeError(reply)
        if 'return' in reply:return reply['return']
try:
    while time.monotonic()-start<a.timeout:
        if process.poll() is not None:raise RuntimeError(f'QEMU exited {process.returncode}')
        if channel is None:
            try:
                channel=socket.create_connection(('127.0.0.1',port),timeout=5)
                reader=channel.makefile('r');json.loads(reader.readline());qmp('qmp_capabilities')
                (directory/'block.json').write_text(json.dumps(qmp('query-block'),indent=2))
                (directory/'usb.txt').write_text(qmp('human-monitor-command',{'command-line':'info usb'}))
            except ConnectionRefusedError:
                time.sleep(.2);continue
        text=serial.read_text(errors='replace')
        if a.boot_capture and not boot_captured and 'vmm-boot:' in text and 'cap-trace: create pid=5 ' not in text:
            qmp('screendump',{'filename':str(directory/'boot-trace.ppm')});boot_captured=True
        if 'KERNEL PANIC' in text or 'boot: no healthy entropy' in text:raise RuntimeError('Kernel failure; see serial.log')
        if 'desktop: GUI service ready' in text:
            time.sleep(2)
            qmp('screendump',{'filename':str(directory/'desktop.ppm')})
            (directory/'result.json').write_text(json.dumps({'status':'PASS','image':str(image),'sha256':digest,'firmware':a.firmware,
                'elapsed_seconds':round(time.monotonic()-start,2),'cpus':a.cpus,'command':command,
                'boot_bus':'IDE optical control' if a.optical_control else 'USB EHCI mass-storage','optical_boot_device':a.optical_control},indent=2))
            print(f'PASS {a.firmware}: {"optical control (NOT USB proof)" if a.optical_control else "actual USB-HDD boot"}, kernel + desktop, {time.monotonic()-start:.1f}s')
            break
        time.sleep(.2)
    else:
        if channel:
            qmp('screendump',{'filename':str(directory/'failure.ppm')})
            (directory/'blockstats.json').write_text(json.dumps(qmp('query-blockstats'),indent=2))
        raise RuntimeError(('UEFI optical control' if a.optical_control else 'USB boot')+' deadline exceeded; see capture and serial')
except Exception as failure:
    (directory/'result.json').write_text(json.dumps({'status':'FAIL','sha256':digest,'firmware':a.firmware,'cpus':a.cpus,'error':str(failure),'command':command},indent=2))
    raise
finally:
    if channel:
        try:qmp('quit')
        except (OSError,ValueError):pass
        channel.close()
    if process.poll() is None:
        process.terminate()
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:process.kill();process.wait()
    err.close()
