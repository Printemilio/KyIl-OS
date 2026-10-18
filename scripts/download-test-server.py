#!/usr/bin/env python3
"""HTTP server for the Vex browser download tests.

Run on the host; the VirtualBox NAT guest reaches it at http://10.0.2.2:<port>/.
Every body is generated from its URL, so the guest-side verifier
(userspace/linux_tests/download_verify.c) can regenerate and compare it without
any shared state:

    /small.txt                    text/plain served as an attachment (1 KiB)
    /p/<seed>-<size>.bin          size bytes of the test pattern, octet-stream
    /slow/<seed>-<size>.bin       same pattern, throttled (default 128 KiB/s)
    /nolength/<seed>-<size>.bin   same pattern, chunked, no Content-Length
    /cut/<seed>-<size>.bin        announces <size> but drops after half of it
    /evil                         Content-Disposition filename="../../etc/pwn"
    /named?filename=<name>        attachment with an explicit file name
    /page                         HTML page whose links start real downloads
    /log                          the request log, as text

The pattern is byte(i) = (i * seed + (i >> 11)) & 0xff, see pattern_byte().
"""

import argparse
import http.server
import socketserver
import threading
import time
import urllib.parse

SMALL_TEXT = ("Vex-OS download test file\n" * 40).encode("ascii")[:1024]
CHUNK = 64 * 1024

request_log = []
log_lock = threading.Lock()


def pattern_byte(index, seed):
    return (index * seed + (index >> 11)) & 0xFF


def pattern_block(offset, length, seed):
    return bytes(pattern_byte(offset + i, seed) for i in range(length))


def parse_pattern_name(name):
    stem = name.rsplit(".", 1)[0]
    seed_text, size_text = stem.split("-", 1)
    return int(seed_text), int(size_text)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    throttle_bytes_per_second = 128 * 1024

    def log_message(self, format, *args):  # noqa: A002 - signature fixed by base class
        line = "%s %s" % (self.address_string(), format % args)
        with log_lock:
            request_log.append(line)
        print(line, flush=True)

    def send_headers(self, length, content_type, disposition=None):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        if length is not None:
            self.send_header("Content-Length", str(length))
        else:
            self.send_header("Transfer-Encoding", "chunked")
        if disposition:
            self.send_header("Content-Disposition", disposition)
        self.send_header("Connection", "close")
        self.end_headers()

    def write_pattern(self, seed, size, limit=None, throttle=None, chunked=False):
        stop = size if limit is None else min(limit, size)
        offset = 0
        while offset < stop:
            length = min(CHUNK, stop - offset)
            block = pattern_block(offset, length, seed)
            if chunked:
                self.wfile.write(b"%x\r\n" % len(block) + block + b"\r\n")
            else:
                self.wfile.write(block)
            self.wfile.flush()
            offset += length
            if throttle:
                time.sleep(length / throttle)
        if chunked:
            self.wfile.write(b"0\r\n\r\n")

    def do_GET(self):  # noqa: N802 - name fixed by base class
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)
        try:
            self.route(path, query)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def route(self, path, query):
        if path == "/small.txt":
            self.send_headers(len(SMALL_TEXT), "text/plain",
                              'attachment; filename="small.txt"')
            self.wfile.write(SMALL_TEXT)
        elif path.startswith("/p/"):
            name = path[3:]
            seed, size = parse_pattern_name(name)
            self.send_headers(size, "application/octet-stream",
                              'attachment; filename="%s"' % name)
            self.write_pattern(seed, size)
        elif path.startswith("/slow/"):
            name = path[6:]
            seed, size = parse_pattern_name(name)
            rate = int(query.get("rate", [self.throttle_bytes_per_second])[0])
            self.send_headers(size, "application/octet-stream",
                              'attachment; filename="%s"' % name)
            self.write_pattern(seed, size, throttle=rate)
        elif path.startswith("/nolength/"):
            name = path[10:]
            seed, size = parse_pattern_name(name)
            self.send_headers(None, "application/octet-stream",
                              'attachment; filename="%s"' % name)
            self.write_pattern(seed, size, chunked=True)
        elif path.startswith("/cut/"):
            name = path[5:]
            seed, size = parse_pattern_name(name)
            self.send_headers(size, "application/octet-stream",
                              'attachment; filename="%s"' % name)
            self.write_pattern(seed, size, limit=size // 2)
            self.close_connection = True
            self.connection.shutdown(2)  # the announced length is never reached
        elif path == "/evil":
            self.send_headers(len(SMALL_TEXT), "text/plain",
                              'attachment; filename="../../etc/pwn"')
            self.wfile.write(SMALL_TEXT)
        elif path == "/named":
            name = query.get("filename", ["named.bin"])[0]
            self.send_headers(len(SMALL_TEXT), "application/octet-stream",
                              'attachment; filename="%s"' % name)
            self.wfile.write(SMALL_TEXT)
        elif path == "/page":
            body = PAGE.encode("utf-8")
            self.send_headers(len(body), "text/html; charset=utf-8")
            self.wfile.write(body)
        elif path == "/log":
            with log_lock:
                body = "\n".join(request_log).encode("utf-8")
            self.send_headers(len(body), "text/plain")
            self.wfile.write(body)
        else:
            self.send_error(404)


PAGE = """<!doctype html><meta charset="utf-8"><title>Downloads</title>
<body style="font:22px sans-serif;background:#fff;color:#000;padding:24px">
<h2>Vex download test</h2>
<p><a id="small" href="/small.txt">small.txt</a></p>
<p><a id="big" href="/p/7-2097152.bin">p/7-2097152.bin (2 MiB)</a></p>
<p><a id="slow" href="/slow/3-1048576.bin">slow/3-1048576.bin</a></p>
<p><a id="attr" download="from-attribute.txt" href="/p/5-4096.txt">download attribute</a></p>
</body>
"""


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8765)
    arguments = parser.parse_args()
    server = Server(("0.0.0.0", arguments.port), Handler)
    print("listening on :%d" % arguments.port, flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
