#!/usr/bin/env python3
"""package_release - build the player release zip (D-009).

Contents of dist/Hitman2Enhanced-<version>.zip:
  d3d8.dll       the proxy (build/Release/d3d8.dll); must contain the version string of src/version.h
  h2e.ini        default configuration: release defaults below, log level 1
  README.txt     docs/INSTALL.md
  CHANGELOG.md
The same four files are also written unpacked to <out>/payload/ (input of installer/Hitman2Enhanced.iss), and the
version to <out>/version.txt.

Usage:
    python tools/package_release.py [--dll build/Release/d3d8.dll] [--out dist]
"""
from __future__ import annotations

import argparse
import hashlib
import re
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import d3d8_logger_install as installer  # noqa: E402  (shares the h2e.ini format)

# Fixes enabled in the shipped h2e.ini: only fixes tested in a mission (docs/FIXES.md). hdfonts stays off: it needs a
# pack built from the player's own game files (tools/hdfonts_build.py), which is never shipped.
RELEASE_FIXES = ["resolution", "menuscale", "framelimit", "timer", "settings"]


def read_version(repo: Path = REPO) -> str:
    text = (repo / "src" / "version.h").read_text(encoding="utf-8")
    m = re.search(r'#define H2E_VERSION "(\d+\.\d+\.\d+)"', text)
    if not m:
        raise SystemExit("src/version.h: H2E_VERSION not found")
    return m.group(1)


def build(dll: Path, out_dir: Path, repo: Path = REPO) -> Path:
    version = read_version(repo)
    data = dll.read_bytes()
    if f"Hitman 2 Enhanced {version}".encode() not in data:
        raise SystemExit(f"{dll} was not built from version {version} (rebuild first)")
    changelog = (repo / "CHANGELOG.md").read_text(encoding="utf-8")
    if f"## {version}" not in changelog:
        raise SystemExit(f"CHANGELOG.md has no '## {version}' section")
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / f"Hitman2Enhanced-{version}.zip"
    ini = installer.config_text(RELEASE_FIXES, 1, f"release {version} defaults")
    readme = (repo / "docs" / "INSTALL.md").read_text(encoding="utf-8")
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("d3d8.dll", data)
        z.writestr("h2e.ini", ini)
        z.writestr("README.txt", readme)
        z.writestr("CHANGELOG.md", changelog)
    # The same files, unpacked, for the installer (installer/Hitman2Enhanced.iss, /DPayloadDir=dist\payload).
    payload = out_dir / "payload"
    payload.mkdir(exist_ok=True)
    (payload / "d3d8.dll").write_bytes(data)
    (payload / "h2e.ini").write_bytes(ini.encode("ascii"))
    (payload / "README.txt").write_bytes(readme.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
    (payload / "CHANGELOG.md").write_bytes(changelog.encode("utf-8"))
    (out_dir / "version.txt").write_text(version, encoding="ascii")
    return target


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Build the release zip")
    ap.add_argument("--dll", type=Path, default=REPO / "build" / "Release" / "d3d8.dll")
    ap.add_argument("--out", type=Path, default=REPO / "dist")
    args = ap.parse_args(argv)
    target = build(args.dll, args.out)
    print(f"wrote {target} (sha256 {hashlib.sha256(target.read_bytes()).hexdigest()})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
