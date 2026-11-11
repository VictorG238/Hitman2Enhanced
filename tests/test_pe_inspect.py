"""Unit tests for tools/pe_inspect.py using synthetic PE images (stdlib unittest)."""
from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
import random
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
sys.path.insert(0, str(HERE))

import pe_inspect  # noqa: E402
from pe_builder import build_pe, version_blob  # noqa: E402

IMPORTS = {"KERNEL32.dll": ["GetProcAddress", "LoadLibraryA", 42], "d3d8.dll": ["Direct3DCreate8"]}
DELAY = {"WINMM.dll": ["timeGetTime", 7]}
EXPORTS = ["Create", "Destroy"]
FORWARDERS = {"Forwarded": "NTDLL.RtlFoo"}
RICH = [(0x5D, 0x7809, 3), (1, 0, 42), (0x0A, 8047, 11)]


def _full(pe32plus=False):
    return build_pe(pe32plus=pe32plus, imports=IMPORTS, delay_imports=DELAY, exports=EXPORTS,
                    forwarders=FORWARDERS, ordinal_only_exports=1, export_ordinal_base=5,
                    pdb_path=r"C:\build\test.pdb", rich_entries=RICH, overlay=b"OVERLAY!" * 4,
                    extra_resources=[("MYDATA", "BLOB", 0, b"hello"), (10, 7, 0x409, b"rc")])


class HeaderTests(unittest.TestCase):
    def test_pe32_headers(self):
        built = _full()
        r = pe_inspect.inspect_bytes(built.data)
        self.assertEqual(r["format"], "PE32")
        self.assertEqual(r["coff"]["machine"], "i386")
        self.assertEqual(r["coff"]["timestamp_utc"], "2003-06-24T14:01:57Z")
        self.assertIn("DLL", r["coff"]["flags"])
        self.assertEqual(r["optional"]["image_base"], 0x10000000)
        self.assertEqual(r["optional"]["entry_point_rva"], 0x1000)
        self.assertEqual(r["optional"]["linker_version"], "6.0")
        self.assertEqual(r["optional"]["size_of_image"], built.expected["size_of_image"])
        self.assertEqual(r["sha256"], hashlib.sha256(built.data).hexdigest())
        self.assertEqual(r["file_size"], len(built.data))
        self.assertEqual(r["warnings"], [])

    def test_pe32plus_headers(self):
        r = pe_inspect.inspect_bytes(_full(pe32plus=True).data)
        self.assertEqual(r["format"], "PE32+")
        self.assertEqual(r["coff"]["machine"], "AMD64")
        self.assertEqual(r["optional"]["image_base"], 0x180000000)
        self.assertIsNone(r["optional"]["base_of_data"])
        self.assertIn("NX_COMPAT", r["optional"]["dll_flags"])
        self.assertEqual(r["warnings"], [])

    def test_sections_and_overlay(self):
        built = _full()
        r = pe_inspect.inspect_bytes(built.data)
        got = [(s["name"], s["virtual_address"], s["raw_pointer"], s["raw_size"]) for s in r["sections"]]
        self.assertEqual(got, built.expected["sections"])
        text = r["sections"][0]
        self.assertIn("EXECUTE", text["flags"])
        self.assertEqual(r["overlay"], {"offset": built.expected["overlay_offset"], "size": 32})

    def test_no_overlay(self):
        r = pe_inspect.inspect_bytes(build_pe().data)
        self.assertIsNone(r["overlay"])

    def test_checksum_not_set(self):
        r = pe_inspect.inspect_bytes(build_pe().data)
        self.assertEqual(r["checksum"]["stored"], 0)
        self.assertIsNone(r["checksum"]["matches"])

    def test_entropy_bounds(self):
        self.assertEqual(pe_inspect._entropy(b""), 0.0)
        self.assertEqual(pe_inspect._entropy(b"\0" * 100), 0.0)
        self.assertEqual(pe_inspect._entropy(bytes(range(256))), 8.0)


