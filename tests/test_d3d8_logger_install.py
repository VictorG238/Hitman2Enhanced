"""Tests for tools/d3d8_logger_install.py (temp dirs only; never the real game folder)."""
from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
import d3d8_logger_install as inst  # noqa: E402


class InstallTests(unittest.TestCase):
    def setUp(self):
        running = mock.patch.object(inst, "game_running", return_value=False)
        running.start()
        self.addCleanup(running.stop)
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.game = root / "game"
        self.game.mkdir()
        exe = self.game / "hitman2.exe"
        exe.write_bytes(b"fake exe")
        self.known = {hashlib.sha256(b"fake exe").hexdigest(): "test"}
        self.dll = root / "d3d8.dll"
        self.dll.write_bytes(b"proxy dll")
        self.logs = root / "logs"

    def tearDown(self):
        self.tmp.cleanup()

    def test_roundtrip_restores_folder(self):
        before = sorted(p.name for p in self.game.iterdir())
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True):
            self.assertIn("no fixes", inst.install(self.game, self.dll))
            self.assertEqual(inst.status(self.game), "proxy installed")
            self.assertTrue((self.game / inst.CONFIG).exists())
            (self.game / inst.LOG).write_text("log line\n")
            inst.uninstall(self.game, self.logs)
        self.assertEqual(sorted(p.name for p in self.game.iterdir()), before)
        moved = list(self.logs.iterdir())
        self.assertEqual(len(moved), 1)
        self.assertEqual(moved[0].read_text(), "log line\n")

    def test_fixes_write_config_and_uninstall_removes_it(self):
        before = sorted(p.name for p in self.game.iterdir())
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True):
            msg = inst.install(self.game, self.dll, ["horplus", "resolution"], log_level=1)
            self.assertIn("log level 1, fixes ['horplus', 'resolution']", msg)
            cfg = (self.game / inst.CONFIG).read_bytes().decode("ascii")
            self.assertIn("[log]\r\n", cfg)
            self.assertIn("Level=1\r\n", cfg)
            self.assertIn("[resolution]\r\nEnabled=1\r\n", cfg)
            self.assertIn("[horplus]\r\nEnabled=1\r\n", cfg)
            self.assertIn("fixes ['horplus', 'resolution']", inst.status(self.game))
            inst.uninstall(self.game, self.logs)
        self.assertEqual(sorted(p.name for p in self.game.iterdir()), before)

    def test_hdfonts_copies_and_removes_pack(self):
        before = sorted(p.name for p in self.game.iterdir())
        pack = Path(self.tmp.name) / "pack.bin"
        pack.write_bytes(b"H2EF pack")
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True):
            inst.install(self.game, self.dll, ["hdfonts"], hdfonts_pack=pack)
            self.assertEqual((self.game / inst.HDFONTS_PACK).read_bytes(), b"H2EF pack")
            self.assertIn(b"[hdfonts]\r\nEnabled=1\r\n", (self.game / inst.CONFIG).read_bytes())
            inst.uninstall(self.game, self.logs)
        self.assertEqual(sorted(p.name for p in self.game.iterdir()), before)

    def test_hdfonts_needs_a_pack_and_keeps_an_edited_one(self):
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True):
            with self.assertRaises(SystemExit):
                inst.install(self.game, self.dll, ["hdfonts"], hdfonts_pack=Path(self.tmp.name) / "missing.bin")
            self.assertFalse((self.game / "d3d8.dll").exists())
            pack = Path(self.tmp.name) / "pack.bin"
            pack.write_bytes(b"H2EF pack")
            inst.install(self.game, self.dll, ["hdfonts"], hdfonts_pack=pack)
        (self.game / inst.HDFONTS_PACK).write_bytes(b"changed")
        with self.assertRaises(SystemExit):
            inst.uninstall(self.game, self.logs)
        self.assertTrue((self.game / "d3d8.dll").exists())  # nothing removed before the refusal

    def test_pack_not_copied_without_hdfonts(self):
        pack = Path(self.tmp.name) / "pack.bin"
        pack.write_bytes(b"H2EF pack")
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True):
            inst.install(self.game, self.dll, ["resolution"], hdfonts_pack=pack)
        self.assertFalse((self.game / inst.HDFONTS_PACK).exists())

    def test_single_fix_default_research_level(self):
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True):
            inst.install(self.game, self.dll, ["resolution"])
            cfg = (self.game / inst.CONFIG).read_text(encoding="ascii")
            self.assertIn("Level=2", cfg)
            self.assertIn("[resolution]\nEnabled=1", cfg)
            self.assertIn("[horplus]\nEnabled=0", cfg)

    def test_uninstall_refuses_edited_config(self):
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True):
            inst.install(self.game, self.dll)
        (self.game / inst.CONFIG).write_text("[log]\nLevel=0\n")
        with self.assertRaises(SystemExit):
            inst.uninstall(self.game, self.logs)
        self.assertTrue((self.game / inst.CONFIG).exists())

    def test_refuses_foreign_config(self):
        (self.game / inst.CONFIG).write_text("[log]\n")
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True), self.assertRaises(SystemExit):
            inst.install(self.game, self.dll, ["resolution"])
        self.assertFalse((self.game / "d3d8.dll").exists())

    def test_refuses_unknown_fix(self):
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True), self.assertRaises(SystemExit):
            inst.install(self.game, self.dll, ["shadows"])

    def test_refuses_while_game_running(self):
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True):
            inst.install(self.game, self.dll)
            before = sorted(p.name for p in self.game.iterdir())
            with mock.patch.object(inst, "game_running", return_value=True):
                with self.assertRaises(SystemExit):
                    inst.uninstall(self.game, self.logs)
                with self.assertRaises(SystemExit):
                    inst.install(self.game, self.dll)
            self.assertEqual(sorted(p.name for p in self.game.iterdir()), before)

    def test_refuses_unknown_exe(self):
        with mock.patch.dict(inst.KNOWN_EXE, {}, clear=True), self.assertRaises(SystemExit):
            inst.install(self.game, self.dll)

    def test_never_overwrites_existing_d3d8(self):
        (self.game / "d3d8.dll").write_bytes(b"someone else's")
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True), self.assertRaises(SystemExit):
            inst.install(self.game, self.dll)
        self.assertEqual((self.game / "d3d8.dll").read_bytes(), b"someone else's")

    def test_uninstall_refuses_foreign_dll(self):
        with mock.patch.dict(inst.KNOWN_EXE, self.known, clear=True):
            inst.install(self.game, self.dll)
        (self.game / "d3d8.dll").write_bytes(b"replaced by something else")
        with self.assertRaises(SystemExit):
            inst.uninstall(self.game, self.logs)
        self.assertTrue((self.game / "d3d8.dll").exists())

    def test_uninstall_without_marker_refuses(self):
        (self.game / "d3d8.dll").write_bytes(b"x")
        with self.assertRaises(SystemExit):
            inst.uninstall(self.game, self.logs)
        self.assertIn("NOT installed", inst.status(self.game))


if __name__ == "__main__":
    unittest.main()
