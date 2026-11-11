# Timing and FPS

Status: **frame timer located** (2026-11-11): integer game clock truncates every frame (see "Frame timer").

## Known so far

- `CONFIRMED` (EXP-002, runtime IAT of hitman2.exe `f85765f1…6701`): the engine imports
  `KERNEL32!QueryPerformanceCounter` (IAT VA 0x6390C0), `QueryPerformanceFrequency` (0x6390C4) and `Sleep` (0x639078).
  It does **not** import `timeGetTime`, `GetTickCount` or winmm. ⇒ `INFERRED`: QPC is the engine's time source.
- `CONFIRMED` (EXP-003 run-001, intro + main menu): **1194–5282 frames/s** presented (111,713 frames in ~71 s) while
  CreateDevice requested `PresentInterval=0x1`. The frame rate is effectively uncapped on this system.
- `CONFIRMED` (EXP-003 run-002, gameplay): **509–1969 frames/s** (54,123 frames in ~50 s), still uncapped.
- `CONFIRMED` RenderD3D reads a `DisableVSync` key (absent from the current `Hitman2.ini`).
- `OPEN` Why interval 0x1 does not cap the frame rate here (modern D3D8→D3D9 path, fullscreen emulation, other).
- `OPEN` Whether simulation (physics, animation, AI) depends on frame time — not tested yet.

## Frame timer (static, 2026-11-11)

Analysis image `main.analysis.img` SHA-256 `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`
(hitman2.exe `f85765f1…5136`), llvm-objdump. REA/Ghidra does not define functions in this area (KI-003), so the
reading below is from the disassembly; constants read from the image.

- `CONFIRMED` The only QueryPerformanceCounter calls are 4, all in the system-interface object (`this` = the object
  at `[0x6A6C5C]`, same field layout as the `Resolution` parser): 0x417DB1/0x417DC1 init (QPC + QPF; flag `+0x195A` =
  QPC available, else a fallback time source via `vtbl[0x130]`), 0x417CF7 timer reset (stores "now" in `+0x1F90`),
  **0x417A6D per-frame update**.
- `CONFIRMED` Per-frame update (0x417A68-0x417B4D), in order:
  1. `raw = (QPC_now - QPC_prev) / [+0x50]` (QPC frequency as float => seconds); `QPC_prev` kept in `+0x1F90`.
  2. `raw = min(raw, 1.0)` (constant 0x63A008 = 1.0).
  3. Smoothed frame time `[+0x1F98]` (double): initialised to `raw` when 0, then
     `smooth = smooth * 0.7 + raw * 0.3` (0x639FF8 = 0.7, 0x639FF0 = 0.3).
  4. `smooth = min(smooth, 0.1)` (0x639FE8) - below 10 fps the game runs in slow motion.
  5. `dt = smooth * [+0x193C]` (time scale) -> stored as float in **`[+0x2C]`** (frame time in seconds).
  6. **`[+0x28] += _ftol(dt * 1024.0)`** (0x639BF8 = 1024.0; `_ftol` at 0x5A01AC truncates toward zero):
     an integer game clock in 1/1024 s ticks.
- `CONFIRMED` at runtime (EXP-004 baseline, 2026-11-11): measured clock rate 0.984 at 144 fps (predicted 0.9846),
  0.994 at 30 fps (0.996), ≈ 0 above ~1300 fps. The `INFERRED` text below is kept for history.
- `INFERRED` **Root cause of the high-fps problems (GB-001)**: the integer clock loses the fractional tick every
  frame. Ticks per frame = trunc(1024 / fps): 60 fps -> 17 (99.6 % speed), 25 fps -> 40 (97.7 %), 600 fps -> 1 (59 %),
  **above 1024 fps -> 0: the integer game clock stops**. Owner reports at ~3000 fps: everything freezes, Esc not
  handled, cutscenes out of sync, black screen after the animation-cancel trick; better when capped at 30 fps
  externally. Systems that use the float `dt` (`+0x2C`) keep running, those that use the integer clock (`+0x28`) stall
  or drift - which would explain the desync. To be confirmed at runtime (watch `[[0x6A6C5C]+0x28]` and `+0x2C` at
  different frame rates).
