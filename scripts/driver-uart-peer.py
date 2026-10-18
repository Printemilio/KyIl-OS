#!/usr/bin/env python3
"""External serial peer for VirtualBox UART2 TCP backend; no guest shortcut.
Echo exactly the raw bytes seen leaving the emulated transmitter. Logs hex.
"""
import argparse,pathlib,socket,time
p=argparse.ArgumentParser();p.add_argument('--port',type=int,default=45672);p.add_argument('--log',default='build/driver-tests/uart-peer.log');p.add_argument('--listen',action='store_true');p.add_argument('--arm-file',default='build/driver-tests/uart-peer.arm');args=p.parse_args()
out=pathlib.Path(args.log);out.parent.mkdir(parents=True,exist_ok=True)
deadline=time.monotonic()+120
with out.open('w',buffering=1) as log:
    sock=None
    if args.listen:
        with socket.socket() as server:
            server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
            server.bind(('127.0.0.1',args.port));server.listen(1);server.settimeout(120)
            log.write('LISTEN 127.0.0.1\n');sock,_=server.accept()
    else:
        while time.monotonic()<deadline:
            try:sock=socket.create_connection(('127.0.0.1',args.port),2);break
            except OSError:time.sleep(.2)
    if sock is None:raise SystemExit('UART peer could not connect')
    with sock:
        log.write('CONNECTED; echo disabled until guest desktop ready\n');sock.settimeout(120)
        armed=False;before=0;total=0
        while True:
            try:data=sock.recv(4096)
            except (socket.timeout,ConnectionResetError):break
            if not data:break
            if not pathlib.Path(args.arm_file).exists():before+=len(data);continue
            if not armed:log.write(f'ARMED; firmware bytes observed before test={before}\n');armed=True
            total+=len(data)
            if total>4096:raise SystemExit('UART test traffic budget exceeded')
            log.write('RX '+data.hex()+'\n')
            try:sock.sendall(data)
            except ConnectionResetError:break
            log.write('TX '+data.hex()+'\n')
