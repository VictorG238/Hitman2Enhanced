"""Tests for tools/iat_annotate.py and tools/runtime_dump.py."""
from __future__ import annotations

import ctypes
import os
import struct
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
sys.path.insert(0, str(HERE))

import iat_annotate  # noqa: E402
from pe_builder import build_pe  # noqa: E402
from test_pe_inspect import to_mapped  # noqa: E402

MODULES = [
    {"name": "KERNEL32.DLL", "base": 0x76000000, "exports": [
        {"ordinal": 1, "names": ["GetTickCount"], "rva": 0x1000},
        {"ordinal": 2, "names": ["HeapAlloc"], "forwarder": "NTDLL.RtlAllocateHeap"},
        {"ordinal": 3, "names": ["Sleep"], "rva": 0x1010},
    ]},
    {"name": "ntdll.dll", "base": 0x77000000, "exports": [
        {"ordinal": 1, "names": ["RtlAllocateHeap"], "rva": 0x2000},
    ]},
    {"name": "WINMM.dll", "base": 0x70000000, "exports": [
        {"ordinal": 7, "names": [], "rva": 0x3000},
        {"ordinal": 8, "names": ["timeGetTime"], "rva": 0x3010},
    ]},
]


def _image_with_iat() -> tuple[bytes, int]:
    img = bytearray(0x3000)
    iat_rva = 0x2000
    slots = [0x76001000, 0x77002000, 0x76001010, 0, 0x70003010, 0x70003000, 0]
    struct.pack_into(f"<{len(slots)}I", img, iat_rva, *slots)
    # a lone pointer not null-preceded must not be taken as an IAT
    struct.pack_into("<II", img, 0x100, 0x12345678, 0x76001000)
    return bytes(img), iat_rva


class WatchChainTests(unittest.TestCase):
    def test_parse_and_follow(self):
        import watch_values
        self.assertEqual(watch_values.parse_chain("w=0x6A6C5C>+0x64"), ("w", 0x6A6C5C, [0x64]))
        self.assertEqual(watch_values.parse_chain("x=0x10>+0x4>-0x8")[2], [4, -8])
        mem = {0x6A6C5C: 0x1000, 0x1064: 1280, 0x1068: 720}
        self.assertEqual(watch_values.read_chain(mem.get, 0x6A6C5C, [0x68]), 720)
        self.assertIsNone(watch_values.read_chain({}.get, 0x6A6C5C, [0x68]))
        with self.assertRaises(ValueError):
            watch_values.parse_chain("bad")

    def test_show_float_and_signed(self):
        import watch_values
        self.assertEqual(watch_values.show("dt:f", 0x3C888889), "0.01666667")
        self.assertEqual(watch_values.show("clock", 0xFFFFFFFF), "-1")
        self.assertEqual(watch_values.show("clock", None), "None")
        name, base, offs = watch_values.parse_chain("dt:f=0x6A6C5C>+0x2C")
        self.assertEqual((name, base, offs), ("dt:f", 0x6A6C5C, [0x2C]))

    def test_summarize_clock_rate(self):
        import watch_values
        # 60 fps with truncation: 17 ticks/frame -> 1020 ticks/s; then a stopped clock (> 1024 fps)
        lines = [f"[{t * 250:10.1f} ms] clock={int(t * 0.25 * 1020)} dt:f=0.01666667" for t in range(41)]
        lines += [f"[{(41 + t) * 250:10.1f} ms] clock={int(40 * 0.25 * 1020)} dt:f=0.0003333" for t in range(40)]
        rows = watch_values.summarize(["# header"] + lines, window_s=5.0)
        self.assertAlmostEqual(rows[0]["clock_rate"], 1020 / 1024, places=2)
        self.assertEqual(rows[0]["fps_from_dt"], 60.0)
        self.assertAlmostEqual(rows[0]["predicted"], 17 / (1024 / 60), places=3)
        self.assertEqual(rows[-1]["clock_rate"], 0.0)
        self.assertGreater(rows[-1]["fps_from_dt"], 2900)
        self.assertEqual(rows[-1]["predicted"], 0.0)

    def test_summarize_reset_and_pause(self):
        import watch_values
        lines = ["[0 ms] clock=500 dt:f=0.01", "[2000 ms] clock=520 dt:f=0.01", "[4000 ms] clock=3 dt:f=0.01",
                 "[6000 ms] clock=3 dt:f=0.02", "[8000 ms] clock=3 dt:f=0.02", "[10000 ms] clock=3 dt:f=0.02",
                 "[12000 ms] clock=3 dt:f=0.02"]
        rows = watch_values.summarize(lines, window_s=3.0)
        self.assertIsNone(rows[0]["clock_rate"])  # 520 -> 3: engine reset, not a huge wrap-around rate
        self.assertTrue(rows[-1]["paused"])


