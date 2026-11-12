# Changelog

All notable changes to Hitman 2 Enhanced. Versions follow [semantic versioning](https://semver.org/): while the
version is 0.x, any release may change the configuration format. The version lives in `src/version.h` (and
`CMakeLists.txt`); every release adds a section here before `tools/package_release.py` is run.

## 0.8.2 — 2026-11-12

- **Fix (regression in 0.8.1)**: the weapon/inventory menu was drawn 2.25x too large (a giant weapon model in the
  middle of the screen), in both builds. 0.8.1 scaled every paused frame; the inventory pauses the game with no window
  open. Now a pause counts as a menu only if a window other than the in-game map was open during it (the in-game
  briefing, key B, still scales its photos).
- Installer: the social-preview artwork generator (`installer/make_art.ps1`) also writes the GitHub social preview.

## 0.8.1 — 2026-11-11

- **Retail/disc version 1.0.0.265 supported** (hitman2.exe `0d7742c8…7560`, RenderD3D.dll `803c32e4…567a`): new
  build table `src/fixes/game_build.{h,cpp}` selected by the files' SHA-256; `resolution`, `timer`, `menuscale`
  (cursor hook, ZWINDOWS probe), `settings` FOV and `horplus` use it. Addresses ported by matching the Steam code with
  absolute operands masked, each verified against the file (EXECUTABLE_ANALYSIS.md "Retail 1.0.0.265").
- `RedirectCall`: the call may start up to 3 bytes into its aligned qword (retail timer call at 0x4183C2); still one
  `lock cmpxchg8b`. Test with the call at qword+2/+3 (applied) and +4 (refused).
- Installer: accepts both versions; texts updated in all languages (GOG: untested, should work if its files match).
- `menuscale`: the in-game briefing (key B) photos are scaled too - a paused game (`ZEngineDataBase::IsPaused`,
  byte `[[g_pSysInterface+0x98]+0xB9C]`) counts as a menu frame unless the in-game map is open.
- `exe_redirect` accepts the retail exe's code pages (mapped WRITECOPY: section headers without EXECUTE, no DEP), so
  the timer and the menu cursor hook apply on 1.0.0.265.
- Proxy: first-chance exception logger in `h2e.log` (up to 24 entries) to diagnose load crashes (GB-012).

## 0.8.0 — 2026-11-11

First public release.

- **Fix `settings`** (on): in-game settings page on **F10** — resolution and display mode (written to
  `Hitman2.ini`, applied after a restart), field of view (missions, live), frame-rate limit (live), restore
  defaults, help texts. The game's DirectInput keyboard/mouse is blocked while the page is open. Choices are kept in
  `h2e_settings.ini`.
- `menuscale`: briefing photos stay enlarged (screens without a 3D scene count as menus).
- `framelimit`: live changes and an "unlimited" mode for the settings page.
- Release defaults: `resolution`, `menuscale`, `framelimit`, `timer`, `settings` on; `hdfonts` off (needs a pack
  built from the player's files).
- Windows installer (`Hitman2Enhanced-Setup-<version>.exe`, Inno Setup, English/Spanish): explains what is changed,
  one checkbox per fix, finds the Steam install, refuses another mod's `d3d8.dll`, builds the HD font pack from the
  player's own game files (frozen `hdfonts_build.exe`), full uninstall. GitHub Actions workflow builds, tests and
  publishes releases.

## 0.7.0 — 2026-11-11 (not released)

- **Fix `timer`** (experimental, off): frame-rate independent game clock (EXP-004). Redirects the single
  `call _ftol` at hitman2.exe 0x417B40 (memory only) to a stub that carries the fractional 1/1024 s tick between
  frames. Applied from the first `Direct3DCreate8` call (not `DllMain`) after verifying the caller, image base,
  file SHA-256, memory protection, the 30-byte context and the `_ftol` prologue, with all other threads suspended
  and checked; the proxy DLL is pinned. Any failed check is logged and nothing is written.
- Fix framework: new event `onDirect3DCreate`; `code_patch` gains `RedirectCall`, `ThreadFreeze`,
  `ModuleFileSha256`.
- `tools/watch_values.py`: sampling mode and `--summarize` (clock rate vs wall clock).
- `tools/d3d8_logger_install.py` refuses to install/uninstall while hitman2.exe is running.
- An experimental viewport-based menu scale (`uiscale`) was tried and reverted: it only enlarged parts of the menus.
- **Fix `framelimit`** (off): caps the frame rate by pacing Present; default = monitor refresh rate, or
  `[framelimit] MaxFps`. Fallback for the engine's other truncating time conversions (running animation above
  1024 fps). `FixContext::deviceWindow`.
- **Fix `menuscale`** (experimental, off): enlarges menu screens by scaling the UI camera and the canvas
  sub-viewports on menu frames only (x2.25 at 1080p, or `[menuscale] ScalePercent`). Framework events `onViewport`,
  `onPresent`; `FixContext::iniPath`. Menu cursor: while a menu is scaled, the mouse delta call at hitman2.exe
  0x40F830 is redirected (memory only, same checks as `timer`, shared `exe_redirect` helper) so the cursor moves at
  normal speed and stops at the screen edges; the same hook corrects the ZWINDOWS cursor (main menu, briefing) via
  the raw mouse accumulator. Level load screens are no longer scaled (they already fill the screen): no scaling
  without a known ZWINDOWS instance, and the zf 10 utility camera is not treated as a UI camera.
- **Fix `hdfonts`** (experimental, off): sharp UI text. `tools/hdfonts_build.py` re-renders the game's bitmap font
  pages at 4x from the TrueType font embedded in the scenes (pack built locally from the player's install, never
  shipped); the proxy swaps them in at `SetTexture` (EXP-005). Framework texture events `onTextureFilled`,
  `onSetTexture`, `onTextureReleased`; `FixContext::device`. The installer copies/removes the pack with
  `--fix hdfonts`.

## 0.6.0 — 2026-11-11

First packaged version (D-009).

- **Fix framework**: fixes are modules in `src/fixes/` registered in `src/fixes/registry.cpp`, hosted by the
  `d3d8.dll` proxy. Shared helpers in `src/common/` (SHA-256, verified in-memory byte patches).
- **Configuration**: `h2e.ini` next to `d3d8.dll`, one `[<fix>] Enabled=0/1` section per fix and
  `[log] Level=0/1/2` (replaces `h2e_d3d8.ini` with its `[fixes]` keys). Log file renamed to `h2e.log`.
- **Fix `resolution`** (confirmed in a mission, EXP-003 run-007): fullscreen keeps the configured `Resolution`
  (e.g. 1920x1080) instead of snapping to 4:3/5:4 modes or failing to create the device (GB-003).
- **Fix `horplus`** (experimental, off): Hor+ FOV by projection rescaling. Known broken - moves the HUD and the
  map (EXP-003 run-008).
- Release zip built by `tools/package_release.py`; player instructions in `docs/INSTALL.md`.

## 0.5.x and earlier — research builds (not released)

- 0.5.1 / 0.5.0: first resolution and Hor+ fixes in the logging proxy (`h2e_d3d8.ini`).
- 0.4.0: screen-space draw statistics, periodic projection samples (EXP-003 run-005).
- 0.3.0 and earlier: read-only Direct3D 8 logging proxy (EXP-003 run-001..003).
