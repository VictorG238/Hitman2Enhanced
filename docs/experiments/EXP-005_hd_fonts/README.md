# EXP-005 — HD UI fonts (bitmap font pages re-rendered from the embedded TrueType font)

- **Date**: 2026-11-11
- **Status**: format reverse-engineered (`CONFIRMED`), generator + fix `hdfonts` built and tested offline and on a
  real D3D8 device (`CONFIRMED`); in-game run 1: menus, Load and briefing text sharp (`CONFIRMED`, owner);
  subtitles/cutscenes and a full mission pending.
- **Goal**: UI text (main menu, pause, briefing, Load/Options) sharp at 1920x1080 with `menuscale` on, by replacing
  the engine's pre-rasterised font pages with higher-resolution pages keeping the glyph layout (UVs unchanged).
- **Hypothesis**: the font pages are plain textures addressed by normalized UVs; a page re-rendered at N x with each
  glyph in its N x box can be swapped at the Direct3D level without touching game code or data.

## Files (read in memory from the install, never extracted into the game folder)

| File | SHA-256 |
|---|---|
| `Scenes/MainMenu.ZIP` | `9164dfe3f741ada39c050caa1a802d7bceca411eef9fbe7e993287270eb9c382` |
| `Scenes/AllLevels/LevelMenu.ZIP` | `14f5486b06295d210c9a48da53283844c268d4a2ace4b0052e3417480a4ab474` |
| `Scenes/C0-1/C0-1__LOAD.ZIP` | `0af9667c40d17a2b905f6b1e96bb68c409be6ec49d15b7b8486992a408dd0050` |
| `Scenes/C0-1/C0-1__MAIN.ZIP` | `b76f72863ee419aca0a4d629817335bfbc545fc9d36078eb61bd51c2b38b4ae3` |
| `RenderD3D.dll` | `e9aff44931aa6c361fdcc49d1f5969d19777923c8f6c2c2e18e439bfd53c871d` |
| `main.analysis.img` (hitman2.exe capture) | `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a` |
| `samples/local/h2e_hdfonts.bin` (output, 4x, git-ignored) | `78bfb0d903110f251ebf0d9437ad4f1ddfa8fc8ca962516a4101c216c0aec1de` |

## Tools

Python `zipfile`/`zlib` (in-memory reads), REA/Ghidra 12.1.4 on RenderD3D.dll and the hitman2.exe capture,
`llvm-objdump -d` on RenderD3D.dll, `tools/hdfonts_build.py` (new), `tests/native/hdfonts_smoke.cpp` (new).

## Procedure

1. `python -I tools/hdfonts_build.py <game> --preview samples/local/fonts/preview` (writes the pack and, per page,
   `<key>_<name>_orig.png` / `_hd.png`; ~50 s).
2. `cmake --build build --config Release`, `ctest --test-dir build -C Release` (`hdfonts_smoke`: real device).
3. In game (pending): `python tools/d3d8_logger_install.py install <game> --fix resolution --fix menuscale
   --fix hdfonts --log-level 1`, launch through Steam, compare menu/pause/briefing/load text with the fix off/on.

## Results

### Scene data (`CONFIRMED`, format details in [FILE_FORMATS.md](../../FILE_FORMATS.md#scene-archives-zip))

- Font pages: 128x128 `PALN` TEX entries, 16-colour palettes of white (`0xFEFFFF`) with alpha 0x00..0xF0
  (4-bit coverage), named `Fonts\COPRGTB.TTF-<px>#<n>`.
- Glyph metrics are in the scene's **.PRM**: one table per font size (32-byte header, 20-byte glyph records with
  the TEX index and the pixel box, 8-byte kerning pairs). MainMenu.PRM holds 6 tables (26, 17, 18, 14, 12, 23 px;
  248 glyphs each, Unicode codes 0x20..0xF002). 57 scenes contain tables; 45 distinct pages are used 695 times.
- The scene's **.ZGF** (inflated) embeds a complete TrueType font: `CopperplateGothic-Bold`, 252-260 glyphs, with
  extra controller-button glyphs (U+E000-E007, U+F001-F002; post names `SymbolA`, `SymbolCross`, ...). Three different
  builds exist (LevelMenu 55146 B, LOAD screens 56096 B, MainMenu 56519 B); each scene's font matches its pages.
- The original pages were rasterised at `size` pixels per em: with px = size/unitsPerEm (2048) the 1x re-render of
  the 26 px table differs from the original by 1.2 % mean alpha (fitting the box heights instead gives 6.7 %).
- The LOAD screens' second table (`-18`, line height 22) is **another typeface** stored under the COPRGTB name and
  not embedded anywhere (best error 0.12-0.14 against every embedded font): its 4 pages are upscaled, not
  re-rendered. Their `-24` table matches the embedded font with hinting differences (error ~0.07).

### Texture upload path (`CONFIRMED`, static, RenderD3D `e9aff449…c871d`)

