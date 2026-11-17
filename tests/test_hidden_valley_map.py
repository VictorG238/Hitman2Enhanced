import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('hidden_valley_map', ROOT / 'tools' / 'hidden_valley_map.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class HiddenValleyMapTest(unittest.TestCase):
    def test_rejects_unidentified_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'unknown.zip'
            archive.write_bytes(b'not the expected scene')
            with self.assertRaisesRegex(ValueError, 'Unexpected archive hash'):
                module.extract(archive)

    def test_known_scene_points_when_snapshot_is_available(self):
        archive = ROOT / 'samples/local/mission-investigation-20261116/C3-2a__MAIN.ZIP'
        if not archive.exists():
            self.skipTest('local game scene snapshot not available')
        result = module.extract(archive)
        self.assertEqual(len(result['splines']), 10)
        self.assertEqual(len(result['patrol']), 72)
        self.assertEqual(len(result['checkpoint']), 9)
        self.assertEqual(len(result['guards']), 4)
        self.assertEqual(result['checkpoint'][0]['name'], 'Examine#0')
        self.assertAlmostEqual(result['checkpoint'][0]['p'][1], -3200, places=0)
        self.assertEqual(result['start_paths'], [8, 6, 4, 2, 0])
        self.assertEqual([g['script_id'] for g in result['guards']], [1, 0, 2, 3])
        self.assertEqual(result['guards'][0]['references'],
                         {'examine': 'Examine#0', 'examine2': 'Examine#1', 'end': 'End'})
        self.assertEqual(result['patrol'][0]['name'], 'Arrow#00')
        self.assertAlmostEqual(result['patrol'][0]['p'][1], -3191.8, places=0)
        self.assertEqual(sum(len(s['v']) for s in result['splines']), 849)


if __name__ == '__main__':
    unittest.main()
