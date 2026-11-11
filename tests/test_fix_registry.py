"""Consistency of the fix framework (D-009): C++ registry, installer ids, docs, release defaults, versions."""
from __future__ import annotations

import re
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(REPO / "tools"))
import d3d8_logger_install as inst  # noqa: E402
import package_release as pkg  # noqa: E402


def cpp_fixes() -> list[tuple[str, str]]:
    """(variable, id) of every `extern const Fix kXxxFix = {"id", ...}` in src/fixes."""
    found = []
    for f in sorted((REPO / "src" / "fixes").glob("*.cpp")):
        found += re.findall(r'extern const Fix (k\w+Fix) = \{"([a-z0-9_]+)"', f.read_text(encoding="utf-8"))
    return found


def cpp_fix_ids() -> list[str]:
    return [fix_id for _, fix_id in cpp_fixes()]


class RegistryTests(unittest.TestCase):
    def test_installer_ids_match_cpp_fixes(self):
        self.assertEqual(sorted(cpp_fix_ids()), sorted(inst.FIX_IDS))

    def test_every_fix_registered_and_documented(self):
        registry = (REPO / "src" / "fixes" / "registry.cpp").read_text(encoding="utf-8")
        fixes_doc = (REPO / "docs" / "FIXES.md").read_text(encoding="utf-8")
        self.assertTrue(cpp_fixes())
        for var, fix_id in cpp_fixes():
            self.assertIn(f"&{var}", registry)
            self.assertIn(f"`{fix_id}`", fixes_doc)

    def test_release_fixes_exist(self):
        self.assertTrue(set(pkg.RELEASE_FIXES) <= set(inst.FIX_IDS))

    def test_versions_agree(self):
        version = pkg.read_version()
        cmake = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(f"VERSION {version}", cmake)
        self.assertIn(f"## {version}", (REPO / "CHANGELOG.md").read_text(encoding="utf-8"))


class PackageTests(unittest.TestCase):
    def test_build_zip(self):
        version = pkg.read_version()
        with tempfile.TemporaryDirectory() as tmp:
            dll = Path(tmp) / "d3d8.dll"
            dll.write_bytes(b"MZ... Hitman 2 Enhanced " + version.encode() + b" ...")
            target = pkg.build(dll, Path(tmp) / "dist")
            self.assertEqual(target.name, f"Hitman2Enhanced-{version}.zip")
            with zipfile.ZipFile(target) as z:
                self.assertEqual(sorted(z.namelist()), ["CHANGELOG.md", "README.txt", "d3d8.dll", "h2e.ini"])
                ini = z.read("h2e.ini").decode("ascii")
        self.assertIn("[resolution]\r\nEnabled=1", ini)
        self.assertIn("[horplus]\r\nEnabled=0", ini)
        self.assertIn("Level=1", ini)

    def test_refuses_dll_of_other_version(self):
        with tempfile.TemporaryDirectory() as tmp:
            dll = Path(tmp) / "d3d8.dll"
            dll.write_bytes(b"Hitman 2 Enhanced 0.0.1")
            with self.assertRaises(SystemExit):
                pkg.build(dll, Path(tmp) / "dist")


if __name__ == "__main__":
    unittest.main()
