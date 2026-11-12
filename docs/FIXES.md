# Fix catalog

One entry per fix in `src/fixes/` (framework: D-009, `src/fixes/fix.h`). The id is the `h2e.ini` section.
Status: `CONFIRMED` = tested in a mission; `EXPERIMENTAL` = built, not working or not yet verified.
Binary addresses below refer to the Steam build (hitman2.exe 1.0.0.277, RenderD3D.dll SHA-256
`e9aff44931aa6c361fdcc49d1f5969d19777923c8f6c2c2e18e439bfd53c871d`); the retail 1.0.0.265 equivalents are in
`src/fixes/game_build.cpp` and EXECUTABLE_ANALYSIS.md "Retail 1.0.0.265". Every fix that needs an engine address
looks up the running build by file SHA-256 and stays off on an unknown one.

| Id | Problem | Status | Default in release |
|---|---|---|---|
| `resolution` | Fullscreen snaps the resolution to 4:3/5:4 modes; widths > 1600 fail (GB-003) | CONFIRMED (run-007/008/009) | on |
| `horplus` | Vert- field of view at widescreen (GB-002) | EXPERIMENTAL - moves HUD/map (run-008) | off |
| `menuscale` | Menus, level list, briefing and load screen drawn as a small 640x480 canvas at high resolutions (GB-006) | EXPERIMENTAL - briefing scales consistently (owner screenshot, 4:3 with side bars); mouse/map checks pending | off |
| `framelimit` | Engine-wide `trunc(dt*1024)` conversions break above ~1024 fps (running animation, GB-001) | EXPERIMENTAL - owner: running animation normal at the cap (2026-11-11); full test pending | off |
| `timer` | Integer game clock loses `dt*1024` fractions every frame; stops above 1024 fps (GB-001) | EXPERIMENTAL - live test 1: clock rate ≈ 1.00 at 30/60/uncapped (EXP-004); gameplay tests pending | off |
| `settings` | The game's graphics menu is imprecise and lacks resolution / FOV / frame-cap options | EXPERIMENTAL - in game (owner 2026-11-11): all rows work; FOV only in missions, moves HUD parts | off |
| `hdfonts` | UI text is a 128x128 bitmap font magnified 2.25x by `menuscale`: soft (GB-010) | EXPERIMENTAL - in game (run 1): main menu, Load, briefing text sharp, 65 pages swapped, no errors (EXP-005); subtitles + full mission pending | off |

## `settings`

- **Problem**: the game's Graphics options are imprecise (owner: sliders miss or jump back) and cannot offer the new
  options (resolution > 1600 wide, FOV, frame-rate cap). Its menus are scene data (`MainMenu.ZIP`, RENDERING.md
  "Options menus are scene data"), so entries cannot be added without changing game files.
- **Change**: an own page drawn by the proxy, toggled with **F10** (a small "F10" hint is shown on menu screens when
  `menuscale` runs). Rows: Resolution and Display mode (written to `Hitman2.ini` - `Resolution WxH`, `Window` flag -
  immediately and again at process exit, because the game rewrites that file when it shuts down; applied after a
  restart; the first change saves the original lines in `h2e_settings.ini [original]`), Field of view (70-130 %,
  tangent scale of scene projections with zf > 4000, live), Frame rate limit (monitor / 30-360 / unlimited, live via
  `SetFrameLimit`, needs `framelimit` enabled), Restore defaults (FOV 100 %, monitor rate), Close. Each row has a help
  text (frame limit: the > 1024 FPS warning). Choices are stored in `h2e_settings.ini` next to `h2e.ini`.
- **Input**: keyboard (arrows, Enter, F10/Esc) and mouse (own cursor, click, wheel). While the page is open the
  game's DirectInput keyboard and mouse data are blanked: the dinput8 device vtable (A and W flavours, found by
  creating a keyboard device in `onDirect3DCreate`) gets `GetDeviceState`/`GetDeviceData` hooks; after closing, keys
  and buttons stay blanked until released. The game keeps running underneath (open it from a menu).
- **Drawing**: GDI renders the page into a DIB (Copperplate Gothic Bold if installed, else Segoe UI), uploaded to a
  managed A8R8G8B8 texture when it changes; drawn before `Present` (new framework callback `onBeforePresent`) inside a
  full state block, own BeginScene/EndScene.