- `FUN_100109d0` (RVA 0x109D0) walks the TEX 2048-slot offset table and dispatches per tag (`PALN` 0x50414C4E,
  `PALO`, `RGBA`, `DXT1`, `DXT3`) to a reader object; the texture is created by a manager method
  (`vtbl+0x10`) which calls `IDirect3DDevice8::CreateTexture` (slot 20) with `D3DPOOL_MANAGED`, decodes the page
  into a w*h*4 buffer and copies it into each level with `IDirect3DTexture8::LockRect(level, NULL, 0)` /
  `UnlockRect` (slots 16/17; e.g. RVA 0x11417 / 0x1150C, converter at RVA 0x11330 with the target format from
  `[mgr+0x20030]`). `INFERRED`: the palette dword is used as ARGB (white text, alpha = coverage) - consistent with
  the masks 0xFF0000/0xFF00/0xFF/0xFF000000 set before the conversion call at RVA 0x1144E.
- Hence a page can be recognised at `UnlockRect` from its contents. The key is the FNV-1a 64 of the 4-bit alpha
  (`alpha >> 4`), identical for A8R8G8B8 and A4R4G4B4 targets; the D3D format actually used in game is logged by the
  fix (`OPEN` until the in-game run).

### Generator (`tools/hdfonts_build.py`)

- Re-renders every glyph from the outline (stdlib TrueType parser: cmap 4, simple + composite glyphs; non-zero
  winding rasteriser, 8 sub-rows per pixel, exact horizontal coverage) at 4x into its 4x box, aligned on the
  coverage centroid of the original glyph; checks each glyph by downsampling to 1x (mean abs alpha error): above
  0.15 the glyph is upscaled from the original instead; a table whose sample error is above 0.10 is upscaled
  entirely.
- Run on the install: 45 pages, 2904 glyphs from outlines, 238 upscaled (the LOAD `-18` table plus a few hinting
  outliers), worst accepted error 0.148; pack 2.3 MB (PackBits). Visual check of the previews: sharp, correct
  glyphs including the controller symbols.

### Fix `hdfonts` (proxy)

- Framework: texture callbacks `onTextureFilled` / `onSetTexture` / `onTextureReleased`; the proxy patches the
  runtime's texture vtable (LockRect, UnlockRect, Release; located through a 1x1 probe texture) and
  `IDirect3DDevice8::SetTexture` (slot 61) only when such a fix is enabled.
- `hdfonts_smoke` (real HAL device, 2026-11-11): a 128x128 A8R8G8B8 page filled like RenderD3D does is replaced by
  a 512x512 texture at `SetTexture`; a texture with other content is untouched; logs as expected. PASS.

### In-game run 1 (2026-11-11, `resolution + menuscale + hdfonts`, 1920x1080 fullscreen)

- Log `samples/local/logs/h2e-20261111-121154.log` (log level 1): texture hooks installed; 65 pages replaced by
  512x512 copies in main menu, LevelMenu, LOAD and briefing scenes, all **A8R8G8B8** (the key matches as designed).
  The only unmatched 128x128 key (`a35660f506296325`) is the all-opaque tile shared by the `LevelMenuDark#*`,
  `MissionBriefing_Background#*` and `*_Foto1#*` pages: not a font, correctly left alone. No errors, no crash.
- Owner video (`2026-11-11 12-09-56.mp4`, 45 s: main menu, Load, mission start, briefing): owner reports the text
  looks good; a 1:1 crop of the main menu shows clean glyph edges next to the still-soft 47 art tile. `CONFIRMED`.
- Not checked: in-game subtitles and cutscene text (without `framelimit` the game freezes at uncapped frame rates,
  GB-001), pause menu, Options.

### In-game run 2 (2026-11-11, `resolution + menuscale + framelimit + hdfonts`)

- Log `samples/local/logs/h2e-20261111-121555.log`, two processes. Process 1 (PID 18164): main menu (19 pages
  replaced), Load, loading a save - **crash** ~14.9 s after start (GB-011: `0xC0000409` fast fail 0x15, then AV);
  the log ends normally up to that point, no hdfonts error. Process 2 (27 s): menus and a scene change, no error.
- Subtitles were not reached in the log of this run. Cause of the crash `OPEN` (GB-011); hdfonts now logs the number
  of live HD copies and every release, to show whether the menu copies are freed at scene changes.

### In-game run 3 (2026-11-11 15:41, same fixes, build with live/freed logging)

- Log `samples/local/logs/h2e-20261111-154211.log` (30.5 s): the owner reloaded the save that crashed in run 2;
  **no crash** (no Application Error event, normal detach). 85 HD copies created, 84 freed when RenderD3D released
  the originals at each scene change (the last one at exit), at most 19 alive at once (~27 MB with mips): the
  textures do not accumulate. `CONFIRMED`. This makes an hdfonts memory leak an unlikely cause of GB-011.

## Conclusions

The font pages can be made sharp without changing any game file or code: the glyph layout is preserved, so the
engine's UVs, metrics and kerning stay valid. Remaining risk: whether the in-game texture format keeps the 4-bit
alpha exactly (A1R5G5B5 would not; the fix then logs "not in the pack" and does nothing).

## Confidence

High for the formats and the generator (round-trip checks on all 45 pages) and for the in-game swap in menus
(run 1); subtitles and a full mission not yet observed.

## Next steps

1. Run with `framelimit` added (so cutscenes do not freeze): subtitles, pause menu, Options, a full mission.
2. If the art tiles (logo, Agent 47, buttons) should also be HD, the same texture swap can serve upscaled art; that
   needs replacement art, not a font.
