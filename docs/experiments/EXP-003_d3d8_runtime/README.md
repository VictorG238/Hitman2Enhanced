# EXP-003 — Direct3D 8 runtime investigation

- **Date**: 2026-11-10
- **Status**: run-001 (intro + menu), run-002 (in-game), run-003 (16:9 attempts), run-004 (height writer found), run-005/006 (windowed 1080p), run-007/008 (fixes, fullscreen) done; resolution fix confirmed, matrix-only Hor+ rejected (HUD shares the
  gameplay projection)
- **Binaries**: `hitman2.exe` `f85765f1…6701`, `RenderD3D.dll` `e9aff44931aa6c361fdcc49d1f5969d19777923c8f6c2c2e18e439bfd53c871d`
- **Proxy**: `build/Release/d3d8.dll` from `src/d3d8_logger/` (moved to `src/proxy/` in 0.6.0, D-009) — run-001 v0.1.0 SHA-256
  `27a6276ddd699663d30cd545718d36490eb6919bafa92784d23bda7f39a3de96`; run-002 v0.2.0 SHA-256
  `7c25a538439b9506f506080ea8cd31c3b36b434990102b8b391e6c85b58b541d`
- **Confidence**: CONFIRMED for logged runtime values; projection interpretation assumes a standard LH
  perspective matrix (checked by the logger's shape test before interpreting).

## Goal

Observe the real device parameters, viewport, aspect ratio and projection matrices, and tie each to the
RenderD3D code that issues it — without changing rendering behavior.

## Method

A Direct3D 8 logging proxy (`d3d8.dll`) chosen because EXP-001 confirmed RenderD3D reaches D3D8 only through
`Direct3DCreate8`. The proxy loads the system `d3d8.dll`, forwards `Direct3DCreate8`, and wraps vtable slots
(verified against mingw-w64 `d3d8.h`): IDirect3D8 6/7/8/15 (mode count, enum modes, display mode, CreateDevice) and
IDirect3DDevice8 14/15/37/40 (Reset, Present, SetTransform, SetViewport). Each wrapper calls the original with the
same arguments and returns its result unchanged. Logs are deduplicated; callers are reported as module+offset
(walking past Windows compatibility-shim frames, see KNOWN_ISSUES KI-008). `H2E_D3D8LOG=0` = pure pass-through.

Verification of the proxy itself: `ctest` smoke test (`tests/native/d3d8_logger_smoke.cpp`, now `proxy_smoke.cpp`) creates a real device
through the proxy and checks forwarding plus exact log content, including fovY=90°/aspect 4:3/zn=1/zf=100 derived
from a known matrix. Install/remove is guarded by `tools/d3d8_logger_install.py` (refuses to overwrite an existing
`d3d8.dll`, hash marker, log moved to `samples/local/logs/`).

## Procedure (reproducible)

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A Win32 && cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python -I tools/d3d8_logger_install.py install ..        # game folder (Steam runs the game from there)
# start the game through Steam, let it run (run-001: 60 s, intro + main menu, no input), quit the game
python -I tools/d3d8_logger_install.py uninstall ..      # removes d3d8.dll + marker, moves the log
```
After run-001 no `d3d8.dll`/marker/log remains in the game folder and all 18 binaries match the baseline manifest.
`Hitman2.ini` did **not** match afterwards: `DefaultScene` changed to `AllLevels/levelmenu.gms` (KNOWN_ISSUES KI-011). Full log: `samples/local/logs/h2e_d3d8-20261110-004813.log` (local); excerpt committed:
[`run-001-excerpt.log`](run-001-excerpt.log).

## Results (run-001, `Hitman2.ini`: `Resolution 800x600`, `ColorDepth 32`, no `Window` key)

1. `CONFIRMED` `Direct3DCreate8(SDKVersion=120)` called from RenderD3D+0x19F1.
2. `CONFIRMED` **CreateDevice** from **RenderD3D+0x1DB6** (= static 0x10001DB6, the return address of the
   `call [edx+0x3C]` in the sequence at 0x10001D8D identified in EXP-001): adapter 0, `D3DDEVTYPE_HAL`,
   BehaviorFlags 0x40, hFocusWindow = game window (style 0x96000000 → 0x16000000 after creation, 800×600 client).
   `D3DPRESENT_PARAMETERS`: **800×600**, X8R8G8B8, 1 back buffer, MultiSample 0, SwapEffect 1 (DISCARD),
   **Windowed=0 (fullscreen)**, AutoDepthStencil D24X8, Flags 0, **FullScreen_RefreshRateInHz=0 (default)**,
   **PresentInterval=0x1**. hr = S_OK. The resolution equals the ini value.
3. `CONFIRMED` RenderD3D **never called `GetAdapterModeCount`/`EnumAdapterModes`**; it called
   `GetAdapterDisplayMode` once (desktop 1920×1080@144 X8R8G8B8).
4. `CONFIRMED` **Viewports**: two values alternate within frames — full `800×600` and letterboxed
   **`x=0 y=75 w=800 h=450` (16:9)** — all from RenderD3D+0x3322.
5. `CONFIRMED` **Projection** matrices all come from **RenderD3D+0x33DD** (= static 0x100033DD, right after the
   `call [ecx+0x94]` candidate at 0x100033D7 from EXP-001). Standard LH perspective shape in every logged case.
   Observed (fovX / fovY / aspect / zn / zf): 45° / 34.52° / 1.3333 / 5 / 3000; **45° / 26.23° / 1.7778** / 5 / 3000;
   18.18° / 13.69° / 1.3333 / 5 / 3000 and 5000; 90° / 58.72° / 1.7778 / 5 / ~3e5; 70° / 55.41° / 1.3333 / 5 / 20000;
   ~43.1° / ~25.06° / 1.7778 / 5 / 10001 (animated); 90° / 90° / 1.0 / 0.5 / 10.
   ⇒ `CONFIRMED` for these scenes: in every matrix **_22 = _11 × aspect**, where the aspect is 1.3333 or 1.7778 —
   exactly the two viewport aspects observed (800×600, 800×450). The horizontal scale _11 carries the scene's FOV
   (45°, 18.18°, 70°, 90° …), so for a given fovX the vertical FOV shrinks when the aspect widens (**Vert-**):
   fovX=45° appears with fovY 34.5° (aspect 4:3) and 26.2° (aspect 16:9).
   `INFERRED`: the aspect fed into the matrix is the active viewport's (the logger records matrix and viewport
   separately; per-draw pairing was not logged).
6. `CONFIRMED` **Frame rate uncapped**: 1194–5282 presents/s in 5 s windows (111,713 frames in ~71 s) despite
   PresentInterval=0x1. 150,883 projection SetTransform calls (≈1.35 per frame).

## Results (run-002, 2026-11-10 00:57–00:59, owner loaded a mission and played ~50 s)

Proxy v0.2.0 (distinct-set dedup, active viewport logged with each projection). Same `Hitman2.ini`
(SHA-256 `468b41a7…bdb33`, unchanged by the run). After the run: proxy removed, 18 binaries == baseline.
Full log `samples/local/logs/h2e_d3d8-20261110-005935.log` (local); committed: [`run-002-excerpt.log`](run-002-excerpt.log),
[`run-002-summary.json`](run-002-summary.json) (from `tools/analyze_d3d8_log.py`).

1. `CONFIRMED` Same device as run-001: CreateDevice from RenderD3D+0x1DB6, 800×600 fullscreen X8R8G8B8, interval 0x1;
   one CreateDevice, no Reset. RenderD3D base this run 0x078C0000.
2. `CONFIRMED` **Gameplay camera** (RenderD3D+0x33DD, active viewport 800×600): **fovX 67.4°, fovY 53.15°, aspect 1.3333**,
   zn 5, zf ≈ 20 000 (and a second pass with zf ≈ 2.1e7). Loading/menu camera: fovX 65°, fovY 51.08°, zf 3000/30000.
   ⇒ `CONFIRMED` **the matrix aspect equals the active viewport aspect** for every scene camera (fovX 45/65/67.4°,
   zn 5): 5/5 — resolves the run-001 pairing inference. The exception is a fixed 90°/90°, aspect 1.0, zn 0.5, zf 10
   matrix issued once at startup (also in run-001); its purpose is `OPEN`.
3. `CONFIRMED` **Second projection source: RenderD3D+0xDBCE** (static 0x1000DBC8 call, the EXP-001 candidate) with a
   **128×128 viewport set at RenderD3D+0xD8C2**: off-center (e.g. _31=-0.0061, _32=0.0361) LH matrices with very
   narrow FOV (fovX 0.6–1.2°, fovY 1.6–1.9°), non-square aspects 0.35–0.67, zn 1, zf 9999. 1994 distinct matrices.
   `INFERRED`: render-to-texture pass — most likely per-character shadow maps (`ShadowDetail 2`). **A widescreen fix must
   not touch this pass.**
4. `CONFIRMED` Viewports (all from RenderD3D+0x3322 unless noted): 800×600; 128×128 (+0xD8C2); 640×480 at (80,60);
   550×169 at (159,276); 800×450 at (0,75) with 800×75 bars at y=0 and y=525 (cutscene letterbox).
   Purposes of 640×480 and 550×169: `OPEN` (menu/map/inventory overlays are candidates).
5. `CONFIRMED` In-game frame rate **509–1969 fps** (9 five-second samples; 54 123 frames in ~50 s), 3–5 projection
   SetTransform calls per frame.

Limitation: v0.2.0 used one global budget of 2000 distinct projections, exhausted by the shadow pass at ~16 s, so only
6 main-camera matrices were logged (the gameplay camera before the cap). v0.3.0 keeps a separate budget (1500) per call site.

## Results (run-003, 2026-11-10 ~01:03-01:08, owner set 16:9 resolutions in `Hitman2.ini`)

Proxy v0.3.0 (SHA-256 `bf69609bf742a6f405a4a602c5e1840162abfd757a947d2d03ec63d218e1101f`, per-call-site budgets).
The owner edited `Resolution` personally (the ini is owner-managed). Two game sessions in one log
(`samples/local/logs/h2e_d3d8-20261110-010841.log`, local); committed: [`run-003-excerpt.log`](run-003-excerpt.log),
[`run-003-summary.json`](run-003-summary.json). Afterwards: proxy removed, 18 binaries == baseline; ini left at the
owner's `Resolution 1280x720`.

1. `CONFIRMED` **`Resolution 1920x1080` -> CreateDevice BackBuffer = 1920 x 117156004** (0x06FBA8A4) -> hr 0x8876086C
   (D3DERR_INVALIDCALL) -> the game shows *"Direct3D: Unable to create device. Try changing resolution of color depth"*
   (owner screenshot) and exits. 0 frames.
2. `CONFIRMED` **`Resolution 1280x720` -> CreateDevice BackBuffer = 1280 x 1024**, S_OK; the game ran ~131 s at 1280x1024 (5:4).
3. `CONFIRMED` (static, RenderD3D `e9aff449...`, VA 0x10001B7A-0x10001B91): RenderD3D copies
   `BackBufferWidth = [*g_pSysInterface + 0x64]` and `BackBufferHeight = [*g_pSysInterface + 0x68]` - the same fields the
   engine `Resolution` parser (hitman2.exe 0x41700B) writes; ~~RenderD3D never writes `+0x64/+0x68`~~ (wrong, see run-004).
   No shim frames in the log.
   => **the height in `Hitman2.ini` is replaced before the device is created** (run-004: by RenderD3D, not the engine): width is kept, height
   becomes 1024 for width 1280 and an unrelated, pointer-like value (0x06FBA8A4) for width 1920.
   `HYPOTHESIS`: the height is looked up from the width in a runtime (heap) list of supported modes; a width not in the
   list reads past its end. ~~Writer location: `OPEN`~~ -> found in run-004 (RenderD3D `FUN_100197b0`); hypothesis superseded.
4. `CONFIRMED` Viewports at 1280x1024: 1280x1024; cutscene letterbox 1280x750 at y=138 with bars 1280x138 / 1280x136;
   128x128 (shadow/RTT, +0xD8C2); 1088x675 at (96,175); 640x480 at (320,272); 310x156 at (351,323).
5. `CONFIRMED` Projection at the 1280x1024 viewport: menu camera **fovX 65 deg -> fovY 54.01 deg**, 45 -> 36.67 deg, matrix
   aspect 1.25 = viewport aspect (Vert- again: fovX fixed; narrower aspect => taller vertical FOV). Cutscene camera
   (letterbox 1280x750): animated fovX ~30.9-37.1 deg, matrix aspect **1.70455** vs viewport 1.70667 (~0.1 % apart; the
   letterbox aspect is computed separately - `OPEN`). The gameplay camera was not captured: the cutscene filled the
   call-site budget (1500) first.
6. `CONFIRMED` Frame rate 378-1352 fps at 1280x1024 (25 samples); shadow pass still RenderD3D+0xDBCE.

### Static search for the height substitution (run-003; superseded by run-004)

Searched the engine image `main.analysis.img` (`f59d353e...`) and RenderD3D: dword/word tables of the four classic
modes (row stride <= 8), immediate stores of 1024/768/600 to `+0x68`, `push h; push w` pairs (only string-pointer false
positives at 0x6454F0/0x67F1F0), stores to `[*g_pSysInterface+0x64/0x68]` via the global (none), stores to `[reg+0x68]`
near the parser (0x415091 constructor default 640x480; 0x416067/0x416078 restore path of `FUN_00415e40`; 0x417062 the
parser). REA/Ghidra (`ev_8b473f57...`, `ev_39ec9c47...`): `FUN_00415e40` is the runtime renderer switch ("This render is
not supported. Using old render."), `FUN_004161b0` releases the renderer object - neither substitutes the height.

## Results (run-004, 2026-11-10 23:15, read-only value watcher, no proxy)

`tools/watch_values.py` polled `[[0x6A6C5C]+0x64]` (width) and `+0x68` (height) read-only while the owner launched the
game via Steam with `Resolution 1280x720` and quit at the menu. Log: [`run-004-watch.log`](run-004-watch.log) (complete,
8 lines). Afterwards: 18 binaries == baseline, ini untouched (`Resolution 1280x720`).

| t (ms) | width x height | modules first seen at this change |
|---|---|---|
| 2534 | 640 x 480 | engine + system DLLs, `steam.dll` (constructor default, hitman2.exe 0x415091) |
| 2700 | 1280 x 720 | HID/SETUPAPI/WINTRUST... (the `Resolution` parser, 0x41700B) |
| 2755 | **1280 x 1024** | **`RenderD3D.dll`, `d3d8.dll`**, d3d8thk, dwmapi, dxcore |
| 12553 | (object freed) | shutdown |

1. `CONFIRMED` (runtime) The height is replaced within ~55 ms after the ini value is parsed, at the moment RenderD3D
   and d3d8 are loaded.
2. `CONFIRMED` (static, RenderD3D `e9aff449...`; llvm-objdump + REA/Ghidra `ev_0b87e391...`) **The writer is RenderD3D
   `FUN_100197b0`** (window setup; `__thiscall`, one argument = the window title, later passed to `SetWindowTextA`;
   only caller 0x10003817 in the window-init function 0x100037D0, which passes the configured title or `"Direct3D"`).
   When fullscreen (`[*g_pSysInterface+0x57] != 0`) it **snaps the width to a fixed 4:3/5:4 list and forces the height**:

   | configured width | result |
   |---|---|
   | < 512 | 512 x 384 |
   | 513-640 | 640 x 480 |
   | 641-800 | 800 x 600 |
   | 801-1024 | 1024 x 768 |
   | 1025-1280 | 1280 x 1024 |
   | 1281-1600 | 1600 x 1200 |
   | **> 1600** | width kept, **height = the function argument (window-title pointer)** - an uninitialised-variable bug |

   (512 and the five list widths map to the same heights.) Stores at 0x100198F4 (`+0x64`) and 0x100198FF (`+0x68`);
   then copies to `+0x6C/+0x70` and centres the window with `GetSystemMetrics(16/17/78/79)`. In windowed mode
   (`+0x57 == 0`) the configured size is kept unchanged.
   This explains both run-003 observations exactly: 1280x720 -> 1280x1024; 1920x1080 -> 1920 x 0x06FBA8A4 (a heap
   string pointer) -> `D3DERR_INVALIDCALL`.
3. `CONFIRMED` (static) RenderD3D also holds a 16-byte-row mode table at VA 0x10049068
   `{w, h, 32, 0}` x {640x480, 800x600, 1024x768, 1280x1024, 1600x1200}, returned by vtable method 0x10019C20; the
   engine's menu lists the same five strings (`%640x480` ... `%1600x1200`, VA 0x690BCC... via pointer table 0x6909EC).
   Its users were not traced (not needed for the height bug).
4. Correction: the run-003 statement "RenderD3D never writes `+0x64/+0x68`" was wrong - that grep was truncated by an
   output limit (`head -30`) before reaching 0x100198F4. The run-003 hypothesis (runtime mode list read past its end)
   is superseded: the bug is a fixed snapping cascade with an unset height for widths above 1600.

## Windowed-mode configuration (static, 2026-11-10)

- `CONFIRMED` (hitman2.exe, analysis image `f59d353e...`, VA 0x41734C-0x417385): key `Window`. Present with an empty
  value or a non-zero number (CRT `atoi`, 0x5A00C1) => windowed (`[*g_pSysInterface+0x57] = 0`); `Window 0` =>
  fullscreen (`+0x57 = 1`); absent => constructor default (fullscreen in every run so far).
- `CONFIRMED` (VA 0x417179-0x4171DB): key `StartUpperPos X,Y` (comma or space) stores the window position in
  `+0x7C/+0x80`. Only when `+0x7C == -1` does RenderD3D `FUN_100197b0` centre the window, and it centres on
  `max(SM_CXFULLSCREEN, SM_CXVIRTUALSCREEN)` - the whole virtual desktop (GB-004). The parser skips digits only, so
  a negative X breaks Y.
- Window style is always `WS_POPUP` (0x96000000 observed); windowed adds `WS_EX_CLIENTEDGE`.

## Results (run-005/006, 2026-11-10 23:37-23:45, windowed 1920x1080, logger 0.4.0, no fix)

Owner set `Resolution 1920x1080` + `Window` (run-005: 22 s, window centred across both monitors at x=678 = 3280/2-960-2;
run-006: `StartUpperPos 0,0` added for the test, ~135 s incl. a mission, owner screenshots).
Logger 0.4.0 SHA-256 `1a1d93aeb9972d2324324a99192945b12c5c79630776cbe1472bc63260496bd4`. Excerpts/summaries:
[`run-005-excerpt.log`](run-005-excerpt.log), [`run-006-excerpt.log`](run-006-excerpt.log),
[`run-006-summary.json`](run-006-summary.json). After each run: proxy removed, 18 binaries == baseline.

1. `CONFIRMED` Windowed mode keeps the configured size: CreateDevice 1920x1080 `Windowed=1`, main viewport 1920x1080.
2. `CONFIRMED` In-mission gameplay camera (RenderD3D+0x33DD): **fovX 67.4 deg, fovY 41.126 deg, aspect 1.77778** = viewport
   aspect - exactly the value predicted from the Vert- rule (GB-002).
3. `CONFIRMED` Cutscenes: letterbox viewport 1920x810 at y=135 (+ bars 1920x135), matrix aspect 2.37037 = viewport, animated
   fovX 21.1-23.5 deg (zn 400, zf 30000).
4. `CONFIRMED` 2D/HUD path: **no pre-transformed (XYZRHW) draws at all**; a constant identity projection is set from
   RenderD3D+0x47F1, and (run-007) the HUD/menus also use the scene call site with a 45 deg, zf 3000 camera.
5. `CONFIRMED` (owner screenshots) HUD elements are anchored to screen corners at their original pixel size (small at
   1080p, not stretched); briefing/pause/main menus are a fixed 640x480 panel centred (viewport 640x480 at (640,300));
   the map fills the screen and **its mouse hit areas are offset vertically** (owner must aim higher for "Streets" /
   "Legend") - GB-005, independent of the projection.
6. `CONFIRMED` Shadow/RTT pass unchanged (RenderD3D+0xDBCE, 128x128, render-target switches at +0xE951/+0xD86A).
7. Owner reports (not verified, not attributed to resolution): black screen after the animation-cancel trick
   (suspected high frame rate, GB-001); a 2-3 s black cursor smear over the briefing when a video opens (OPEN).

## Results (run-007, 2026-11-10 23:50-23:54, fullscreen 1920x1080, proxy 0.5.0 with both fixes)

`Window 0`, `Resolution 1920x1080`; proxy 0.5.0 (SHA-256 `4870e2d8c356601ed1e72c22ba8459f40c8e75895943fe4a630fc3bc060325d8`)
installed with `--fix resolution --fix fov`. Excerpt/summary: [`run-007-excerpt.log`](run-007-excerpt.log),
[`run-007-summary.json`](run-007-summary.json). Afterwards: proxy + `h2e_d3d8.ini` removed, 18 binaries == baseline.

1. `CONFIRMED` **Resolution fix works**: the loaded RenderD3D.dll hashed to the pinned `e9aff449...`, bytes at +0x197F7 were
   the expected ones, the branch was patched in memory; CreateDevice **1920x1080 `Windowed=0`** -> S_OK, window
   (0,0)-(1920,1080). Owner played a mission (Hidden Valley) in real fullscreen 1080p.
2. `CONFIRMED` Hor+ widened the 3D view (owner reported it was wider), **but also shrank the HUD and menus by 0.75 towards the
   centre** (pause dimming rectangle exactly 1440x810 at (240,135); HUD moved from the corner to ~(258,150); briefing
   panels showed black boxes). Cause: the 2D layer is drawn through the same call site with its own camera
   (fovX 45, zn 5, **zf 3000.08**), which was scaled too. **Fix v0.5.1** (not yet mission-tested): Hor+ only for
   projections with zf > 4000 - every scene/cutscene camera in runs 001-007 has zf >= 11999, every UI camera zf 3000
   (one zn 0.5 / zf 10 utility pass is also excluded). `INFERRED` from logs.
3. `OPEN` Owner: **Esc stopped working in menus**. Not yet known whether this is caused by a fix, by fullscreen, or is
   original behaviour - needs an A/B run.
4. Owner: cutscenes look out of sync - suspected frame-rate issue (GB-001), not verified.

## Results (run-008, 2026-11-11 ~00:00, fullscreen 1920x1080, proxy 0.5.1 with both fixes)

Proxy 0.5.1 (SHA-256 `e28d558046d71cde7fbbb132bb8976cf1b7105e2fbd1f4c2a43ce2f90792c696`, Hor+ limited to zf > 4000).
Excerpt/summary: [`run-008-excerpt.log`](run-008-excerpt.log), [`run-008-summary.json`](run-008-summary.json).
Afterwards: proxy + ini removed, 18 binaries == baseline.

1. `CONFIRMED` Menus drawn with the zf-3000 camera are back to their native size (level list: 640x480 panel at
   (640,300), as without fixes). The black area around them is the game's own behaviour (GB-006), not a fix effect.
2. `CONFIRMED` (owner screenshot) **The HUD is still pulled towards the centre**: health/compass at ~(258,150) instead
   of ~(25,25). That is exactly the corner position scaled by 0.75 about the screen centre
   (960 - 935 x 0.75 = 259, 540 - 515 x 0.75 = 154). => the HUD is geometry placed in front of the **gameplay camera
   and drawn with the same projection matrix** (fovX 67.4, zf ~20000), so a matrix-only Hor+ cannot separate it from
   the world. `INFERRED`.
3. `CONFIRMED` (owner screenshot) The map screen is broken by Hor+: the map content is drawn smaller than its frame /
   background layer.
4. Conclusion: **Hor+ by projection rescaling is not viable as built** (0.5.0 and 0.5.1). It stays disabled by default.
   Next: find how RenderD3D/the engine positions HUD and map geometry (the FOV/aspect inputs of helper 0x10001750
   and the HUD draw path) before another attempt. The resolution fix is unaffected and stays.

## Results (run-009, 2026-11-11, fullscreen 1920x1080, release-layout proxy 0.6.0, `resolution` only, log level 1)

Proxy 0.6.0 (SHA-256 `b0b4ccbfa8d1c6512a59d72d44756c42a425976a9d279bfb4ea993b05fef9ffb`), `h2e.ini`
`[resolution] Enabled=1`, `[horplus] Enabled=0`, `[log] Level=1`. Owner played (Temple City Ambush) and sent screenshots.

1. `CONFIRMED` The 0.6.0 framework applies the resolution fix (`patch RenderD3D.dll+0x197F7: applied`, pinned hash),
   CreateDevice 1920x1080 `Windowed=0` S_OK. Six device `Reset`s (RenderD3D+0x38BB) during the session, all S_OK.
2. `CONFIRMED` (owner) In-game pause menu and main menu look correct; the main menu wastes space at 1080p (owner would
   like it larger - UI scaling, GB-006).
3. `CONFIRMED` (owner screenshots) Briefing screen: the 640x480 panel is centred with black around it (native size,
   GB-006). Map screen **without Hor+** still shows the map content offset/zoomed against its frame, and the "Legend"
   label only reacts when the cursor is well away from it (GB-005 - independent of Hor+).
4. `OPEN` Esc opens the pause menu but does not close it / go back in menus. Not yet known whether the original game
   behaves the same (A/B without the proxy pending).

## Interpretation and open points

- ~~`INFERRED` The render resolution comes straight from `Resolution` in `Hitman2.ini`~~ - **refuted by run-003**: the
  width comes from the ini, the height is substituted (1280 -> 1024; 1920 -> garbage) - run-004: by RenderD3D
  `FUN_100197b0` in fullscreen mode.
- `INFERRED` PresentInterval 0x1 is `D3DPRESENT_INTERVAL_ONE` (constant not yet verified from a header). Why
  ~5000 fps occur with it on this system (D3D8→D3D9 runtime, flip/exclusive mode emulation, `DisableVSync` default?)
  is `OPEN` — important for TIMING_AND_FPS.
- ~~`OPEN` In-game projection~~ → run-002: gameplay uses the full 800×600 viewport, fovX 67.4°, aspect = viewport aspect.
- `OPEN` Where the FOV value originates (engine camera → RenderD3D function containing 0x100033D7, matrix built by
  helper 0x10001750).

## Next steps

1. Hor+ redesign: trace the FOV/aspect inputs of RenderD3D helper 0x10001750 and the HUD/map draw path (REA), so
   HUD geometry keeps its screen anchors. A/B the Esc problem (fixes on/off).
   ~~Fix design for the resolution bug~~ -> D-008, resolution fix confirmed in run-007.
2. Longer in-game run with v0.3.0 to log the gameplay camera at 1280x1024.
3. Decompile RenderD3D 0x10003390–0x100033E0 and 0x10001750 (define functions manually) to locate the FOV inputs.