- **In game (owner, 2026-11-11)**: page, keyboard/mouse and input blocking work; resolution change applied after a
  restart (1366x768), display mode and frame limit (live) work. FOV first "changed nothing": it was tried on the
  briefing, which has no scene camera; in a mission it works (log: 10043 scene-site projections, 3168 changed).
- **Limits**: FOV acts only where a 3D scene camera is used (missions, main-menu background), not in the briefing;
  it moves the HUD parts drawn with the gameplay projection (as `horplus`, run-008).
- **Code/tests**: `src/fixes/settings.{h,cpp}`; `tests/native/fixes_test.cpp` (ini line editing, choices, FOV, key
  repeat).

## `resolution`

- **Problem**: RenderD3D `FUN_100197b0` (window setup) snaps the configured width to 512/640/800/1024/1280/1600 and
  forces a 4:3/5:4 height in fullscreen; above 1600 the height is left unset (window-title pointer) and device
  creation fails. Evidence: EXP-003 run-003/004 (`docs/experiments/EXP-003_d3d8_runtime/README.md`).
- **Change**: in-memory patch of 6 bytes at RenderD3D+0x197F7: `0F 84 05 01 00 00` (jz, taken only when windowed)
  -> `E9 06 01 00 00 90` (jmp to the same target RVA 0x19902). Fullscreen then behaves like windowed mode: the
  configured size is kept.
- **Safety**: applied from the proxy's `DllMain` (RenderD3D is mapped, none of its code has run) only if the
  RenderD3D.dll file has the pinned SHA-256 and the 6 bytes are the expected ones (`src/common/code_patch.cpp`).
- **Verified**: run-007 (fullscreen 1920x1080, `Windowed=0`, device S_OK, mission played), run-008, and run-009 with
  the 0.6.0 framework build.
- **Limits**: HUD/menus stay at native pixel size (GB-006); map cursor offset (GB-005); Esc in menus OPEN.
- **Code/tests**: `src/fixes/resolution.{h,cpp}`; `tests/native/fixes_test.cpp`.

## `framelimit`

- **Problem**: besides the main clock (`timer`), 138 more `_ftol(x * 1024)` conversions truncate per-frame time; above
  ~1024 fps some systems stop (owner: running animation breaks with `timer` on). TIMING_AND_FPS.md.
- **Change**: paces `Present` to a frame cap: by default the refresh rate of the monitor showing the game window
  (`[framelimit] MaxFps=0`), or a fixed value (`MaxFps=60`, clamped 15-1000). Deadline scheduling on
  QueryPerformanceCounter, sleeping on a high-resolution waitable timer and spinning the last ~1 ms; a late frame
  restarts the schedule instead of causing a burst. No game code or memory is changed.
- **Note**: truncation loss per conversion at the cap: 60 fps 0.4 %, 144 fps 1.6 % (7.11 -> 7 units). Use `MaxFps=60`
  if anything still feels slightly slow at a high refresh rate.
- **Code/tests**: `src/fixes/framelimit.{h,cpp}`; `tests/native/fixes_test.cpp`.

## `menuscale`

- **Problem**: the menu code centres a fixed 640x480 layout (`(w-640)/2`, `(h-480)/2`, ~15 sites, no scale), so at
  1920x1080 menus use a third of the width (RENDERING.md "Menu / briefing layout").
- **Finding**: the menus are a tree of 2D objects (`ZGEOM`, e.g. child "ConfirmationDialog" at hitman2.exe 0x52CC11)
  drawn through the UI camera (RenderD3D+0x33DD, zn 5, zf ~3000) plus a few sub-viewports inside the canvas. Evidence:
  matrix-only Hor+ x0.75 shrank the level list by 0.75 (run-007); excluding zf <= 4000 restored it (run-008).
- **Change**: on menu frames only (no scene camera with zf > 4000 in the last 3 frames), the UI camera's `_11/_22`
  are multiplied by s = min(w/640, h/480) (1920x1080: 2.25) and every viewport inside the centred 640x480 canvas is
  scaled by s about the screen centre. Re-rasterised at native resolution (not an image upscale).
  `[menuscale] ScalePercent` overrides s (e.g. 150).
- **Expected**: cursor and hit areas stay consistent because they live in the same scaled space. **Open**: intro
  videos, transitions.
