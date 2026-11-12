# Executable and DLL analysis

Status as of 2026-11-10. Source: [EXP-001](experiments/EXP-001_pe_survey/README.md).
Tags: `CONFIRMED` / `INFERRED` / `HYPOTHESIS` / `OPEN` (see [CONTRIBUTING.md](../CONTRIBUTING.md)).

## Binary identity

Snapshot `samples/local/snapshot-20261110` (manifest: `MANIFEST.sha256` inside the snapshot).
All are PE32 i386, subsystem WINDOWS_GUI. `CONFIRMED` (pe_inspect, cross-checked with llvm-readobj).

| File | Size | SHA-256 | TimeDateStamp (UTC) | Notes |
|---|---:|---|---|---|
| hitman2.exe | 873984 | `f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136` | 2003-06-24 14:01:57 | **Packed**. VERSIONINFO 1.0.0.277 |
| testapp.exe | 873984 | identical to hitman2.exe | — | Byte-for-byte copy of hitman2.exe |
| RenderD3D.dll | 339968 | `e9aff44931aa6c361fdcc49d1f5969d19777923c8f6c2c2e18e439bfd53c871d` | 2003-06-24 14:03:52 | Direct3D 8 renderer |
| RenderOpenGL.dll | 319488 | `d81d5bc9802ca9252d0ca93d080179bdfc0a1058306eeb3524082837e9c65688` | 2003-06-24 14:03:59 | OpenGL renderer |
| config.exe | 53248 | `8ad654db7c608bf523103a03fae408384af66f600beba0e85564e28cd19ccd67` | 2002-12-03 18:40:04 | Settings tool (writes hitman2.ini) |
| eax.dll | 135168 | `e56ee2d67bfeebbbcaa154b61e0ef248b0a25d6a1e65450f0ed86231d1768e1c` | 2001-12-11 20:52:23 | VERSIONINFO 3.0.1.0, exports `EAX.DLL` |
| jpegdll.dll | 159744 | `8f4047c1a47e485161064eb7d3a9275b61b0de9be156d9d613bfc2d03f0ad51f` | 2002-10-17 12:11:00 | 103 exports |
| xmlparse.dll | 131072 | `d48c3494dc1f1f876c363225ece3927513f8980addbdee82f261fbd27cf7ffb9` | 2002-10-17 12:11:11 | 54 exports |
| p5dll.dll | 90112 | `2b4720e74e4d7c61e67f1fc13d44e33b3954e2369236b3d3caa86dc0a4312d0a` | 2002-05-09 02:19:51 | 4.6.25.7; imports HID.DLL/SETUPAPI |
| msvcrt.dll | 290869 | `11b2e109ba8012d8ddcee1dd8b6ca060aedccbb60663f964d34d4ae50449d105` | 2001-09-20 21:52:56 | 6.1.9359.0 |
| Msvcp60.dll | 401462 | `f1fd0f1a54f196b19a6f21044092c89c02353dad173c236d80f6474cb8a7ea7f` | 2000-08-29 10:19:12 | 6.0.8972.0 |
| runme.exe | 40960 | `e07f112452240049472ced6c4487b5d9b53ad567b3e7cc7603b2e2740a22bafd` | 2005-11-05 02:50:09 | PDB `u:\valve_main\src\utils\runme\...` (Steam launcher stub); `runme.dat` contains `hitman2.exe` |
| uninstall.exe / UNWISE.EXE | — | see manifest | — | Installer leftovers |

Linker 6.0 on every game binary plus valid Rich headers ⇒ `INFERRED`: built with the Visual C++ 6.0
toolchain. Rich `prod_id` values are recorded raw in the JSON reports, not interpreted.

### Retail 1.0.0.265 (second supported build, 2026-11-11)

Owner's disc/retail install (`E:\...\Hitman 2 Silent Assassin 1.0`), read-only analysis of the files on disk.

