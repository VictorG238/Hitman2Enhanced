# Architecture

## Game (as observed — evidence and tags in EXECUTABLE_ANALYSIS.md, RENDERING.md, EXP-002/003)

```
Steam ──> hitman2.exe  [Steam DRM wrapper stub @0x6EB86C → OEP 0x5A184E]     CONFIRMED / INFERRED (DRM)
            │  static VC6 CRT; IAT @0x639000: ADVAPI32 DINPUT8 DSOUND EAX GDI32 KERNEL32 USER32
            │  exports 679 engine symbols (g_pSysInterface @0x6A6C5C, ZEngineDataBase, …)
            ├─ reads Hitman2.ini via g_pSysInterface->vtbl[0x54](key,&val)            CONFIRMED
            │     Resolution "WxH" → parser 0x41700B → [obj+0x64]/[obj+0x68]            CONFIRMED
            ├─ plug-in loader 0x418156: LoadLibrary(DrawDll) → GetProcAddress("Create")  CONFIRMED
            │     └─ RenderD3D.dll (relocated at runtime, e.g. base 0x07950000)
            │           ├─ imports 28 engine symbols from hitman2.exe                  CONFIRMED
            │           ├─ reads RefreshRate/GammaValue/DisableVSync/Antialias/Anisotropy CONFIRMED
            │           └─ d3d8!Direct3DCreate8(120) → IDirect3D8::CreateDevice (RVA 0x1DB3)
            │                 → IDirect3DDevice8: SetViewport (returns to RVA 0x3322), SetTransform(PROJ) (RVA 0x33D7)
            ├─ RenderOpenGL.dll (alternative DrawDll)                                   CONFIRMED
            ├─ EAX.DLL (loaded at 0x10000000), DSOUND, DINPUT8                         CONFIRMED
            ├─ ScriptDll / ScriptIfDll keys read (SDL_Engine.dll etc. not on disk)      CONFIRMED read / OPEN handling
            └─ Scenes/**/*.{ZIP,WHD,WAV}                                                OPEN (format)
config.exe → writes Resolution/Window to hitman2.ini (fixed list 640x480…1280x1024)       CONFIRMED
```

## Project components

```
tools/   Python research tools (stdlib only)
  pe_inspect.py          PE inspector (files and --mapped memory images)
  snapshot_game.py       hashed snapshots of the install
  runtime_dump.py        read-only capture of a Steam-launched hitman2.exe (attach only)
  iat_annotate.py        names the runtime IAT (sidecar iat.json/csv) + disassembler image
  d3d8_logger_install.py developer install/remove of the proxy + h2e.ini in the game folder (hash-checked)
  package_release.py     builds dist/Hitman2Enhanced-<version>.zip (D-009)
src/                     C++ (CMake, Win32), layout D-009
  version.h              single version (semantic, see CHANGELOG.md)
  common/                d3d8_min.h (verified D3D8 layouts), sha256, code_patch (hash+byte verified patches)
  fixes/                 fix.h (Fix record), registry.cpp, one module per fix (resolution, horplus)
  proxy/                 d3d8_proxy.cpp + d3d8.def -> d3d8.dll: reads h2e.ini, runs fixes, research logger
tests/                   unittest suites (+ tests/native: proxy_smoke, fixes_test run by ctest)
docs/                    knowledge base (INDEX.md is the entry point)
samples/local/           git-ignored: snapshots, game copy, captures, logs
```
