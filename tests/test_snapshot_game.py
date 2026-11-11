"""Tests for tools/snapshot_game.py."""
from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
import snapshot_game  # noqa: E402


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.game = root / "game"
        self.game.mkdir()
        (self.game / "hitman2.exe").write_bytes(b"MZ fake exe")
        (self.game / "RenderD3D.dll").write_bytes(b"MZ fake dll")
        (self.game / "Hitman2.ini").write_text("Resolution 800x600\n")
        (self.game / "streams.wav").write_bytes(b"not copied")
        (self.game / "Scenes").mkdir()
        self.dest = root / "snap"

    def tearDown(self):
        self.tmp.cleanup()

    def test_copies_selected_files_with_manifest(self):
        entries = snapshot_game.snapshot(self.game, self.dest)
        self.assertEqual([e["name"] for e in entries],  # sorted case-insensitively
                         ["hitman2.exe", "Hitman2.ini", "RenderD3D.dll"])
        self.assertFalse((self.dest / "streams.wav").exists())
        for e in entries:
            data = (self.dest / e["name"]).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), e["sha256"])
            self.assertEqual(data, (self.game / e["name"]).read_bytes())
        lines = (self.dest / "MANIFEST.sha256").read_text().splitlines()
        self.assertEqual(len(lines), 3)
        self.assertTrue(all(" *" in line for line in lines))
        meta = json.loads((self.dest / "manifest.json").read_text())
        self.assertEqual(len(meta["files"]), 3)

    def test_source_untouched(self):
        before = {p.name: p.read_bytes() for p in self.game.iterdir() if p.is_file()}
        snapshot_game.snapshot(self.game, self.dest)
        after = {p.name: p.read_bytes() for p in self.game.iterdir() if p.is_file()}
        self.assertEqual(before, after)

    def test_refuses_non_empty_destination(self):
        self.dest.mkdir()
        (self.dest / "existing").write_text("x")
        with self.assertRaises(FileExistsError):
            snapshot_game.snapshot(self.game, self.dest)


if __name__ == "__main__":
    unittest.main()