class DirectoryTests(unittest.TestCase):
    def _check_imports(self, pe32plus):
        built = _full(pe32plus)
        r = pe_inspect.inspect_bytes(built.data)
        self.assertEqual([i["dll"] for i in r["imports"]], list(IMPORTS))
        for imp in r["imports"]:
            exp = built.expected["imports"][imp["dll"]]
            got = []
            for f in imp["functions"]:
                got.append(("ordinal", f["ordinal"]) if "ordinal" in f else ("name", f["name"], f["hint"]))
            self.assertEqual(got, exp)
            self.assertEqual([f["iat_va"] for f in imp["functions"]], built.expected["iat_va"][imp["dll"]])

    def test_imports_pe32(self):
        self._check_imports(False)

    def test_imports_pe32plus(self):
        self._check_imports(True)

    def test_delay_imports(self):
        for pe32plus in (False, True):
            built = _full(pe32plus)
            r = pe_inspect.inspect_bytes(built.data)
            self.assertEqual(len(r["delay_imports"]), 1)
            d = r["delay_imports"][0]
            self.assertEqual(d["dll"], "WINMM.dll")
            self.assertTrue(d["rva_based"])
            self.assertEqual(d["functions"][0]["name"], "timeGetTime")
            self.assertEqual(d["functions"][1]["ordinal"], 7)

    def test_exports(self):
        built = _full()
        r = pe_inspect.inspect_bytes(built.data)
        ex = r["exports"]
        self.assertEqual(ex["dll_name"], "test.dll")
        self.assertEqual(ex["ordinal_base"], 5)
        by_ord = {f["ordinal"]: f for f in ex["functions"]}
        self.assertEqual(set(by_ord), set(built.expected["exports"]))
        for ordinal, exp in built.expected["exports"].items():
            f = by_ord[ordinal]
            self.assertEqual(f["names"], [exp["name"]] if exp["name"] else [])
            self.assertEqual(f["rva"], exp["rva"])
            if exp["forwarder"]:
                self.assertEqual(f["forwarder"], exp["forwarder"])
                self.assertNotIn("va", f)
            else:
                self.assertEqual(f["va"], built.image_base + exp["rva"])

    def test_resources_and_version(self):
        r = pe_inspect.inspect_bytes(_full().data)
        types = {(res["type"], res["name"], res["language"]): res["size"] for res in r["resources"]}
        self.assertEqual(types[("MYDATA", "BLOB", 0)], 5)
        self.assertEqual(types[("RCDATA", 7, 0x409)], 2)
        self.assertIn(("VERSION", 1, 0x409), types)
        vi = r["version_info"]
        self.assertEqual(vi["fixed"]["file_version"], "1.2.3.4")
        self.assertEqual(vi["fixed"]["product_version"], "5.6.7.8")
        self.assertEqual(vi["strings"]["040904b0"]["ProductName"], "Test Product")

    def test_version_parser_direct(self):
        blob = version_blob(file_version=(1, 0, 0, 277), strings={"CompanyName": "X", "Empty": ""})
        vi = pe_inspect.parse_version_info(blob)
        self.assertEqual(vi["fixed"]["file_version"], "1.0.0.277")
        self.assertEqual(vi["strings"]["040904b0"], {"CompanyName": "X", "Empty": ""})

    def test_debug_codeview(self):
        r = pe_inspect.inspect_bytes(_full().data)
        cv = r["debug"][0]["codeview"]
        self.assertEqual(r["debug"][0]["type"], "CODEVIEW")
        self.assertEqual(cv["format"], "RSDS")
        self.assertEqual(cv["pdb_path"], r"C:\build\test.pdb")
        self.assertEqual(cv["age"], 3)
        self.assertEqual(cv["guid"], bytes(range(16)).hex())

    def test_rich_header(self):
        built = _full()
        rich = pe_inspect.inspect_bytes(built.data)["rich_header"]
        self.assertEqual(rich["key"], f"0x{built.expected['rich_key']:08x}")
        self.assertTrue(rich["checksum_valid"])
        self.assertEqual([(e["prod_id"], e["build"], e["count"]) for e in rich["entries"]], RICH)

    def test_rich_checksum_detects_tampering(self):
        data = bytearray(_full().data)
        data[0x50] ^= 0xFF  # inside the DOS stub, covered by the Rich checksum
        self.assertFalse(pe_inspect.inspect_bytes(bytes(data))["rich_header"]["checksum_valid"])

    def test_minimal_image_has_no_optional_directories(self):
        r = pe_inspect.inspect_bytes(build_pe(version=None).data)
        self.assertEqual(r["imports"], [])
        self.assertIsNone(r["exports"])
        self.assertEqual(r["resources"], [])
        self.assertIsNone(r["version_info"])
        self.assertIsNone(r["rich_header"])


