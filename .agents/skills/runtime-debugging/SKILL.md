---
name: runtime-debugging
description: "Read-only runtime observation of a Steam-launched Hitman 2 process. Use when inspecting live process memory or engine state, listing loaded or dynamically loaded modules and their relocated bases, watching a pointer chain or value over time, studying QueryPerformanceCounter and frame-timing behavior, installing and removing the d3d8 logging proxy, or confirming a static finding with runtime evidence. Preserves the Steam DRM boundary."
---

# Runtime debugging (read-only, Steam-launched)

## Hard boundaries (AGENTS.md, D-005, D-006)

- The game is started **only through Steam** (`steam://rungameid/6850` or the user). Never launch
  `hitman2.exe` directly (it exits with "Failed to find Steam.", KI-007), never bypass or strip DRM, never
  write an unpacked or rebuilt executable.
- Tools only **attach read-only** (`PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ`). No writes to the
  process, no injected code, no breakpoints that alter execution.
- The game folder is changed only by `tools/d3d8_logger_install.py`. After every proxy test run `uninstall`,
  then verify the install against `samples/local/baseline-*/MANIFEST.sha256` (command in AGENTS.md section 3).
  `Hitman2.ini`: the owner allowed agents (2026-11-10) to edit `Hitman2.ini` for experiments: back it up first to `samples/local/ini-backups/`, change only the keys the test needs, keep the owner's keys (e.g. `DefaultScene=AllLevels/levelmenu.gms`) and the CRLF line endings, and report every change.
- Captures, dumps and full logs stay in `samples/local/` (git-ignored). Commit only small excerpts/summaries.

## Which tool

| Question | Tool |
|---|---|
| Loaded modules, bases, memory image of exe/RenderD3D | `python -I tools/runtime_dump.py samples/local/dumps/<name> --marker RenderD3D.dll --wait 240`, then `tools/iat_annotate.py <dir> --analysis-image` |
| Is a value changing, and what is it over time | `tools/watch_values.py --chain name=0xBASE>+off>+off` (read-only pointer chain; present in the working tree) |
| Direct3D 8 calls, device params, viewports, projections, fps | d3d8 logging proxy + `tools/analyze_d3d8_log.py <log>` (see `graphics-research`) |
| Do a static address and live data agree | read it from the capture, compare with `read_bytes` on the same hash in REA |
| Call tracing, breakpoints | **not available**: REA `observe_native_calls` / `capture_process_scenario` are unsupported on this Windows host and no debugger is installed. Report this and propose a log-based or read-only alternative; do not improvise injection |

## Recording addresses correctly

- Module base differs per run for RenderD3D (KI-009): store `module+RVA`, plus the observed base as context.
- hitman2.exe has no relocations: runtime VA = 0x400000 + RVA, capture offset == RVA.
- Every capture: process image hash (the tools refuse unknown hashes), date, scenario, tool version. Quote them.
- Different frame rates make runtime values vary; report ranges and the sample size, not a single number.

## Interactive tests

If a test needs gameplay or menu actions, stop and tell the user the exact steps and what the tool is waiting
for (for example "Start the game from Steam, load the first mission, stand still for 30 s, then press Esc"). Start
the passive capture tool first so it can wait for the process. Never claim runtime confirmation without a capture.

## Timing and FPS work

Known: QPC is the only high-resolution time import; fps is uncapped (see `docs/TIMING_AND_FPS.md`). Static side:
find xrefs to IAT 0x6390C0 / 0x6390C4 in `main.analysis.img` with `ghidra-analysis`. Runtime side: compare behavior
at controlled frame rates. A frame limiter is a stopgap, not a simulation fix.

## Finish

Uninstall any proxy, verify baseline, then use `research-documentation` to record the run (EXP README, hashes,
log excerpt, tags).
