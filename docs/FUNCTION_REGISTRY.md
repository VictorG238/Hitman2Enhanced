# Function and symbol registry

Single place for named addresses, so names survive between sessions. REA/Ghidra annotations are session-only
(programs are ephemeral), so a name that is not here is lost. One row per address; add detail in the linked document.
Tags: `CONFIRMED` / `INFERRED` / `HYPOTHESIS` / `OPEN` (see [CONTRIBUTING.md](../CONTRIBUTING.md)).

Rules: write the address as **VA** (static, preferred base) or **RVA** / `module+offset`; RenderD3D relocates at
runtime, so runtime logs use `RenderD3D+RVA`. Names ending in `?` are provisional. Rows were seeded from existing
documents only (source column); add new rows with `research-documentation`.

## hitman2.exe — SHA-256 `f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136`

Image base 0x400000, relocations stripped (runtime VA = 0x400000 + RVA). On-disk file is a DRM-wrapped stub; engine
addresses below are from the runtime capture (`main.analysis.img`).

| VA | Name | Description | Tag | Source |
|---|---|---|---|---|
| 0x6EB86C | `DrmStubEntry` | Stub code the packed file's entry point jumps to (`mov eax,0x6EB86C; jmp eax`) | CONFIRMED | EXP-001, EXP-002 |
| 0x6EB903 | `OepSlot` | Stub stores the original entry point here (holds 0x5A184E after a Steam launch) | CONFIRMED | EXP-002 |
| 0x5A184E | `WinMainCRTStartup` | VC6 CRT entry (OEP); first call is `GetVersion` via IAT 0x639104 | CONFIRMED | EXP-002 |
| 0x41700B-0x417062 | `Config::ParseResolution?` | Parses `Resolution WxH`; width to `[obj+0x64]`, height to `[obj+0x68]` | CONFIRMED (parse); name provisional | EXP-002 §B3, RENDERING |
| 0x418156 | `Engine::LoadDrawDll?` | `LoadLibrary(DrawDll)` + `GetProcAddress("Create")`, then `obj->vtbl[0x30]` | CONFIRMED | EXP-002 §B3 |
| 0x418307 | `Config::WriteSettings?` | Settings writer | CONFIRMED (location); purpose INFERRED | EXP-002 |
| 0x415E40 | `FUN_00415e40` | Runtime renderer switch ("This render is not supported. Using old render.") | CONFIRMED (Ghidra string/pseudocode); name keeps Ghidra label | EXP-003 run-003 |
| 0x4161B0 | `FUN_004161b0` | Releases the renderer object | INFERRED | EXP-003 run-003 |
| 0x415091 | (inside a constructor) | Sets default 640x480 at `+0x64/+0x68`; function start not recorded | INFERRED | EXP-003 run-003 |
| 0x4108E0 | `ZSysInputWintel::ZSysInputWintel` | Vtable 0x639C10; stores `this` in `g_pSysInput` | CONFIRMED (static) | RENDERING "Menu cursor" |
| 0x410E10 | `ZSysInputWintel::Update` (vtbl+0x24) | Polls devices, then calls 0x40F7C0; called by the main loop 0x416810 | CONFIRMED (static) | RENDERING "Menu cursor" |
| 0x40F7C0 | `ZSysInput::UpdateCursor?` | Cursor `[this+0x18/+0x1C]` += mouse delta, clamped to 0..W/H (renderer vtbl 0xB0/0xB4) | CONFIRMED (static); name provisional | RENDERING "Menu cursor" |
| 0x40F830 | call site | `call 0x40F9D0` (mouse delta); redirected by `menuscale` while a menu is scaled | CONFIRMED (bytes) | FIXES `menuscale` |
| 0x4014C0 | `ZEngineDataBase::IsPaused` (export) | `mov al,[ecx+0xB9C]`; db = `[g_pSysInterface+0x98]` (main loop 0x4169E4 reads it so); `menuscale` scales paused frames | CONFIRMED (static) | FIXES `menuscale` v5 |

## hitman2.exe retail 1.0.0.265 — SHA-256 `0d7742c85e328886e91152036ae6615ca4c98bbb97c7a36d8f9e150235e57560`

Not packed; file offset == RVA for `.text`/`.rdata`/`.data`. Ported from the Steam build by masked-signature matching,
each read back from the file (`src/fixes/game_build.cpp`, EXECUTABLE_ANALYSIS.md "Retail 1.0.0.265").

| VA | Name | Description | Tag | Source |
|---|---|---|---|---|
| 0x69F054 | `g_pSysInterface` (export) | Same role as Steam 0x6A6C5C | CONFIRMED | EXECUTABLE_ANALYSIS |
| 0x4183C2 | call site | `call _ftol` (0x59DD9C) in the frame timer 0x418250..0x4184F0; redirected by `timer` (call at qword+2) | CONFIRMED (bytes) | FIXES `timer` |
| 0x40FAA0 | call site | `call 0x40FC40` (mouse delta) in the input frame function 0x40FA30; redirected by `menuscale` | CONFIRMED (bytes) | FIXES `menuscale` |
| 0x646928 | `ZWINDOWS` vtable | Setter/getter 0x4BA5B0/0x4BA590 at +0x2E8/+0x2EC | CONFIRMED (static) | EXECUTABLE_ANALYSIS |
| 0x651DD8 | `ZWINDOW_IngameMap` vtable | Constructor 0x53B930 | CONFIRMED (static) | EXECUTABLE_ANALYSIS |
| 0x4015C0 | `ZEngineDataBase::IsPaused` (export) | `mov al,[ecx+0xB9C]`, as Steam | CONFIRMED (static) | FIXES `menuscale` v5 |
| 0x40F9D0 | `ZSysInput::GetMouseDelta?` | thiscall (device index), returns `float*` {dx, dy, dz} or 0 | CONFIRMED (static) | RENDERING "Menu cursor" |
| 0x411680 | `WintelMouse::WintelMouse` | Vtable 0x639CA4 | CONFIRMED (static) | RENDERING "Menu cursor" |
| 0x411810 | `WintelMouse::Update` | DirectInput GetDeviceState(0x14); deltas to `+0x3A..`, `g_pSysInterface+0x7E4/+0x7F0` | CONFIRMED (static) | RENDERING "Menu cursor" |

