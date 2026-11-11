# Rendering

Status as of 2026-11-10. Static addresses refer to **RenderD3D.dll SHA-256
`e9aff44931aa6c361fdcc49d1f5969d19777923c8f6c2c2e18e439bfd53c871d`** (preferred base 0x10000000).
**At runtime RenderD3D is relocated** (observed bases 0x07A00000, 0x07950000 — EAX.DLL occupies 0x10000000):
runtime VA = base + RVA. Sources: [EXP-001](experiments/EXP-001_pe_survey/README.md) (static),
[EXP-002](experiments/EXP-002_executable_unpacking/README.md) (engine side),
[EXP-003](experiments/EXP-003_d3d8_runtime/README.md) (runtime values).

## Current conclusions (summary)

| Topic | Conclusion | Tag |
|---|---|---|
| Window placement (windowed) | `StartUpperPos X,Y` sets the position; without it RenderD3D centres the window on the whole virtual desktop (spans monitors, GB-004) | CONFIRMED (static + run-005) |
| 2D / HUD layer | No XYZRHW draws; HUD and menus go through the scene call site +0x33DD with a 45 deg camera, zn 5, zf 3000 (plus an identity projection from +0x47F1). HUD anchored to corners at native pixel size; menus a fixed 640x480 centred panel | CONFIRMED (run-006/007) |
| Menu / briefing layout | The menu code centres a fixed 640x480 layout itself: `(width-640)/2`, `(height-480)/2` from `[*g_pSysInterface+0x64/0x68]` at ~15 sites (hitman2.exe 0x52CB90, 0x52CE7C, 0x5462A1, 0x546E14-0x54705D, 0x549184, 0x54B84D, 0x54BB3C, 0x55B7CF-0x55B9A4), no scale factor. Some parts (save list, briefing text scroller, map content, intro logos) are drawn in sub-viewports, the rest in full-screen viewports at engine-computed pixel positions | CONFIRMED static (2026-11-11) + runtime (uiscale test) |
| Render resolution | Parsed from `Resolution WxH` into `[*g_pSysInterface+0x64/0x68]`; in fullscreen **RenderD3D `FUN_100197b0` snaps the width to 512/640/800/1024/1280/1600 and forces the 4:3/5:4 height**; widths > 1600 keep the width but get an unset height (window-title pointer) -> CreateDevice fails. Windowed mode keeps the configured size. RenderD3D copies the result into the present parameters (0x10001B7A) | CONFIRMED (run-003 + run-004) |
| Mode validation | RenderD3D never calls `GetAdapterModeCount`/`EnumAdapterModes`; the parser has no range check; the only "validation" is the fixed snapping in `FUN_100197b0` (above) | CONFIRMED |
| Fullscreen | `Windowed=0`, refresh rate default (0), DISCARD, 1 back buffer, D24X8 depth; no Reset during play | CONFIRMED (run-001/002) |
| VSync | `PresentInterval=0x1` requested, yet 1194–5282 fps (menu) and 509–1969 fps (in-game) observed | CONFIRMED observation; cause OPEN |
| Viewports | Gameplay 800×600; letterbox 800×450 at y=75 (+ 800×75 bars) in intro/cutscenes; 128×128 render-to-texture; 640×480@(80,60) and 550×169@(159,276) overlays | CONFIRMED (run-001/002) |
| Projection (scene) | Standard LH perspective from RenderD3D+0x33DD; `_22 = _11 × aspect`, aspect = active viewport aspect ⇒ fixed horizontal FOV, **Vert-**. Gameplay: fovX 67.4°, fovY 53.15° at 800×600 | CONFIRMED (run-001/002) |
| Projection (shadow/RTT) | Second source RenderD3D+0xDBCE with 128×128 viewport: off-center, fovX ~0.6–1.2°, zn 1, zf 9999 — must be excluded from any FOV fix | CONFIRMED source / INFERRED purpose |
| FOV source | Matrix built by RenderD3D helper 0x10001750 from caller arguments; FOV values per scene (45°, 18.18°, 70°, 90° …) | INFERRED / OPEN |

## UI system (ZWINDOWS) - why menus, briefing, load screens and HUD are small

Static analysis 2026-11-11, hitman2.exe analysis image `f59d353e…05266a` (capture of exe `f85765f1…5136`), REA/Ghidra
12.1.4 + `llvm-objdump` on the same image.

