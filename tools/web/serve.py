#!/usr/bin/env python3
"""Local HTTP server for the wasm port: static files with CORS + Range
requests, bound to localhost only (game assets must never leave this machine).

Usage: serve.py [docroot] [port]
"""

import os
import re
import stat
import sys
import threading
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


# Open files for Range requests, kept across requests: the game layout links
# into the Windows install, which WSL2 serves over 9P where every open/stat
# costs ~2ms -- per 64Kb block that dominated the browser's load time.
# (path -> (fd, size)). Never invalidated: restart the server after changing
# the layout (regenerating files.json/pack.bin means a restart anyway).
_FILES = {}
_FILES_LOCK = threading.Lock()


def _open_cached(path):
    with _FILES_LOCK:
        hit = _FILES.get(path)
        if hit:
            return hit
    try:
        fd = os.open(path, os.O_RDONLY)
    except OSError:
        return None
    st = os.fstat(fd)
    if not stat.S_ISREG(st.st_mode):  # directories open fine on Linux
        os.close(fd)
        return None
    size = st.st_size
    with _FILES_LOCK:
        if path in _FILES:  # lost a race: keep the first
            os.close(fd)
            return _FILES[path]
        _FILES[path] = (fd, size)
        return fd, size


class RangeHandler(SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Cache-Control", "no-cache")
        # Cross-origin isolation: lets the page use SharedArrayBuffer, which
        # XRFS needs for its fast worker transport (binary fetch instead of a
        # main-thread sync XHR decoded through a UTF-16 string).
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()

    def send_head(self):
        # Range support: the FS backend in the browser reads big files
        # (levels, textures) in aligned blocks via 206 responses.
        range_header = self.headers.get("Range")
        if not range_header:
            return super().send_head()

        path = self.translate_path(self.path)
        cached = _open_cached(path)
        if cached is None:
            return super().send_head()
        fd, size = cached
        match = re.fullmatch(r"bytes=(\d*)-(\d*)", range_header.strip())
        if not match:
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{size}")
            self.end_headers()
            return None

        start_s, end_s = match.groups()
        start = int(start_s) if start_s else 0
        end = int(end_s) if end_s else size - 1
        end = min(end, size - 1)
        if start > end or start >= size:
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{size}")
            self.end_headers()
            return None

        self.send_response(206)
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Content-Type", self.guess_type(path))
        self.end_headers()
        return _SliceFile(fd, start, end)


    def do_POST(self):
        # Dev: the page's "send saves" button (index.html) uploads the
        # browser's app data here, to reproduce a player's report locally.
        # Paths stay under <docroot>/_uploads/_appdata_/.
        match = re.fullmatch(r"/upload\?path=(_appdata_/[\w\-. /]+)", self.path)
        length = int(self.headers.get("Content-Length") or 0)
        if not match or ".." in match.group(1) or length > 256 * 1024 * 1024:
            self.send_response(400)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        dest = os.path.join(self.directory, "_uploads", match.group(1))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            remaining = length
            while remaining:
                chunk = self.rfile.read(min(remaining, 1 << 20))
                if not chunk:
                    break
                f.write(chunk)
                remaining -= len(chunk)
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()


class _SliceFile:
    """A byte range of a cached fd, read with pread (no shared file position)."""

    def __init__(self, fd, start, end):
        self.fd = fd
        self.pos = start
        self.remaining = end - start + 1

    def read(self, size=1 << 20):
        if self.remaining <= 0:
            return b""
        data = os.pread(self.fd, min(size, self.remaining), self.pos)
        self.pos += len(data)
        self.remaining -= len(data)
        return data

    def close(self):
        pass  # the fd stays cached


def main() -> None:
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    port = int(sys.argv[2] if len(sys.argv) > 2 else 8000)
    handler = partial(RangeHandler, directory=root)
    server = ThreadingHTTPServer(("127.0.0.1", port), handler)
    print(f"serving {root} at http://127.0.0.1:{port}/")
    server.serve_forever()


if __name__ == "__main__":
    main()