- **Menu cursor (2026-11-11)**: owner report at 1920x1080: the scaled menu cursor moves too far, gets lost at the sides
  and is not stopped by the screen edges. Cause (static, RENDERING.md "Menu cursor"): the cursor is
  `g_pSysInput+0x18/+0x1C` in unscaled screen pixels, advanced by the raw mouse delta and clamped to 0..W x 0..H by
  hitman2.exe 0x40F7C0, then drawn through the scaled UI camera - s times faster, clamp off screen. Change: the
  `call 0x40F9D0` at 0x40F830 (returns the delta) is redirected, in memory only, to `h2e_MenuCursorStub` from
  `onDirect3DCreate`, with the `timer` fix's checks (shared helper `exe_redirect.{h,cpp}`: caller, image base, file
  SHA-256, executable image memory, 34 context bytes, 11-byte target prologue, other threads frozen, aligned atomic
  write). While the previous frame was scaled, the stub returns a delta that ends the cursor at
  clamp(p + d/s) inside centre +/- (W/2s, H/2s) - (533.3,300)-(1386.7,780) at 1080p, i.e. the screen edges once drawn;
  otherwise the engine's delta unchanged. Logged: redirect outcome, every confine/release with the bounds and call
  counts. If the redirect is refused, menus are still scaled and the log says the cursor is not confined.
  Test 1 (owner, 2026-11-11): pause menu correct; main menu and briefing unchanged - they use the ZWINDOWS cursor
  (RENDERING.md "Menu cursor"). v2: the same hook also rewrites the raw accumulator `g_pSysInterface+0x7E4/+0x7E8`
  so that ZWINDOWS's step `(raw - [+0x78]) * [+0x90]` is divided by s and ends inside the bounds (only with the
  DirectInput flag `[g_pSysInterface+0x62]` set, a valid ZWINDOWS instance, and not on its first frame). Side effect:
  other readers of that accumulator also see the scaled motion while a menu is scaled. Test 2 (owner, 2026-11-11,
  `samples/local/logs/h2e-20261111-160547.log` + `cursor-watch-20261111-c.log`): `CONFIRMED` main menu, briefing and
  pause menu cursor at normal speed and stopped at the screen edges ("works fine"); log: redirect applied, 32271
  ZWINDOWS steps adjusted, 16 skipped (scene starts / no instance). Mission-length test: pending.
- **Level load screens (2026-11-11)**: owner: "super zoomed in". Load spans have no ZWINDOWS instance
  (`windows=-1`), no canvas viewport (`canvasVp=0`, so not the double-scale hypothesis) and two cameras: a 640x480-era
  UI camera (`_11` 1.5697 or 2.4142, zf 3000) and the zn 0.5 / zf 10 utility camera (`_11` 1.0). Step 1: the zf 10
  camera is no longer a UI camera (`kMinUiFarPlane` 100) - owner: a progress bar now shows at the bottom of the
  screen, the main image still zoomed. `INFERRED`: the load screen already fills the screen in the original (like
  the in-game map). Step 2: with the probe enabled, nothing is scaled while no ZWINDOWS instance is known (loads,
  logos, the first frames of a scene); the scene-camera rule stays only for an unknown hitman2.exe. Owner
  (`samples/local/logs/h2e-20261111-161455.log`): load screen correct, menus still scaled. Trade-off: intro logos
  are no longer scaled.
- **Run 2026-11-11 (frame-kind log, `samples/local/logs/h2e-20261111-105146.log`)**: `CONFIRMED` the intro logos are
  scaled; the **main menu is not**: its frames use a 3D scene camera (zf > 4000) together with the UI camera
  (`scene=1 ui=1 scaled=0` for 11.5 s), so the "no scene camera" detector treats it as gameplay - the same rule
  excludes the pause menu by design. Short UI-only spans (8-23 frames, scaled) occur at screen transitions; the owner
  reports the load screen as heavily over-zoomed. `HYPOTHESIS`: a canvas sub-viewport drawn while the scaled UI
  projection is still active is enlarged twice; the log now counts these (`canvasVp=`, `double=`). Next: a run that
  includes gameplay, to see whether the UI camera is used outside menus (if not, "UI camera used" can replace the
  scene-camera rule and cover the main and pause menus).