Globals (exported) and tables:

| VA | Name | Description | Tag | Source |
|---|---|---|---|---|
| 0x6A6C50 / 54 / 58 | `g_pSysMem` / `g_pSysCom` / `g_pSysFile` | Engine system interfaces | CONFIRMED (exports) | EXP-002 |
| 0x6A6C5C | `g_pSysInterface` | Config getter at `vtbl[0x54](key, &val)`; fields `+0x64` width, `+0x68` height | CONFIRMED | EXP-002, EXP-003 |
| 0x6A6C60 | `g_pSysInput` | Input interface (`ZSysInputWintel`, vtable 0x639C10); menu cursor floats at `+0x18/+0x1C` | CONFIRMED (export; fields static) | EXP-002, RENDERING "Menu cursor" |
| 0x6A6C74 / 78 | `g_pScriptDll` / `g_pScriptIfDll` | Script DLL interfaces | CONFIRMED (export) | EXP-002 |
| 0x639000 | `RuntimeIAT` | 144 slots; `QueryPerformanceCounter` 0x6390C0, `QueryPerformanceFrequency` 0x6390C4, `Sleep` 0x639078 | CONFIRMED | EXP-002, TIMING_AND_FPS |

## RenderD3D.dll — SHA-256 `e9aff44931aa6c361fdcc49d1f5969d19777923c8f6c2c2e18e439bfd53c871d`

Preferred base 0x10000000; runtime base varies. Runtime form is `RenderD3D+RVA`.

| VA (RVA) | Name | Description | Tag | Source |
|---|---|---|---|---|
| 0x10001000 (0x1000) | `Create` | Only export; renderer factory | CONFIRMED | EXP-001 |
| 0x10001B7A (0x1B7A) | `RenderD3D::CopyResolution?` | Copies `[*g_pSysInterface+0x64/0x68]` to the renderer | CONFIRMED (static) | EXP-003 §3 |
| 0x10001D8D (0x1D8D) | `RenderD3D::CreateDeviceSite?` | Sequence ending in `call [edx+0x3C]` = `IDirect3D8::CreateDevice`; return address 0x10001DB6 seen at runtime | CONFIRMED | EXP-001, EXP-003 |
| 0x10001750 (0x1750) | `RenderD3D::BuildFrustumMatrix?` | `__thiscall`, 6 float args; writes `2n/(r-l)`, `2n/(t-b)`, `(r+l)/(r-l)`, `(t+b)/(t-b)`, `f/(f-n)`, `-nf/(f-n)` into a 4x4 matrix. Only caller 0x100033B0. Argument order l,r,b,t,n,f is read from the formulas | pseudocode CONFIRMED (Ghidra 12.1.4, 2026-11-10); semantics INFERRED | RENDERING |
| 0x100033B0 (0x33B0) | call to 0x10001750 | Scene projection build | CONFIRMED | RENDERING |
| 0x100033D7 (0x33D7) | `SetTransform(PROJECTION)` call | `call [ecx+0x94]`; runtime caller RenderD3D+0x33DD; all logged scene projections | CONFIRMED | EXP-003 |
| 0x10003322 (0x3322) | `SetViewport` site | Runtime return address RenderD3D+0x3322 | CONFIRMED | EXP-003 |
| 0x1000DBC8 (0xDBC8) | secondary `SetTransform(PROJECTION)` | 128x128 shadow/render-to-texture pass, return RenderD3D+0xDBCE; exclude from FOV fixes | CONFIRMED (source) / INFERRED (purpose) | EXP-003 run-002 |
| 0x1000E63E (0xE63E) | candidate `SetTransform` | No logged calls in run-001/002 | OPEN | RENDERING |
| 0x100109D0 (0x109D0) | `TexManager::LoadTex?` | Walks the .TEX 2048-slot offset table, dispatches per tag (`PALN`/`PALO`/`RGBA`/`DXT1`/`DXT3`) to a reader object and creates each texture through `this->vtbl[0x10]` | CONFIRMED (Ghidra 12.1.4, 2026-11-11) | EXP-005 |
| 0x10011330 (0x11330) | `TexManager::CreateFromReader?` | `CreateTexture` (`call [edx+0x50]` at 0x10011391, MANAGED, format `[this+0x20030]`), decode to w*h*4, per level `LockRect(level,NULL,0)` 0x10011417 / copy / `UnlockRect` 0x1001150C | CONFIRMED (disassembly); role INFERRED | EXP-005 |
| 0x1002AF60 (0x2AF60) | `PalnReader::ctor?` | Sets tag 0x50414C4E (`PALN`), vtable 0x10043810 | CONFIRMED | EXP-005 |

Renderer object layout (INFERRED, RENDERING): `+0x128` HWND, `+0x1366` `IDirect3DDevice8*`, `+0x136A`
`D3DPRESENT_PARAMETERS` (byte-packed class).

## Other binaries

None registered yet. Add a section per binary with its SHA-256 (see
[EXECUTABLE_ANALYSIS.md](EXECUTABLE_ANALYSIS.md#binary-identity)).
