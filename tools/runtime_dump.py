#!/usr/bin/env python3
"""runtime_dump - read-only capture of a running hitman2.exe (EXP-002, option A).

Windows only, standard library only (ctypes). The game must be started the normal
way, through Steam; this tool never launches the game and never intervenes in its
startup. It attaches to the already-running process with
PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ only (no write, suspend, debug
or terminate rights) and copies memory.

Sequence:
  1. Find the running process by image name; verify the on-disk image's SHA-256
     against a known profile (refuse unknown builds).
  2. Optionally wait until a marker module (default RenderD3D.dll) is loaded.
  3. Copy the main image and the marker module region by region, and record every
     loaded 32-bit module with its export table (used by iat_annotate.py).

Outputs go to OUT_DIR (keep it under samples/local/, which is git-ignored):
  main.bin, <marker>.bin   raw memory images starting at each module base (RVA == offset)
  modules.json             loaded modules: name, path, base, size, exports
  meta.json                profile, timings, region maps, SHA-256 of every capture

The process keeps running while it is read, so a capture is not an atomic
snapshot: code pages are stable, data may change between regions.
Policy (docs/DECISIONS.md D-005): no standalone executable is produced from these
captures, and they are never committed or distributed.

Usage:
    python tools/runtime_dump.py OUT_DIR [--marker RenderD3D.dll] [--wait 120]
"""
from __future__ import annotations

import argparse
import ctypes
import datetime as _dt
import hashlib
import json
import sys
import time
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pe_inspect  # noqa: E402

# Hash-pinned profiles. Addresses are only valid for the exact binary hash.
PROFILES = {
    "f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136": {
        "name": "hitman2.exe 1.0.0.277 (Steam, packed)",
        "image_base": 0x00400000,
        "size_of_image": 0x002EC000,
        # The entry stub writes its second stage's return value here before `jmp eax`.
        # EXP-002 showed it stays 0 at runtime; recorded as an observation only.
        "oep_slot_va": 0x006EB903,
    },
}

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
PROCESS_VM_READ = 0x0010
LIST_MODULES_32BIT = 0x01
MEM_COMMIT = 0x1000
PAGE_NOACCESS = 0x01
PAGE_GUARD = 0x100


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wintypes.DWORD), ("PartitionId", wintypes.WORD),
                ("RegionSize", ctypes.c_size_t), ("State", wintypes.DWORD),
                ("Protect", wintypes.DWORD), ("Type", wintypes.DWORD)]


class MODULEINFO(ctypes.Structure):
    _fields_ = [("lpBaseOfDll", ctypes.c_void_p), ("SizeOfImage", wintypes.DWORD),
                ("EntryPoint", ctypes.c_void_p)]


def _api():
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    k32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                      ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    k32.VirtualQueryEx.restype = ctypes.c_size_t
    k32.VirtualQueryEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
                                   ctypes.POINTER(MEMORY_BASIC_INFORMATION), ctypes.c_size_t]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    k32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                               ctypes.POINTER(wintypes.DWORD)]
    psapi.EnumProcesses.argtypes = [ctypes.POINTER(wintypes.DWORD), wintypes.DWORD,
                                    ctypes.POINTER(wintypes.DWORD)]
    psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p),
                                           wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
                                           wintypes.DWORD]
    psapi.GetModuleBaseNameW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.LPWSTR, wintypes.DWORD]
    psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.LPWSTR, wintypes.DWORD]
    psapi.GetModuleInformation.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
                                           ctypes.POINTER(MODULEINFO), wintypes.DWORD]
    return k32, psapi


