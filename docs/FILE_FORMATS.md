# File formats

Status: inventory (2026-11-10); scene archive members `.TEX`, `.PRM` font tables, `.ZGF`/`.GMS` compression parsed
2026-11-11 (EXP-005). Reader: `tools/hdfonts_build.py` (stdlib, in memory).

## Scene archives (`.ZIP`)

- `CONFIRMED` Standard ZIP archives (Python `zipfile`). `Scenes/MainMenu.ZIP` (`9164dfe3…c382`) holds
  `SCENES/MainMenu.{ZGF,SUP,BUF,GMS,TEX,SND,LOC,OCT,SGP,RMC,RMI,PRM}`; level scenes use `SCENES/<level>/<scene>.*`.
- `CONFIRMED` **Compressed members** (`.GMS`, `.ZGF`): u32 inflated size, u32 stored size, u8 (0), then a raw
  deflate stream (zlib wbits -15) of exactly the inflated size.
- `CONFIRMED` **`.ZGF`** (inflated, header `TFGZ`): carries the scene's TrueType font(s) (MainMenu: one complete
  `CopperplateGothic-Bold` sfnt at offset 40, 56519 bytes, with extra controller-symbol glyphs). Other content
  `OPEN`.
- `CONFIRMED` **`.TEX`**: u32 offset of a 2048-slot u32 entry-offset table (indexed by texture index; 0 = unused),
  u32 offset of a second 2048-slot table (all zero in MainMenu), u32 3, u32 4, then the entries. RenderD3D reads the
  file through the offset table (`FUN_100109d0`, RenderD3D RVA 0x109D0); walking entries sequentially fails because
  some entries are followed by extra data (e.g. a u32 count + u32 index list in `C0-1__MAIN.TEX`).
  Entry: u32 size, 4-char tag twice (stored reversed: `NLAP` = `PALN`, also `PALO`, `RGBA`, `1TXD` = `DXT1`,
  `DXT3`), u32 index, u16 width, u16 height, u32 mip count, u32 x3 (flags/hash, `OPEN`), name `\0`, then per mip
  u32 byte size + data; palettised tags end with u32 count + count ARGB dwords (`INFERRED` byte order ARGB).
- `CONFIRMED` **Font pages**: `PALN` 128x128, 1 mip, 8-bit indices into 16 palette entries `0xA0FEFFFF`-style
  (white, alpha 0x00..0xF0 = 4-bit coverage), named `Fonts\COPRGTB.TTF-<pixel size>#<page>`.
- `CONFIRMED` **`.PRM` font tables** (one per font size; found by their header): 32-byte header
  `u32 1, u32 8, u32 pixel size, f32 1.0, u32 glyph count, u32 line height, u32 kerning pair count, u32 page count`,
  then `count` glyph records of 20 bytes: `u32 character code` (Unicode; MainMenu: 0x20-0x25CA plus controller
  symbols at U+E000-E007 and U+F001-F002, all present in the embedded font's cmap),
  `u32 0x800 | TEX index of the page`, `u8 x0, y0, x1, y1` (pixel box, each stored + 0x80, end exclusive),
  `u8 width, height` (= x1-x0, y1-y0), then 8 bytes (`INFERRED`: bearing x/y, advance, line height, u16 kerning
  link), then `nkern` kerning pairs of 8 bytes (`OPEN` layout). The tables are referenced from the `.GMS` text
  objects (`"Fonts\COPRGTB.TTF"` followed by a PRM offset, `INFERRED`).
- `CONFIRMED` The pages were rasterised at `pixel size` pixels per em (FreeType convention; EXP-005).
- `CONFIRMED` The LOAD scenes' second table (`-18`, line height 22) is a different typeface under the COPRGTB name,
  not embedded in any scene.

## Inventory (CONFIRMED by directory listing of the install)

| Location | Files | Notes |
|---|---|---|
| `Scenes/` | `MainMenu`, `InventoryMenu` × {`.ZIP`, `.WHD`, `.WAV`} | Same triplet pattern as levels |
| `Scenes/AllLevels/` | `AlwaysGfx`, `ErrorMessages`, `LevelMenu`, `Logos`, `ScratchedDisc` × triplet | `Hitman2.ini`: `DefaultScene=AllLevels/Logos.gms` ⇒ `.gms` scene names resolve into these packages (`INFERRED`) |
| `Scenes/C<chapter>-<n>/` | `<level>__MAIN`, `<level>__LOAD`, `*_Briefing`, `*_CUT_*` × triplet | 22 level folders + `Intro` |
| root | `streams.wav` (109 MB) | `OPEN` |
| `Save/` | `N.blap`, `N.1`, `N.2`, `N.3` | Save files; `OPEN` |
| root | `Hitman2.ini`, `keyboard.cfg` (XML key mappings) | Plain text |

## Hypotheses to test (not facts)

- `HYPOTHESIS` `.WHD` is a header/index for the `.WAV` sound bank of the same package.
- Do not reuse Hitman: Codename 47 / Contracts / Blood Money format docs without verification.
