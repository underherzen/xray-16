#!/usr/bin/env python3
"""Generate files.json -- the file index the wasm engine's CLocatorAPI uses
instead of scanning directories.

Usage: make_index.py <game_root> <out_json>

The index is a flat list of [relative_path, size, mtime] entries. Only the
entries the game itself needs are included (see INCLUDE); dev leftovers in
the layout dir (native builds, run scripts, logs) are skipped. Symlinks are
followed so the layout's links into the Windows game install are indexed.
"""

import json
import os
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


def main() -> None:
    root, out = sys.argv[1], sys.argv[2]
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


if __name__ == "__main__":
    main()
