# Knowledge index

Entry point for all project knowledge. Read this first, then only what the task needs.
Status tags used everywhere: `CONFIRMED` / `INFERRED` / `HYPOTHESIS` / `OPEN` (see [CONTRIBUTING.md](../CONTRIBUTING.md)).

## Current state (2026-11-10)

- Phase 1 (rendering / widescreen) — engine and renderer paths located; runtime values captured at intro/menu and
  in gameplay (EXP-003 run-001/002). run-003: **the engine replaces the configured height** (1280x720 -> 1280x1024;
  1920x1080 -> device failure). run-004: the writer is **RenderD3D `FUN_100197b0`** (fullscreen width snapping,
  unset height above 1600). Proxy 0.5.x (D-008): resolution fix confirmed in fullscreen 1080p; matrix-only Hor+ rejected
  (the HUD shares the gameplay projection, run-008) - redesign pending.
- Phase 2 (timing / FPS) — first evidence: QPC time source, uncapped frame rate. Main loop not analyzed yet.

Facts to know first:
1. `hitman2.exe` (`f85765f1…6701`) is wrapped by what appears to be **Steam DRM**. Analyze it only from read-only
   captures of a Steam-launched process; never produce an unpacked executable. → [DECISIONS D-005](DECISIONS.md), [EXP-002](experiments/EXP-002_executable_unpacking/README.md)
