#!/usr/bin/env python3
"""analyze_d3d8_log - summarize a d3d8_logger (>= 0.2.0) log (EXP-003).

Reports:
  * device creation / reset parameters
  * distinct viewports (with caller)
  * projection matrices grouped by (fovX, fovY, matrix aspect, zn, zf) rounded, with
    the active-viewport aspect paired to each, and whether matrix aspect == viewport aspect
  * frame-rate summary lines
  * (logger >= 0.4.0) screen-space draws per call site (union of the 5 s bounding boxes, draw counts)
    and the periodic projection samples per call site, grouped

Usage:
    python tools/analyze_d3d8_log.py LOG [--json OUT]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

RE_PROJ = re.compile(r"SetTransform\(PROJECTION\) #(\d+) caller=(\S+)")
RE_ACTIVE = re.compile(r"active viewport x=(\d+) y=(\d+) w=(\d+) h=(\d+) aspect\(w/h\)=([\d.]+)")
RE_LH = re.compile(r"fovY=([\d.]+) deg fovX=([\d.]+) deg aspect\(x/y\)=([\d.]+) zn=([-\d.]+) zf=([-\d.e+]+)")
RE_VP = re.compile(r"SetViewport x=(\d+) y=(\d+) w=(\d+) h=(\d+) .*caller=(\S+)")
RE_FPS = re.compile(r"Present: (\d+) frames in ([\d.]+)s = ([\d.]+) fps")
RE_CREATE = re.compile(r"(CreateDevice \(in\) |Reset \(in\) ) *(BackBuffer=.*)")
RE_SCREEN = re.compile(r"Screen2D\[(5s|total)\] caller=(\S+) .*?fvf=(0x[0-9A-Fa-f]+) draws=(\d+) buffered=(\d+) "
                       r"bbox=(?:\(([-\d.e+]+),([-\d.e+]+)\)-\(([-\d.e+]+),([-\d.e+]+)\)|none)")
RE_SAMPLE = re.compile(r"ProjSample\[5s\] caller=(\S+) .*?(?:fovX=([\d.]+) fovY=([\d.]+) aspect\(x/y\)=([\d.]+)|"
                       r"(non-perspective)).*viewport=(\d+)x(\d+)\+(\d+)\+(\d+)")
RE_RT = re.compile(r"SetRenderTarget .*caller=(\S+)")


def analyze(text: str) -> dict:
    lines = text.splitlines()
    groups: dict[tuple, dict] = {}
    viewports: Counter = Counter()
    vp_callers: dict[tuple, str] = {}
    fps: list[float] = []
    devices: list[str] = []
    proj_callers: Counter = Counter()
    nonstandard = 0
    screen: dict[str, dict] = {}
    samples: Counter = Counter()
    rt_callers: list[str] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = RE_PROJ.search(line)
        if m:
            proj_callers[m.group(2)] += 1
            active = None
            lh = None
            for j in range(i + 1, min(i + 8, len(lines))):
                if RE_PROJ.search(lines[j]):
                    break
                a = RE_ACTIVE.search(lines[j])
                if a:
                    active = (int(a.group(3)), int(a.group(4)), float(a.group(5)))
                h = RE_LH.search(lines[j])
                if h:
                    lh = h
            if lh is None:
                nonstandard += 1
            else:
                fovy, fovx, asp, zn, zf = (float(x) for x in lh.groups())
                key = (round(fovx, 1), round(asp, 3), round(zn, 2), round(zf, -1))
                g = groups.setdefault(key, {"fovx": round(fovx, 3), "fovy": round(fovy, 3), "aspect": round(asp, 5),
                                            "zn": zn, "zf": zf, "count": 0, "viewports": Counter(),
                                            "aspect_matches_viewport": 0})
                g["count"] += 1
                if active:
                    g["viewports"][f"{active[0]}x{active[1]}"] += 1
                    if abs(active[2] - asp) < 1e-3:
                        g["aspect_matches_viewport"] += 1
        v = RE_VP.search(line)
        if v:
            key = (int(v.group(1)), int(v.group(2)), int(v.group(3)), int(v.group(4)))
            viewports[key] += 1
            vp_callers.setdefault(key, v.group(5))
        f = RE_FPS.search(line)
        if f:
            fps.append(float(f.group(3)))
        sc = RE_SCREEN.search(line)
        if sc and sc.group(1) == "5s":
            e = screen.setdefault(sc.group(2), {"fvf": sc.group(3), "draws": 0, "buffered": 0, "windows": 0,
                                                "bbox": None})
            e["draws"] += int(sc.group(4))
            e["buffered"] += int(sc.group(5))
            e["windows"] += 1
            if sc.group(6) is not None:
                x0, y0, x1, y1 = (float(sc.group(k)) for k in range(6, 10))
                b = e["bbox"]
                e["bbox"] = [x0, y0, x1, y1] if b is None else [min(b[0], x0), min(b[1], y0), max(b[2], x1),
                                                                max(b[3], y1)]
        sm = RE_SAMPLE.search(line)
        if sm:
            vp = f"{sm.group(6)}x{sm.group(7)}+{sm.group(8)}+{sm.group(9)}"
            if sm.group(5):
                samples[(sm.group(1), "non-perspective", None, None, vp)] += 1
            else:
                samples[(sm.group(1), round(float(sm.group(2)), 1), round(float(sm.group(3)), 1),
                         round(float(sm.group(4)), 4), vp)] += 1
        rt = RE_RT.search(line)
        if rt and rt.group(1) not in rt_callers:
            rt_callers.append(rt.group(1))
        c = RE_CREATE.search(line)
        if c:
            devices.append(f"{c.group(1).strip()} {c.group(2)}")
        i += 1
    proj = sorted(groups.values(), key=lambda g: -g["count"])
    for g in proj:
        g["viewports"] = dict(g["viewports"])
    return {
        "devices": devices,
        "viewports": [{"x": k[0], "y": k[1], "w": k[2], "h": k[3], "caller": vp_callers[k]} for k in viewports],
        "projection_callers": dict(proj_callers),
        "projection_groups": proj,
        "projections_logged": sum(g["count"] for g in proj) + nonstandard,
        "projections_nonstandard": nonstandard,
        "screen_space": screen,
        "projection_samples": [{"caller": k[0], "fovx": k[1], "fovy": k[2], "aspect": k[3], "viewport": k[4],
                                "windows": n} for k, n in samples.most_common()],
        "render_target_callers": rt_callers,
        "fps": {"samples": len(fps), "min": min(fps) if fps else None, "max": max(fps) if fps else None},
    }


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Summarize a d3d8_logger log")
    ap.add_argument("log", type=Path)
    ap.add_argument("--json", type=Path)
    args = ap.parse_args(argv)
    res = analyze(args.log.read_text(encoding="utf-8", errors="replace"))
    if args.json:
        args.json.write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8", newline="\n")
    for d in res["devices"]:
        print(d)
    print("viewports:")
    for v in res["viewports"]:
        print(f"  x={v['x']} y={v['y']} w={v['w']} h={v['h']}  ({v['caller']})")
    print("projection callers:", res["projection_callers"])
    print(f"projections logged: {res['projections_logged']} (non-standard: {res['projections_nonstandard']})")
    print("groups (fovX / fovY / aspect / zn / zf : count, viewports, aspect==viewport):")
    for g in res["projection_groups"][:40]:
        print(f"  {g['fovx']:7.3f} {g['fovy']:7.3f} {g['aspect']:.5f} {g['zn']:g} {g['zf']:g} : {g['count']}, "
              f"{g['viewports']}, {g['aspect_matches_viewport']}/{g['count']}")
    if res["screen_space"]:
        print("screen-space draws (caller: draws/buffered over 5 s windows, union bbox):")
        for c, e in res["screen_space"].items():
            print(f"  {c} fvf={e['fvf']}: {e['draws']}/{e['buffered']} in {e['windows']} windows, bbox={e['bbox']}")
    if res["projection_samples"]:
        print("projection samples (caller fovX fovY aspect viewport : 5 s windows):")
        for s_ in res["projection_samples"][:40]:
            print(f"  {s_['caller']} {s_['fovx']} {s_['fovy']} {s_['aspect']} {s_['viewport']} : {s_['windows']}")
    if res["render_target_callers"]:
        print("SetRenderTarget callers:", res["render_target_callers"])
    print("fps:", res["fps"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