- `CONFIRMED` Every 2D screen is a class of one widget system, registered by name: `ZWINDOWS`, `ZWINDOW`,
  `ZWINGROUP`, `ZWINOBJ`, `ZWINPIC`, `ZWINGROUP_AutoLayout` (registered at 0x4C0A24), and per screen
  `ZWINDOW_MainMenuWindow`, `ZWINGROUP_MainMenu`, `ZWINDOW_BriefingMenu`, `ZWINGROUP_LoadScreen` (registered at
  0x549ED3, factory 0x54A2C0), `ZWINDOW_IngameMenu`, `ZWINDOW_IngameMap`, `ZWINDOW_MapMenu`, `ZWINGROUP_InventoryMenu`,
  `ZWINDOW_LoadMenu`/`SaveMenu`, `ZWINDOW_OptionsMenu`/`GraphicsMenu`/…, `ZWINDOW_StatisticsLevel`, and the HUD groups
  `ZWINGROUP_Stressmeter`, `ZWINGROUP_WeaponAmmo` (0x55EC36), `ZWINGROUP_InfoDisplay`, `ZWINGROUP_Bar` (class-name
  strings 0x688A94-0x691688).
- `CONFIRMED` **Coordinates are screen pixels, authored for 640x480.** A window places its root geometry
  (`[this+0x6E]`, `ZGEOM::vtbl+0x74` = set position) at `((W-640)/2, (H-480)/2, 0)` with W, H =
  `[*0x6A6C5C+0x64/+0x68]` (the `Resolution` values). Example `FUN_0052cb80` (confirmation dialog): exactly this, then
  child `"ConfirmationDialog"`. Children keep their 640x480-space offsets, so nothing grows with the resolution.
- `CONFIRMED` Sub-viewports (save list, scrollers, map content) convert pixel rectangles to fractions of the screen by
  dividing by W and H (0x4C0401-0x4C044F; 0x54B944-0x54B99D computes `(W-640)/(2W)`, `(H-480)/(2H)`, `640/W`, `480/H`).
  Their size in pixels is therefore constant too.
- `CONFIRMED` **The engine only ever scales down.** Two sites have a scale path, both disabled at W >= 640:
  - Load screen method 0x549160 (vtable slot at 0x659C4C): if `W < 640`, scale = (W/640, W/448)
    (constants 0x6591E0 = 1/640, 0x6591DC = 1/448), else (1.0, 1.0) at 0x5491C3; applied with `ZGEOM::vtbl+0x238`.
  - 0x54B99D/0x54B9C4: the fractions `640/W`, `480/H` are clamped to 1.0 when greater than 1.
  There is no up-scaling code: at 1920x1080 every 2D screen is drawn 1:1, i.e. at 1/2.25 of the height it was
  designed for.
- `CONFIRMED` (run-008, earlier) The HUD is drawn with the gameplay projection, not the UI camera, and is anchored to
  the screen corners in pixels; it therefore also keeps its 640x480-era pixel size.
