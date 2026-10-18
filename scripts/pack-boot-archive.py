"""Immutable boot files in one module; aliases share exact ORA byte views."""
import argparse, hashlib, pathlib, struct

ROOT_APPS = set('hello alloc_stress elevate_test attack_test security_boundary_test vex_event_fault_test attack_kill_test attack_kill_payload net_test gpu_test vk_test mouse_test app_window_demo gfm vex_editor vex_paint vex_launcher vex_terminal webkit webkit_linux_main WebKitWebProcess WebKitNetworkProcess WPEWebDriver jsc'.split())

def pack(root, output):
    root, output = root.resolve(), output.resolve()
    files, names = {}, {}
    for source in sorted(root.rglob('*')):
        if not source.is_file() or source.resolve() == output:
            continue
        relative = source.relative_to(root).as_posix()
        if relative.startswith(('boot/', 'EFI/', 'limine/')) or relative in ('limine.conf', 'boot.catalog', 'system.boot') or relative.startswith('auto_'):
            continue
        path = '/' + relative
        files[path] = source
        names[path] = path
        if '/' not in relative:
            alias = ('/home/root/bin/' if relative in ROOT_APPS else '/home/root/packages/imports/') + relative
            names.setdefault(alias, path)
    records = sorted(names)
    if not records or len(records) > 65536:
        raise ValueError('invalid archive file count')
    for name in records:
        if len(name.encode('utf-8')) >= 256:
            raise ValueError(f'boot path too long: {name}')
    position = (64 + 272 * len(records) + 4095) & ~4095
    locations = {}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('w+b') as stream:
        stream.truncate(position)
        stream.seek(position)
        for name, source in sorted(files.items()):
            length = source.stat().st_size
            locations[name] = position, length
            with source.open('rb') as inp:
                while block := inp.read(1024 * 1024):
                    stream.write(block)
            position += length
            aligned = (position + 4095) & ~4095
            stream.write(bytes(aligned - position))
            position = aligned
        stream.seek(64)
        for name in records:
            offset, length = locations[names[name]]
            stream.write(struct.pack('<256sQQ', name.encode(), offset, length))
        stream.flush()
        stream.seek(64)
        digest = hashlib.sha256()
        while block := stream.read(1024 * 1024):
            digest.update(block)
        stream.seek(0)
        stream.write(struct.pack('<8sIIQ32sQ', b'KYILBOOT', 1, len(records), position, digest.digest(), 0))
    print(f'Boot archive: {len(records)} paths, {len(files)} payloads, {position/2**20:.2f} MiB, SHA256={digest.hexdigest()}')
    return len(records), position

if __name__ == '__main__':
    args = argparse.ArgumentParser()
    args.add_argument('root', type=pathlib.Path)
    args.add_argument('output', type=pathlib.Path)
    options = args.parse_args()
    pack(options.root, options.output)