- `CONFIRMED` (REA/Ghidra 12.1.4 on the same image, 2026-11-11; Ghidra defines no function here, KI-003, so the
  evidence is instruction-level): `read_bytes` 0x417AC6 (139 bytes, `ev_9b8841c4…`) is byte-identical to the
  llvm-objdump listing; `inspect_native_instruction` 0x417B38 = `FLD float ptr [0x00639bf8]` (`ev_f81a1da2…`),
  0x417B40 = `CALL 0x005a01ac`, bytes `E8 67 86 18 00` (`ev_f80bfc69…`); `read_bytes` 0x5A01AC (`ev_304878c1…`) =
  `_ftol` with `or ah,0Ch` (rounding control = truncate) before `fistp qword`.
- Function: **0x4179F0** (`thiscall`, `esi = this`, prologue `sub esp,10h`), the per-frame timer update. It is entry
  **0xE4** of the vtable at 0x639E68 (`INFERRED` table start). 0x4179E0 sets `+0x1FA4 = 1` (skips the dt update,
  likely "pause timer"). Entry 0xEC (0x417C60) resets the timer and zeroes `+0x04..+0x44` (incl. `+0x28`) and
  `+0x1F98`. Calls are virtual; the call sites and the consumers of `+0x28`/`+0x2C` are not resolved statically
  (`OPEN`, to be measured at runtime - EXP-004).
- Exact bytes of the tail (0x417B32-0x417B4F, 30 bytes):
  `D8 8E 3C 19 00 00` fmul [esi+193Ch] · `D9 05 F8 9B 63 00` fld 1024.0 · `D8 C9` fmul st,st(1) ·
  `E8 67 86 18 00` call _ftol · `8B 4E 28` mov ecx,[esi+28h] · `03 C8` add ecx,eax · `89 4E 28` mov [esi+28h],ecx ·
  `D9 5E 2C` fstp [esi+2Ch].
- `CONFIRMED` (static, 2026-11-11) The 1/1024 s unit is engine-wide: of 649 calls to `_ftol`, **139** convert a value
  multiplied by 1024.0 (e.g. 0x4103B6, 0x4115EB, 0x416735, 0x416748, 0x417A3F, 0x417B40 = the clock, 0x420B45,
  0x42271B, 0x42C033, 0x42CB8B, 0x435820, 0x435856, ...); 7 more use 25.0, 7 use 100.0. Each one that converts a
  per-frame `dt` truncates like the clock did. `OBSERVED` (owner, 2026-11-11, `timer` on): above ~1024 fps the
  character's running animation breaks - consistent with another per-frame `trunc(dt*1024)` (`HYPOTHESIS`, site not
  identified). `_ftol` itself cannot carry fractions (it serves positions, colours, etc.), so the remaining sites need
  either individual fixes or a frame cap. At 60 fps `dt*1024` = 17.07 (0.4 % truncation loss), the conditions the game
  was built for.
- Fix direction (frame-rate independent, no limiter; design in EXP-004): keep the fractional remainder between frames
  (`acc += dt * 1024; n = floor(acc); acc -= n; clock += n`). Then the integer clock advances at the correct average
  rate at any frame rate. This requires changing the instructions at 0x417B38-0x417B4D in hitman2.exe's memory.

## Plan

1. Find callers of IAT 0x6390C0/0x6390C4 in the analysis image (`main.analysis.img`) to locate the delta-time code
   and the main loop.
2. Design comparative runtime tests at fixed frame rates (e.g. 30/60/120/240 via an external limiter) on reproducible
   scenarios; compare physics/animation/AI behavior.
3. Only then decide whether decoupling simulation from rendering is justified.

A frame limiter is **not** a fix of the simulation; document it separately if used as a stopgap.
