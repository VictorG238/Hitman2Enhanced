# Roadmap

## Phase 1 — Resolution / widescreen / FOV

| Step | Status |
|---|---|
| Inventory game files, hash binaries, PE metadata | ✅ EXP-001 |
| Identify renderer wiring (RenderD3D ↔ d3d8 ↔ engine) | ✅ EXP-001 + EXP-002 |
| Locate D3D device creation and projection matrix in RenderD3D | ✅ confirmed at runtime (EXP-003) |
| Analyze packed `hitman2.exe` (read-only, Steam launch) | ✅ EXP-002 (D-005) |
| Find where `Resolution WxH` is parsed | ✅ 0x41700B; downstream validation `OPEN` |
| Observe device params / viewport / projection at runtime | ✅ intro/menu (run-001) + gameplay (run-002) |
| EXP-003 run-003: 16:9 `Resolution` test | ✅ height is substituted by the engine (1280->1024, 1920->failure) |
| run-004: find the height writer | ✅ RenderD3D `FUN_100197b0` (fullscreen snapping, GB-003) |
| Choose fix strategy | ✅ D-008 (proxy, per-fix switches) |
| Resolution fix (fullscreen keeps `Resolution`) | ✅ confirmed in a mission (run-007) |
| Hor+ by projection rescaling (0.5.0/0.5.1) | ❌ moves the HUD/map (they share the gameplay projection), run-008 |
| **Hor+ redesign**: trace FOV inputs of helper 0x10001750 and the HUD draw path | ⏭ next research |
| Fix framework + packaging (D-009) | ✅ 0.6.0: `src/common|fixes|proxy`, `h2e.ini`, CHANGELOG, release zip |
| Launcher with per-fix checkboxes; DLL version resource | later |
| Esc in menus (OPEN), map cursor offset (GB-005), HUD scaling (GB-006) | planned |
| Decompile RenderD3D projection helper 0x10001750 and caller; trace FOV source | planned |
| Research existing solutions (dxwrapper, d3d8to9, widescreen fixes) from their sources | planned |
| Choose fix strategy (RENDERING.md options) | after run-002/003 |
| Implement launcher + fix module (C++/CMake, Win32) | later |

## Phase 2 — Timing / high FPS

| Step | Status |
|---|---|
| Locate the frame timer (QPC callers) | ✅ 0x4179F0: integer clock adds `trunc(dt*1024)` per frame |
| EXP-004 design: accumulator via one redirected call + `framelimit` fallback | ✅ design; ⏸ awaiting owner approval |
| Read-only runtime baseline of clock rate vs fps | next (no patch needed) |
| Implement `timer` + `framelimit`, unit/ABI tests, mission tests | after approval |

First evidence (TIMING_AND_FPS.md): QPC time source; 1–5k fps uncapped at menu. Next: QPC callers / main loop in
the analysis image; in-game frame rate (run-002); fixed-rate comparison tests.

## Phase 3 — Level editor research

Not started. Data lives in `Scenes/**/*.{ZIP,WHD,WAV}`; see [FILE_FORMATS.md](FILE_FORMATS.md).

## History

- EXP-002 was originally planned as "dump + rebuild an unpacked executable from a test copy". It was re-scoped when
  the packer turned out to be a Steam DRM layer (D-005): read-only capture of a Steam-launched process, no rebuilt PE.
