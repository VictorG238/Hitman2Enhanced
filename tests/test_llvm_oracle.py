"""Cross-check pe_inspect against an independent implementation (llvm-readobj).

Targets: real Windows system DLLs (PE32 from SysWOW64, PE32+ from System32) and,
if HITMAN2_SNAPSHOT points to a snapshot directory, every game EXE/DLL in it.
Skipped automatically when llvm-readobj or the targets are unavailable.
Also checks the PE checksum and Rich checksum algorithms against Microsoft-built
system files, whose stored checksums are known to be valid.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
import pe_inspect  # noqa: E402

READOBJ = shutil.which("llvm-readobj") or next(
    (p for p in (r"C:\Program Files\LLVM\bin\llvm-readobj.exe",) if Path(p).exists()), None)
WINDIR = Path(os.environ.get("SystemRoot", r"C:\Windows"))
SYSTEM_TARGETS = [WINDIR / "SysWOW64" / n for n in ("kernel32.dll", "user32.dll", "shell32.dll")] + \
                 [WINDIR / "System32" / n for n in ("kernel32.dll", "shell32.dll", "oleaut32.dll")]


def _targets() -> list[Path]:
    out = [p for p in SYSTEM_TARGETS if p.exists()]
    snap = os.environ.get("HITMAN2_SNAPSHOT")
    if snap and Path(snap).is_dir():
        out += sorted(p for p in Path(snap).iterdir() if p.suffix.lower() in (".exe", ".dll"))
    return out


def _readobj(path: Path, *flags: str) -> str | None:
    proc = subprocess.run([READOBJ, *flags, str(path)], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    return proc.stdout if proc.returncode == 0 else None


def _llvm_imports(text: str) -> dict:
    # Top-level "Import {" / "DelayImport {" blocks; DelayImport nests one
    # "Import {" block per symbol, so track depth.
    result = {"Import": [], "DelayImport": []}
    kind, depth = None, 0
    for line in text.splitlines():
        s = line.strip()
        if s.endswith("{"):
            if depth == 0 and s in ("Import {", "DelayImport {"):
                kind = s[:-2]
                result[kind].append([None, []])
            depth += 1
        elif s == "}":
            depth -= 1
            if depth == 0:
                kind = None
        elif kind and depth == 1 and s.startswith("Name: ") and result[kind][-1][0] is None:
            result[kind][-1][0] = s[6:]
        elif kind and s.startswith("Symbol:"):
            m = re.match(r"Symbol: ?(.*) \((\d+)\)$", s)
            result[kind][-1][1].append((m.group(1).strip(), int(m.group(2))))
    return result


def _llvm_exports(text: str) -> set:
    out, cur = set(), None
    for line in text.splitlines():
        s = line.strip()
        if s == "Export {":
            cur = {"name": ""}
        elif cur is not None and s.startswith("Ordinal:"):
            cur["ordinal"] = int(s.split()[1])
        elif cur is not None and s.startswith("Name:"):
            cur["name"] = s[5:].strip()
        elif cur is not None and s.startswith("RVA:"):
            cur["target"] = int(s.split()[1], 16)
        elif cur is not None and s.startswith("ForwardedTo:"):
            cur["target"] = s.split(":", 1)[1].strip()
        elif cur is not None and s == "}":
            # llvm-readobj also lists empty export-address-table slots (RVA 0);
            # pe_inspect deliberately omits them (they are not real exports).
            if not (cur["name"] == "" and cur.get("target") == 0):
                out.add((cur["ordinal"], cur["name"], cur.get("target")))
            cur = None
    return out


def _mine_imports(funcs) -> list:
    return [("", f["ordinal"]) if "ordinal" in f else (f["name"], f["hint"]) for f in funcs]


def _mine_exports(exports) -> set:
    out = set()
    for f in (exports or {}).get("functions", []):
        target = f.get("forwarder", f["rva"])
        for name in f["names"] or [""]:
            out.add((f["ordinal"], name, target))
    return out


@unittest.skipUnless(READOBJ, "llvm-readobj not available")
class LlvmOracleTests(unittest.TestCase):
    def test_targets_exist(self):
        self.assertTrue(_targets(), "no system DLLs found to cross-check")

    def test_imports_match(self):
        for path in _targets():
            with self.subTest(path=path.name):
                text = _readobj(path, "--coff-imports")
                if text is None:
                    self.skipTest(f"llvm-readobj failed on {path}")
                ref = _llvm_imports(text)
                rep = pe_inspect.inspect_file(path)
                mine = [[i["dll"], _mine_imports(i["functions"])] for i in rep["imports"]]
                self.assertEqual(mine, ref["Import"])
                mine_d = [[i["dll"], _mine_imports(i["functions"])] for i in rep["delay_imports"]]
                self.assertEqual(mine_d, ref["DelayImport"])

    def test_exports_match(self):
        for path in _targets():
            rep = pe_inspect.inspect_file(path)
            if any(w.startswith("exports:") for w in rep["warnings"]):
                continue  # e.g. packed hitman2.exe: export table not present in the file
            with self.subTest(path=path.name):
                text = _readobj(path, "--coff-exports")
                if text is None:
                    continue
                self.assertEqual(_mine_exports(rep["exports"]), _llvm_exports(text))

    def test_headers_match(self):
        for path in _targets():
            with self.subTest(path=path.name):
                text = _readobj(path, "--file-headers")
                rep = pe_inspect.inspect_file(path)
                fields = dict(re.findall(r"^\s*(ImageBase|AddressOfEntryPoint|CheckSum): (0x[0-9A-Fa-f]+)",
                                         text, re.M))
                self.assertEqual(int(fields["ImageBase"], 16), rep["optional"]["image_base"])
                self.assertEqual(int(fields["AddressOfEntryPoint"], 16), rep["optional"]["entry_point_rva"])
                self.assertEqual(int(fields["CheckSum"], 16), rep["checksum"]["stored"])


class SystemChecksumTests(unittest.TestCase):
    """Microsoft system DLLs carry valid PE checksums and Rich headers."""

    def test_pe_and_rich_checksums(self):
        targets = [p for p in SYSTEM_TARGETS if p.exists()]
        if not targets:
            self.skipTest("no system DLLs")
        for path in targets:
            with self.subTest(path=str(path)):
                rep = pe_inspect.inspect_file(path)
                self.assertTrue(rep["checksum"]["matches"])
                if rep["rich_header"]:
                    self.assertTrue(rep["rich_header"]["checksum_valid"])


if __name__ == "__main__":
    unittest.main()