class IatAnnotateTests(unittest.TestCase):
    def test_forwarders_resolve_to_forwarding_name(self):
        amap = iat_annotate.build_address_map(MODULES)
        self.assertIn(("KERNEL32.DLL", "HeapAlloc", True), amap[0x77002000])
        self.assertIn(("ntdll.dll", "RtlAllocateHeap", False), amap[0x77002000])

    def test_annotate_groups(self):
        img, iat_rva = _image_with_iat()
        res = iat_annotate.annotate(img, 0x400000, MODULES)
        best = res["clusters"][0]
        self.assertEqual(best["start_va"], 0x400000 + iat_rva)
        self.assertEqual(best["entries"], 5)
        k32, winmm = best["groups"]
        self.assertEqual(k32["dll"], "KERNEL32.DLL")
        self.assertEqual([s["name"] for s in k32["slots"]], ["GetTickCount", "HeapAlloc", "Sleep"])
        self.assertEqual(k32["unexplained"], 0)
        self.assertEqual(winmm["dll"], "WINMM.dll")
        self.assertEqual([s["name"] for s in winmm["slots"]], ["timeGetTime", "#7"])
        self.assertEqual(winmm["slots"][1]["va"], 0x400000 + iat_rva + 0x14)
        self.assertEqual(len(res["clusters"]), 1)

    def test_analysis_image_keeps_entry_and_imports(self):
        data = to_mapped(build_pe(imports={"KERNEL32.dll": ["Sleep"]}).data)
        out = iat_annotate.analysis_image(data)
        e = struct.unpack_from("<I", data, 0x3C)[0]
        opt = e + 24
        self.assertEqual(out[opt + 16:opt + 20], data[opt + 16:opt + 20])        # entry point
        self.assertEqual(out[opt + 96 + 8:opt + 96 + 16], data[opt + 96 + 8:opt + 96 + 16])  # IMPORT dir
        self.assertEqual(struct.unpack_from("<I", out, opt + 36)[0], 0x1000)     # FileAlignment
        tab = opt + struct.unpack_from("<H", data, e + 20)[0]
        _vs, va, _rs, rptr = struct.unpack_from("<IIII", out, tab + 8)
        self.assertEqual(va, rptr)
        self.assertEqual(out[0x1000:], data[0x1000:])                            # bytes untouched


@unittest.skipUnless(sys.platform == "win32", "Windows only")
class RuntimeDumpTests(unittest.TestCase):
    def test_read_only_access_reads_own_memory(self):
        import runtime_dump
        payload = bytes(range(256)) * 32
        buf = ctypes.create_string_buffer(payload, len(payload))
        proc = runtime_dump.Process(os.getpid())
        try:
            self.assertEqual(proc.read(ctypes.addressof(buf), len(payload)), payload)
            self.assertTrue(proc.image_path().lower().endswith(".exe"))
        finally:
            proc.close()

    def test_find_pids_finds_this_process(self):
        import runtime_dump
        name = Path(sys.executable).name
        self.assertIn(os.getpid(), runtime_dump.find_pids(name))

    def test_watch_chain_reads_own_memory(self):
        import runtime_dump
        import watch_values
        target = (ctypes.c_uint32 * 32)()
        target[0x64 // 4], target[0x68 // 4] = 1280, 720
        holder = ctypes.c_uint32(ctypes.addressof(target))  # pointer like g_pSysInterface
        name, base, offs = watch_values.parse_chain(f"h=0x{ctypes.addressof(holder):X}>+0x68")
        self.assertEqual((name, offs), ("h", [0x68]))
        proc = runtime_dump.Process(os.getpid())
        try:
            # 64-bit test process: pointers are 8 bytes, so only check when the address fits a dword
            if ctypes.addressof(target) < 2**32:
                self.assertEqual(watch_values.read_chain(proc.u32, base, offs), 720)
            self.assertEqual(proc.u32(ctypes.addressof(target) + 0x64), 1280)
        finally:
            proc.close()

    def test_unknown_hash_profile_absent(self):
        import runtime_dump
        self.assertNotIn("0" * 64, runtime_dump.PROFILES)


if __name__ == "__main__":
    unittest.main()