- **Menu detection v2 (commit 7289a72)**: with the read-only ZWINDOWS probe (`src/fixes/zwindows.cpp`), a frame is a
  menu frame when the engine's open-window list is not empty (RENDERING.md "UI system"); the scene-camera rule is
  only the fallback while no ZWINDOWS instance is known (logos, load screens, first ~2 s of a scene). Run 3
  (`samples/local/logs/h2e-20261111-probe-run3-list.log` + video) matched the list to main menu, Options, briefing,
  map and pause; gameplay = empty. Visual check of v2 (owner video 2026-11-11 11-11-35): main menu, Load, Options and
  pause menu now fill the height (4:3 layout with side bars); **in-game map broken** - it already adapts to the
  screen in the original, so scaling pushes the legend and the area selector off screen; the main menu showed ~0.5 s
  unscaled (instance re-detection delay; fixed in a11d6f0 by rescanning the last region every frame). Owner: scaled
  UI looks low-resolution (the 2D art and bitmap fonts are 640x480-era textures magnified 2.25x - a resolution limit
  of the assets, not of the scaling; HD needs replacement textures). The map and the pause menu are not
  distinguishable by the list (both one node, value 0) or by the frame kind; a11d6f0 logs the UI camera parameters
  per span to find a difference. Result (`samples/local/logs/h2e-20261111-uicam.log`): the map uses the same UI camera
  as the menus (`_11` 2.4142 = 45 deg, zf 3000.1) - no difference. Next: the `ZWINDOW_IngameMap` object itself
  (vtable 0x659058, located by vtable + `[+0x5C]` == ZWINDOWS) is diffed while the map opens/closes (8a7e5b5).
  Runtime (`samples/local/logs/h2e-20261111-mapdiff.log`): exactly one heap object of that class, only in mission
  scenes (its `[+0x5C]` is not the ZWINDOWS cache); `[map+0x20]` = 0x37 open / 0x27 closed over two cycles, unchanged
  while the pause menu was open - `CONFIRMED` as the "map open" flag (bit 0x10). **Detection v3 (d5656e1)**: scale when
  the window list is not empty and the map is not open. Visual check: pending.
- **Detection v4 (briefing photos)**: owner 2026-11-11: briefing photos were enlarged for ~1 s, then shrank. Log
  (`samples/local/logs/h2e-20261111-162357.log`): in the briefing scene the window list is usually empty and gets one
  node (static ref, e.g. 0x400005B0) for ~1-1.5 s when a photo opens; no scene camera in that scene except while its
  map is open. Rule now (`ProbeSaysMenu`): scale when ZWINDOWS is known, the map is not open, and the list is not
  empty **or** no scene camera was seen in the last 3 frames. Load screens (no ZWINDOWS) are still not scaled.
  In game (owner, same day): photos fill the screen; log shows no unscaled briefing span and no scaled load screen.
- **Detection v5 (in-game briefing, key B)**: owner 2026-11-11, both builds: the briefing opened during a mission
  shows its photos small - the same empty window list, but over the mission's scene camera. A paused game now also
  counts (`ZEngineDataBase::IsPaused` = byte `[[g_pSysInterface+0x98]+0xB9C]`, Steam 0x4014C0 / retail 0x4015C0, read
  the same way by the main loop at 0x4169E4; `CONFIRMED` static). The map open flag still wins. `HYPOTHESIS`: the
  B briefing pauses the game while a photo is shown - pause transitions are logged ("game paused/running"). Owner:
  fixed on 1.0 and Steam. **Regression** (owner 2026-11-12, both builds): the weapon/inventory menu also pauses the
  game, with an empty window list throughout (Steam log frames 12453-13442, `scaled=1 windows=0`), and was drawn 2.25x.
  0.8.2 (`PausedMenu`): a pause counts only if a non-map window was open during it.
- **Crashes GB-009** (two, while loading a save from the main menu) with the 8a7e5b5 probe build: see KNOWN_ISSUES.md;
  the scans now read 64 KB chunks into a static buffer (bfa1e78); three later runs were clean.
- **Previous attempt**: viewport-only scaling (`uiscale`, reverted) enlarged only the sub-panels.
- **Code/tests**: `src/fixes/menuscale.{h,cpp}`, `src/fixes/exe_redirect.{h,cpp}`; `tests/native/fixes_test.cpp`
  (cursor bounds, delta, context bytes; the stub itself only runs in the game).

## `timer`

- **Problem**: hitman2.exe 0x4179F0 adds `_ftol(dt * 1024)` to the integer clock `[this+0x28]` every frame; the
  truncation makes the clock run slow (0.984 at 144 fps) and stop above ~1300 fps (EXP-004 baseline, CONFIRMED).
- **Change**: the `call _ftol` at 0x417B40 is redirected (rel32 only, memory only) to `h2e_TimerTickStub`, which keeps
  `_ftol`'s ABI and returns whole ticks while carrying the fraction (`TickAccumulator`, bound to the timer instance,
  dropped on engine resets and on exceptional values, for which the result is bit-identical to `_ftol`).