def to_mapped(data: bytes) -> bytes:
    """Lay a PE file out the way the loader maps it (RVA == offset)."""
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    nsect, opt_size = struct.unpack_from("<H", data, e_lfanew + 6)[0], struct.unpack_from("<H", data, e_lfanew + 20)[0]
    opt = e_lfanew + 24
    size_of_image, size_of_headers = struct.unpack_from("<II", data, opt + 56)
    img = bytearray(size_of_image)
    img[:size_of_headers] = data[:size_of_headers]
    for i in range(nsect):
        _n, vsize, va, rsize, rptr = struct.unpack_from("<8sIIII", data, opt + opt_size + 40 * i)
        img[va:va + rsize] = data[rptr:rptr + rsize]
    return bytes(img)


class MappedImageTests(unittest.TestCase):
    def test_mapped_matches_file_layout(self):
        for pe32plus in (False, True):
            data = _full(pe32plus).data
            f = pe_inspect.inspect_bytes(data)
            m = pe_inspect.inspect_bytes(to_mapped(data), mapped=True)
            self.assertEqual(m["layout"], "mapped")
            self.assertEqual(m["warnings"], [])
            for key in ("imports", "delay_imports", "exports", "version_info", "rich_header"):
                self.assertEqual(m[key], f[key], key)
            self.assertEqual(m["debug"][0]["codeview"], f["debug"][0]["codeview"])
            self.assertEqual([r["name"] for r in m["resources"]], [r["name"] for r in f["resources"]])
            self.assertIsNone(m["overlay"])

    def test_cli_mapped_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "image.bin"
            path.write_bytes(to_mapped(_full().data))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = pe_inspect.main([str(path), "--mapped", "--json"])
            self.assertEqual(code, 0)
            self.assertEqual(json.loads(out.getvalue())[0]["exports"]["dll_name"], "test.dll")


class ChecksumTests(unittest.TestCase):
    def test_checksum_roundtrip(self):
        built = bytearray(build_pe().data)
        e_lfanew = struct.unpack_from("<I", built, 0x3C)[0]
        off = e_lfanew + 24 + 64
        value = pe_inspect.compute_pe_checksum(bytes(built), off)
        struct.pack_into("<I", built, off, value)
        r = pe_inspect.inspect_bytes(bytes(built))
        self.assertEqual(r["checksum"]["stored"], value)
        self.assertTrue(r["checksum"]["matches"])
        # The computed value must not depend on the stored field.
        self.assertEqual(pe_inspect.compute_pe_checksum(bytes(built), off), value)

    def test_checksum_odd_length(self):
        data = build_pe(overlay=b"x").data
        self.assertEqual(len(data) % 2, 1)
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        pe_inspect.compute_pe_checksum(data, e_lfanew + 24 + 64)  # must not raise