2. Engine: OEP 0x5A184E, `Resolution` parser 0x41700B (→ `[obj+0x64]/[obj+0x68]`), config getter
   `g_pSysInterface->vtbl[0x54]`, renderer loader 0x418156. → [EXECUTABLE_ANALYSIS](EXECUTABLE_ANALYSIS.md#unpacked-runtime-structure-exp-002-steam-launch-read-only-capture)
3. Rendering: RenderD3D → `Direct3DCreate8` only; CreateDevice at RVA 0x1DB3, SetTransform(PROJECTION) at RVA 0x33D7;
   RenderD3D is **relocated at runtime** (use RVAs). → [RENDERING](RENDERING.md)
4. Runtime (800×600 ini): fullscreen 800×600, VSync interval 1 requested but 0.5–5k fps; scene projection is **Vert-**
   (gameplay fovX 67.4°); a separate 128×128 shadow/RTT projection pass (RVA 0xDBCE) exists. → [EXP-003](experiments/EXP-003_d3d8_runtime/README.md)
5. Reference baseline: `samples/local/baseline-20261110-repaired`; verify the install against its manifest after
   any runtime test. Never modify the install except via `tools/d3d8_logger_install.py`. → [CONTRIBUTING.md](../CONTRIBUTING.md#game-files)

## Documents

| Document | Contents |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Game module layout (with verified addresses) and our components |
| [ROADMAP.md](ROADMAP.md) | Phases, next experiments, blockers |
| [RESEARCH_LOG.md](RESEARCH_LOG.md) | Chronological log of sessions/findings |
| [EXECUTABLE_ANALYSIS.md](EXECUTABLE_ANALYSIS.md) | Binary identity (hashes), PE facts, DRM wrapper, unpacked runtime structure |
| [RENDERING.md](RENDERING.md) | Renderer architecture, D3D8 layout, config keys, device creation, projection/FOV, fix options |
| [TIMING_AND_FPS.md](TIMING_AND_FPS.md) | Time source and frame-rate observations |
| [FUNCTION_REGISTRY.md](FUNCTION_REGISTRY.md) | One row per named address (function, global, call site) with binary SHA-256 and tag; names survive between sessions |
| [AGENT_TOOLING.md](AGENT_TOOLING.md) | AI agent skills (`.claude/skills/`, `.agents/skills/`), reverse engineering tool-selection policy, REA MCP capabilities |
| [FILE_FORMATS.md](FILE_FORMATS.md) | Game data files: inventory; scene ZIP members (.TEX textures, .PRM font tables, .ZGF/.GMS compression, embedded TrueType font) |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | Environment/tool issues and observed game limitations |
| [DECISIONS.md](DECISIONS.md) | Decision records (D-005 DRM boundary, D-006 d3d8 proxy) |

## Experiments

| ID | Title | Status |
|---|---|---|
| [EXP-001](experiments/EXP-001_pe_survey/README.md) | PE survey of installed binaries + RenderD3D static analysis | done |
| [EXP-002](experiments/EXP-002_executable_unpacking/README.md) | Packed hitman2.exe: stub, Steam DRM layer, OEP, runtime IAT, engine functions | done |
| [EXP-003](experiments/EXP-003_d3d8_runtime/README.md) | D3D8 runtime: device parameters, viewports, projection, frame rate, resolution handling | run-001..009 done |
| [EXP-004](experiments/EXP-004_timer_accumulator/README.md) | Frame-rate independent game clock: root cause (integer clock truncation), patch design, risks, test plan | root cause confirmed (baseline); `timer` fix (0.7.0, off by default) live test 1: clock rate ≈ 1.00 at 30/60/uncapped; gameplay tests pending |
| [EXP-005](experiments/EXP-005_hd_fonts/README.md) | HD UI fonts: TEX/PRM/ZGF formats, texture upload path, 4x font pages re-rendered from the embedded TrueType font, fix `hdfonts` | done; in game: menus/Load/briefing sharp (run 1); subtitles + full mission pending |

## Tools

| Tool | Purpose | Tests |
|---|---|---|
| `tools/pe_inspect.py` | Read-only PE inspector (files, or `--mapped` memory images) | `test_pe_inspect.py`, `test_llvm_oracle.py` |
| `tools/snapshot_game.py` | Hashed working snapshot of the install | `test_snapshot_game.py` |
| `tools/runtime_dump.py` | Read-only capture of a running (Steam-launched) hitman2.exe | `test_runtime_tools.py` |
| `tools/iat_annotate.py` | Names the runtime IAT (sidecar json/csv) + disassembler image | `test_runtime_tools.py` |
| `tools/d3d8_logger_install.py` | Safe install/remove of the logging proxy | `test_d3d8_logger_install.py` |
| `tools/watch_values.py` | Read-only polling of pointer chains in the running game: change log, or sampling mode (`--interval`, float chains `name:f`) + `--summarize` clock rates (EXP-004) | `test_runtime_tools.py` |
| `tools/analyze_d3d8_log.py` | Summarize a proxy log (devices, viewports, projection groups, fps) | `test_analyze_d3d8_log.py` |
| `tools/hdfonts_build.py` | Reads scene ZIPs in memory (TEX, PRM font tables, ZGF fonts) and builds the `hdfonts` pack (4x font pages); `--preview` PNGs (EXP-005) | `test_hdfonts_build.py`, native `hdfonts_smoke` |
| `src/` (CMake): `common/`, `fixes/`, `proxy/` | `d3d8.dll` hosting the fixes and the research logger (D-009); fix catalog [FIXES.md](FIXES.md) | `ctest` → `tests/native/proxy_smoke.cpp`, `fixes_test.cpp`; `tests/test_fix_registry.py` |
| `tools/package_release.py` | Release zip (DLL, default `h2e.ini`, [INSTALL.md](INSTALL.md), [CHANGELOG](../CHANGELOG.md)) | `test_fix_registry.py` |

## AI agent skills

Project skills for AI coding agents: `.claude/skills/` (Claude Code, loaded by description; `CLAUDE.md` imports
`AGENTS.md`) and the same files in `.agents/skills/` for other agents. Usage guide: [AGENT_TOOLING.md](AGENT_TOOLING.md).
Structural test: `tests/test_agent_skills.py`.

| Skill | Use for |
|---|---|
| `reverse-engineering` | Coordinating an investigation: prior research, hash, tool choice, evidence |
| `binary-analysis` | PE structure, imports/exports, strings, VA/RVA/offset |
| `ghidra-analysis` | Targeted decompilation, xrefs, naming through REA/Ghidra |
| `runtime-debugging` | Read-only runtime capture, timing, logging proxy |
| `graphics-research` | Resolution, FOV, projection, viewports, VSync |
| `research-documentation` | Writing findings into `docs/` |
