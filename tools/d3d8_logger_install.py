#!/usr/bin/env python3
"""d3d8_logger_install - add/remove the Hitman 2 Enhanced proxy (d3d8.dll) in a game folder (developer tool).

Steam always runs the game from its install folder, so the proxy must sit next
to hitman2.exe while a test runs. This tool makes that reversible and safe:

  install    copy build/Release/d3d8.dll to GAME_DIR/d3d8.dll and write GAME_DIR/h2e.ini
             ([log] Level, one [<fix>] Enabled section per fix; --fix enables one, repeatable).
             Refuses if d3d8.dll or h2e.ini already exists, or if hitman2.exe has an unknown hash.
             With --fix hdfonts also copies the font pack (--hdfonts-pack, default samples/local/h2e_hdfonts.bin)
             to GAME_DIR/h2e_hdfonts.bin. Writes a marker file h2e.installed.json with the SHA-256 values.
  uninstall  remove d3d8.dll, h2e.ini (and h2e_hdfonts.bin) only if their hashes match the marker; move h2e.log
             and h2e_settings.ini (settings page choices) to LOG_DIR (default samples/local/logs/) with a
             timestamp; remove the marker.
  status     report what is installed.

Nothing else in the game folder is touched. Players use the release zip instead (docs/INSTALL.md).

Usage:
    python tools/d3d8_logger_install.py install   GAME_DIR [--dll build/Release/d3d8.dll] [--fix resolution] [--log-level 2]
                                                  [--hdfonts-pack samples/local/h2e_hdfonts.bin]
    python tools/d3d8_logger_install.py uninstall GAME_DIR [--log-dir samples/local/logs]
    python tools/d3d8_logger_install.py status    GAME_DIR
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
KNOWN_EXE = {"f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136": "hitman2.exe 1.0.0.277 (Steam)",
             "0d7742c85e328886e91152036ae6615ca4c98bbb97c7a36d8f9e150235e57560": "hitman2.exe 1.0.0.265 (retail)"}
MARKER = "h2e.installed.json"
LOG = "h2e.log"
CONFIG = "h2e.ini"
# Must match the fix ids in src/fixes/registry.cpp (checked by tests/test_fix_registry.py).
FIX_IDS = ["resolution", "horplus", "timer", "menuscale", "framelimit", "hdfonts", "settings"]
# Written by the settings fix at runtime (the player's choices); moved to LOG_DIR on uninstall.
USER_SETTINGS = "h2e_settings.ini"
# Data file of the hdfonts fix (built from the player's own game files by tools/hdfonts_build.py, never shipped).
HDFONTS_PACK = "h2e_hdfonts.bin"
DEFAULT_HDFONTS_PACK = REPO / "samples" / "local" / HDFONTS_PACK


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def game_running() -> bool:
    """True if a hitman2.exe process exists (its d3d8.dll is locked and h2e.log is open)."""
    if sys.platform != "win32":
        return False
    sys.path.insert(0, str(REPO / "tools"))
    import runtime_dump
    return bool(runtime_dump.find_pids("hitman2.exe"))


def _refuse_if_running() -> None:
    if game_running():
        raise SystemExit("refusing: hitman2.exe is running - close the game first (nothing was changed)")


def config_text(fixes: list[str], log_level: int = 1, header: str = "written by tools/d3d8_logger_install.py") -> str:
    lines = [f"; Hitman 2 Enhanced configuration - {header}", "[log]",
             "; 0 = no log, 1 = setup and fix outcomes, 2 = research logging (large)", f"Level={log_level}"]
    for fix in FIX_IDS:
        lines += ["", f"[{fix}]", f"Enabled={1 if fix in fixes else 0}"]
    return "\r\n".join(lines) + "\r\n"


def install(game: Path, dll: Path, fixes: list[str] | None = None, log_level: int = 2,
            hdfonts_pack: Path | None = None) -> str:
    _refuse_if_running()
    fixes = fixes or []
    unknown = sorted(set(fixes) - set(FIX_IDS))
    if unknown:
        raise SystemExit(f"unknown fix: {unknown}")
    if "hdfonts" in fixes:
        hdfonts_pack = hdfonts_pack or DEFAULT_HDFONTS_PACK
        if not hdfonts_pack.is_file():
            raise SystemExit(f"hdfonts needs its pack: {hdfonts_pack} missing (run tools/hdfonts_build.py)")
        if (game / HDFONTS_PACK).exists():
            raise SystemExit(f"refusing: {game / HDFONTS_PACK} already exists (not overwriting)")
    else:
        hdfonts_pack = None
    exe = game / "hitman2.exe"
    if not exe.is_file() or _sha(exe) not in KNOWN_EXE:
        raise SystemExit(f"refusing: {exe} missing or unknown hash")
    target = game / "d3d8.dll"
    if target.exists():
        raise SystemExit(f"refusing: {target} already exists (not overwriting)")
    config = game / CONFIG
    if config.exists():
        raise SystemExit(f"refusing: {config} already exists (not overwriting)")
    if not dll.is_file():
        raise SystemExit(f"proxy not built: {dll}")
    shutil.copy2(dll, target)
    digest = _sha(target)
    config.write_bytes(config_text(fixes, log_level).encode("ascii"))
    info = {"installed": _dt.datetime.now().astimezone().isoformat(), "source": str(dll), "sha256": digest,
            "config_sha256": _sha(config), "fixes": sorted(fixes), "log_level": log_level}
    if hdfonts_pack:
        shutil.copy2(hdfonts_pack, game / HDFONTS_PACK)
        info["hdfonts_pack_sha256"] = _sha(game / HDFONTS_PACK)
    (game / MARKER).write_text(json.dumps(info, indent=1) + "\n", encoding="utf-8", newline="\n")
    return (f"installed {target} ({digest}), log level {log_level}"
            + (f", fixes {sorted(fixes)}" if fixes else ", no fixes"))


def uninstall(game: Path, log_dir: Path) -> str:
    _refuse_if_running()
    target, marker = game / "d3d8.dll", game / MARKER
    if not marker.exists():
        raise SystemExit("refusing: no marker; d3d8.dll was not installed by this tool")
    info = json.loads(marker.read_text(encoding="utf-8"))
    pack = game / HDFONTS_PACK
    remove_pack = "hdfonts_pack_sha256" in info and pack.exists()
    if remove_pack and _sha(pack) != info["hdfonts_pack_sha256"]:
        raise SystemExit(f"refusing: {pack} was changed after install; remove it by hand")
    msgs = []
    if target.exists():
        if _sha(target) != info["sha256"]:
            raise SystemExit("refusing: d3d8.dll hash differs from the installed proxy")
        target.unlink()
        msgs.append(f"removed {target}")
    config = game / CONFIG
    if "config_sha256" in info and config.exists():
        if _sha(config) != info["config_sha256"]:
            raise SystemExit(f"refusing: {config} was changed after install; remove it by hand")
        config.unlink()
        msgs.append(f"removed {config}")
    if remove_pack:
        pack.unlink()
        msgs.append(f"removed {pack}")
    stamp = f"{_dt.datetime.now():%Y%m%d-%H%M%S}"
    log = game / LOG
    if log.exists():
        log_dir.mkdir(parents=True, exist_ok=True)
        dest = log_dir / f"h2e-{stamp}.log"
        shutil.move(str(log), dest)
        msgs.append(f"moved log to {dest}")
    user = game / USER_SETTINGS
    if user.exists():
        log_dir.mkdir(parents=True, exist_ok=True)
        dest = log_dir / f"h2e_settings-{stamp}.ini"
        shutil.move(str(user), dest)
        msgs.append(f"moved {USER_SETTINGS} to {dest}")
    marker.unlink()
    msgs.append("removed marker")
    return "; ".join(msgs)


def status(game: Path) -> str:
    target, marker = game / "d3d8.dll", game / MARKER
    if not target.exists():
        return "no d3d8.dll in game folder" + (" (stale marker present)" if marker.exists() else "")
    if not marker.exists():
        return "d3d8.dll present but NOT installed by this tool"
    info = json.loads(marker.read_text(encoding="utf-8"))
    ok = _sha(target) == info["sha256"]
    fixes = f", fixes {info['fixes']}" if info.get("fixes") else ""
    return ("proxy installed" + ("" if ok else " (HASH MISMATCH)") + fixes
            + (", log present" if (game / LOG).exists() else ""))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Install/remove the Hitman 2 Enhanced proxy (developer tool)")
    ap.add_argument("action", choices=["install", "uninstall", "status"])
    ap.add_argument("game_dir", type=Path)
    ap.add_argument("--dll", type=Path, default=REPO / "build" / "Release" / "d3d8.dll")
    ap.add_argument("--log-dir", type=Path, default=REPO / "samples" / "local" / "logs")
    ap.add_argument("--fix", action="append", choices=FIX_IDS, default=[], help="enable a fix (repeatable)")
    ap.add_argument("--log-level", type=int, choices=[0, 1, 2], default=2, help="h2e.ini [log] Level (default 2)")
    ap.add_argument("--hdfonts-pack", type=Path, default=None,
                    help=f"pack copied as {HDFONTS_PACK} with --fix hdfonts (default samples/local/{HDFONTS_PACK})")
    args = ap.parse_args(argv)
    if args.action == "install":
        print(install(args.game_dir, args.dll, args.fix, args.log_level, args.hdfonts_pack))
    elif args.action == "uninstall":
        print(uninstall(args.game_dir, args.log_dir))
    else:
        print(status(args.game_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