- `INFERRED` The PS3 HD release (owner's screenshots) shows two remedies: (1) briefing and load screen = the same
  4:3 layout scaled to the screen height with side bars (what `menuscale` does), (2) main menu = re-authored for 16:9
  (logo and character moved apart). (2) needs new layout data per screen; (1) is a uniform scale.
- `CONFIRMED` (static) The window manager `ZWINDOWS` (vtable 0x64DBA8, constructor 0x4BADF0, size 0x5F7, class id
  0x100030) has no global pointer: it registers itself by name (`"rWindows"`) in the scene variables; every
  `ZWINOBJ` caches it in `[obj+0x5C]` (`GetWindows` 0x4B76F0; on the root `[+0x5C]` == itself).
- `CONFIRMED` (runtime, probe in `src/fixes/zwindows.cpp`, log `samples/local/logs/h2e-20261111-probe-run1.log` +
  owner video) A new ZWINDOWS instance is created on every scene load (logos, main menu, mission); a scan of private
  RW memory for the vtable finds exactly one valid instance (15-46 ms). `[ZWINDOWS+0x1D4]` (getter 0x4BB580, setter
  0x4BB590) is **not** the focused window: it is set once per scene to an object allocated right after the manager
  (`ZWINDOWS+0x600`, vtable 0x657D80) and does not change for pause, map or inventory.
- `CONFIRMED` (runtime field diff, `samples/local/logs/h2e-20261111-probe-run2-diff.log` + owner video, offset log
  ≈ video + 5 s) **Open-window list**: a node container at ZWINDOWS+0xC4 (constructor 0x5B13E0 with block size 8,
  3 dwords per node; +0xD8 = nodes allocated, +0xE0 = head, +0xE4 = tail, +0xE8 = free list). Main menu: one node;
  Options pushes a second node 12 bytes further and pops it on return. Mission: head = tail = 0 while playing; one node
  while the in-game map (log 43.0-47.3 s) or the pause menu (from 48.3 s) is open. The HUD pick-up message and the
  inventory strip do not touch it. Node layout `INFERRED` {prev, next, value}; which class each node is: `OPEN`
  (resolver commit d42db77).
- `CONFIRMED` (static, `Scenes/MainMenu.ZIP` -> `SCENES/MainMenu.TEX`, read in memory, 2026-11-11) **UI asset
  resolution**: the menu art is 67 small textures (1x 256x256, 8x 64x64, 19x 32x32, 11x 16x16, 27x 8x8 - ABGR,
  e.g. `MainScreen_Logo_01`), i.e. the logo and Agent 47 are tiles authored for 640x480. Text uses pre-rasterised
  bitmap fonts: 128x128 palettised (`PALN`) glyph pages named after the TrueType source and pixel size,
  `Fonts\COPRGTB.TTF-26#0..4`, `-17#5..7`, `-18#8..10`, `-14#11` (Copperplate Gothic Bold). Scaling the UI camera
  re-rasterises geometry at native resolution but cannot add texel detail: text and art look soft at 2.25x.
  HD would need replacement textures: font pages regenerated from the TTF at 4x with the same glyph layout, and
  upscaled art tiles, substituted by the proxy at texture creation. Fonts: done in [EXP-005](experiments/EXP-005_hd_fonts/README.md)
  (fix `hdfonts`; glyph tables in the .PRM, TrueType font embedded in the .ZGF, FILE_FORMATS.md); art tiles `OPEN`.
- `CONFIRMED` (static, RenderD3D `e9aff449…c871d`, EXP-005) **Texture upload**: the TEX loader `FUN_100109d0`
  (RVA 0x109D0) creates every texture with `IDirect3DDevice8::CreateTexture` in `D3DPOOL_MANAGED` and fills each
  level with `IDirect3DTexture8::LockRect(level, NULL, 0)` / copy / `UnlockRect` (e.g. RVA 0x11417 / 0x1150C);
  no `UpdateTexture`/`CopyRects` staging in these paths (RVA 0x10F90-0x11700). D3D8 slots used by the proxy: device 20 `CreateTexture`, 61 `SetTexture`;
  texture 2 `Release`, 13 `GetLevelCount`, 14 `GetLevelDesc`, 16 `LockRect`, 17 `UnlockRect` (mingw-w64 `d3d8.h`
  order; LockRect/UnlockRect match RenderD3D's `call [ecx+0x40]` / `[ecx+0x44]` on the new texture).
- `INFERRED` ZWINDOWS+0x1DC..0x344 hold per-element float timers ramping from 0 (fades: HUD at mission start, pick-up
  message, map, pause).
- Load screens and in-game menus (2026-11-11, FIXES.md `menuscale`): pause menu scaled (window list), map excluded
  (already adapts to the screen), level load screens `INFERRED` already full-screen (owner: correct only when not
  scaled; drawn with the 640x480-era UI camera plus a zn 0.5 / zf 10 camera for the bottom progress bar, no ZWINDOWS
  instance found during the load). `OPEN`: inventory; how HUD groups compute their corner anchors (start:
  `ZWINGROUP_WeaponAmmo` registration 0x55EC36, function 0x560B60 which divides HUD pixel positions by 640/480).

### Menu cursor (static 2026-11-11, analysis image `f59d353e…05266a`, exe `f85765f1…5136`, REA/Ghidra + llvm-objdump)

- `CONFIRMED` `g_pSysInput` (0x6A6C60) is a `ZSysInputWintel` (vtable 0x639C10, constructor 0x4108E0 stores `this`
  in 0x6A6C60). The main loop (0x416810) calls `g_pSysInput->vtbl[0x24]` = 0x410E10 before
  `ZEngineDataBase::MainLoop`; 0x410E10 polls the devices, then calls 0x40F7C0.
- `CONFIRMED` The mouse device `WintelMouse` (constructor 0x411680, vtable 0x639CA4) update 0x411810 reads
  `GetDeviceState(DIMOUSESTATE2, 0x14 bytes)` and stores lX/lY/lZ as floats at `+0x3A/+0x3E/+0x42`, also copied to
  `g_pSysInterface+0x7F0..` (delta) and summed into `+0x7E4/+0x7E8` (unbounded; set to (W/2, H/2) at 0x415EE6 and
  0x41607D, set by ZWINDOWS 0x4BBD80). Its vtbl+0x20 (0x411920) returns `this+0x3A`.
- `CONFIRMED` **Cursor position** = `[g_pSysInput+0x18]` (x) / `[+0x1C]` (y), floats in unscaled screen pixels.
  0x40F7C0: `d = this->GetDelta(0)` (`call 0x40F9D0` at 0x40F830; 0x40F9D0 = thiscall, returns device[0]->vtbl[0x20]
  or 0), `x += d[0]`, `y += d[1]`, then clamps x to 0..`renderer->vtbl[0xB0]()` and y to 0..`vtbl[0xB4]()` (renderer =
  `[g_pSysInterface+0x170C]`, whose `+0x128` is the HWND) - 0x40F84E..0x40F900. If `[g_pSysInterface+0x62]` is 0
  (`INFERRED` windowed / no DirectInput mouse), the delta is taken from `+0x7E4` instead (GetCursorPos +
  ScreenToClient at 0x4168FB).
- `INFERRED` (owner report + the camera math) The menu cursor is drawn through the UI camera, hit tests use the
  unscaled position: with the camera scaled by s the cursor moves s times faster and the clamp lies off screen; the
  visible part of the unscaled screen is centre +/- (W/2s, H/2s). The `menuscale` fix confines it there (FIXES.md).
- `CONFIRMED` (static) **Second cursor in ZWINDOWS**: position `[ZWINDOWS+0x70/+0x74]` (floats; getter 0x4BBDC0 =
  vtbl+0x2EC, setter 0x4BBDE0 = vtbl+0x2E8, which clamps to 0..W, 0..H). Update 0x4BC1F0: `+0x80 = raw - [+0x78]`,
  `[+0x78] = raw` (raw = `g_pSysInterface+0x7E4/+0x7E8`), `+0x80 *= [+0x90]` (0x5C2810), pos += +0x80 (0x5C2670), set
  if the step is not zero. `CONFIRMED` (runtime, `tools/watch_values.py`, log
  `samples/local/logs/cursor-watch-20261111-b.log`, menuscale build with only the g_pSysInput hook): the g_pSysInput
  cursor stayed inside (533..1387, 300..780) while the owner still saw the main menu / briefing cursor too fast and
  unconfined (pause menu fixed); raw went to x = -38387 (unbounded). `INFERRED`: the main menu and the briefing use the
  ZWINDOWS cursor, the pause menu the g_pSysInput one.
- `OPEN` Which ZWINGROUP draws the cursor (`ZWINGROUP_MouseGroup`, vtable 0x64DEE8, factory 0x4BCA70, is a candidate)
  and where menu hit tests read the position.

### Options menus are scene data (2026-11-11, `Scenes/MainMenu.ZIP` sha256 `9164dfe3…eb9c382`)

- `CONFIRMED` (file contents) `SCENES/MainMenu.BUF` names the menu tree as scene objects: `OptionsMenu`
  (`0Graphics`, `1Sound`, `2Controls`), `GraphicsMenu1` (`03DrawDistance`, `04LevelOfDetail`, `05Shadows`,
  `06Weather`, `09Subtitles`, `10Blood`, `21MoreOptions`, `22Back`), `GraphicsMenu2` (`01TextureResolution`,
  `02TextureFilter`, `07AntiAliasing`, `08DXT`, `11EMBM`, `12Gamma`, `b1MoreOptions`, `b2Back`), each item with
  `Button`/`Focus`/`TextNormal` children. The two-digit prefix looks like a setting id - `INFERRED`.
- `SCENES/MainMenu.LOC` holds the English labels (`DrawDistance` -> "Draw distance", ...), including an unused
  `TextureColorDepth` -> "Texture color depth".
- Consequence: a new menu entry needs new scene objects (an asset change, not allowed on the install) or a page drawn
  by the proxy; the engine code that maps item ids to settings is `OPEN`.

## Architecture

- `CONFIRMED` `Hitman2.ini` selects the renderer with `DrawDll RenderD3D.dll` (`RenderOpenGL.dll` is the alternative).
- `CONFIRMED` The engine loads it with `GetProcAddress(hMod, "Create")` (hitman2.exe VA 0x418156) and calls the
  returned factory, then `obj->vtbl[0x30](hMod, …)`.
- `CONFIRMED` The renderer DLL imports engine globals/classes from `hitman2.exe` (`g_pSysInterface`, `g_pSysMem`,
  `g_pSysFile`, `g_pSysCom`, `ZEngineDataBase`, …); all 28 resolve against the runtime export table.
- `CONFIRMED` RenderD3D talks to Direct3D only through `Direct3DCreate8` (SDKVersion 120 at runtime).

## Direct3D 8 interface layout (verified)

`CONFIRMED` against mingw-w64 `d3d8.h`/`d3d8types.h` (2026-11-10): IDirect3D8 — 6 GetAdapterModeCount,
7 EnumAdapterModes, 8 GetAdapterDisplayMode, **15 CreateDevice (offset 0x3C)**; IDirect3DDevice8 — 14 Reset,
15 Present, **37 SetTransform (0x94)**, **40 SetViewport (0xA0)**. `D3DPRESENT_PARAMETERS` = 13 dwords
(BackBufferWidth … FullScreen_PresentationInterval); `D3DTS_PROJECTION` = 3; `D3DDEVTYPE_HAL` = 1;
`D3DFMT_X8R8G8B8` = 22, `D3DFMT_D24X8` = 77. Minimal definitions: `src/common/d3d8_min.h`.

## Config keys

- `CONFIRMED` RenderD3D reads `RefreshRate`, `GammaValue`, `DisableVSync`, `Antialias`, `Anisotropy`.
- `CONFIRMED` hitman2.exe reads `Resolution`, `ColorDepth`, `Window`, `DrawDll`, `TextureResolution`,
  `ShadowDetail`, `ScriptDll`, `ScriptIfDll`, … (strings at VA 0x682224–0x6823D0, used at 0x416FEE+ and 0x418307+).
- `CONFIRMED` Both use the same getter: `g_pSysInterface->vtbl[0x54](key, &value) -> bool`
  (RenderD3D bytes at 0x10015046; engine at 0x417000). `g_pSysInterface` = hitman2.exe export VA 0x6A6C5C.

## Device creation

`CONFIRMED` (static bytes EXP-001 + runtime caller RenderD3D+0x1DB6 in EXP-003; vtable slot verified):
```
a1 bc cf 0d 10       mov  eax, [0x100DCFBC]        ; global IDirect3D8*
8d be 66 13 00 00    lea  edi, [esi+0x1366]        ; ppReturnedDeviceInterface
8d 8e 6a 13 00 00    lea  ecx, [esi+0x136A]        ; pPresentationParameters
...  push [esi+0x128] (hFocusWindow) ; push 1 (HAL) ; push 0 (adapter) ; push eax
ff 52 3c             call [edx+0x3C]               ; IDirect3D8::CreateDevice  (0x10001DB3, returns to 0x10001DB6)
3d 7c 01 76 88       cmp  eax, 0x8876017C          ; D3DERR_OUTOFVIDEOMEMORY -> "Not enough video memory..."
```
Runtime parameters (run-001): 800×600 X8R8G8B8, fullscreen, 1 back buffer, DISCARD, D24X8, refresh 0, interval 0x1,
BehaviorFlags 0x40. Renderer object layout (INFERRED from the code): `+0x128` HWND, `+0x1366` IDirect3DDevice8*,
`+0x136A` D3DPRESENT_PARAMETERS (unaligned ⇒ byte-packed class).

## Projection matrix / FOV

`CONFIRMED` call site (static bytes + runtime caller RenderD3D+0x33DD; slot 37 verified):
```
8b 86 66 13 00 00    mov  eax, [esi+0x1366]        ; device
8d 54 24 04 / 52     push &matrix
6a 03                push 3                        ; D3DTS_PROJECTION
ff 91 94 00 00 00    call [ecx+0x94]               ; IDirect3DDevice8::SetTransform (0x100033D7)
```
All 300 logged distinct projection matrices of run-001 came from this site. The matrix is filled by the call to
0x10001750 at 0x100033B0 (corrected 2026-11-10; EXP-001 originally said 0x1000174F/0x100033AF — a one-byte decoding slip). Observed values (EXP-003 §5): horizontal scale `_11` encodes the scene FOV; `_22 = _11 × aspect`
with aspect 1.3333 (800×600 viewport) or 1.7778 (800×450 letterbox) ⇒ **Vert-** behavior: widening the aspect
reduces the vertical FOV instead of widening the horizontal one. zn = 5 for scene cameras; zf 3000–20000 (one ~3e5).

`CONFIRMED` (run-002): gameplay uses the same function with the full-screen viewport. `HYPOTHESIS`: with a 16:9 back
buffer the viewport becomes 16:9 and the vertical FOV is cropped — to be measured in run-003 (owner sets the resolution).

Other `push 3 … call [reg+0x94]` byte candidates: **0x1000DBC8 is live in gameplay** (returns to RVA 0xDBCE; the 128×128
render-to-texture pass, EXP-003 run-002); 0x1000E63E produced no logged calls in run-001/002.

Gameplay camera (run-002): fovX 67.4°, fovY 53.147°, aspect 1.33333 at viewport 800×600, zn 5, zf ≈ 20 000.
Under the confirmed Vert- rule, a 16:9 viewport would keep fovX 67.4° and give fovY ≈ 41.1° (= 2·atan(tan(33.7°)·9/16); computed, not yet observed).

## Fix (D-008, proxy 0.5.x)

- `CONFIRMED` (run-007) **Resolution**: in-memory patch of RenderD3D +0x197F7 `0F 84 05 01 00 00` -> `E9 06 01 00 00 90` (always skip the snapping), applied by the proxy's DllMain only after checking the RenderD3D.dll SHA-256 and the 6 bytes. Fullscreen 1920x1080 confirmed in a mission.
- `CONFIRMED` **not viable as built** (run-007/008; kept in the code, off by default) **Hor+ by matrix rescaling**: the
  HUD and parts of the map are drawn with the gameplay projection itself, so they move towards the centre by the
  same factor. A working Hor+ must act where the camera FOV / HUD placement is computed. Original design: for the scene call site, full-width viewports, centred LH perspective with zf > 4000 and a back buffer wider than 4:3, `_11` and `_22` are multiplied by (4/3)/backBufferAspect: vertical FOV as at 4:3, wider horizontal FOV (gameplay 53.1 x 83.3 deg at 16:9). v0.5.0 without the zf filter shrank the HUD/menus (run-007).
- `CONFIRMED` **rejected (2026-11-11, reverted in git) menu scaling by viewport**: an experimental `uiscale` fix scaled
  every viewport inside the centred 640x480 rectangle by 2.25. Owner screenshots: the intro logos scaled correctly,
  but the main menu did not change, in the Load menu only the save list grew, in the briefing only the text scroller
  grew, and the map content was zoomed against its frame. Cause: most menu elements are positioned by the engine in
  full-screen viewports (see "Menu / briefing layout"). A working menu scale must act on the whole composed menu
  frame (e.g. upscale the centred region before Present, with the cursor scaled the same way so clicks stay
  consistent) or on the engine's layout code; both need a reliable "menu frame" signal first (`OPEN`).
- Not addressed: HUD/menus stay at native pixel size (small at 1080p); map cursor offset (GB-005); Esc in menus (OPEN).
- Switches (0.6.0, D-009): `h2e.ini` `[resolution] Enabled`, `[horplus] Enabled`; catalog in `docs/FIXES.md`.
  Before 0.6.0: `h2e_d3d8.ini` `[fixes] Resolution=0/1 HorPlusFov=0/1`, written by `tools/d3d8_logger_install.py install GAME --fix resolution --fix fov`; off by default.

## Implementation options (evaluated 2026-11-10; B chosen, D-008)

| Option | Basis | Pros | Cons |
|---|---|---|---|
| A. `d3d8.dll` proxy | `CONFIRMED` single `Direct3DCreate8` entry; logging proxy already works | No game-file changes; can adjust CreateDevice params and rescale `_11`/`_22` in SetTransform(PROJECTION) | Must distinguish 3D scene vs. 2D/HUD projections; per-call interception |
| B. Patch RenderD3D in memory (loader/ASI) | call sites above (hash-pinned) | Fix at the source (matrix helper 0x10001750 / caller) | Hash-specific; needs decompiled semantics; base relocates (use RVAs) |
| C. Patch the engine at runtime | EXP-002 functions | Fix FOV/aspect at origin | DRM-wrapped exe: only in-memory, after legitimate start (D-005) |

`OPEN`: existing community solutions (dxwrapper, widescreen fixes) have not been researched in this repo yet;
document their behavior from their sources before relying on them.
