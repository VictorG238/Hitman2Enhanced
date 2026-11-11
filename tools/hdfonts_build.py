#!/usr/bin/env python3
"""hdfonts_build - build high-resolution replacements for the game's bitmap font pages (fix `hdfonts`).

The UI text of Hitman 2 is drawn from pre-rasterised 128x128 palettised font pages (`PALN` entries named
`Fonts\\COPRGTB.TTF-<px>#<n>` in each scene's .TEX) whose glyph boxes are listed in the scene's .PRM font tables;
the TrueType font itself is embedded in the scene's .ZGF. Format notes: docs/FILE_FORMATS.md ("Scene archives").

This tool reads every `Scenes/**/*.ZIP` of a game folder IN MEMORY (nothing is extracted or written to the game
folder), re-renders every glyph of every font page from the embedded TrueType outlines at SCALE x the original
size into the same normalized box (so the engine's UVs stay valid), and writes one pack file for the proxy:

    h2e_hdfonts.bin   (derived from game data: never commit or redistribute it; default output samples/local/)

Each page is keyed by a hash of its 4-bit alpha content, which the proxy recomputes when RenderD3D fills the
Direct3D texture (src/fixes/hdfonts.cpp). Glyphs that cannot be matched to an outline are upscaled from the
original page instead, and every glyph is checked by downsampling the new render and comparing it with the
original (`--report` lists the per-page error).

Usage:
    python -I tools/hdfonts_build.py GAME_DIR [--out samples/local/h2e_hdfonts.bin] [--scale 4] [--preview DIR]
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
import zipfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PACK_MAGIC = b"H2EF"
PACK_VERSION = 1
FONT_PREFIX = "Fonts\\"

# ---------------------------------------------------------------------------------------------------------------
# Scene data
# ---------------------------------------------------------------------------------------------------------------


@dataclass
class TexEntry:
    index: int
    kind: str  # "PALN", "ABGR", "DXT1", ... (the on-disk tag reversed)
    width: int
    height: int
    name: str
    pixels: bytes  # level 0 only
    palette: list[int] = field(default_factory=list)  # ARGB dwords (PALN only)


def read_tex(data: bytes) -> list[TexEntry]:
    """Parses a .TEX file: u32 offset of a 2048-slot entry-offset table, u32 offset of a second 2048-slot table,
    u32 x2, then the entries, reached through the offset table (as RenderD3D does: FUN_100109d0, RVA 0x109D0).

    Entry: u32 size, 4-char tag twice, u32 index, u16 width, u16 height, u32 mip count, u32 x3, name\\0, per mip
    u32 size + bytes, and for palettised tags u32 count + count ARGB dwords. Some entries carry extra data after
    that (e.g. a u32 count + indices list), so only `parsed <= size` is required.
    """
    table, _second = struct.unpack_from("<II", data, 0)
    out = []
    for p in struct.unpack_from("<2048I", data, table):
        if not p:
            continue
        size, tag, _tag2, index, w, h, mips = struct.unpack_from("<I4s4sIHHI", data, p)
        if size < 36 or p + size > table:
            raise ValueError(f"bad TEX entry at 0x{p:x}")
        q = p + 36
        end = data.index(b"\0", q)
        name = data[q:end].decode("latin-1")
        q = end + 1
        pixels = b""
        for m in range(mips):
            (ms,) = struct.unpack_from("<I", data, q)
            if m == 0:
                pixels = data[q + 4:q + 4 + ms]
            q += 4 + ms
        kind = tag[::-1].decode("latin-1")
        palette = []
        if kind.startswith("PAL"):
            (count,) = struct.unpack_from("<I", data, q)
            palette = list(struct.unpack_from(f"<{count}I", data, q + 4))
            q += 4 + 4 * count
        if q > p + size:
            raise ValueError(f"TEX entry 0x{p:x} ({name}) overruns its size")
        out.append(TexEntry(index, kind, w, h, name, pixels, palette))
    return out


@dataclass
class Glyph:
    code: int
    tex: int  # TEX entry index of the page
    x0: int
    y0: int
    x1: int  # exclusive
    y1: int


@dataclass
class FontTable:
    offset: int
    size: int  # nominal pixel size (the "-<px>" of the page names)
    line_height: int
    pages: int
    glyphs: list[Glyph]


def find_font_tables(prm: bytes) -> list[FontTable]:
    """Finds the bitmap-font tables in a .PRM file.

    Header (32 bytes): u32 1, u32 8, u32 pixel size, f32 1.0, u32 glyph count, u32 line height, u32 kerning pair
    count, u32 page count; then glyph records of 20 bytes (u32 code, u32 0x800|TEX index, u8 x0,y0,x1,y1 each
    biased by 0x80, u8 width, u8 height, ...) and kerning pairs of 8 bytes.
    """
    out = []
    for p in range(0, len(prm) - 31, 4):
        one, eight, size, scale, count, line, nkern, npages = struct.unpack_from("<IIIfIIII", prm, p)
        if one != 1 or eight != 8 or not 4 <= size <= 128 or scale != 1.0:
            continue
        if not 1 <= count <= 4096 or not 1 <= npages <= 64 or p + 32 + 20 * count + 8 * nkern > len(prm):
            continue
        glyphs = []
        ok = True
        for i in range(count):
            r = p + 32 + 20 * i
            code, tex = struct.unpack_from("<II", prm, r)
            bx0, by0, bx1, by1, w, h = prm[r + 8:r + 14]
            x0, y0, x1, y1 = bx0 - 0x80, by0 - 0x80, bx1 - 0x80, by1 - 0x80
            if tex & ~0x7FF != 0x800 or x1 - x0 != w or y1 - y0 != h or min(x0, y0) < 0:
                ok = False
                break
            glyphs.append(Glyph(code, tex & 0x7FF, x0, y0, x1, y1))
        if ok:
            out.append(FontTable(p, size, line, npages, glyphs))
    return out


def inflate_glacier(data: bytes) -> bytes:
    """Compressed scene member (.GMS, .ZGF): u32 raw size, u32 packed size, u8, raw deflate stream."""
    raw_size, _packed = struct.unpack_from("<II", data, 0)
    raw = zlib.decompressobj(-15).decompress(data[9:])
    if len(raw) != raw_size:
        raise ValueError("inflate size mismatch")
    return raw


def find_sfnts(blob: bytes) -> list[bytes]:
    """The TrueType fonts embedded in a blob (located through their table directories)."""
    out = []
    pos = blob.find(b"\x00\x01\x00\x00")
    while pos >= 0:
        nxt = pos + 1
        try:
            n = struct.unpack_from(">H", blob, pos + 4)[0]
            if 4 <= n <= 64:
                tables = {struct.unpack_from(">4s", blob, pos + 12 + 16 * i)[0]: struct.unpack_from(
                    ">II", blob, pos + 12 + 16 * i + 8) for i in range(n)}
                if b"glyf" in tables and b"cmap" in tables:
                    end = max(o + ln for o, ln in tables.values())
                    if pos + end <= len(blob):
                        out.append(blob[pos:pos + end])
                        nxt = pos + end
        except struct.error:
            pass
        pos = blob.find(b"\x00\x01\x00\x00", nxt)
    return out


# ---------------------------------------------------------------------------------------------------------------
# TrueType outlines
# ---------------------------------------------------------------------------------------------------------------


class TrueType:
    def __init__(self, data: bytes):
        self.data = data
        n = struct.unpack_from(">H", data, 4)[0]
        self.tables = {}
        for i in range(n):
            tag, _chk, off, ln = struct.unpack_from(">4sIII", data, 12 + 16 * i)
            self.tables[tag.decode("latin-1")] = (off, ln)
        head = self.tables["head"][0]
        self.units_per_em = struct.unpack_from(">H", data, head + 18)[0]
        self.long_loca = struct.unpack_from(">h", data, head + 50)[0] == 1
        self.num_glyphs = struct.unpack_from(">H", data, self.tables["maxp"][0] + 4)[0]
        self.cmap = self._read_cmap()

    def _read_cmap(self) -> dict[int, int]:
        base = self.tables["cmap"][0]
        n = struct.unpack_from(">H", self.data, base + 2)[0]
        subs = {}
        for i in range(n):
            pid, eid, off = struct.unpack_from(">HHI", self.data, base + 4 + 8 * i)
            subs[(pid, eid)] = base + off
        for key in ((3, 1), (3, 0), (0, 3), (0, 1), (0, 0)):
            if key in subs and struct.unpack_from(">H", self.data, subs[key])[0] == 4:
                return self._cmap4(subs[key])
        raise ValueError("no format-4 cmap")

    def _cmap4(self, p: int) -> dict[int, int]:
        d = self.data
        seg2 = struct.unpack_from(">H", d, p + 6)[0]
        seg = seg2 // 2
        ends = struct.unpack_from(f">{seg}H", d, p + 14)
        starts = struct.unpack_from(f">{seg}H", d, p + 16 + seg2)
        deltas = struct.unpack_from(f">{seg}h", d, p + 16 + 2 * seg2)
        ro_pos = p + 16 + 3 * seg2
        ros = struct.unpack_from(f">{seg}H", d, ro_pos)
        out = {}
        for i in range(seg):
            for c in range(starts[i], ends[i] + 1):
                if c == 0xFFFF:
                    continue
                if ros[i] == 0:
                    g = (c + deltas[i]) & 0xFFFF
                else:
                    gp = ro_pos + 2 * i + ros[i] + 2 * (c - starts[i])
                    g = struct.unpack_from(">H", d, gp)[0]
                    if g:
                        g = (g + deltas[i]) & 0xFFFF
                if g:
                    out[c] = g
        return out

    def _glyph_range(self, gid: int) -> tuple[int, int]:
        loca = self.tables["loca"][0]
        if self.long_loca:
            a, b = struct.unpack_from(">II", self.data, loca + 4 * gid)
        else:
            a, b = (2 * v for v in struct.unpack_from(">HH", self.data, loca + 2 * gid))
        glyf = self.tables["glyf"][0]
        return glyf + a, b - a

    def contours(self, gid: int, depth: int = 0) -> list[list[tuple[float, float, bool]]]:
        """Contours of a glyph in font units: lists of (x, y, on_curve)."""
        if depth > 8 or not 0 <= gid < self.num_glyphs:
            return []
        p, length = self._glyph_range(gid)
        if length <= 0:
            return []
        d = self.data
        ncont = struct.unpack_from(">h", d, p)[0]
        if ncont >= 0:
            return self._simple(p, ncont)
        out = []
        q = p + 10
        while True:
            flags, sub = struct.unpack_from(">HH", d, q)
            q += 4
            if flags & 1:
                a, b = struct.unpack_from(">hh", d, q)
                q += 4
            else:
                a, b = struct.unpack_from(">bb", d, q)
                q += 2
            m = (1.0, 0.0, 0.0, 1.0)
            if flags & 0x8:
                s = struct.unpack_from(">h", d, q)[0] / 16384
                m = (s, 0.0, 0.0, s)
                q += 2
            elif flags & 0x40:
                sx, sy = (v / 16384 for v in struct.unpack_from(">hh", d, q))
                m = (sx, 0.0, 0.0, sy)
                q += 4
            elif flags & 0x80:
                m = tuple(v / 16384 for v in struct.unpack_from(">hhhh", d, q))
                q += 8
            dx, dy = (a, b) if flags & 2 else (0, 0)  # point-matching composites are not used by this font
            for c in self.contours(sub, depth + 1):
                out.append([(m[0] * x + m[2] * y + dx, m[1] * x + m[3] * y + dy, on) for x, y, on in c])
            if not flags & 0x20:
                break
        return out

    def _simple(self, p: int, ncont: int) -> list[list[tuple[float, float, bool]]]:
        d = self.data
        ends = struct.unpack_from(f">{ncont}H", d, p + 10)
        npts = ends[-1] + 1 if ncont else 0
        q = p + 10 + 2 * ncont
        q += 2 + struct.unpack_from(">H", d, q)[0]
        flags = []
        while len(flags) < npts:
            f = d[q]
            q += 1
            flags.append(f)
            if f & 8:
                flags.extend([f] * d[q])
                q += 1
        coords = []
        for short, same in ((2, 16), (4, 32)):
            v = 0
            vals = []
            for f in flags[:npts]:
                if f & short:
                    delta = d[q]
                    q += 1
                    v += delta if f & same else -delta
                elif not f & same:
                    v += struct.unpack_from(">h", d, q)[0]
                    q += 2
                vals.append(v)
            coords.append(vals)
        out = []
        start = 0
        for e in ends:
            out.append([(coords[0][i], coords[1][i], bool(flags[i] & 1)) for i in range(start, e + 1)])
            start = e + 1
        return out


def flatten(contour: list[tuple[float, float, bool]], steps: int = 8) -> list[tuple[float, float]]:
    """Polyline of a quadratic TrueType contour (implied on-curve midpoints between off-curve points)."""
    n = len(contour)
    if n < 2:
        return []
    pts = list(contour)
    # Start at an on-curve point (or the midpoint of the first two off-curve points).
    k = next((i for i, p in enumerate(pts) if p[2]), None)
    if k is None:
        a, b = pts[0], pts[1]
        pts.insert(0, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, True))
        k = 0
    pts = pts[k:] + pts[:k]
    out = [(pts[0][0], pts[0][1])]
    cur = pts[0]
    ctrl = None
    for p in pts[1:] + [pts[0]]:
        if p[2]:
            if ctrl is None:
                out.append((p[0], p[1]))
            else:
                _quad(out, cur, ctrl, p, steps)
            cur, ctrl = p, None
        else:
            if ctrl is not None:
                mid = ((ctrl[0] + p[0]) / 2, (ctrl[1] + p[1]) / 2, True)
                _quad(out, cur, ctrl, mid, steps)
                cur = mid
            ctrl = p
    return out


def _quad(out, a, c, b, steps):
    for i in range(1, steps + 1):
        t = i / steps
        u = 1 - t
        out.append((u * u * a[0] + 2 * u * t * c[0] + t * t * b[0], u * u * a[1] + 2 * u * t * c[1] + t * t * b[1]))


def rasterize(polys: list[list[tuple[float, float]]], width: int, height: int, sub: int = 8) -> list[float]:
    """Non-zero winding coverage (0..1) of polygons given in pixel coordinates (y down), width x height.

    Vertical: `sub` sample rows per pixel; horizontal: exact span coverage.
    """
    edges = []
    for poly in polys:
        for i in range(len(poly)):
            x0, y0 = poly[i]
            x1, y1 = poly[(i + 1) % len(poly)]
            if y0 == y1:
                continue
            w = 1 if y1 > y0 else -1
            if y0 > y1:
                x0, y0, x1, y1 = x1, y1, x0, y0
            edges.append((y0, y1, x0, (x1 - x0) / (y1 - y0), w))
    cov = [0.0] * (width * height)
    if not edges:
        return cov
    edges.sort()
    active_from = 0
    for py in range(height):
        acc = [0.0] * (width + 2)
        diff = [0.0] * (width + 2)
        for s in range(sub):
            y = py + (s + 0.5) / sub
            while active_from < len(edges) and edges[active_from][1] <= y:
                active_from += 1  # cheap prune of edges that ended above (edges sorted by y0 only)
            xs = []
            for e in edges[active_from:]:
                if e[0] > y:
                    break
                if e[1] > y:
                    xs.append((e[2] + (y - e[0]) * e[3], e[4]))
            if not xs:
                continue
            xs.sort()
            wind = 0
            for i in range(len(xs) - 1):
                wind += xs[i][1]
                if wind:
                    _span(acc, diff, xs[i][0], xs[i + 1][0], width)
        run = 0.0
        row = py * width
        for x in range(width):
            run += diff[x]
            v = (acc[x] + run) / sub
            cov[row + x] = 1.0 if v > 1.0 else v
    return cov


def _span(acc, diff, a, b, width):
    if b <= 0 or a >= width or b <= a:
        return
    a = max(a, 0.0)
    b = min(b, float(width))
    ia, ib = int(a), int(b)
    if ia == ib:
        acc[ia] += b - a
        return
    acc[ia] += ia + 1 - a
    if ib < width:
        acc[ib] += b - ib
    diff[ia + 1] += 1.0
    diff[ib] -= 1.0


# ---------------------------------------------------------------------------------------------------------------
# Pages
# ---------------------------------------------------------------------------------------------------------------


def page_alpha4(entry: TexEntry) -> bytes:
    """4-bit alpha (0..15) of a palettised page, one byte per pixel: the key material shared with the proxy."""
    lut = [(c >> 28) & 0xF for c in entry.palette]
    return bytes(lut[i] for i in entry.pixels)


def page_key(alpha4: bytes) -> int:
    """64-bit FNV-1a of the 4-bit alpha bytes (must match h2e::hdfonts::PageKey in src/fixes/hdfonts.cpp)."""
    h = 0xCBF29CE484222325
    for b in alpha4:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


@dataclass
class GlyphResult:
    code: int
    source: str  # "outline" or "upscaled"
    error: float  # mean absolute alpha error of the downsampled render vs the original box (0..1)


def _centroid(vals: list[float], w: int, h: int) -> tuple[float, float, float]:
    tot = sx = sy = 0.0
    for y in range(h):
        row = y * w
        for x in range(w):
            v = vals[row + x]
            if v:
                tot += v
                sx += v * (x + 0.5)
                sy += v * (y + 0.5)
    return (sx / tot, sy / tot, tot) if tot else (w / 2, h / 2, 0.0)


def _downsample(vals: list[float], w: int, h: int, k: int) -> list[float]:
    out = []
    for y in range(h // k):
        for x in range(w // k):
            s = 0.0
            for yy in range(k):
                row = (y * k + yy) * w + x * k
                s += sum(vals[row:row + k])
            out.append(s / (k * k))
    return out


def render_glyph(font: TrueType, gid: int, px_per_unit: float, scale: int, box_w: int, box_h: int,
                 original: list[float]) -> tuple[list[float], float]:
    """Renders glyph `gid` at scale x into a (box_w*scale, box_h*scale) box aligned on the original's centroid.

    Returns (coverage, mean abs error of the 1x downsample vs `original`).
    """
    W, H = box_w * scale, box_h * scale
    s = px_per_unit * scale
    contours = font.contours(gid)
    pts = [p for c in contours for p in c]
    if not pts:
        return [0.0] * (W * H), (sum(original) / len(original) if original else 0.0)
    xmin, xmax = min(p[0] for p in pts), max(p[0] for p in pts)
    ymin, ymax = min(p[1] for p in pts), max(p[1] for p in pts)
    flat = [flatten(c) for c in contours]
    ox = (W - (xmax - xmin) * s) / 2 - xmin * s
    oy = (H - (ymax - ymin) * s) / 2 + ymax * s
    cx0, cy0, tot0 = _centroid(original, box_w, box_h)
    for _ in range(2):
        polys = [[(ox + x * s, oy - y * s) for x, y in poly] for poly in flat]
        cov = rasterize(polys, W, H)
        if not tot0:
            break
        cx, cy, tot = _centroid(cov, W, H)
        dx, dy = cx0 * scale - cx, cy0 * scale - cy
        if abs(dx) < 0.25 and abs(dy) < 0.25 or not tot:
            break
        ox += dx
        oy += dy
    down = _downsample(cov, W, H, scale)
    err = sum(abs(a - b) for a, b in zip(down, original)) / max(1, len(original))
    return cov, err


def upscale_box(original: list[float], box_w: int, box_h: int, scale: int) -> list[float]:
    """Bilinear upscale of an original glyph box (fallback for glyphs without a usable outline)."""
    W, H = box_w * scale, box_h * scale
    out = [0.0] * (W * H)
    for y in range(H):
        fy = min(max((y + 0.5) / scale - 0.5, 0.0), box_h - 1.0)
        y0 = int(fy)
        y1 = min(y0 + 1, box_h - 1)
        ty = fy - y0
        for x in range(W):
            fx = min(max((x + 0.5) / scale - 0.5, 0.0), box_w - 1.0)
            x0 = int(fx)
            x1 = min(x0 + 1, box_w - 1)
            tx = fx - x0
            a = original[y0 * box_w + x0] * (1 - tx) + original[y0 * box_w + x1] * tx
            b = original[y1 * box_w + x0] * (1 - tx) + original[y1 * box_w + x1] * tx
            out[y * W + x] = a * (1 - ty) + b * ty
    return out


# Mean absolute alpha error (0..1) of the 1x downsample against the original box. Hinting differences between the
# original rasteriser and this unhinted one stay below ~0.09; a wrong glyph or typeface is well above.
GLYPH_ERROR_LIMIT = 0.15  # above: that glyph is upscaled from the original instead
TABLE_ERROR_LIMIT = 0.10  # above (sample mean): the table uses a typeface that is not embedded; upscale all of it


def build_page(font: TrueType | None, page: TexEntry, glyphs: list[Glyph], px_per_unit: float, scale: int,
               ) -> tuple[bytearray, list[GlyphResult]]:
    """HD alpha (0..255, one byte per pixel) of one font page and the per-glyph results (font None: upscale)."""
    alpha4 = page_alpha4(page)
    W = page.width * scale
    out = bytearray(W * page.height * scale)
    results = []
    for g in glyphs:
        bw, bh = g.x1 - g.x0, g.y1 - g.y0
        if bw <= 0 or bh <= 0:
            continue
        orig = [alpha4[(g.y0 + y) * page.width + g.x0 + x] / 15.0 for y in range(bh) for x in range(bw)]
        gid = font.cmap.get(g.code) if font else None  # glyph codes are Unicode (FILE_FORMATS.md)
        cov, err, source = None, 1.0, "upscaled"
        if gid:
            cov, err = render_glyph(font, gid, px_per_unit, scale, bw, bh, orig)
            source = "outline"
        if cov is None or err > GLYPH_ERROR_LIMIT:
            cov = upscale_box(orig, bw, bh, scale)
            err = sum(abs(a - b) for a, b in zip(_downsample(cov, bw * scale, bh * scale, scale), orig)) / (bw * bh)
            source = "upscaled"
        results.append(GlyphResult(g.code, source, err))
        for y in range(bh * scale):
            row = (g.y0 * scale + y) * W + g.x0 * scale
            src = y * bw * scale
            for x in range(bw * scale):
                v = int(cov[src + x] * 255 + 0.5)
                if v > out[row + x]:
                    out[row + x] = v
    return out, results


# ---------------------------------------------------------------------------------------------------------------
# Pack file
# ---------------------------------------------------------------------------------------------------------------


def packbits(data: bytes) -> bytes:
    """PackBits RLE (n < 128: n+1 literals; n > 128: next byte repeated 257-n times)."""
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        j = i
        while j + 1 < n and data[j + 1] == data[i] and j - i < 127:
            j += 1
        if j > i:
            out += bytes((257 - (j - i + 1), data[i]))
            i = j + 1
            continue
        j = i + 1
        while j < n and j - i < 128 and not (j + 1 < n and data[j] == data[j + 1]):
            j += 1
        out.append(j - i - 1)
        out += data[i:j]
        i = j
    return bytes(out)


def unpackbits(data: bytes, size: int) -> bytes:
    out = bytearray()
    i = 0
    while len(out) < size:
        n = data[i]
        i += 1
        if n < 128:
            out += data[i:i + n + 1]
            i += n + 1
        elif n > 128:
            out += bytes([data[i]]) * (257 - n)
            i += 1
    if len(out) != size:
        raise ValueError("packbits overrun")
    return bytes(out)


@dataclass
class PackPage:
    key: int
    src_w: int
    src_h: int
    dst_w: int
    dst_h: int
    color: int  # RGB of the page (alpha byte zero); the proxy writes alpha from `alpha`
    name: str
    alpha: bytes


def write_pack(pages: list[PackPage]) -> bytes:
    """Pack: 'H2EF', u32 version, u32 count, then per page: u64 key, u16 src w/h, u16 dst w/h, u32 RGB color,
    u8 name length + name (latin-1), u32 packed size, PackBits alpha (dst_w * dst_h bytes)."""
    out = bytearray(PACK_MAGIC + struct.pack("<II", PACK_VERSION, len(pages)))
    for p in pages:
        name = p.name.encode("latin-1")[:255]
        data = packbits(p.alpha)
        out += struct.pack("<QHHHHI", p.key, p.src_w, p.src_h, p.dst_w, p.dst_h, p.color & 0xFFFFFF)
        out += bytes([len(name)]) + name + struct.pack("<I", len(data)) + data
    return bytes(out)


def read_pack(blob: bytes) -> list[PackPage]:
    if blob[:4] != PACK_MAGIC:
        raise ValueError("not an hdfonts pack")
    version, count = struct.unpack_from("<II", blob, 4)
    if version != PACK_VERSION:
        raise ValueError(f"pack version {version}")
    p = 12
    out = []
    for _ in range(count):
        key, sw, sh, dw, dh, color = struct.unpack_from("<QHHHHI", blob, p)
        p += 20
        nl = blob[p]
        name = blob[p + 1:p + 1 + nl].decode("latin-1")
        p += 1 + nl
        (size,) = struct.unpack_from("<I", blob, p)
        p += 4
        out.append(PackPage(key, sw, sh, dw, dh, color, name, unpackbits(blob[p:p + size], dw * dh)))
        p += size
    return out


# ---------------------------------------------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------------------------------------------


def write_png(path: Path, w: int, h: int, gray: bytes) -> None:
    raw = b"".join(b"\0" + gray[y * w:(y + 1) * w] for y in range(h))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)

    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def scene_members(z: zipfile.ZipFile, ext: str) -> list[str]:
    return [n for n in z.namelist() if n.upper().endswith(ext)]


def choose_font(fonts: list[TrueType], table: FontTable, entries: dict[int, TexEntry], sample: int = 24
                ) -> tuple[TrueType | None, float]:
    """The font (of `fonts`) whose outlines best reproduce `table`'s glyphs at 1x (mean error on a sample).

    Scenes embed different builds of the font, and some tables named after COPRGTB.TTF are another typeface.
    """
    glyphs = [g for g in table.glyphs if g.x1 > g.x0 and g.y1 > g.y0 and g.tex in entries]
    glyphs.sort(key=lambda g: -(g.x1 - g.x0) * (g.y1 - g.y0))
    glyphs = glyphs[:sample]
    best, best_err = None, 2.0
    for font in fonts:
        errs = []
        for g in glyphs:
            page = entries[g.tex]
            if not page.palette:
                continue
            a4 = page_alpha4(page)
            bw, bh = g.x1 - g.x0, g.y1 - g.y0
            orig = [a4[(g.y0 + y) * page.width + g.x0 + x] / 15.0 for y in range(bh) for x in range(bw)]
            gid = font.cmap.get(g.code)
            errs.append(render_glyph(font, gid, table.size / font.units_per_em, 1, bw, bh, orig)[1] if gid else 1.0)
        err = sum(errs) / len(errs) if errs else 2.0
        if err < best_err:
            best, best_err = font, err
    return best, best_err


def build(game: Path, scale: int, preview: Path | None = None, log=print) -> tuple[list[PackPage], dict]:
    zips = sorted((game / "Scenes").rglob("*.ZIP"), key=lambda p: str(p).lower())
    known_fonts: dict[bytes, TrueType] = {}  # every font seen so far, by content
    pages: dict[int, PackPage] = {}
    report = {"scenes": 0, "pages": 0, "unique": 0, "outline": 0, "upscaled": 0, "worst": 0.0, "page_errors": {}}
    for zp in zips:
        with zipfile.ZipFile(zp) as z:
            prms = scene_members(z, ".PRM")
            texs = scene_members(z, ".TEX")
            if not prms or not texs:
                continue
            tables = [t for n in prms for t in find_font_tables(z.read(n))]
            if not tables:
                continue
            scene_fonts = []
            for n in scene_members(z, ".ZGF"):
                for sf in find_sfnts(inflate_glacier(z.read(n))):
                    if sf not in known_fonts:
                        known_fonts[sf] = TrueType(sf)
                        log(f"font #{len(known_fonts)}: {zp.name}:{n} ({len(sf)} bytes, "
                            f"{known_fonts[sf].num_glyphs} glyphs)")
                    scene_fonts.append(known_fonts[sf])
            candidates = scene_fonts + [f for f in known_fonts.values() if f not in scene_fonts]
            if not candidates:
                continue
            entries = {e.index: e for n in texs for e in read_tex(z.read(n))}
            report["scenes"] += 1
            for t in tables:
                chosen = False
                font = None
                by_page: dict[int, list[Glyph]] = {}
                for g in t.glyphs:
                    by_page.setdefault(g.tex, []).append(g)
                for tex, glyphs in sorted(by_page.items()):
                    page = entries.get(tex)
                    if page is None or not page.kind.startswith("PAL") or not page.name.startswith(FONT_PREFIX):
                        continue
                    report["pages"] += 1
                    alpha4 = page_alpha4(page)
                    key = page_key(alpha4)
                    if key in pages:
                        continue
                    if not chosen:
                        chosen = True
                        font, ferr = choose_font(candidates, t, entries)
                        which = f"font #{list(known_fonts.values()).index(font) + 1}" if font else "no font"
                        if ferr > TABLE_ERROR_LIMIT:
                            font = None
                            which += " rejected (typeface not embedded): upscaling"
                        log(f"{zp.stem}: table size {t.size} line {t.line_height}: {which}, "
                            f"1x sample error {ferr:.3f}")
                    # The pages were rasterised at a pixel size of `size` per em (FreeType convention): EXP-005.
                    px = t.size / font.units_per_em if font else 0.0
                    hd, results = build_page(font, page, glyphs, px, scale)
                    colors = {c & 0xFFFFFF for c in page.palette}
                    color = colors.pop() if len(colors) == 1 else 0xFFFFFF
                    pages[key] = PackPage(key, page.width, page.height, page.width * scale, page.height * scale,
                                          color, page.name, bytes(hd))
                    worst = max((r.error for r in results), default=0.0)
                    report["unique"] += 1
                    report["outline"] += sum(r.source == "outline" for r in results)
                    report["upscaled"] += sum(r.source == "upscaled" for r in results)
                    report["worst"] = max(report["worst"], worst)
                    report["page_errors"][f"{zp.stem}:{page.name}"] = (
                        worst, [hex(r.code) for r in results if r.source == "upscaled"])
                    log(f"{zp.stem}: {page.name} key={key:016x} glyphs={len(results)} "
                        f"upscaled={sum(r.source == 'upscaled' for r in results)} worst_err={worst:.3f}")
                    if preview:
                        preview.mkdir(parents=True, exist_ok=True)
                        stem = f"{key:016x}_{page.name.replace(FONT_PREFIX, '').replace('#', '_')}"
                        write_png(preview / f"{stem}_hd.png", page.width * scale, page.height * scale, bytes(hd))
                        write_png(preview / f"{stem}_orig.png", page.width, page.height,
                                  bytes(v * 17 for v in alpha4))
    return list(pages.values()), report


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Build the hdfonts pack (HD font pages) from a game folder")
    ap.add_argument("game_dir", type=Path)
    ap.add_argument("--out", type=Path, default=REPO / "samples" / "local" / "h2e_hdfonts.bin")
    ap.add_argument("--scale", type=int, default=4, choices=[2, 3, 4])
    ap.add_argument("--preview", type=Path, help="write original/HD PNGs of every page here")
    args = ap.parse_args(argv)
    if not (args.game_dir / "Scenes").is_dir():
        raise SystemExit(f"no Scenes folder in {args.game_dir}")
    pages, report = build(args.game_dir, args.scale, args.preview)
    if not pages:
        raise SystemExit("no font pages found")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    blob = write_pack(pages)
    args.out.write_bytes(blob)
    print(f"wrote {args.out}: {len(pages)} pages ({report['pages']} page uses in {report['scenes']} scenes), "
          f"{len(blob)} bytes; glyphs: {report['outline']} from outlines, {report['upscaled']} upscaled; "
          f"worst error {report['worst']:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
