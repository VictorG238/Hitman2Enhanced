#!/usr/bin/env python3
"""snapshot_game - copy the game's binaries/configs into a working snapshot.

Analysis must run on copies, never on the live install (see CONTRIBUTING.md).
The snapshot directory gets a MANIFEST.sha256 (sha256sum-compatible) and a
manifest.json with sizes and source mtimes. The source is only read.

Usage:
    python tools/snapshot_game.py GAME_DIR DEST_DIR
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import shutil
import sys
from pathlib import Path

PATTERNS = ("*.exe", "*.dll", "*.ini", "*.cfg", "*.dat", "*.vdf", "*.txt")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def snapshot(game_dir: Path, dest: Path) -> list[dict]:
    if dest.exists() and any(dest.iterdir()):
        raise FileExistsError(f"destination not empty: {dest}")
    dest.mkdir(parents=True, exist_ok=True)
    files = sorted({p for pat in PATTERNS for p in game_dir.glob(pat) if p.is_file()},
                   key=lambda p: p.name.lower())
    entries = []
    for src in files:
        before = sha256_file(src)
        dst = dest / src.name
        shutil.copy2(src, dst)
        after = sha256_file(dst)
        if before != after or sha256_file(src) != before:
            raise RuntimeError(f"{src.name} changed while copying; retry the snapshot")
        st = src.stat()
        entries.append({
            "name": src.name, "size": st.st_size, "sha256": after,
            "source_mtime": _dt.datetime.fromtimestamp(st.st_mtime).astimezone().isoformat(),
        })
    (dest / "MANIFEST.sha256").write_text(
        "".join(f"{e['sha256']} *{e['name']}\n" for e in entries),
        encoding="utf-8", newline="\n")  # LF on every OS: manifests are compared/diffed
    (dest / "manifest.json").write_text(json.dumps({
        "source": str(game_dir.resolve()),
        "created": _dt.datetime.now().astimezone().isoformat(),
        "files": entries,
    }, indent=2) + "\n", encoding="utf-8", newline="\n")
    return entries


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("game_dir", type=Path)
    ap.add_argument("dest", type=Path)
    args = ap.parse_args(argv)
    for e in snapshot(args.game_dir, args.dest):
        print(f"{e['sha256']}  {e['size']:>9}  {e['name']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
