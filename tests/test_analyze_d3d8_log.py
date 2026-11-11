"""Tests for tools/analyze_d3d8_log.py on a synthetic log in the d3d8_logger format."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
import analyze_d3d8_log as a  # noqa: E402

LOG = """\
[       0 ms] === h2e d3d8_logger 0.2.0 attached, process 1 (x) ===
[     484 ms] CreateDevice (in)  BackBuffer=800x600 fmt=22(X8R8G8B8) count=1 Windowed=0 PresentInterval=0x1
[    3656 ms] SetViewport x=0 y=0 w=800 h=600 minZ=0.000 maxZ=1.000 aspect(w/h)=1.33333 caller=RenderD3D.dll+0x3322 (va 1)
[    3719 ms] SetTransform(PROJECTION) #12 caller=RenderD3D.dll+0x33DD (va 0x1)
[    3719 ms]   active viewport x=0 y=0 w=800 h=600 aspect(w/h)=1.33333
[    3719 ms]   [ 2.414214  0.000000  0.000000  0.000000]
[    3719 ms]   as LH perspective: fovY=34.516 deg fovX=45.000 deg aspect(x/y)=1.33333 zn=5.0000 zf=3000.08 offcenter=(0,0)
[   10938 ms] SetViewport x=0 y=0 w=128 h=128 minZ=0.000 maxZ=1.000 aspect(w/h)=1.00000 caller=RenderD3D.dll+0xD8C2 (va 2)
[   10938 ms] SetTransform(PROJECTION) #40974 caller=RenderD3D.dll+0xDBCE (va 0x2)
[   10938 ms]   active viewport x=0 y=0 w=128 h=128 aspect(w/h)=1.00000
[   10938 ms]   as LH perspective: fovY=1.855 deg fovX=0.738 deg aspect(x/y)=0.39806 zn=1.0000 zf=9999.34 offcenter=(-0.0061,0.0361)
[   10950 ms] SetTransform(PROJECTION) #40975 caller=RenderD3D.dll+0x33DD (va 0x1)
[   10950 ms]   [ 1.0 0 0 0]
[   11000 ms] Present: 7378 frames in 5.00s = 1475.6 fps (total 7378); projection calls 14741
[   16000 ms] Present: 2544 frames in 5.00s = 508.8 fps (total 9922); projection calls 12720
"""

LOG04 = """[       0 ms] === h2e d3d8_logger 0.4.0 attached, process 1 (x) ===
[     900 ms] SetRenderTarget rt=0A rt=0B caller=RenderD3D.dll+0xD800 (va 0x1, base 0x0)
[    5000 ms] Screen2D[5s] caller=RenderD3D.dll+0x5000 (va 0x1, base 0x0) fvf=0x144 draws=10 buffered=0 bbox=(0.0,20.0)-(960.0,1080.0)
[    5000 ms] Screen2D[5s] caller=RenderD3D.dll+0x6000 (va 0x1, base 0x0) fvf=0x144 draws=0 buffered=4 bbox=none
[    5000 ms] ProjSample[5s] caller=RenderD3D.dll+0x33DD (va 0x1, base 0x0) fovX=67.400 fovY=41.100 aspect(x/y)=1.77778 viewport=1920x1080+0+0
[    5000 ms] ProjSample[5s] caller=RenderD3D.dll+0xE000 (va 0x1, base 0x0) non-perspective _11=0.001 _22=0.001 _41=-1 _42=1 _44=1 viewport=1920x1080+0+0
[   10000 ms] Screen2D[5s] caller=RenderD3D.dll+0x5000 (va 0x1, base 0x0) fvf=0x144 draws=20 buffered=2 bbox=(5.0,10.0)-(1920.0,900.0)
[   10000 ms] ProjSample[5s] caller=RenderD3D.dll+0x33DD (va 0x1, base 0x0) fovX=67.400 fovY=41.100 aspect(x/y)=1.77778 viewport=1920x1080+0+0
[   12000 ms] Screen2D[total] caller=RenderD3D.dll+0x5000 (va 0x1, base 0x0) fvf=0x144 draws=30 buffered=2 bbox=(0.0,10.0)-(1920.0,1080.0)
"""


class AnalyzeTests(unittest.TestCase):
    def test_summary(self):
        r = a.analyze(LOG)
        self.assertEqual(len(r["devices"]), 1)
        self.assertEqual([(v["w"], v["h"]) for v in r["viewports"]], [(800, 600), (128, 128)])
        self.assertEqual(r["projection_callers"], {"RenderD3D.dll+0x33DD": 2, "RenderD3D.dll+0xDBCE": 1})
        self.assertEqual(r["projections_logged"], 3)
        self.assertEqual(r["projections_nonstandard"], 1)
        main = next(g for g in r["projection_groups"] if g["fovx"] == 45.0)
        self.assertEqual(main["viewports"], {"800x600": 1})
        self.assertEqual(main["aspect_matches_viewport"], 1)
        shadow = next(g for g in r["projection_groups"] if g["fovx"] < 1)
        self.assertEqual(shadow["aspect_matches_viewport"], 0)
        self.assertEqual(r["fps"], {"samples": 2, "min": 508.8, "max": 1475.6})
        self.assertEqual(r["screen_space"], {})

    def test_screen_space_and_samples(self):
        r = a.analyze(LOG04)
        hud = r["screen_space"]["RenderD3D.dll+0x5000"]
        self.assertEqual((hud["draws"], hud["buffered"], hud["windows"]), (30, 2, 2))
        self.assertEqual(hud["bbox"], [0.0, 10.0, 1920.0, 1080.0])
        self.assertEqual(r["screen_space"]["RenderD3D.dll+0x6000"]["bbox"], None)
        self.assertEqual(r["projection_samples"][0], {"caller": "RenderD3D.dll+0x33DD", "fovx": 67.4, "fovy": 41.1,
                                                      "aspect": 1.7778, "viewport": "1920x1080+0+0", "windows": 2})
        self.assertEqual(r["projection_samples"][1]["fovx"], "non-perspective")
        self.assertEqual(r["render_target_callers"], ["RenderD3D.dll+0xD800"])


if __name__ == "__main__":
    unittest.main()
