# Research log

Newest first. Each entry links to the experiment/document holding the details.

## 2026-11-11 — Session 2l: menu cursor under `menuscale`

- Static (REA/Ghidra + llvm-objdump, image `f59d353e…05266a`): cursor = `g_pSysInput+0x18/+0x1C` floats, advanced by
  the DirectInput delta and clamped to the screen in 0x40F7C0 ([RENDERING.md](RENDERING.md) "Menu cursor",
  [FUNCTION_REGISTRY.md](FUNCTION_REGISTRY.md)).
- `menuscale` redirects the delta call at 0x40F830 while a menu is scaled (speed / s, confined to the visible region);
  the `timer` redirect procedure moved to `src/fixes/exe_redirect.{h,cpp}` ([FIXES.md](FIXES.md)). In-game check pending.

## 2026-11-11 — Session 2k: HD UI fonts (EXP-005)

- [EXP-005](experiments/EXP-005_hd_fonts/README.md): scene formats decoded ([FILE_FORMATS.md](FILE_FORMATS.md)):
  .TEX entry table, font pages (128x128 `PALN`, 4-bit alpha), **glyph tables in the .PRM**, **TrueType font
  embedded in the .ZGF** (three builds; the LOAD screens' `-18` table is a non-embedded typeface). RenderD3D fills
  textures with LockRect/UnlockRect on MANAGED textures (RVA 0x109D0 / 0x11330).
- New `tools/hdfonts_build.py` (stdlib TrueType rasteriser) builds 4x pages for all 45 distinct pages; new fix
  `hdfonts` + framework texture events; end-to-end test on a real device passes.
- In-game run 1 (`resolution + menuscale + hdfonts`): 65 pages swapped (A8R8G8B8), main menu/Load/briefing text
  sharp (owner video); subtitles not reached (cutscenes freeze without `framelimit`). Proxy uninstalled, baseline OK.
- Run 2 (+ `framelimit`): crash while loading a save (GB-011, fast fail 0x15, like GB-008 without proxy). Run 3, same
  save: no crash; hdfonts copies are freed at every scene change (max 19 live). Baseline OK after each run.

## 2026-11-11 — Session 2j: framelimit confirmed, UI system analysis

- Owner: with `framelimit` (cap = monitor refresh rate) the running animation looks normal.
- Static analysis of the 2D UI ([RENDERING.md](RENDERING.md) "UI system (ZWINDOWS)"): all menus, briefing, load
  screen and HUD are `ZWINDOW`/`ZWINGROUP` classes laid out in screen pixels for 640x480; the engine centres them and
  only has scale-down code (W < 640). Root cause of the small UI at high resolutions.

## 2026-11-10 — Session 3: tool-selection policy

- [FUNCTION_REGISTRY.md](FUNCTION_REGISTRY.md) (seeded from existing docs only). Decision: [D-007](DECISIONS.md).
- Ghidra 12.1.4 verified on the snapshot RenderD3D (hash match, pseudocode of 0x10001750, xref to 0x100033B0).
  Dynamic tracing tools are unsupported on this Windows host.

## 2026-11-11 — Session 2i: timer live test, menu scaling attempt

- EXP-004 live test 1: `timer` keeps the game clock at 1.00 at 30/60/uncapped fps; no crash (0.7.0).
- Experimental `uiscale` (viewport scaling of the 640x480 menu panel) failed in game - only sub-viewports grew;
  the engine centres its menu layout itself at ~15 sites (RENDERING.md). Reverted (`git revert`), documented.
- Installer refuses to install/uninstall while hitman2.exe runs (a locked d3d8.dll previously failed halfway).

## 2026-11-11 — Session 2h: timer fix design rev. 2 (EXP-004)

- REA instruction-level evidence for the frame timer (0x4179F0); `_ftol` truncation confirmed from its bytes.
- Owner safety review addressed: patch point moved out of `DllMain` (Direct3DCreate8), `RedirectCall` with aligned
  `lock cmpxchg8b` + protection restore + flush + read-back, instance-bound accumulator with reset detection,
  bit-exact `_ftol` semantics for exceptional values, full ABI incl. XMM0-7 (an XMM clobber found by the harness).
- `tests/native/timer_test.cpp` runs the game's real 30-byte sequence and `_ftol` in the test process: bug
  reproduced unpatched, exact clock patched at 24-5000 fps. Nothing applied to the game.
- `tools/watch_values.py`: sampling mode + `--summarize` for the read-only baseline (pending owner run).

## 2026-11-11 — Session 2g: run-008 and the fix framework (0.6.0)

- run-008 (proxy 0.5.1): the HUD sits at the corner position scaled by 0.75 about the centre => it is drawn with the
  gameplay projection; matrix-only Hor+ rejected. Resolution fix still fine.
- D-009: code split into `src/common`, `src/fixes` (registry, `resolution`, `horplus`) and `src/proxy`; `h2e.ini`
  with per-fix sections and `[log] Level`; `CHANGELOG.md`, `docs/FIXES.md`, `docs/INSTALL.md`,
  `tools/package_release.py`. New tests: end-to-end verified byte patch, registry consistency, release zip.
- Git history rewritten (local only, no remote) to drop co-author trailers; commit hashes changed.

## 2026-11-10 — Session 2f: windowed validation and first fixes (EXP-003 run-005/006/007)

- Static: `Window` and `StartUpperPos` keys decoded (hitman2.exe 0x41734C, 0x417179).
- Logger 0.4.0: screen-space draw stats, periodic projection samples, render-target callers.
- Windowed 1920x1080 in a mission: gameplay fovY 41.126 deg (Vert- confirmed), HUD native pixel size, menus 640x480
  centred, map cursor offset (GB-005), window across monitors (GB-004, workaround `StartUpperPos`).
- Proxy 0.5.0 (D-008): resolution fix confirmed in fullscreen 1080p; Hor+ also shrank the HUD (UI camera zf 3000);
  0.5.1 restricts Hor+ to zf > 4000, mission test pending. Esc in menus reported broken (OPEN).

## 2026-11-10 — Session 2e: EXP-003 run-004 (height writer)

- Read-only watcher: 640x480 (ctor) -> 1280x720 (parser) -> 1280x1024 within ~55 ms, when RenderD3D/d3d8 load.
- Static: RenderD3D `FUN_100197b0` snaps fullscreen widths to 512..1600 and forces 4:3/5:4 heights; widths > 1600
  get the window-title pointer as height (GB-003). REA `ev_0b87e391...`.
- Corrected run-003's "RenderD3D never writes +0x64/+0x68" (a truncated grep).
- Found RenderD3D mode table 0x10049068 (5 modes) and the engine menu strings for the same modes.

## 2026-11-10 — Session 2d: EXP-003 run-003 (16:9 attempts)

- Owner set `Resolution 1920x1080` (device creation failed: height 117156004) and `1280x720` (ran at 1280x1024).
- RenderD3D copies width/height from `[*g_pSysInterface+0x64/0x68]` (0x10001B7A); the engine replaces the height before.
  Static search for the writer (REA/Ghidra + llvm-objdump) negative; `FUN_00415e40` = runtime renderer switch.
- At 1280x1024: menu camera fovX 65 deg / fovY 54.0 deg (aspect 1.25), Vert- consistent; cutscene letterbox 1280x750.
- New `tools/watch_values.py` (read-only pointer-chain watcher) for run-004.

## 2026-11-10 — Session 2c: EXP-003 run-002 (in-game)

- `Hitman2.ini` `DefaultScene=levelmenu.gms` confirmed as the owner's own edit (KI-011 resolved); rule added: never modify the ini.
- Proxy v0.2.0 (distinct dedup + active viewport per projection); owner played a mission ~50 s via Steam.
- Gameplay camera fovX 67.4° / fovY 53.15° at 800×600; matrix aspect == viewport aspect (5/5 scene cameras) ⇒ Vert- confirmed.
- Second projection source RenderD3D+0xDBCE with 128×128 viewport (shadow/render-to-texture, inferred); in-game 509–1969 fps.
- v0.3.0: per-call-site budgets (the shadow pass exhausted v0.2's global budget). New `tools/analyze_d3d8_log.py`.

## 2026-11-10 — Session 2b: EXP-002 completed (option A) + EXP-003 run-001

- Owner chose option A (D-005): Steam launch + read-only attach. `runtime_dump.py` rewritten as attach-only
  (no launch, read-only rights); `rebuild_image.py` deleted and replaced by `iat_annotate.py` (sidecar names).
- [EXP-002](experiments/EXP-002_executable_unpacking/README.md): with a Steam launch the OEP slot holds
  **0x5A184E** (VC6 CRT entry; earlier "refuted" result was due to the failed Steam check). Runtime IAT 0x639000
  (144 slots, 7 DLLs). Engine functions: `Resolution` parser 0x41700B, settings writer 0x418307, DLL loader 0x418156,
  `g_pSysInterface` 0x6A6C5C. QPC is the only high-resolution time import.
- D3D8 vtable layout verified against mingw-w64 `d3d8.h` (KI-004 resolved).
- [EXP-003](experiments/EXP-003_d3d8_runtime/README.md): logging proxy built (CMake, Win32) with ctest smoke test;
  installed via `d3d8_logger_install.py`, game run through Steam 60 s, removed; install verified against baseline.
  CreateDevice 800×600 fullscreen from RenderD3D+0x1DB6; projection from RenderD3D+0x33DD; viewports 800×600 and
  800×450 letterbox; **Vert-** projection; **1–5k fps** despite interval 1; no mode enumeration.
- Correction: RenderD3D projection helper is 0x10001750 (called at 0x100033B0), not 0x1000174F/0x100033AF.

## 2026-11-10 — Session 2: baseline + EXP-002 (paused)

- Owner confirmed the session-1 file changes were an intentional install repair; fresh baseline
  `samples/local/baseline-20261110-repaired` (identical to snapshot-20261110). Initial commit `787d7f2` (was `50621ae` before the 2026-11-11 message rewrite that removed co-author trailers).
- `pe_inspect` gained `--mapped` (memory images); new `runtime_dump.py`, `rebuild_image.py` (not run).
- [EXP-002](experiments/EXP-002_executable_unpacking/README.md): stub decoded; runtime unpacking confirmed
  (679 exports in memory, all 28 RenderD3D imports resolve); OEP-slot hypothesis refuted; direct launch
  fails with "Failed to find Steam." from code outside the image ⇒ likely Steam DRM wrapper. Paused for an
  owner decision on the DRM boundary.

## 2026-11-10 — Session 1: project bootstrap + PE survey

- Created repo, docs skeleton, AGENTS.md.
- Built `tools/pe_inspect.py` (stdlib) and `tools/snapshot_game.py`; 38 tests
  (synthetic PEs, fuzz/truncation robustness, llvm-readobj oracle on system DLLs + all game binaries).
- Snapshot `samples/local/snapshot-20261110` taken after detecting that six game DLLs/EXEs were
  rewritten by an external process mid-session (KI-001).
- [EXP-001](experiments/EXP-001_pe_survey/README.md): `hitman2.exe` is packed (KI-002);
  RenderD3D uses D3D8 via `Direct3DCreate8` only; config keys read via `g_pSysInterface->vtbl[0x54]`;
  `CreateDevice` site at 0x10001D8D and projection `SetTransform` candidate at 0x100033C8 (RenderD3D
  `e9aff449…c871d`). Details: [RENDERING.md](RENDERING.md), [EXECUTABLE_ANALYSIS.md](EXECUTABLE_ANALYSIS.md).
- Next: EXP-002 (unpack hitman2.exe), EXP-003 (D3D8 call logger proxy).
