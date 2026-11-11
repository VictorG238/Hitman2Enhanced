#!/usr/bin/env python3
"""pe_inspect - read-only inspector for Windows PE files (EXE/DLL).

Standard library only. Never writes to the inspected files.

Extracts: SHA-256, COFF/optional headers, sections (with entropy), data
directories, imports (incl. IAT addresses), delay imports, exports, resource
tree, VS_VERSIONINFO, debug directory (CodeView/PDB path), Rich header and the
PE checksum (stored vs. computed).

Usage:
    python tools/pe_inspect.py FILE_OR_DIR [...] [--json] [--output-dir DIR]
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import math
import struct
import sys
from array import array
from pathlib import Path

TOOL_VERSION = "0.1.0"


class PEFormatError(ValueError):
    """The file is not a structurally valid PE image."""


MACHINES = {0x14C: "i386", 0x8664: "AMD64", 0x1C0: "ARM", 0xAA64: "ARM64", 0x200: "IA64"}
SUBSYSTEMS = {
    1: "NATIVE", 2: "WINDOWS_GUI", 3: "WINDOWS_CUI", 5: "OS2_CUI", 7: "POSIX_CUI",
    9: "WINDOWS_CE_GUI", 10: "EFI_APPLICATION", 14: "XBOX", 16: "WINDOWS_BOOT_APPLICATION",
}
FILE_CHARACTERISTICS = {
    0x0001: "RELOCS_STRIPPED", 0x0002: "EXECUTABLE_IMAGE", 0x0004: "LINE_NUMS_STRIPPED",
    0x0008: "LOCAL_SYMS_STRIPPED", 0x0020: "LARGE_ADDRESS_AWARE", 0x0100: "32BIT_MACHINE",
    0x0200: "DEBUG_STRIPPED", 0x1000: "SYSTEM", 0x2000: "DLL",
}
DLL_CHARACTERISTICS = {
    0x0020: "HIGH_ENTROPY_VA", 0x0040: "DYNAMIC_BASE", 0x0080: "FORCE_INTEGRITY",
    0x0100: "NX_COMPAT", 0x0200: "NO_ISOLATION", 0x0400: "NO_SEH", 0x0800: "NO_BIND",
    0x1000: "APPCONTAINER", 0x2000: "WDM_DRIVER", 0x4000: "GUARD_CF",
    0x8000: "TERMINAL_SERVER_AWARE",
}
SECTION_FLAGS = {
    0x00000020: "CODE", 0x00000040: "INITIALIZED_DATA", 0x00000080: "UNINITIALIZED_DATA",
    0x02000000: "DISCARDABLE", 0x10000000: "SHARED", 0x20000000: "EXECUTE",
    0x40000000: "READ", 0x80000000: "WRITE",
}
DATA_DIRECTORY_NAMES = [
    "EXPORT", "IMPORT", "RESOURCE", "EXCEPTION", "SECURITY", "BASERELOC", "DEBUG",
    "ARCHITECTURE", "GLOBALPTR", "TLS", "LOAD_CONFIG", "BOUND_IMPORT", "IAT",
    "DELAY_IMPORT", "COM_DESCRIPTOR", "RESERVED",
]
RESOURCE_TYPES = {
    1: "CURSOR", 2: "BITMAP", 3: "ICON", 4: "MENU", 5: "DIALOG", 6: "STRING",
    7: "FONTDIR", 8: "FONT", 9: "ACCELERATOR", 10: "RCDATA", 11: "MESSAGETABLE",
    12: "GROUP_CURSOR", 14: "GROUP_ICON", 16: "VERSION", 17: "DLGINCLUDE",
    19: "PLUGPLAY", 20: "VXD", 21: "ANICURSOR", 22: "ANIICON", 23: "HTML", 24: "MANIFEST",
}
DEBUG_TYPES = {
    0: "UNKNOWN", 1: "COFF", 2: "CODEVIEW", 3: "FPO", 4: "MISC", 5: "EXCEPTION",
    6: "FIXUP", 7: "OMAP_TO_SRC", 8: "OMAP_FROM_SRC", 9: "BORLAND", 11: "CLSID",
    12: "VC_FEATURE", 13: "POGO", 14: "ILTCG", 16: "REPRO", 20: "EX_DLLCHARACTERISTICS",
}

PE32_MAGIC = 0x10B
PE32PLUS_MAGIC = 0x20B
VS_FIXEDFILEINFO_SIGNATURE = 0xFEEF04BD
RICH_DANS = 0x536E6144  # b"DanS" read as little-endian uint32

# Safety caps against malformed / hostile input.
MAX_SECTIONS = 96
MAX_IMPORT_DESCRIPTORS = 4096
MAX_THUNKS = 65536
MAX_EXPORTS = 65536
MAX_RESOURCE_ENTRIES = 65536


def _flags(value: int, table: dict[int, str]) -> list[str]:
    return [name for bit, name in table.items() if value & bit]


def _timestamp(ts: int) -> str | None:
    if ts in (0, 0xFFFFFFFF):
        return None
    return _dt.datetime.fromtimestamp(ts, tz=_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _entropy(blob: bytes) -> float:
    if not blob:
        return 0.0
    counts = [0] * 256
    for b in blob:
        counts[b] += 1
    n = len(blob)
    return round(-sum(c / n * math.log2(c / n) for c in counts if c), 4)


def _rol32(value: int, shift: int) -> int:
    shift %= 32
    value &= 0xFFFFFFFF
    return ((value << shift) | (value >> (32 - shift))) & 0xFFFFFFFF


def compute_pe_checksum(data: bytes, checksum_offset: int) -> int:
    """Standard PE image checksum (as computed by CheckSumMappedFile)."""
    padded = data + b"\0" if len(data) % 2 else data
    words = array("H")
    words.frombytes(padded)
    if sys.byteorder != "little":
        words.byteswap()
    total = sum(words)
    i = checksum_offset // 2
    total -= words[i] + words[i + 1]
    while total > 0xFFFF:
        total = (total & 0xFFFF) + (total >> 16)
    return (total + len(data)) & 0xFFFFFFFF


class _PE:
    def __init__(self, data: bytes, mapped: bool = False):
        self.data = data
        self.mapped = mapped  # True: data is a memory image (RVA == offset)
        self.warnings: list[str] = []
        self.sections: list[dict] = []
        self.size_of_headers = 0

    # -- low level reads -------------------------------------------------
    def unpack(self, fmt: str, off: int) -> tuple:
        size = struct.calcsize(fmt)
        if off < 0 or off + size > len(self.data):
            raise PEFormatError(f"read of {size} bytes at 0x{off:x} past end of file")
        return struct.unpack_from(fmt, self.data, off)

    def cstring(self, off: int, limit: int = 4096) -> str:
        if off is None or off < 0 or off >= len(self.data):
            raise PEFormatError(f"string offset {off!r} outside file")
        end = self.data.find(b"\0", off, off + limit)
        if end < 0:
            raise PEFormatError(f"unterminated string at 0x{off:x}")
        return self.data[off:end].decode("latin-1")

    def rva_to_offset(self, rva: int) -> int | None:
        if self.mapped:
            return rva if 0 <= rva < len(self.data) else None
        if rva < self.size_of_headers:
            return rva if rva < len(self.data) else None
        for s in self.sections:
            va, raw_size = s["virtual_address"], s["raw_size"]
            span = max(s["virtual_size"], raw_size)
            if va <= rva < va + span:
                delta = rva - va
                if delta >= raw_size:
                    return None  # zero-filled, not backed by the file
                return (s["raw_pointer"] & ~0x1FF) + delta
        return None

    def must_offset(self, rva: int, what: str) -> int:
        off = self.rva_to_offset(rva)
        if off is None:
            raise PEFormatError(f"{what}: RVA 0x{rva:x} not mapped to file data")
        return off

    def rva_cstring(self, rva: int, what: str) -> str:
        return self.cstring(self.must_offset(rva, what))


def _parse_rich(pe: _PE, e_lfanew: int) -> dict | None:
    data = pe.data
    rich = data.find(b"Rich", 0x40, e_lfanew)
    if rich < 0 or rich % 4:
        return None
    (key,) = pe.unpack("<I", rich + 4)
    start = None
    pos = rich - 4
    while pos >= 0x40:
        (v,) = pe.unpack("<I", pos)
        if v ^ key == RICH_DANS:
            start = pos
            break
        pos -= 4
    if start is None:
        return None
    entries = []
    for off in range(start + 16, rich, 8):
        comp_id, count = (x ^ key for x in pe.unpack("<II", off))
        entries.append({"prod_id": comp_id >> 16, "build": comp_id & 0xFFFF, "count": count})
    checksum = start
    for i in range(start):
        if 0x3C <= i < 0x40:
            continue
        checksum = (checksum + _rol32(data[i], i)) & 0xFFFFFFFF
    for e in entries:
        comp_id = (e["prod_id"] << 16) | e["build"]
        checksum = (checksum + _rol32(comp_id, e["count"])) & 0xFFFFFFFF
    return {
        "offset": start,
        "key": f"0x{key:08x}",
        "checksum_valid": checksum == key,
        "entries": entries,
    }


def _parse_imports(pe: _PE, rva: int, pe32plus: bool, image_base: int) -> list[dict]:
    ord_flag = 1 << 63 if pe32plus else 1 << 31
    thunk_fmt, thunk_size = ("<Q", 8) if pe32plus else ("<I", 4)
    off = pe.must_offset(rva, "import directory")
    result = []
    for i in range(MAX_IMPORT_DESCRIPTORS):
        oft, ts, fwd, name_rva, ft = pe.unpack("<IIIII", off + 20 * i)
        if not (oft or ts or fwd or name_rva or ft):
            break
        dll = pe.rva_cstring(name_rva, "import DLL name")
        lookup = oft or ft
        funcs = []
        toff = pe.must_offset(lookup, f"{dll} thunks")
        for j in range(MAX_THUNKS):
            (val,) = pe.unpack(thunk_fmt, toff + thunk_size * j)
            if val == 0:
                break
            iat_rva = ft + thunk_size * j
            entry = {"iat_rva": iat_rva, "iat_va": image_base + iat_rva}
            if val & ord_flag:
                entry["ordinal"] = val & 0xFFFF
            else:
                hn = pe.must_offset(val & 0x7FFFFFFF, f"{dll} hint/name")
                (entry["hint"],) = pe.unpack("<H", hn)
                entry["name"] = pe.cstring(hn + 2)
            funcs.append(entry)
        result.append({"dll": dll, "bound": ts == 0xFFFFFFFF, "functions": funcs})
    return result


def _parse_delay_imports(pe: _PE, rva: int, pe32plus: bool, image_base: int) -> list[dict]:
    ord_flag = 1 << 63 if pe32plus else 1 << 31
    thunk_fmt, thunk_size = ("<Q", 8) if pe32plus else ("<I", 4)
    off = pe.must_offset(rva, "delay import directory")
    result = []
    for i in range(MAX_IMPORT_DESCRIPTORS):
        attrs, name, hmod, iat, int_, biat, uiat, ts = pe.unpack("<8I", off + 32 * i)
        if not (attrs or name or iat or int_):
            break
        # Attribute bit 0 clear = legacy (VC6) descriptor holding VAs instead of RVAs.
        fix = (lambda v: v) if attrs & 1 else (lambda v: v - image_base if v else 0)
        dll = pe.rva_cstring(fix(name), "delay import DLL name")
        funcs = []
        toff = pe.must_offset(fix(int_), f"{dll} delay thunks")
        for j in range(MAX_THUNKS):
            (val,) = pe.unpack(thunk_fmt, toff + thunk_size * j)
            if val == 0:
                break
            iat_rva = fix(iat) + thunk_size * j
            entry = {"iat_rva": iat_rva, "iat_va": image_base + iat_rva}
            if val & ord_flag:
                entry["ordinal"] = val & 0xFFFF
            else:
                hn = pe.must_offset(fix(val & 0x7FFFFFFF), f"{dll} delay hint/name")
                (entry["hint"],) = pe.unpack("<H", hn)
                entry["name"] = pe.cstring(hn + 2)
            funcs.append(entry)
        result.append({"dll": dll, "rva_based": bool(attrs & 1), "functions": funcs})
    return result


def _parse_exports(pe: _PE, rva: int, size: int, image_base: int) -> dict:
    off = pe.must_offset(rva, "export directory")
    (_chars, ts, major, minor, name_rva, base, nfuncs, nnames,
     aof, aon, aono) = pe.unpack("<IIHHIIIIIII", off)
    if nfuncs > MAX_EXPORTS or nnames > MAX_EXPORTS:
        raise PEFormatError(f"implausible export counts ({nfuncs} functions, {nnames} names)")
    names: dict[int, list[str]] = {}
    if nnames:
        aon_off = pe.must_offset(aon, "export name table")
        aono_off = pe.must_offset(aono, "export ordinal table")
        for i in range(nnames):
            (nrva,) = pe.unpack("<I", aon_off + 4 * i)
            (idx,) = pe.unpack("<H", aono_off + 2 * i)
            names.setdefault(idx, []).append(pe.rva_cstring(nrva, "export name"))
    functions = []
    aof_off = pe.must_offset(aof, "export address table") if nfuncs else 0
    for idx in range(nfuncs):
        (frva,) = pe.unpack("<I", aof_off + 4 * idx)
        if frva == 0:
            continue
        entry = {"ordinal": base + idx, "names": names.get(idx, []), "rva": frva}
        if rva <= frva < rva + size:
            entry["forwarder"] = pe.rva_cstring(frva, "export forwarder")
        else:
            entry["va"] = image_base + frva
        functions.append(entry)
    return {
        "dll_name": pe.rva_cstring(name_rva, "export DLL name") if name_rva else None,
        "timestamp": ts,
        "timestamp_utc": _timestamp(ts),
        "version": f"{major}.{minor}",
        "ordinal_base": base,
        "functions": functions,
    }


def _parse_resources(pe: _PE, rva: int) -> list[dict]:
    base = pe.must_offset(rva, "resource directory")
    out: list[dict] = []
    visited: set[int] = set()

    def name_of(field: int):
        if field & 0x80000000:
            p = base + (field & 0x7FFFFFFF)
            (n,) = pe.unpack("<H", p)
            raw = pe.data[p + 2:p + 2 + 2 * n]
            return raw.decode("utf-16-le", errors="replace")
        return field

    def walk(rel: int, path: list):
        if rel in visited or len(path) > 2:
            return
        visited.add(rel)
        _c, _t, _ma, _mi, nnamed, nid = pe.unpack("<IIHHHH", base + rel)
        for i in range(nnamed + nid):
            if len(out) >= MAX_RESOURCE_ENTRIES:
                return
            nfield, dfield = pe.unpack("<II", base + rel + 16 + 8 * i)
            key = name_of(nfield)
            if dfield & 0x80000000:
                walk(dfield & 0x7FFFFFFF, path + [key])
                continue
            data_rva, size, codepage, _r = pe.unpack("<IIII", base + dfield)
            full = path + [key]
            rtype = full[0]
            out.append({
                "type": RESOURCE_TYPES.get(rtype, rtype) if isinstance(rtype, int) else rtype,
                "type_id": rtype if isinstance(rtype, int) else None,
                "name": full[1] if len(full) > 1 else None,
                "language": full[2] if len(full) > 2 else None,
                "rva": data_rva,
                "size": size,
                "codepage": codepage,
            })

    walk(0, [])
    return out


def _parse_version_block(blob: bytes, pos: int, depth: int = 0) -> tuple[dict, int]:
    if pos + 6 > len(blob) or depth > 8:
        raise PEFormatError("version block truncated")
    length, value_len, vtype = struct.unpack_from("<HHH", blob, pos)
    if length < 6:
        raise PEFormatError("version block with invalid length")
    end = min(pos + length, len(blob))
    p = pos + 6
    key_end = p
    while key_end + 1 < end and blob[key_end:key_end + 2] != b"\0\0":
        key_end += 2
    key = blob[p:key_end].decode("utf-16-le", errors="replace")
    p = (key_end + 2 + 3) & ~3
    vbytes = value_len * 2 if vtype == 1 else value_len
    value = blob[p:p + vbytes]
    p = (p + vbytes + 3) & ~3
    children = []
    while p < end:
        child, child_len = _parse_version_block(blob, p, depth + 1)
        children.append(child)
        p = (p + child_len + 3) & ~3
    return {"key": key, "type": vtype, "value": value, "children": children}, length


def _ver(ms: int, ls: int) -> str:
    return f"{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}"


def parse_version_info(blob: bytes) -> dict:
    root, _ = _parse_version_block(blob, 0)
    if root["key"] != "VS_VERSION_INFO":
        raise PEFormatError(f"unexpected version root key {root['key']!r}")
    info: dict = {"fixed": None, "strings": {}}
    val = root["value"]
    if len(val) >= 52:
        f = struct.unpack_from("<13I", val)
        if f[0] == VS_FIXEDFILEINFO_SIGNATURE:
            info["fixed"] = {
                "file_version": _ver(f[2], f[3]),
                "product_version": _ver(f[4], f[5]),
                "file_flags": f[7] & f[6],
                "file_os": f"0x{f[8]:x}",
                "file_type": f[9],
            }
    for child in root["children"]:
        if child["key"] != "StringFileInfo":
            continue
        for table in child["children"]:
            strings = {}
            for s in table["children"]:
                strings[s["key"]] = s["value"].decode("utf-16-le", errors="replace").rstrip("\0")
            info["strings"][table["key"]] = strings
    return info


def _parse_debug(pe: _PE, rva: int, size: int) -> list[dict]:
    off = pe.must_offset(rva, "debug directory")
    out = []
    for i in range(min(size // 28, 64)):
        _c, ts, maj, mn, dtype, dsize, draw_rva, draw_ptr = pe.unpack("<IIHHIIII", off + 28 * i)
        entry = {"type": DEBUG_TYPES.get(dtype, dtype), "timestamp": ts,
                 "timestamp_utc": _timestamp(ts), "size": dsize}
        if dtype == 2:
            p = pe.rva_to_offset(draw_rva) if pe.mapped else (draw_ptr or pe.rva_to_offset(draw_rva))
            if p is not None and p + 4 <= len(pe.data):
                sig = pe.data[p:p + 4]
                if sig == b"RSDS":
                    guid = pe.data[p + 4:p + 20]
                    (age,) = pe.unpack("<I", p + 20)
                    entry["codeview"] = {"format": "RSDS", "guid": guid.hex(), "age": age,
                                         "pdb_path": pe.cstring(p + 24)}
                elif sig == b"NB10":
                    _o, cvsig, age = pe.unpack("<III", p + 4)
                    entry["codeview"] = {"format": "NB10", "signature": f"0x{cvsig:08x}",
                                         "age": age, "pdb_path": pe.cstring(p + 16)}
        out.append(entry)
    return out


def inspect_bytes(data: bytes, mapped: bool = False) -> dict:
    """Parse a PE image and return a JSON-serialisable report.

    mapped=False: `data` is an on-disk file. mapped=True: `data` is a memory image
    (e.g. a process dump starting at the image base), where RVA == offset.
    """
    pe = _PE(data, mapped)
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise PEFormatError("missing MZ signature")
    (e_lfanew,) = pe.unpack("<I", 0x3C)
    if pe.unpack("<4s", e_lfanew)[0] != b"PE\0\0":
        raise PEFormatError(f"missing PE signature at e_lfanew=0x{e_lfanew:x}")
    coff_off = e_lfanew + 4
    machine, nsect, ts, _sym, _nsym, opt_size, chars = pe.unpack("<HHIIIHH", coff_off)
    if nsect > MAX_SECTIONS:
        raise PEFormatError(f"implausible section count {nsect}")
    opt_off = coff_off + 20
    (magic,) = pe.unpack("<H", opt_off)
    if magic == PE32_MAGIC:
        o = pe.unpack("<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII", opt_off)
        (_, lmaj, lmin, size_code, size_idata, size_udata, entry, base_code, base_data,
         image_base, sect_align, file_align, os_maj, os_min, img_maj, img_min, ss_maj, ss_min,
         _w32, size_image, size_headers, checksum, subsystem, dll_chars,
         stack_res, stack_com, heap_res, heap_com, _lf, n_rva) = o
        dd_off = opt_off + 96
    elif magic == PE32PLUS_MAGIC:
        o = pe.unpack("<HBBIIIIIQIIHHHHHHIIIIHHQQQQII", opt_off)
        (_, lmaj, lmin, size_code, size_idata, size_udata, entry, base_code,
         image_base, sect_align, file_align, os_maj, os_min, img_maj, img_min, ss_maj, ss_min,
         _w32, size_image, size_headers, checksum, subsystem, dll_chars,
         stack_res, stack_com, heap_res, heap_com, _lf, n_rva) = o
        base_data = None
        dd_off = opt_off + 112
    else:
        raise PEFormatError(f"unknown optional header magic 0x{magic:x}")
    pe32plus = magic == PE32PLUS_MAGIC
    pe.size_of_headers = size_headers

    directories = []
    for i in range(min(n_rva, 16)):
        drva, dsize = pe.unpack("<II", dd_off + 8 * i)
        directories.append({"index": i, "name": DATA_DIRECTORY_NAMES[i], "rva": drva, "size": dsize})

    sect_off = opt_off + opt_size
    raw_end = size_headers
    for i in range(nsect):
        (name, vsize, va, rsize, rptr, _pr, _pl, _nr, _nl,
         schars) = pe.unpack("<8sIIIIIIHHI", sect_off + 40 * i)
        if mapped:
            raw = data[va:va + max(vsize, rsize)]
        else:
            raw = data[rptr:rptr + rsize] if rsize else b""
        pe.sections.append({
            "name": name.rstrip(b"\0").decode("latin-1"),
            "virtual_address": va, "virtual_size": vsize,
            "raw_pointer": rptr, "raw_size": rsize,
            "characteristics": schars, "flags": _flags(schars, SECTION_FLAGS),
            "entropy": _entropy(raw),
        })
        if rsize and not mapped:
            raw_end = max(raw_end, rptr + rsize)

    report: dict = {
        "layout": "mapped" if mapped else "file",
        "file_size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "format": "PE32+" if pe32plus else "PE32",
        "coff": {
            "machine": MACHINES.get(machine, f"0x{machine:x}"),
            "number_of_sections": nsect,
            "timestamp": ts, "timestamp_utc": _timestamp(ts),
            "characteristics": chars, "flags": _flags(chars, FILE_CHARACTERISTICS),
        },
        "optional": {
            "linker_version": f"{lmaj}.{lmin}",
            "size_of_code": size_code, "size_of_initialized_data": size_idata,
            "size_of_uninitialized_data": size_udata,
            "entry_point_rva": entry, "entry_point_va": image_base + entry if entry else None,
            "base_of_code": base_code, "base_of_data": base_data,
            "image_base": image_base, "section_alignment": sect_align, "file_alignment": file_align,
            "os_version": f"{os_maj}.{os_min}", "image_version": f"{img_maj}.{img_min}",
            "subsystem_version": f"{ss_maj}.{ss_min}",
            "size_of_image": size_image, "size_of_headers": size_headers,
            "subsystem": SUBSYSTEMS.get(subsystem, subsystem),
            "dll_characteristics": dll_chars,
            "dll_flags": _flags(dll_chars, DLL_CHARACTERISTICS),
            "stack_reserve": stack_res, "stack_commit": stack_com,
            "heap_reserve": heap_res, "heap_commit": heap_com,
        },
        "checksum": {
            "stored": checksum,
            "computed": compute_pe_checksum(data, opt_off + 64),
        },
        "data_directories": [d for d in directories if d["rva"] or d["size"]],
        "sections": pe.sections,
        "overlay": ({"offset": raw_end, "size": len(data) - raw_end}
                    if not mapped and len(data) > raw_end else None),
        "rich_header": None, "imports": [], "delay_imports": [], "exports": None,
        "resources": [], "version_info": None, "debug": [],
        "warnings": pe.warnings,
    }
    # A stored checksum of 0 means "not set" (normal for non-driver images).
    stored = report["checksum"]["stored"]
    report["checksum"]["matches"] = None if stored == 0 else stored == report["checksum"]["computed"]

    def guarded(label, fn):
        try:
            return fn()
        except (PEFormatError, struct.error, UnicodeDecodeError) as exc:
            pe.warnings.append(f"{label}: {exc}")
            return None

    report["rich_header"] = guarded("rich header", lambda: _parse_rich(pe, e_lfanew))
    dirs = {d["index"]: d for d in directories}

    def dir_present(i):
        d = dirs.get(i)
        return d is not None and d["rva"] != 0

    if dir_present(1):
        report["imports"] = guarded("imports", lambda: _parse_imports(
            pe, dirs[1]["rva"], pe32plus, image_base)) or []
    if dir_present(13):
        report["delay_imports"] = guarded("delay imports", lambda: _parse_delay_imports(
            pe, dirs[13]["rva"], pe32plus, image_base)) or []
    if dir_present(0):
        report["exports"] = guarded("exports", lambda: _parse_exports(
            pe, dirs[0]["rva"], dirs[0]["size"], image_base))
    if dir_present(2):
        report["resources"] = guarded("resources", lambda: _parse_resources(pe, dirs[2]["rva"])) or []
        for res in report["resources"]:
            if res["type_id"] == 16:
                def _vi(res=res):
                    off = pe.must_offset(res["rva"], "version resource")
                    return parse_version_info(data[off:off + res["size"]])
                report["version_info"] = guarded("version info", _vi)
                break
    if dir_present(6):
        report["debug"] = guarded("debug", lambda: _parse_debug(
            pe, dirs[6]["rva"], dirs[6]["size"])) or []
    return report


def inspect_file(path: str | Path, mapped: bool = False) -> dict:
    path = Path(path)
    with open(path, "rb") as fh:  # read-only by construction
        data = fh.read()
    report = {"tool": f"pe_inspect {TOOL_VERSION}", "path": str(path), "file_name": path.name}
    report.update(inspect_bytes(data, mapped))
    return report


# ---------------------------------------------------------------------------
# Presentation
# ---------------------------------------------------------------------------

def format_report(r: dict, show_functions: bool = True) -> str:
    o, c = r["optional"], r["coff"]
    lines = [
        f"== {r['file_name']} ==",
        f"  size        {r['file_size']} bytes",
        f"  sha256      {r['sha256']}",
        f"  format      {r['format']} {c['machine']}  subsystem={o['subsystem']}",
        f"  timestamp   {c['timestamp_utc']} (0x{c['timestamp']:08x})",
        f"  linker      {o['linker_version']}  os={o['os_version']}  subsystem_ver={o['subsystem_version']}",
        f"  image_base  0x{o['image_base']:x}  entry_rva=0x{o['entry_point_rva']:x}  size_of_image=0x{o['size_of_image']:x}",
        f"  flags       {', '.join(c['flags'])}",
        f"  dll_flags   {', '.join(o['dll_flags']) or '(none)'}",
        f"  checksum    stored=0x{r['checksum']['stored']:08x} computed=0x{r['checksum']['computed']:08x}"
        f" {({None: 'not set', True: 'OK', False: 'MISMATCH'})[r['checksum']['matches']]}",
    ]
    vi = r.get("version_info")
    if vi:
        if vi["fixed"]:
            lines.append(f"  version     file={vi['fixed']['file_version']} product={vi['fixed']['product_version']}")
        for table, strings in vi["strings"].items():
            for k, v in strings.items():
                lines.append(f"    [{table}] {k} = {v}")
    for d in r["debug"]:
        cv = d.get("codeview")
        if cv:
            lines.append(f"  pdb         {cv['pdb_path']} ({cv['format']})")
    rich = r.get("rich_header")
    if rich:
        lines.append(f"  rich        {len(rich['entries'])} entries, checksum_valid={rich['checksum_valid']}")
        for e in rich["entries"]:
            lines.append(f"    prod_id={e['prod_id']:>3} build={e['build']:>5} count={e['count']}")
    lines.append("  sections:")
    for s in r["sections"]:
        lines.append(
            f"    {s['name']:<8} va=0x{s['virtual_address']:08x} vsize=0x{s['virtual_size']:08x} "
            f"raw=0x{s['raw_pointer']:08x}+0x{s['raw_size']:08x} H={s['entropy']:.2f} {'|'.join(s['flags'])}")
    if r["overlay"]:
        lines.append(f"  overlay     offset=0x{r['overlay']['offset']:x} size={r['overlay']['size']}")
    lines.append("  data directories:")
    for d in r["data_directories"]:
        lines.append(f"    {d['name']:<15} rva=0x{d['rva']:08x} size=0x{d['size']:x}")

    def fn_label(f):
        return f.get("name") or f"#{f['ordinal']}"

    for label, key in (("imports", "imports"), ("delay imports", "delay_imports")):
        if r[key]:
            lines.append(f"  {label}: {len(r[key])} DLLs, {sum(len(i['functions']) for i in r[key])} functions")
            for imp in r[key]:
                lines.append(f"    {imp['dll']} ({len(imp['functions'])})")
                if show_functions:
                    for f in imp["functions"]:
                        lines.append(f"      0x{f['iat_va']:08x}  {fn_label(f)}")
    ex = r["exports"]
    if ex:
        lines.append(f"  exports: {ex['dll_name']} ({len(ex['functions'])} functions, base={ex['ordinal_base']})")
        if show_functions:
            for f in ex["functions"]:
                tgt = f"-> {f['forwarder']}" if "forwarder" in f else f"rva=0x{f['rva']:08x}"
                lines.append(f"    @{f['ordinal']:<5} {','.join(f['names']) or '(no name)'}  {tgt}")
    if r["resources"]:
        summary: dict = {}
        for res in r["resources"]:
            summary[str(res["type"])] = summary.get(str(res["type"]), 0) + 1
        lines.append("  resources:  " + ", ".join(f"{k}x{v}" for k, v in summary.items()))
    for w in r["warnings"]:
        lines.append(f"  WARNING     {w}")
    return "\n".join(lines)


def _json_default(obj):
    if isinstance(obj, bytes):
        return obj.hex()
    raise TypeError(type(obj).__name__)


def _expand(paths: list[str]) -> list[Path]:
    out = []
    for p in map(Path, paths):
        if p.is_dir():
            out.extend(sorted(x for x in p.iterdir()
                              if x.is_file() and x.suffix.lower() in (".exe", ".dll")))
        else:
            out.append(p)
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Read-only Windows PE inspector")
    ap.add_argument("paths", nargs="+", help="PE files, or directories (top-level *.exe/*.dll)")
    ap.add_argument("--json", action="store_true", help="print JSON instead of text")
    ap.add_argument("--output-dir", type=Path, help="also write <file>.json reports here")
    ap.add_argument("--summary", action="store_true", help="omit per-function listings in text output")
    ap.add_argument("--mapped", action="store_true",
                    help="inputs are memory images (process dumps), not on-disk files")
    args = ap.parse_args(argv)

    status = 0
    reports = []
    for path in _expand(args.paths):
        try:
            rep = inspect_file(path, args.mapped)
        except (OSError, PEFormatError) as exc:
            print(f"error: {path}: {exc}", file=sys.stderr)
            status = 1
            continue
        reports.append(rep)
        if args.output_dir:
            args.output_dir.mkdir(parents=True, exist_ok=True)
            (args.output_dir / f"{path.name}.json").write_text(
                json.dumps(rep, indent=2, default=_json_default), encoding="utf-8")
        if not args.json:
            print(format_report(rep, show_functions=not args.summary))
            print()
    if args.json:
        print(json.dumps(reports, indent=2, default=_json_default))
    return status


if __name__ == "__main__":
    sys.exit(main())
