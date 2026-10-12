#!/usr/bin/env python3
"""Generate files.json -- the file index the wasm engine's CLocatorAPI uses
instead of scanning directories -- plus pack.bin, a bundle of every small
file (see PACK_LIMIT) that XRFS fetches in a single request at startup.

Usage: make_index.py <game_root> <out_json> [out_pack]

The index is a flat list of [relative_path, size, mtime] entries. Only the
entries the game itself needs are included (see INCLUDE); dev leftovers in
the layout dir (native builds, run scripts, logs) are skipped. Symlinks are
followed so the layout's links into the Windows game install are indexed.

The pack removes per-file request latency for the thousands of tiny files
the engine opens during startup (3417 .thm alone cost ~2 minutes of sync
XHR round trips). Layout: b"XRPF", u32 version, u32 count, then per entry
u32 pathLen, path (lowercase, forward slashes), u32 size, raw bytes.
"""

import json
import os
import struct
import sys
import time

# What the engine's fsgame.ltx actually references. Everything else in the
# layout dir is dev tooling that must not leak into the web index.
INCLUDE = (
    "fsgame.ltx",
    "_appdata_",
    "gamedata",
    "levels",
    "localization",
    "mp",
    "patches",
    "resources",
)

# Native-build leftovers inside the included trees. The cdb_cache trees are
# 64-bit layouts the wasm32 engine rejects after reading them in full (the
# ObjectSpace one alone is ~140Mb per level load); the GL shader binaries are
# desktop-only; the logs belong to native runs. The web build regenerates
# what it needs in its own (IndexedDB-persisted) app data.
EXCLUDE = (
    "_appdata_/cdb_cache",
    "_appdata_/shaders_cache_oxr",
    "_appdata_/logs",
)

# Files up to this size go into pack.bin; bigger ones are read through the
# normal block path (they are rare at startup and would bloat the bundle).
PACK_LIMIT = 512 * 1024

PACK_MAGIC = b"XRPF"
PACK_VERSION = 1


def main() -> None:
    root, out = sys.argv[1], sys.argv[2]
    out_pack = sys.argv[3] if len(sys.argv) > 3 else None
    files = []
    dirs = []
    for name in INCLUDE:
        top = os.path.join(root, name)
        if os.path.isfile(top):
            st = os.stat(top)
            files.append([name.replace("\\", "/"), st.st_size, int(st.st_mtime)])
            continue
        for dirpath, dirnames, filenames in os.walk(top, followlinks=True):
            rel_dir = os.path.relpath(dirpath, root).replace("\\", "/")
            if any(rel_dir == e or rel_dir.startswith(e + "/") for e in EXCLUDE):
                dirnames[:] = []
                continue
            if rel_dir != ".":
                dirs.append(rel_dir)
            for fn in filenames:
                p = os.path.join(dirpath, fn)
                st = os.stat(p)  # follows symlinks; broken ones raise
                files.append(
                    [f"{rel_dir}/{fn}".lstrip("./"), st.st_size, int(st.st_mtime)]
                )
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"files": files, "dirs": dirs}, f, separators=(",", ":"))
    total = sum(s for _, s, _ in files)
    print(f"{len(files)} files, {len(dirs)} dirs, {total / 1024 / 1024:.0f} MiB -> {out}")

    if not out_pack:
        return
    packed = [e for e in files if e[1] <= PACK_LIMIT]
    with open(out_pack, "wb") as f:
        f.write(PACK_MAGIC)
        f.write(struct.pack("<II", PACK_VERSION, len(packed)))
        for path, size, _ in packed:
            key = path.lower().encode("utf-8")
            f.write(struct.pack("<II", len(key), size))
            f.write(key)
            with open(os.path.join(root, path), "rb") as src:
                f.write(src.read(size))
    pack_size = sum(s for _, s, _ in packed)
    print(f"pack: {len(packed)} files <= {PACK_LIMIT // 1024} KiB, "
          f"{pack_size / 1024 / 1024:.0f} MiB -> {out_pack}")


if __name__ == "__main__":
    main()

