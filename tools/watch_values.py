#!/usr/bin/env python3
"""watch_values - read-only polling of values in a running process (EXP-003 run-004).

Attaches with PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ (via runtime_dump.Process),
then polls pointer chains as fast as possible and logs every change with a timestamp and
the modules newly loaded since the previous change. Never writes to the process.

Chain syntax: "0x6A6C5C>+0x64" means read dword at 0x6A6C5C (a pointer), then read the
dword at pointer+0x64. Several '>' hops are allowed: "0xADDR>+0x10>+0x4".

A chain name ending in ":f" is shown as a 32-bit float ("dt:f=0x6A6C5C>+0x2C").

Two modes:
  * change log (default): one line per change of any value, with newly loaded modules;
  * sampling (--interval S): one line every S seconds with the wall time, for rate measurements
    (EXP-004: game clock [[0x6A6C5C]+0x28] vs wall clock). `--summarize LOG` prints per-window rates of a
    sampling log: clock rate = d(clock) / (1024 * d(wall)) and the mean of the float dt.

The executable hash must match a runtime_dump profile (addresses are hash-pinned).

Usage (start the tool first, then launch the game through Steam):
    python tools/watch_values.py OUT.log --chain width=0x6A6C5C>+0x64 --chain height=0x6A6C5C>+0x68 --seconds 60
    python tools/watch_values.py OUT.log --chain clock=0x6A6C5C>+0x28 --chain dt:f=0x6A6C5C>+0x2C --interval 0.25 --seconds 600
    python tools/watch_values.py --summarize OUT.log [--window 5]
"""
from __future__ import annotations

import argparse
import datetime as _dt
import re
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import runtime_dump  # noqa: E402


def parse_chain(spec: str) -> tuple[str, int, list[int]]:
    """'name=0xBASE>+0x64>+0x8' -> (name, base, [0x64, 0x8])."""
    name, _, expr = spec.partition("=")
    if not expr:
        raise ValueError(f"chain needs name=expr: {spec!r}")
    parts = expr.split(">")
    base = int(parts[0], 0)
    offsets = []
    for p in parts[1:]:
        p = p.strip()
        if not p.startswith(("+", "-")):
            raise ValueError(f"offset must start with + or -: {p!r}")
        offsets.append(int(p, 0))
    return name, base, offsets


def read_chain(read_u32, base: int, offsets: list[int]) -> int | None:
    """Follow base -> [base] + off1 -> ... ; return the final dword or None."""
    addr = base
    for off in offsets:
        ptr = read_u32(addr)
        if not ptr:
            return None
        addr = ptr + off
    return read_u32(addr)


def show(name: str, value: int | None) -> str:
    """Formats a dword; names ending in ':f' are shown as float."""
    if value is None:
        return "None"
    if name.endswith(":f"):
        return f"{struct.unpack('<f', struct.pack('<I', value & 0xFFFFFFFF))[0]:.7g}"
    return str(value - (1 << 32) if value >= 1 << 31 else value)  # signed int32


RE_SAMPLE = re.compile(r"^\[\s*([\d.]+) ms\] (.*)$")