- **Safety**: applied from the first `Direct3DCreate8` (not `DllMain`); checks the caller (RenderD3D), image base,
  file SHA-256, page state/protection, 30-byte context and `_ftol` prologue; all other threads suspended and checked
  not to execute the context; aligned `lock cmpxchg8b`, protection restored, cache flushed, read back; proxy pinned.
  Every failure is logged and leaves the game untouched.
- **Verified**: isolated harness (`tests/native/timer_test.cpp`); live verification in EXP-004.
- **Code/tests**: `src/fixes/timer.{h,cpp}`, `src/common/code_patch.*`; `tests/native/timer_test.cpp`.

## `hdfonts`

- **Problem**: all UI text is drawn from 128x128 palettised font pages (`Fonts\COPRGTB.TTF-<px>#<n>` in each scene's
  .TEX); scaled by `menuscale` (2.25x at 1080p) the glyphs are magnified textures and look soft (GB-010).
- **Data**: `tools/hdfonts_build.py <game>` reads every scene ZIP in memory, re-renders each glyph from the TrueType
  font embedded in the scene's .ZGF at 4x into its 4x box (glyph tables in the .PRM; formats in FILE_FORMATS.md),
  verifies every glyph against the original and writes `samples/local/h2e_hdfonts.bin` (45 pages, 2.3 MB). The pack
  is derived from the game's files: never committed or shipped; each player builds it from their own install.
- **Change**: no game code or file is changed. When RenderD3D fills a texture (`LockRect`/`UnlockRect`, EXP-005),
  the proxy hashes its 4-bit alpha; a page found in the pack gets a 512x512 copy (same format, full mip chain,
  `D3DPOOL_MANAGED`) that `SetTexture` binds instead of the original; it is released with the original. Glyph UVs,
  metrics and kerning are unchanged because the layout is the same, only 4x finer.
- **Config**: `[hdfonts] Enabled=1`, `Pack=<file>` (default `h2e_hdfonts.bin` next to `h2e.ini`). Without a pack, or
  for textures in a format without 4-bit alpha (A1R5G5B5), it logs and does nothing.
- **Framework**: texture events `onTextureFilled`, `onSetTexture`, `onTextureReleased`; `FixContext::device`. The proxy
  patches the shared texture vtable (LockRect, UnlockRect, Release) and `SetTexture` only when such a fix is on.
- **Install for tests**: `tools/d3d8_logger_install.py install <game> --fix hdfonts` copies the pack
  (`--hdfonts-pack`) and removes it again on `uninstall` (hash-checked).
- **Verified**: `tests/native/fixes_test.cpp` (key, formats, pack, PackBits vs. Python vectors),
  `tests/native/hdfonts_smoke.cpp` (real device: page replaced by 512x512, other texture untouched),
  `tests/test_hdfonts_build.py` (synthetic font + scene; optional real MainMenu check). In game run 1 (2026-11-11,
  with `resolution + menuscale`, `samples/local/logs/h2e-20261111-121154.log` + owner video): textures are
  A8R8G8B8, 65 pages replaced, main menu/Load/briefing text sharp. Run 2 (+ `framelimit`): crash while loading a
  save (GB-011, cause open, also seen without proxy as GB-008); run 3: same save loaded fine, HD copies freed at every
  scene change (max 19 live). Subtitles, pause, Options, full mission: pending.
- **Limits**: the LOAD screens' second font (`-18`, line height 22) is a typeface that is not embedded: its pages are
  only upscaled (smooth, not sharper). Art tiles (logo, Agent 47) are not covered.
- **Code/tests**: `src/fixes/hdfonts.{h,cpp}`, `tools/hdfonts_build.py`; tests above.

## `horplus`

- **Problem**: horizontal FOV is fixed, vertical FOV shrinks at wider aspect (gameplay 41.1 deg vs 53.1 deg at 4:3).
- **Change (current)**: for RenderD3D+0x33DD projections on full-width viewports with zf > 4000, multiply `_11`
  and `_22` by (4/3)/backBufferAspect.
- **Why it fails**: the HUD and parts of the map are drawn with the gameplay projection itself (run-008), so they
  move towards the centre by the same factor. A working version must change the FOV where the engine/RenderD3D
  computes the camera and the HUD placement. Kept, off by default, as the base for that redesign.
- **Code/tests**: `src/fixes/horplus.{h,cpp}`; `tests/native/fixes_test.cpp`.