| File | Size | SHA-256 | Notes |
|---|---:|---|---|
| hitman2.exe | 3452367 | `0d7742c85e328886e91152036ae6615ca4c98bbb97c7a36d8f9e150235e57560` | VERSIONINFO 1.0.0.265; **not** packed (sections `.text` raw==va, plus `.cms_t`/`.cms_d`); PE timestamp 2012 (header rewritten) |
| RenderD3D.dll | 327680 | `803c32e44e9441e2a430356f1fad8dd043dec67f81f4d6ba6070be0f52c7567a` | timestamp 2002-09-18 |

Addresses ported from the Steam build (`src/fixes/game_build.cpp`). Method: the Steam bytes (analysis image
`f59d353e…05266a`) with rel32 operands and absolute addresses in 0x401000..0x700000 masked are searched in the
1.0 file; every hit was unique and then read back from the file (`CONFIRMED`, static). Same instruction sequences,
shifted code; object layouts match where compared (ZWINDOWS ctor fields +0x5F6/+0x5F2/+0x1D4/+0x94, GeomDb `+0x98`,
cursor fields +0x70/+0x78/+0x90, raw cursor `+0x7E4/+0x7E8`, DirectInput flag `+0x62`, IngameMap ctor).

| Item | Steam 1.0.0.277 | Retail 1.0.0.265 |
|---|---|---|
| `g_pSysInterface` | 0x6A6C5C | 0x69F054 |
| Timer `call _ftol` (context start) | 0x417B40 (0x417B32) | 0x4183C2 (0x4183B4); time scale field 0x18B8 instead of 0x193C |
| Timer function | 0x4179F0..0x417C60 | 0x418250..0x4184F0 |
| `_ftol` | 0x5A01AC | 0x59DD9C |
| Menu cursor call / GetMouseDelta | 0x40F830 / 0x40F9D0 | 0x40FAA0 / 0x40FC40 |
| Input frame function | 0x40F7C0..0x40F906 | 0x40FA30..0x40FB76 |
| ZWINDOWS vtable (setter/getter at +0x2E8/+0x2EC) | 0x64DBA8 | 0x646928 (0x4BA5B0 / 0x4BA590) |
| ZWINDOW_IngameMap vtable | 0x659058 | 0x651DD8 |
| RenderD3D width-snap `jz` (fix `resolution`) | RVA 0x197F7 | RVA 0x18767 (same 40 context bytes) |
| RenderD3D camera SetTransform return | RVA 0x33DD | RVA 0x349D |

`CONFIRMED` (owner run 2026-11-11): the ZWINDOWS open-window list (+0xE0/+0xE4) works the same - menus scaled.
`OPEN`: the IngameMap open flag (+0x20) in game.

The 1.0.0.265 exe's section headers are rewritten (all `INITIALIZED_DATA|READ|WRITE`, no `EXECUTE`) and it has no
NX-compat flag, so its code is mapped `PAGE_WRITECOPY` and runs without DEP. `exe_redirect` therefore accepts
`READWRITE`/`WRITECOPY` image pages besides executable ones (first run refused the timer and cursor hooks).

### Files changed during session 1 — resolved (installation repair)

`CONFIRMED`: on 2026-11-09 at 23:55:41 (-03) six files were rewritten: `RenderD3D.dll`,
`RenderOpenGL.dll`, `config.exe`, `jpegdll.dll`, `msvcrt.dll`, `xmlparse.dll` (old hashes in EXP-001).
**Cause (stated by the project owner, 2026-11-10):** the owner manually verified and repaired the
installation, which was previously incomplete/incorrect. The table above is the **repaired** state.

**Reference baseline**: `samples/local/baseline-20261110-repaired` (created 2026-11-10 00:20 -03).
Its 19-file manifest is byte-identical to `snapshot-20261110` and to the live install at that time,
so all EXP-001 results apply to the repaired installation. The pre-repair files were not preserved.

## hitman2.exe — packed executable

`CONFIRMED`:
- 2 sections: `.text` (VA 0x1000, vsize 0x2DB000, raw size 0xC5800, **entropy 8.00**) and `.rsrc`
  (flagged CODE|EXECUTE|WRITE).
- Imports only 4 kernel32 functions: `LoadLibraryA`, `GetProcAddress`, `VirtualAlloc`, `VirtualFree`.
- Entry point RVA 0x1000: `B8 6C B8 6E 00 FF E0` = `mov eax, 0x006EB86C ; jmp eax` → stub inside `.rsrc` (RVA 0x2EB86C).
- The EXPORT directory (RVA 0x274760, size 0x976E) points at memory not backed by the file:
  the export table only exists after unpacking.