def summarize(lines: list[str], window_s: float = 5.0, clock: str = "clock", dt: str = "dt:f") -> list[dict]:
    """Per-window rates from a sampling log.

    clock_rate     d(clock) / (1024 * d(wall)); None if the clock went backwards (engine reset) in the window
    predicted      mean over samples of trunc(dt*1024) / (dt*1024): the rate the truncating update would give
    paused         clock and dt both unchanged (the engine skips the update, e.g. menu / pause / loading)
    """
    samples = []
    for line in lines:
        m = RE_SAMPLE.match(line)
        if not m:
            continue
        vals = dict(kv.split("=", 1) for kv in m.group(2).split())
        if vals.get(clock, "None") == "None":
            continue
        samples.append((float(m.group(1)) / 1000.0, int(vals[clock]),
                        float(vals[dt]) if vals.get(dt, "None") != "None" else None))
    out, i = [], 0
    while i < len(samples) - 1:
        j = i + 1
        while j < len(samples) - 1 and samples[j][0] - samples[i][0] < window_s:
            j += 1
        t0, c0, _ = samples[i]
        t1, c1, _ = samples[j]
        window = samples[i:j + 1]
        dts = [s[2] for s in window if s[2]]
        mean_dt = sum(dts) / len(dts) if dts else None
        reset = any(b[1] < a[1] for a, b in zip(window, window[1:]))
        paused = all(s[1] == c0 and s[2] == window[0][2] for s in window)
        predicted = (sum(int(d * 1024) / (d * 1024) for d in dts) / len(dts)) if dts else None
        out.append({"t0": round(t0, 2), "t1": round(t1, 2),
                    "clock_rate": None if reset else round((c1 - c0) / (1024.0 * (t1 - t0)), 4),
                    "predicted": round(predicted, 4) if predicted is not None else None, "paused": paused,
                    "mean_dt": mean_dt, "fps_from_dt": round(1.0 / mean_dt, 1) if mean_dt else None})
        i = j
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Read-only value watcher")
    ap.add_argument("out", type=Path, nargs="?")
    ap.add_argument("--chain", action="append", help="name=0xBASE>+off[>+off...] (name:f = float)")
    ap.add_argument("--image", default="hitman2.exe")
    ap.add_argument("--seconds", type=float, default=60.0, help="watch duration after attach")
    ap.add_argument("--wait", type=float, default=180.0, help="seconds to wait for the process")
    ap.add_argument("--interval", type=float, help="sampling mode: one line every INTERVAL seconds")
    ap.add_argument("--summarize", type=Path, help="print per-window rates of a sampling log and exit")
    ap.add_argument("--window", type=float, default=5.0, help="window for --summarize (seconds)")
    args = ap.parse_args(argv)
    if args.summarize:
        for row in summarize(args.summarize.read_text(encoding="utf-8").splitlines(), args.window):
            state = "paused" if row["paused"] else ("reset" if row["clock_rate"] is None else "")
            print(f"{row['t0']:8.2f}-{row['t1']:8.2f} s  clock rate {row['clock_rate']}  predicted {row['predicted']}"
                  f"  fps(dt) {row['fps_from_dt']}  {state}")
        return 0
    if not args.out or not args.chain:
        ap.error("OUT and at least one --chain are required unless --summarize is used")
    chains = [parse_chain(c) for c in args.chain]

    deadline = time.perf_counter() + args.wait
    pids: list[int] = []
    while not pids and time.perf_counter() < deadline:
        pids = runtime_dump.find_pids(args.image)
        if not pids:
            time.sleep(0.005)
    if len(pids) != 1:
        raise SystemExit(f"expected exactly one {args.image}, found {pids}")
    proc = runtime_dump.Process(pids[0])
    t0 = time.perf_counter()
    try:
        exe = Path(proc.image_path())
        h = runtime_dump.sha256(exe.read_bytes())
        if h not in runtime_dump.PROFILES:
            raise SystemExit(f"refusing: unknown executable hash {h}")
        with open(args.out, "w", encoding="utf-8", newline="\n") as log:
            log.write(f"# watch_values pid={pids[0]} exe={exe} sha256={h} start={_dt.datetime.now().astimezone().isoformat()}\n")
            log.write("# chains: " + ", ".join(args.chain) + "\n")
            last: dict[str, int | None] = {}
            seen_mods: set[str] = set()
            next_alive_check = 0.0
            next_sample = 0.0
            while args.interval and time.perf_counter() - t0 < args.seconds:
                now = time.perf_counter() - t0
                if now >= next_sample:
                    next_sample = now + args.interval
                    vals = {name: read_chain(proc.u32, base, offs) for name, base, offs in chains}
                    log.write(f"[{now * 1000:10.1f} ms] " + " ".join(f"{k}={show(k, v)}" for k, v in vals.items())
                              + "\n")
                    log.flush()
                if now >= next_alive_check:
                    next_alive_check = now + 0.5
                    if pids[0] not in runtime_dump.find_pids(args.image):
                        log.write("# process exited\n")
                        break
                time.sleep(0.001)
            while not args.interval and time.perf_counter() - t0 < args.seconds:
                vals = {name: read_chain(proc.u32, base, offs) for name, base, offs in chains}
                if vals != last:
                    mods = [m["name"] for m in proc.modules()]
                    new = [m for m in mods if m not in seen_mods]
                    seen_mods.update(new)
                    ms = (time.perf_counter() - t0) * 1000
                    log.write(f"[{ms:9.1f} ms] " + " ".join(f"{k}={show(k, v)}" for k, v in vals.items())
                              + (f"  new modules: {', '.join(new)}" if new else "") + "\n")
                    log.flush()
                    last = vals
                now = time.perf_counter()
                if now >= next_alive_check:  # enumerating processes is slow: only every 0.5 s
                    next_alive_check = now + 0.5
                    if pids[0] not in runtime_dump.find_pids(args.image):
                        log.write("# process exited\n")
                        break
                time.sleep(0.0005)
    finally:
        proc.close()
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
