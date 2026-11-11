---
name: graphics-research
description: "Glacier 1 rendering investigation for Hitman 2: resolution and refresh settings, widescreen or aspect-ratio bugs, field of view, projection matrices, Direct3D 8 device creation, viewports and render targets, letterbox and HUD or interface scaling, present interval and VSync. Use when a task is about how the game renders or how to correct its camera, reusing the existing d3d8 logging proxy, log analyzer and EXP-003 data."
---

# Glacier 1 graphics research

## Read first (do not re-derive)

1. `docs/RENDERING.md` (current conclusions table, D3D8 layout, projection section, fix options A/B/C).
2. `docs/experiments/EXP-003_d3d8_runtime/README.md` and its `run-*-summary.json`.
3. `docs/KNOWN_ISSUES.md` (GB-001 uncapped fps, GB-002 Vert- projection, KI-009 relocation).

Binary for static addresses: `RenderD3D.dll` SHA-256 `e9aff44931aa6c361fdcc49d1f5969d19777923c8f6c2c2e18e439bfd53c871d`.
Write addresses as RVA or `RenderD3D+0x…`; the DLL relocates at runtime.

## Established facts to build on (verify the tag in the docs before relying on them)

- Width and height come from `Resolution WxH` (engine parser 0x41700B); in fullscreen RenderD3D `FUN_100197b0`
  snaps them to 4:3/5:4 modes (GB-003, run-004); the proxy's resolution fix (D-008) removes that.
- The HUD/menus use the scene call site +0x33DD too (45 deg camera, zf 3000): never adjust every matrix from it. CreateDevice is at RenderD3D+0x1DB6, with no mode enumeration. Re-read `docs/RENDERING.md` for the
  current tag before using this.
- Scene projection is built by helper 0x10001750 (VA, static) and set at RenderD3D+0x33DD; it is **Vert-**:
  fixed horizontal FOV, vertical FOV from viewport aspect.
- Not every projection is the player camera. Distinguish: gameplay camera, cutscene/letterbox (800x450 style
  viewports), shadow/render-to-texture pass (second source RenderD3D+0xDBCE, 128x128, tiny off-center FOV), and
  2D overlays (640x480 and other sub-viewports). A correction applied to all matrices breaks shadows and HUD.

## Workflow

1. State the quantity to determine (for example "gameplay fovX at 1280x1024").
2. Reuse existing logs/summaries; only run a new capture when the question is not covered.
3. Static side: `ghidra-analysis` on the RenderD3D call site or caller chain (helper arguments, FOV source).
4. Runtime side: install the proxy with `tools/d3d8_logger_install.py`, ask the user to perform the scenario,
   uninstall, verify the baseline (`runtime-debugging` rules), then
   `python -I tools/analyze_d3d8_log.py <log> --json <summary.json>`.
5. Compute from the matrix: fovX = 2*atan(1/_11), fovY = 2*atan(1/_22), aspect = _22/_11. Compare with the
   viewport that was active for that matrix. Show the numbers next to the observed values; a calculation that is
   not compared with a runtime value is `INFERRED`.
6. `Hitman2.ini`: the owner allowed agents (2026-11-10) to edit `Hitman2.ini` for experiments: back it up first to `samples/local/ini-backups/`, change only the keys the test needs, keep the owner's keys (e.g. `DefaultScene=AllLevels/levelmenu.gms`) and the CRLF line endings, and report every change.

## Timing and presentation

VSync: interval 1 was requested but fps was uncapped (GB-001, cause `OPEN`). For FPS-dependent behavior hand the
timing side to `reverse-engineering` + `runtime-debugging` (QPC xrefs, main loop) and keep this skill to the
presentation parameters (PresentInterval, refresh rate, `DisableVSync` key).

## Fix design

Choose among the options in `docs/RENDERING.md` only with evidence; any fix must classify the matrix (gameplay
vs shadow vs cutscene vs overlay) before changing it, and must be hash-pinned to the known RenderD3D build.
Record the decision in `docs/DECISIONS.md` via `research-documentation`.
