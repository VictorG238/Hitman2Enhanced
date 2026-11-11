"""Tests for tools/hdfonts_build.py: synthetic font + scene (no game data); optional check on a game snapshot."""
from __future__ import annotations

import os
import struct
import sys
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
import hdfonts_build as hb  # noqa: E402

UPEM = 1000


def _table_dir(tables: dict[bytes, bytes]) -> bytes:
    n = len(tables)
    out = struct.pack(">IHHHH", 0x00010000, n, 0, 0, 0)
    off = 12 + 16 * n
    body = b""
    for tag, data in tables.items():
        out += struct.pack(">4sIII", tag, 0, off + len(body), len(data))
        body += data + b"\0" * (-len(data) % 4)
    return out + body


def make_ttf() -> bytes:
    """Minimal TrueType: 'A' = 1000x1000 square (simple glyph), 'B' = the square moved by +100 (composite)."""
    head = struct.pack(">IIIIHH", 0x00010000, 0, 0, 0x5F0F3CF5, 0, UPEM) + b"\0" * 16 + struct.pack(
        ">hhhhHHhhh", 0, 0, 1000, 1000, 0, 8, 2, 0, 0)
    assert len(head) == 54
    maxp = struct.pack(">IH", 0x00005000, 3)
    square = struct.pack(">hhhhhH", 1, 0, 0, 1000, 1000, 3) + struct.pack(">H", 0) + bytes([1, 1, 1, 1]) + \
        struct.pack(">hhhh", 0, 0, 1000, 0) + struct.pack(">hhhh", 0, 1000, 0, -1000)
    composite = struct.pack(">hhhhh", -1, 100, 0, 1100, 1000) + struct.pack(">HHhh", 0x0003, 1, 100, 0)
    glyf = square + composite
    loca = struct.pack(">HHHH", 0, 0, len(square) // 2, len(glyf) // 2)
    segs = [(0x41, 0x42, 1 - 0x41), (0xFFFF, 0xFFFF, 1)]
    seg2 = 2 * len(segs)
    fmt4 = struct.pack(">HHHHHHH", 4, 16 + 8 * len(segs), 0, seg2, 4, 1, 0)
    fmt4 += struct.pack(f">{len(segs)}H", *[e for _, e, _ in segs]) + b"\0\0"
    fmt4 += struct.pack(f">{len(segs)}H", *[s for s, _, _ in segs])
    fmt4 += struct.pack(f">{len(segs)}h", *[d for _, _, d in segs]) + b"\0\0" * len(segs)
    cmap = struct.pack(">HHHHI", 0, 1, 3, 1, 12) + fmt4
    return _table_dir({b"cmap": cmap, b"glyf": glyf, b"head": head, b"loca": loca, b"maxp": maxp})


def make_tex(index: int, name: str, w: int, h: int, alpha4: bytes) -> bytes:
    """TEX with one PALN entry (palette index = alpha level, palette ARGB = level << 28 | 0xFEFFFF)."""
    pal = [(a << 28) | 0xFEFFFF for a in range(16)]
    body = b"NLAPNLAP" + struct.pack("<IHHIIII", index, w, h, 1, 0, 0, 0) + name.encode("latin-1") + b"\0"
    body += struct.pack("<I", w * h) + alpha4 + struct.pack("<I", 16) + struct.pack("<16I", *pal)
    entry = struct.pack("<I", 4 + len(body)) + body
    table_off = 16 + len(entry)
    offsets = [0] * 2048
    offsets[index] = 16
    return struct.pack("<IIII", table_off, table_off + 8192, 3, 4) + entry + struct.pack(
        "<2048I", *offsets) + b"\0" * 8192


def make_prm(size: int, glyphs: list[tuple[int, int, int, int, int, int]]) -> bytes:
    """PRM holding one font table: glyphs = (code, tex, x0, y0, x1, y1)."""
    out = b"\0" * 64 + struct.pack("<IIIfIIII", 1, 8, size, 1.0, len(glyphs), size + 1, 0, 1)
    for code, tex, x0, y0, x1, y1 in glyphs:
        out += struct.pack("<II", code, 0x800 | tex) + bytes([x0 + 0x80, y0 + 0x80, x1 + 0x80, y1 + 0x80,
                                                              x1 - x0, y1 - y0, 0, 0, 0, 0, 0, 0])
    return out


def glacier(raw: bytes) -> bytes:
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    packed = c.compress(raw) + c.flush()
    return struct.pack("<II", len(raw), len(packed)) + b"\0" + packed


class FormatTests(unittest.TestCase):
    def test_page_key_matches_native_vector(self):
        # Same vector as tests/native/fixes_test.cpp (PageKey).
        self.assertEqual(hb.page_key(bytes(range(16))), 0x7C84DC9477851775)

    def test_packbits_roundtrip(self):
        for data in (b"", b"\0" * 1000, bytes(range(256)) * 3, b"\1\1\2\3\3\3" * 50 + b"\7"):
            self.assertEqual(hb.unpackbits(hb.packbits(data), len(data)), data)

    def test_read_tex_uses_offset_table(self):
        a4 = bytes([i % 16 for i in range(16)])
        e = hb.read_tex(make_tex(5, "Fonts\\X.TTF-8#0", 4, 4, a4))
        self.assertEqual(len(e), 1)
        self.assertEqual((e[0].index, e[0].kind, e[0].name, e[0].width), (5, "PALN", "Fonts\\X.TTF-8#0", 4))
        self.assertEqual(hb.page_alpha4(e[0]), a4)

    def test_find_font_tables(self):
        t = hb.find_font_tables(make_prm(8, [(0x41, 3, 1, 2, 11, 12), (0x20, 3, 0, 0, 0, 0)]))
        self.assertEqual(len(t), 1)
        self.assertEqual((t[0].size, len(t[0].glyphs)), (8, 2))
        self.assertEqual(t[0].glyphs[0], hb.Glyph(0x41, 3, 1, 2, 11, 12))

    def test_pack_roundtrip(self):
        p = hb.PackPage(0x1122334455667788, 128, 128, 512, 512, 0xFEFFFF, "Fonts\\X#0", bytes(512 * 512))
        (q,) = hb.read_pack(hb.write_pack([p]))
        self.assertEqual(q, p)


class RasterTests(unittest.TestCase):
    def test_rectangle_coverage_is_exact_area(self):
        cov = hb.rasterize([[(1.5, 1.5), (5.5, 1.5), (5.5, 3.5), (1.5, 3.5)]], 8, 8)
        self.assertAlmostEqual(sum(cov), 8.0, places=6)
        self.assertAlmostEqual(cov[2 * 8 + 3], 1.0)
        self.assertAlmostEqual(cov[1 * 8 + 1], 0.25)

    def test_ttf_outlines_simple_and_composite(self):
        f = hb.TrueType(make_ttf())
        self.assertEqual((f.units_per_em, f.cmap), (UPEM, {0x41: 1, 0x42: 2}))
        self.assertEqual(sorted((x, y) for x, y, _ in f.contours(1)[0]), [(0, 0), (0, 1000), (1000, 0), (1000, 1000)])
        self.assertEqual(sorted((x, y) for x, y, _ in f.contours(2)[0]), [(100, 0), (100, 1000), (1100, 0), (1100, 1000)])

    def test_flatten_quadratic_stays_inside_hull(self):
        pts = hb.flatten([(0, 0, True), (10, 10, False), (20, 0, True)])
        self.assertEqual(pts[0], (0, 0))
        self.assertTrue(all(0 <= y <= 5 for _, y in pts))


class BuildTests(unittest.TestCase):
    def test_build_scene_renders_glyph_into_scaled_box(self):
        # Page 16x16: glyph 'A' (8 px square at size 8) drawn in box (2,2)-(12,12) with a 1 px margin.
        w = 16
        a4 = bytearray(w * w)
        for y in range(3, 11):
            for x in range(3, 11):
                a4[y * w + x] = 15
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            (game / "Scenes").mkdir()
            with zipfile.ZipFile(game / "Scenes" / "Test.ZIP", "w") as z:
                z.writestr("SCENES/Test.TEX", make_tex(7, "Fonts\\X.TTF-8#0", w, w, bytes(a4)))
                z.writestr("SCENES/Test.PRM", make_prm(8, [(0x41, 7, 2, 2, 12, 12)]))
                z.writestr("SCENES/Test.ZGF", glacier(b"TFGZ" + b"\0" * 36 + make_ttf()))
            pages, report = hb.build(game, 4, log=lambda *_: None)
        self.assertEqual(len(pages), 1)
        p = pages[0]
        self.assertEqual((p.key, p.src_w, p.dst_w, p.color), (hb.page_key(bytes(a4)), 16, 64, 0xFEFFFF))
        self.assertEqual((report["outline"], report["upscaled"]), (1, 0))
        self.assertLess(report["worst"], 0.02)
        # The 32x32 square sits at (12,12)-(44,44) in the 64x64 page; nothing outside the 40x40 box.
        self.assertEqual(p.alpha[20 * 64 + 20], 255)
        self.assertEqual(p.alpha[10 * 64 + 10], 0)
        self.assertEqual(sum(p.alpha[y * 64 + x] for y in range(64) for x in range(64)
                             if not (8 <= x < 48 and 8 <= y < 48)), 0)

    def test_wrong_glyph_falls_back_to_upscaled_original(self):
        # Box content is a thin bar: the square outline does not match it, so the original is upscaled.
        w = 16
        a4 = bytearray(w * w)
        for x in range(3, 11):
            a4[6 * w + x] = 15
        f = hb.TrueType(make_ttf())
        page = hb.TexEntry(7, "PALN", w, w, "Fonts\\X#0", bytes(a4), [(a << 28) | 0xFEFFFF for a in range(16)])
        _, results = hb.build_page(f, page, [hb.Glyph(0x41, 7, 2, 2, 12, 12)], 8 / UPEM, 4)
        self.assertEqual(results[0].source, "upscaled")


@unittest.skipUnless(os.environ.get("HITMAN2_SNAPSHOT"), "set HITMAN2_SNAPSHOT to a game snapshot or install folder")
class SnapshotTests(unittest.TestCase):
    def test_main_menu_fonts(self):
        zp = Path(os.environ["HITMAN2_SNAPSHOT"]) / "Scenes" / "MainMenu.ZIP"
        if not zp.is_file():
            self.skipTest("snapshot has no Scenes/MainMenu.ZIP")
        with zipfile.ZipFile(zp) as z:
            tables = hb.find_font_tables(z.read("SCENES/MainMenu.PRM"))
            entries = {e.index: e for e in hb.read_tex(z.read("SCENES/MainMenu.TEX"))}
            fonts = [hb.TrueType(s) for s in hb.find_sfnts(hb.inflate_glacier(z.read("SCENES/MainMenu.ZGF")))]
        self.assertEqual(sorted(t.size for t in tables), [12, 14, 17, 18, 23, 26])
        self.assertEqual(len(fonts), 1)
        t26 = next(t for t in tables if t.size == 26)
        self.assertEqual(entries[t26.glyphs[0].tex].name, "Fonts\\COPRGTB.TTF-26#0")
        font, err = hb.choose_font(fonts, t26, entries)
        self.assertLess(err, 0.03)


if __name__ == "__main__":
    unittest.main()