class Process:
    """Read-only handle to another process."""

    def __init__(self, pid: int):
        self.k32, self.psapi = _api()
        access = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ  # read-only by construction
        self.h = self.k32.OpenProcess(access, False, pid)
        if not self.h:
            raise OSError(ctypes.get_last_error(), "OpenProcess failed")

    def close(self):
        self.k32.CloseHandle(self.h)

    def image_path(self) -> str:
        buf = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(1024)
        if not self.k32.QueryFullProcessImageNameW(self.h, 0, buf, ctypes.byref(size)):
            raise OSError(ctypes.get_last_error(), "QueryFullProcessImageNameW failed")
        return buf.value

    def read(self, addr: int, size: int) -> bytes | None:
        buf = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t(0)
        ok = self.k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, size, ctypes.byref(got))
        return buf.raw[:got.value] if ok and got.value == size else None

    def u32(self, addr: int) -> int | None:
        raw = self.read(addr, 4)
        return int.from_bytes(raw, "little") if raw else None

    def regions(self, start: int, size: int):
        addr, end = start, start + size
        mbi = MEMORY_BASIC_INFORMATION()
        while addr < end:
            if not self.k32.VirtualQueryEx(self.h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
                break
            base = mbi.BaseAddress or 0
            rend = min(base + mbi.RegionSize, end)
            yield addr, rend - addr, mbi.State, mbi.Protect
            addr = rend

    def dump_range(self, start: int, size: int) -> tuple[bytes, list[dict]]:
        """Read [start, start+size); unreadable pages become zeros and are reported."""
        image = bytearray(size)
        region_map = []
        for addr, length, state, protect in self.regions(start, size):
            readable = state == MEM_COMMIT and not (protect & (PAGE_NOACCESS | PAGE_GUARD))
            entry = {"rva": addr - start, "size": length, "state": f"0x{state:x}",
                     "protect": f"0x{protect:x}", "read": False}
            if readable:
                data = self.read(addr, length)
                if data is None:  # fall back to page granularity
                    buf = bytearray(length)
                    ok = 0
                    for off in range(0, length, 0x1000):
                        page = self.read(addr + off, min(0x1000, length - off))
                        if page:
                            buf[off:off + len(page)] = page
                            ok += 1
                    entry["pages_read"] = ok
                    data = bytes(buf)
                else:
                    entry["read"] = True
                image[addr - start:addr - start + length] = data
            region_map.append(entry)
        return bytes(image), region_map

    def modules(self) -> list[dict]:
        arr = (ctypes.c_void_p * 1024)()
        needed = wintypes.DWORD(0)
        if not self.psapi.EnumProcessModulesEx(self.h, arr, ctypes.sizeof(arr), ctypes.byref(needed),
                                               LIST_MODULES_32BIT):
            return []
        out = []
        name = ctypes.create_unicode_buffer(512)
        path = ctypes.create_unicode_buffer(1024)
        for hmod in arr[:needed.value // ctypes.sizeof(ctypes.c_void_p)]:
            info = MODULEINFO()
            if not self.psapi.GetModuleInformation(self.h, hmod, ctypes.byref(info), ctypes.sizeof(info)):
                continue
            self.psapi.GetModuleBaseNameW(self.h, hmod, name, 512)
            self.psapi.GetModuleFileNameExW(self.h, hmod, path, 1024)
            out.append({"name": name.value, "path": path.value,
                        "base": info.lpBaseOfDll or 0, "size": info.SizeOfImage})
        return out


def module_exports(proc: Process, mod: dict) -> list[dict]:
    """Read a loaded module and return its export entries (pe_inspect format)."""
    image, _ = proc.dump_range(mod["base"], mod["size"])
    try:
        rep = pe_inspect.inspect_bytes(image, mapped=True)
    except pe_inspect.PEFormatError:
        return []
    return [{k: f[k] for k in ("ordinal", "names", "rva", "forwarder") if k in f}
            for f in (rep["exports"] or {}).get("functions", [])]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def find_pids(image_name: str) -> list[int]:
    """PIDs whose image file name matches `image_name` (case-insensitive)."""
    _k32, psapi = _api()
    arr = (wintypes.DWORD * 4096)()
    needed = wintypes.DWORD(0)
    psapi.EnumProcesses(arr, ctypes.sizeof(arr), ctypes.byref(needed))
    out = []
    for pid in arr[:needed.value // 4]:
        if not pid:
            continue
        try:
            proc = Process(pid)
        except OSError:
            continue
        try:
            if Path(proc.image_path()).name.lower() == image_name.lower():
                out.append(pid)
        except OSError:
            pass
        finally:
            proc.close()
    return out


def _write_json(path: Path, obj) -> None:
    path.write_text(json.dumps(obj, indent=1) + "\n", encoding="utf-8", newline="\n")


def attach_capture(out_dir: Path, image_name: str = "hitman2.exe", marker: str | None = "RenderD3D.dll",
                   wait: float = 120.0, log=print) -> dict:
    deadline = time.perf_counter() + wait
    pids: list[int] = []
    while not pids and time.perf_counter() < deadline:
        pids = find_pids(image_name)
        if not pids:
            time.sleep(0.5)
    if len(pids) != 1:
        raise SystemExit(f"expected exactly one running {image_name}, found {pids}")
    proc = Process(pids[0])
    try:
        exe = Path(proc.image_path())
        exe_hash = sha256(exe.read_bytes())
        profile = PROFILES.get(exe_hash)
        if profile is None:
            raise SystemExit(f"refusing: unknown executable hash {exe_hash} ({exe})")
        meta: dict = {"tool": "runtime_dump 0.2.0 (attach, read-only)", "pid": pids[0], "exe": str(exe),
                      "exe_sha256": exe_hash, "profile": profile["name"],
                      "attached": _dt.datetime.now().astimezone().isoformat()}
        log(f"attached read-only to pid {pids[0]} ({exe})")
        if marker:
            while time.perf_counter() < deadline:
                if any(m["name"].lower() == marker.lower() for m in proc.modules()):
                    break
                time.sleep(0.25)
            else:
                log(f"warning: {marker} not loaded before timeout")
        out_dir.mkdir(parents=True, exist_ok=False)
        mods = proc.modules()
        main = next(m for m in mods if m["base"] == profile["image_base"])
        image, regions = proc.dump_range(main["base"], main["size"])
        (out_dir / "main.bin").write_bytes(image)
        meta["main"] = {"name": main["name"], "base": main["base"], "size": main["size"],
                        "sha256": sha256(image), "regions": regions}
        meta["oep_slot_va"] = profile["oep_slot_va"]
        meta["oep_slot_value"] = int.from_bytes(image[profile["oep_slot_va"] - main["base"]:][:4], "little")
        marker_now = next((m for m in mods if marker and m["name"].lower() == marker.lower()), None)
        if marker_now:
            mimg, mregions = proc.dump_range(marker_now["base"], marker_now["size"])
            fname = f"{marker_now['name']}.bin"
            (out_dir / fname).write_bytes(mimg)
            meta["marker"] = {"name": marker_now["name"], "file": fname, "base": marker_now["base"],
                              "size": marker_now["size"], "sha256": sha256(mimg), "regions": mregions}
        for m in mods:
            m["exports"] = module_exports(proc, m) if m is not main else []
        _write_json(out_dir / "modules.json", mods)
        meta["finished"] = _dt.datetime.now().astimezone().isoformat()
        _write_json(out_dir / "meta.json", meta)
        log(f"captured main image ({len(image)} bytes) and {len(mods)} module records")
        return meta
    finally:
        proc.close()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Read-only capture of a running hitman2.exe")
    ap.add_argument("out_dir", type=Path, help="new directory under samples/local/")
    ap.add_argument("--image", default="hitman2.exe", help="process image name to attach to")
    ap.add_argument("--marker", default="RenderD3D.dll", help="wait for this module first ('' = don't wait)")
    ap.add_argument("--wait", type=float, default=120.0, help="seconds to wait for the process/marker")
    args = ap.parse_args(argv)
    meta = attach_capture(args.out_dir, args.image, args.marker or None, args.wait)
    print(json.dumps({k: v for k, v in meta.items() if k not in ("main", "marker")}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