- `RenderD3D.dll` and `RenderOpenGL.dll` import 28 C++ symbols by name from `hitman2.exe`
  (e.g. `?g_pSysInterface@@3PAVZSysInterface@@A`, `ZEngineDataBase::GeomRefToPtr`,
  `ZSaveGame`/`ZLoadGame` operators, `ConstrainedParticleSystem`). The unpacked exe exports engine classes.
- Stored PE checksum equals computed checksum (0x000DA146).

`INFERRED`: `Hitman2.ini` names `SoundEngine.dll`, `SDL_Engine.dll`, `ScriptInterfaces.dll` and
`Locale\English.xml`; none exist on disk, and the original installer (INSTALL.LOG) never copied them.
They are most likely linked into / embedded in `hitman2.exe`.

~~`HYPOTHESIS`: packer is unidentified.~~ **Updated by [EXP-002](experiments/EXP-002_executable_unpacking/README.md):**
`INFERRED` (high) the packing layer is a **Steam DRM wrapper** (direct launch → "Failed to find Steam.", from code
outside the image); product/version not identified. Work on it follows [D-005](DECISIONS.md): legitimate Steam
launch + read-only memory capture, no standalone executable is produced.

### Unpacked runtime structure (EXP-002, Steam launch, read-only capture)

- `CONFIRMED` **OEP VA 0x005A184E** (RVA 0x1A184E): VC6 `WinMainCRTStartup` prologue; the stub stores it at VA 0x6EB903.
- `CONFIRMED` Image base 0x400000, `RELOCS_STRIPPED` ⇒ VA = 0x400000 + RVA in every run; capture offset == RVA.
- `CONFIRMED` `.text` entropy 8.00 (file) → 6.19 (memory); 679 exports present in memory.
- `CONFIRMED` Runtime IAT VA 0x639000, 144 slots: ADVAPI32 4, DINPUT8 1, DSOUND 1, EAX.DLL 1, GDI32 1,
  KERNEL32 103 (incl. `QueryPerformanceCounter/Frequency`, `Sleep`, `LoadLibraryA`, `GetProcAddress`), USER32 33.
  No d3d8/winmm/msvcrt imports (static CRT). Identical copy of the resolved values at VA 0x673A90 (`OPEN`).
- `CONFIRMED` Exported engine globals: `g_pSysMem` 0x6A6C50, `g_pSysCom` 0x6A6C54, `g_pSysFile` 0x6A6C58,
  `g_pSysInterface` 0x6A6C5C, `g_pSysInput` 0x6A6C60, `g_pScriptDll` 0x6A6C74, `g_pScriptIfDll` 0x6A6C78.
- `CONFIRMED` `Resolution` parser VA 0x41700B–0x417062 (width → `[obj+0x64]`, height → `[obj+0x68]`); settings
  writer 0x418307; plug-in DLL loader (`GetProcAddress "Create"`) 0x418156. Details: EXP-002 §B3.

## RenderD3D.dll

Rendering details: [RENDERING.md](RENDERING.md).

`CONFIRMED` (imports/exports):
- Single export: `Create` (ordinal 1, RVA 0x1000) — renderer factory.
- `d3d8.dll`: only `Direct3DCreate8` (IAT VA 0x10042160).
- USER32 includes `EnumDisplaySettingsA`, `AdjustWindowRectEx`, `CreateWindowExA`, `ClipCursor`, `MoveWindow`.
- 28 imports from `hitman2.exe` (full list in `experiments/EXP-001_pe_survey/reports/RenderD3D.dll.json`).
- Has a `.reloc` section (relocatable); `.data` vsize 0x1851A0 vs raw 0x7000 (large BSS).

## config.exe

`CONFIRMED` (strings): fixed resolution list `640x480`, `800x600`, `1024x768`, `1280x1024`; writes
`Resolution %s` and `Window %s` to `\hitman2.ini`; contains an `IDR_INI_FILE` resource with a default ini.
