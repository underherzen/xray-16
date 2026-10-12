#!/usr/bin/env python3
"""Local HTTP server for the wasm port: static files with CORS + Range
requests, bound to localhost only (game assets must never leave this machine).

Usage: serve.py [docroot] [port]
"""

import os
import re
import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


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
        if not os.path.isfile(path):
            return super().send_head()

        size = os.path.getsize(path)
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
        return _SliceFile(path, start, end)


class _SliceFile:
    def __init__(self, path, start, end):
        self.f = open(path, "rb")
        self.f.seek(start)
        self.remaining = end - start + 1

    def read(self, size=65536):
        if self.remaining <= 0:
            return b""
        data = self.f.read(min(size, self.remaining))
        self.remaining -= len(data)
        return data

    def close(self):
        self.f.close()


def main() -> None:
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    port = int(sys.argv[2] if len(sys.argv) > 2 else 8000)
    handler = partial(RangeHandler, directory=root)
    server = ThreadingHTTPServer(("127.0.0.1", port), handler)
    print(f"serving {root} at http://127.0.0.1:{port}/")
    server.serve_forever()


if __name__ == "__main__":
    main()
