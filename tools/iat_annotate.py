#!/usr/bin/env python3
"""iat_annotate - name the runtime IAT of a captured image (EXP-002, option A).

Input: a runtime_dump capture directory (main.bin + modules.json + meta.json).

Outputs (in the capture directory):
  iat.json  recovered IAT slots: VA -> "dll!function", grouped as the loader filled them
  iat.csv   "address,label" lines, ready to apply as labels in Ghidra/REA
  main.analysis.img  (only with --analysis-image) the memory image with *only* its section
            headers adjusted (PointerToRawData = VirtualAddress, FileAlignment = 0x1000) so a
            disassembler maps every byte at its runtime VA.

Policy (docs/DECISIONS.md D-005): this tool never writes an import table, never changes
the entry point and never produces a runnable executable. Import names stay in a sidecar
file. The analysis image keeps the packed header's entry point (the protection stub) and its
import directory unchanged, so it cannot be run as the game.

How IAT slots are recognised: null-terminated runs of dwords whose values are all export
addresses of modules loaded in the process (modules.json). Each run is attributed to the
module explaining most of its entries; forwarders (e.g. kernel32!HeapAlloc ->
ntdll!RtlAllocateHeap) are resolved back to the forwarding name.

Usage:
    python tools/iat_annotate.py CAPTURE_DIR [--analysis-image]
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

SECT_ALIGN = 0x1000


def _norm(dll: str) -> str:
    dll = dll.lower()
    return dll[:-4] if dll.endswith(".dll") else dll


def build_address_map(modules: list[dict]) -> dict[int, list[tuple[str, str | int, bool]]]:
    """Map absolute address -> [(module_name, name_or_ordinal, is_forward_alias)]."""
    by_name = {_norm(m["name"]): m for m in modules}
    resolved: dict[tuple[str, str | int], int] = {}
    amap: dict[int, list] = {}
    for m in modules:
        for e in m.get("exports", []):
            if "forwarder" in e:
                continue
            addr = m["base"] + e["rva"]
            for k in e["names"] or [e["ordinal"]]:
                amap.setdefault(addr, []).append((m["name"], k, False))
                resolved[(_norm(m["name"]), k)] = addr
            resolved[(_norm(m["name"]), e["ordinal"])] = addr
    for m in modules:
        for e in m.get("exports", []):
            fwd = e.get("forwarder")
            if not fwd or "." not in fwd:
                continue
            target_dll, func = fwd.rsplit(".", 1)
            key: str | int = int(func[1:]) if func.startswith("#") and func[1:].isdigit() else func
            candidates = [_norm(target_dll)]
            if candidates[0].startswith(("api-ms-", "ext-ms-")):
                candidates += ["kernelbase", "ntdll", "ucrtbase"] + list(by_name)
            addr = next((resolved[(c, key)] for c in candidates if (c, key) in resolved), None)
            if addr is None:
                continue
            for k in e["names"] or [e["ordinal"]]:
                amap.setdefault(addr, []).append((m["name"], k, True))
    return amap


def find_iat_groups(image: bytes, amap: dict, min_len: int = 1) -> list[dict]:
    """Null-terminated runs of dwords that are all known export addresses."""
    n = len(image) // 4
    words = struct.unpack_from(f"<{n}I", image)
    groups, i = [], 0
    while i < n:
        if words[i] in amap and (i == 0 or words[i - 1] == 0):
            j = i
            while j < n and words[j] in amap:
                j += 1
            if j < n and words[j] == 0 and j - i >= min_len:
                groups.append({"rva": 4 * i, "values": list(words[i:j])})
            i = j
        else:
            i += 1
    return groups


def cluster_groups(groups: list[dict], max_gap: int = 8) -> list[list[dict]]:
    """Cluster groups separated by at most `max_gap` bytes (their null terminators)."""
    clusters: list[list[dict]] = []
    for g in groups:
        if clusters:
            last = clusters[-1][-1]
            if g["rva"] - (last["rva"] + 4 * len(last["values"])) <= max_gap:
                clusters[-1].append(g)
                continue
        clusters.append([g])
    return clusters


def attribute_group(values: list[int], amap: dict) -> tuple[str, list[str | int], int]:
    """Pick the DLL explaining most entries; return (dll, names, unexplained_count)."""
    score: dict[str, list[int]] = {}
    for v in values:
        for mod in {mod for mod, _k, _a in amap[v]}:
            direct = any(mm == mod and not a for mm, _k, a in amap[v])
            s = score.setdefault(mod, [0, 0])
            s[0] += 1
            s[1] += direct
    dll = max(score, key=lambda m: tuple(score[m]))
    names, missing = [], 0
    for v in values:
        cands = [k for mm, k, _a in amap[v] if mm == dll]
        if cands:
            names.append(cands[0])
        else:
            missing += 1
            names.append(f"{amap[v][0][0]}!{amap[v][0][1]}")
    return dll, names, missing


def annotate(image: bytes, image_base: int, modules: list[dict]) -> dict:
    amap = build_address_map(modules)
    clusters = cluster_groups(find_iat_groups(image, amap))
    out = {"image_base": image_base, "clusters": []}
    for c in clusters:
        groups = []
        for g in c:
            dll, names, missing = attribute_group(g["values"], amap)
            groups.append({
                "dll": dll, "iat_va": image_base + g["rva"], "unexplained": missing,
                "slots": [{"va": image_base + g["rva"] + 4 * k, "name": nm if isinstance(nm, str) else f"#{nm}"}
                          for k, nm in enumerate(names)],
            })
        out["clusters"].append({"start_va": image_base + c[0]["rva"],
                                "entries": sum(len(g["values"]) for g in c), "groups": groups})
    out["clusters"].sort(key=lambda c: -c["entries"])
    return out


def analysis_image(image: bytes) -> bytes:
    """Adjust only section raw pointers/sizes so offset == RVA for disassemblers."""
    img = bytearray(image)
    (e_lfanew,) = struct.unpack_from("<I", img, 0x3C)
    nsect = struct.unpack_from("<H", img, e_lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", img, e_lfanew + 20)[0]
    opt = e_lfanew + 24
    tab = opt + opt_size
    for i in range(nsect):
        off = tab + 40 * i
        vsize, va = struct.unpack_from("<II", img, off + 8)
        raw = min((vsize + SECT_ALIGN - 1) // SECT_ALIGN * SECT_ALIGN, len(img) - va)
        struct.pack_into("<II", img, off + 16, raw, va)
    struct.pack_into("<I", img, opt + 36, SECT_ALIGN)  # FileAlignment
    return bytes(img)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Name the runtime IAT of a captured image")
    ap.add_argument("capture_dir", type=Path)
    ap.add_argument("--analysis-image", action="store_true",
                    help="also write main.analysis.img (section headers adjusted for disassemblers)")
    args = ap.parse_args(argv)
    d = args.capture_dir
    meta = json.loads((d / "meta.json").read_text(encoding="utf-8"))
    modules = json.loads((d / "modules.json").read_text(encoding="utf-8"))
    image = (d / "main.bin").read_bytes()
    result = annotate(image, meta["main"]["base"], modules)
    (d / "iat.json").write_text(json.dumps(result, indent=1) + "\n", encoding="utf-8", newline="\n")
    best = result["clusters"][0] if result["clusters"] else None
    lines = []
    if best:
        for g in best["groups"]:
            for s in g["slots"]:
                lines.append(f"0x{s['va']:08x},{g['dll'].replace('.', '_')}::{s['name']}")
    (d / "iat.csv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    if args.analysis_image:
        (d / "main.analysis.img").write_bytes(analysis_image(image))
    for c in result["clusters"][:5]:
        print(f"cluster @0x{c['start_va']:08x}: {c['entries']} entries in {len(c['groups'])} groups")
    if best:
        for g in best["groups"]:
            print(f"  {g['dll']:<22} iat=0x{g['iat_va']:08x} n={len(g['slots'])} unexplained={g['unexplained']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