class MalformedInputTests(unittest.TestCase):
    def test_not_mz(self):
        for blob in (b"", b"XX" + b"\0" * 100, b"MZ"):
            with self.assertRaises(pe_inspect.PEFormatError):
                pe_inspect.inspect_bytes(blob)

    def test_bad_e_lfanew(self):
        data = bytearray(build_pe().data)
        struct.pack_into("<I", data, 0x3C, 0xFFFFFF00)
        with self.assertRaises(pe_inspect.PEFormatError):
            pe_inspect.inspect_bytes(bytes(data))

    def test_bad_pe_signature(self):
        data = bytearray(build_pe().data)
        data[0x80:0x84] = b"NE\0\0"
        with self.assertRaises(pe_inspect.PEFormatError):
            pe_inspect.inspect_bytes(bytes(data))

    def test_bad_optional_magic(self):
        data = bytearray(build_pe().data)
        struct.pack_into("<H", data, 0x80 + 24, 0x1234)
        with self.assertRaises(pe_inspect.PEFormatError):
            pe_inspect.inspect_bytes(bytes(data))

    def test_corrupt_import_directory_is_a_warning(self):
        data = bytearray(build_pe(imports=IMPORTS).data)
        dd = 0x80 + 24 + 96
        struct.pack_into("<I", data, dd + 8, 0x7FFF0000)  # IMPORT rva -> unmapped
        r = pe_inspect.inspect_bytes(bytes(data))
        self.assertEqual(r["imports"], [])
        self.assertTrue(any(w.startswith("imports:") for w in r["warnings"]))

    def test_truncations_never_crash(self):
        data = _full().data
        for n in range(0, len(data), 23):
            try:
                pe_inspect.inspect_bytes(data[:n])
            except pe_inspect.PEFormatError:
                pass

    def test_random_mutations_never_crash(self):
        rng = random.Random(1234)  # fixed seed: reproducible
        base = _full().data
        for _ in range(300):
            data = bytearray(base)
            for _ in range(rng.randint(1, 8)):
                data[rng.randrange(0, 0x800)] = rng.randrange(256)
            try:
                pe_inspect.inspect_bytes(bytes(data))
            except pe_inspect.PEFormatError:
                pass


class CliTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.dll = self.dir / "sample.dll"
        self.dll.write_bytes(_full().data)

    def tearDown(self):
        self.tmp.cleanup()

    def _run(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = pe_inspect.main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def test_json_output(self):
        code, out, _ = self._run(str(self.dll), "--json")
        self.assertEqual(code, 0)
        reports = json.loads(out)
        self.assertEqual(reports[0]["file_name"], "sample.dll")
        self.assertEqual(reports[0]["exports"]["dll_name"], "test.dll")

    def test_text_output(self):
        code, out, _ = self._run(str(self.dll))
        self.assertEqual(code, 0)
        self.assertIn("Direct3DCreate8", out)
        self.assertIn("@5", out)

    def test_directory_and_output_dir(self):
        (self.dir / "notes.txt").write_text("ignored")
        outdir = self.dir / "reports"
        code, _, _ = self._run(str(self.dir), "--summary", "--output-dir", str(outdir))
        self.assertEqual(code, 0)
        self.assertEqual(sorted(p.name for p in outdir.iterdir()), ["sample.dll.json"])

    def test_non_pe_file_sets_error_status(self):
        bad = self.dir / "bad.exe"
        bad.write_bytes(b"not a pe")
        code, _, err = self._run(str(bad), str(self.dll), "--summary")
        self.assertEqual(code, 1)
        self.assertIn("missing MZ", err)

    def test_inspection_does_not_modify_file(self):
        before = (self.dll.read_bytes(), os.stat(self.dll).st_mtime_ns)
        pe_inspect.inspect_file(self.dll)
        self.assertEqual(before, (self.dll.read_bytes(), os.stat(self.dll).st_mtime_ns))


if __name__ == "__main__":
    unittest.main()
