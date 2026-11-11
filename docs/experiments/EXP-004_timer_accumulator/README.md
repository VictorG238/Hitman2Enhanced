# EXP-004 — Frame-rate independent game clock

- **Date**: 2026-11-11 (rev. 2 after the owner's safety review)
- **Status**: design revised; mechanism implemented and verified **in an isolated native harness only**
  (`tests/native/timer_test.cpp`). The fix is not registered in the proxy and cannot be enabled in the game.
  Runtime baseline done: root cause confirmed. Owner approved controlled live testing (2026-11-11); `timer`
  registered in 0.7.0 (off by default). **Live test 1: clock rate ≈ 1.00 at 30, 60 and uncapped (610-850 fps), no
  crash.** Gameplay/behaviour tests pending.
- **Binaries**: hitman2.exe file SHA-256 `f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136`
  (1.0.0.277, Steam DRM-wrapped; image base 0x400000, no relocations); analysis image `main.analysis.img`
  `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a` (read-only capture, D-005).
- **Evidence**: `docs/TIMING_AND_FPS.md` "Frame timer" (REA Evidence IDs, exact bytes).
- **Code**: `src/fixes/timer.{h,cpp}` (accumulator, `_ftol`-compatible conversion, stub), `src/common/code_patch.*`
  (`RedirectCall`); CMake target `h2e_timer` is linked only into `timer_test`.

## Root cause (static, confirmed at runtime - see "Baseline")

Per frame, function 0x4179F0 (`this` in ESI) computes a smoothed frame time `dt` (seconds, clamped to <= 0.1 s,
times the time scale `[this+0x193C]`), stores it as float in `[this+0x2C]`, and adds `_ftol(dt * 1024.0)` to the
integer clock `[this+0x28]` (1/1024 s). `_ftol` truncates: 60 fps -> 17 ticks/frame (99.6 % speed), 144 fps -> 7
(98.4 %), 600 fps -> 1 (58.6 %), **> 1024 fps -> 0 (stopped)**. `HYPOTHESIS`: this causes the reported freezes /
ignored Esc / cutscene desync at ~3000 fps. Other causes are possible and are tested separately ("Not covered").

## Baseline (read-only, 2026-11-11 00:49-00:51, no patch)

Proxy 0.6.0 without fixes at log level 2 (rendering fps, SHA-256 `73202d6444f3821c70fcb287c592d9ae812d17f9730a952fe3019f98dc82e807`),
`tools/watch_values.py` sampling `clock=[[0x6A6C5C]+0x28]` and `dt=[[0x6A6C5C]+0x2C]` every 0.25 s; windowed
1920x1080; the owner capped the frame rate with RivaTuner during one ~87 s session. Files:
[`baseline-summary.txt`](baseline-summary.txt) (2 s windows), [`baseline-render-fps.txt`](baseline-render-fps.txt)
(proxy), [`baseline-header.txt`](baseline-header.txt). Afterwards: proxy removed, 18 binaries == baseline.

| Segment (render fps from proxy / from dt) | Measured clock rate | Predicted `trunc(dt*1024)/(dt*1024)` |
|---|---|---|
| 144 fps, steady (8 windows, 67-83 s) | **0.9815 - 0.9857** | **0.9844 - 0.9848** |
| 30 fps, steady (5 windows, 49-59 s) | 0.9930 - 0.9945 (one 1.0103 sampling outlier) | 0.9960 - 0.9966 |
| 60 fps (35-39 s) | 0.9523 - 1.0037 (short segment) | 0.9967 |
| ~490-590 fps, varying (11-21 s) | 0.64 - 0.85 | 0.64 - 0.96 (dt varies inside windows) |
| 1300-3600 fps (3-11 s) | ~0 (0.035 at 3566 fps), clock repeatedly reset | 0 |

1. `CONFIRMED` (runtime) `[[0x6A6C5C]+0x28]` / `+0x2C` are the frame timer's integer clock and float `dt`; the timer
   object is the one behind `g_pSysInterface`. Before initialisation both read 0.
2. `CONFIRMED` **The integer clock advances at `trunc(dt*1024)` per frame**: at a steady frame rate the measured rate
   matches the prediction within the ±0.5 % sampling error (144 fps: 0.984; 30 fps: 0.994-0.996), and above
   ~1300 fps it does not advance. This is the root cause of the clock's frame-rate dependence (GB-001). Whether it
   also causes each reported symptom is still tested separately ("Not covered").
3. `CONFIRMED` "paused" windows (clock and dt both frozen) occur while the owner was in menus / changing the cap:
   the engine skips the whole update then (pause flag `+0x1FA4` path).
4. `OBSERVED`, cause `OPEN`: between 1300 and 2200 fps the clock went backwards to small values several times
   (engine resets of the timer, vtable 0xEC) - this is during the intro / menus; the accumulator design drops its
   fraction on such resets.
5. Not measured in this run (still `OPEN`): the thread layout of the process and whether RenderD3D's window setup
   runs before or after `Direct3DCreate8` (needed only for moving the `resolution` patch out of `DllMain`).

## Live test 1: `timer` enabled (2026-11-11 ~01:05, owner approval of 2026-11-11)

Proxy 0.7.0 (SHA-256 `a1a11d2867f1632a108d4c5587abc123d64e3cc9e448f5729444857eefbc4183`) with **only `timer`**
enabled (`resolution` and `horplus` off), log level 2; windowed 1920x1080; same read-only sampler as the baseline.
Owner played one ~222 s session: menus, then a mission uncapped, then RivaTuner 60 fps, then 30 fps (144 fps was not
run). Files: [`timer-on-summary.txt`](timer-on-summary.txt) (5 s windows),
[`timer-on-proxy-excerpt.txt`](timer-on-proxy-excerpt.txt). Afterwards: proxy + `h2e.ini` removed, 18 binaries ==
baseline.

1. `CONFIRMED` Verification and patch, from the log: `Direct3DCreate8` called from `RenderD3D.dll+0x19F1`; all checks
   passed; **8 other threads were suspended during the write, 0 of them executing hitman2.exe code, 0 inside the timer
   function**; `call at 0x00417B40 redirected to the accumulator stub` (applied). This also answers the thread-layout
   question for the patch moment: the engine's other threads (system/driver/sound) were outside the engine image.
2. `CONFIRMED` No crash, freeze or error during the session; the game exited normally (detach after 79,209 frames).
3. `CONFIRMED` **Clock rate ≈ 1.00 independent of rendering fps** in steady segments:

   | Segment (proxy fps) | Clock rate with `timer` | Truncating original (predicted from dt) |
   |---|---|---|
   | Mission, uncapped, 610-850 fps (13 windows, 22-82 s) | **0.9997 - 1.0003** | 0.60 - 0.84 |
   | 60 fps (10 windows, 102-153 s) | **0.9974 - 1.0022** | 0.996 |
   | 30 fps (12 windows, 163-218 s) | **0.9962 - 1.0039** (alternating, sampling jitter of the 0.25 s sampler) | 0.996 |

   Transition windows (cap changes, menus) and the intro/menu phase at 1800-2000 fps show engine resets of the clock,
   as in the baseline, and are not steady-state.
4. Not yet covered by this test: a **steady in-mission segment above 1024 fps** (the uncapped mission ran at
   610-850 fps on this machine; the >1000 fps phases were menus with clock resets) and 144 fps. Both should be
   measured before calling the clock fix complete.
5. Gameplay, AI, physics, animation, cutscene and input behaviour: **not yet tested** - the high-fps problem is not
   declared fixed.

## Live test 2: `timer` + `framelimit` 144 fps + `resolution` + `menuscale` + `hdfonts` (2026-11-11 ~16:30)

Proxy build of commit 63241f4 (SHA-256 `3646b748431ca60f766b98f38ce13c36d356c27e03cb49cd70f9d1d68a7f0114`), level 1,
fullscreen 1920x1080, ~5 min session (menus + mission). Log: `samples/local/logs/` (moved on uninstall).

1. `CONFIRMED` patch applied (8 threads suspended, 0 in hitman2.exe code), framelimit 144 fps, clean exit, no errors.
2. Clock rate at 144 fps not measured (no sampler in this run).
3. Owner: NPCs sometimes behave oddly. `OPEN` - not attributed: could be original AI behaviour, the frame rate
   (138 other truncating `_ftol` sites still see 144 fps steps), or `timer`. Needs an A/B on the same save
   (timer on/off, and a low frame cap) before any conclusion.

## NPC A/B test (planned 2026-11-11)

Same save, same mission spot, same actions (~3-5 min each), owner notes what the NPCs do. Read-only clock sampler
in every run (`watch_values.py ... --chain clock=0x6A6C5C>+0x28 --chain dt:f=0x6A6C5C>+0x2C --interval 0.25`).

| Run | Fixes | Cap | Sampler log |
|---|---|---|---|
| A | resolution, menuscale, hdfonts, framelimit, **timer** | 144 (framelimit) | `exp004-npc-A-timer-on.log` |
| B | same **without timer** | 144 (framelimit) | `exp004-npc-B-timer-off.log` |
| C | same as A | **30** (RivaTuner) | `exp004-npc-C-30fps.log` |

Run A result (2026-11-11 16:37-16:43, ~6 min, mission): `CONFIRMED` clock rate **0.9996 - 1.0005 at a steady
144.0 fps** (34 10-s windows; the truncating original would give 0.981 - 0.985), closing the 144 fps gap of live
test 1. Resets only at scene changes. NPC observations: pending from the owner.

Run B (16:45-16:51, second launch; the first crashed on the save load, GB-009): clock rate **0.9839 - 0.9848** at
144.0 fps, matching the truncation prediction (0.9843 - 0.9848) - the engine's original 1.6 % slow clock.

Status: **cancelled by the owner** before run C and before reporting NPC observations; the NPC question stays `OPEN`.

Reading: odd behaviour in A only => timer; in A and B but not C => frame rate; in all three => original AI.

## Approaches compared

| | 1. Frame limiter | 2. In-place instruction patch | 3. Redirect one call to an accumulator |
|---|---|---|---|
| Fixes the cause | No (still 0.4 % slow at 60 fps, 1.6 % at 144) | No: no room for a persistent fraction in the 30 bytes; `_ftol` is shared engine-wide | **Yes**: within 1 tick of the engine's own dt at any fps (harness: 24-5000 fps) |
| hitman2.exe memory changed | none | yes | 4 bytes (the call's rel32) |
| Role | fallback `framelimit` (to be built; no limiter exists in the code today) | rejected | **chosen** `timer` |

## Owner concerns (2026-11-11) and how they are addressed

| # | Concern | Resolution | Status |
|---|---|---|---|
| 1 | No patching from `DllMain` / loader lock | The timer patch is applied from the proxy's exported `Direct3DCreate8`, which RenderD3D calls in normal code (RenderD3D+0x19F1, seen in run-009) on the engine's thread during renderer initialisation - outside the loader lock, with the game's main loop not running. Needs a new framework event `onDirect3DCreate` (added with the implementation). Note: the existing `resolution` patch still runs in `DllMain`; moving it is possible only if RenderD3D's window setup (which snaps the size) runs after `Direct3DCreate8` - **ordering OPEN**, to be checked in the baseline log before changing it. | design resolved; resolution-patch move OPEN |
| 2 | Atomic patch safety | `RedirectCall`: full 30-byte context and call-target prologue verified first; the call at 0x417B40 is 8-byte aligned, so only the rel32 changes inside **one aligned `lock cmpxchg8b`** (another CPU sees the old or the new instruction, never a mix; fails if memory changed); `VirtualProtect` only around those bytes and restored; `FlushInstructionCache`; read-back. Intel SDM 3 §8.1.3 additionally requires the site not to be executing on another thread: guaranteed by the timing of #1 (main loop not running); the thread layout is checked read-only in the baseline. | resolved (harness-verified: refusals write nothing, protection restored, only rel32 changes) |
| 3 | Accumulator lifecycle | `TickAccumulator` is bound to the timer instance (`ESI`) and to the clock value it produced: the fraction is dropped when the instance changes, when the clock was changed by anyone else (engine reset via vtable 0xEC, load, set), or after an exceptional value. Time scale needs no special case (ticks already include it; scale 0 adds 0 and keeps the fraction). The pause flag `+0x1FA4` skips the call entirely. | resolved (harness: reset, scale 0.5, scale 0) |
| 4 | Exceptional values | `FtolLow32` reproduces `fistp qword` with truncation: NaN, +-inf and values outside int64 give the integer indefinite (low dword 0); int64 values beyond int32 give the low dword. Out-of-range or negative ticks bypass the accumulator and return exactly that. Verified by **executing the game's original `_ftol` bytes** in the harness against 23 values x 3 precision modes. | resolved |
| 5 | Dynamic root-cause validation | `tools/watch_values.py` sampling mode (`--interval`, float chains `name:f`) and `--summarize` (clock rate, predicted truncation rate, pause/reset detection). REA's runtime observation tools are not supported on this Windows host (runtime-debugging skill), so the read-only watcher is the tool. Baseline run: measured clock rate matches `trunc(dt*1024)/(dt*1024)` (see "Baseline"). | **resolved: root cause CONFIRMED at runtime** |
| 6 | Full x86 ABI | Stub contract = `_ftol`'s: pops ST(0) (ticks), leaves ST(1) (dt) **bit-exact** (80-bit save/restore), returns EAX; preserves EBX/ESI/EDI/EBP, **XMM0-XMM7** (found by the harness: the C++ part uses SSE registers that `_ftol` never touches), the x87 control word and MXCSR; x87 stack empty during the C++ call; 4-byte stack alignment (unaligned SSE moves only). Clobbers ECX/EDX/EFLAGS: the game overwrites ECX at 0x417B45 (`8B 4E 28`) and EDX at 0x417B50 (`8B 16`) before reading them. D3D8 without `D3DCREATE_FPU_PRESERVE` (BehaviorFlags 0x40) switches the x87 to 24-bit precision (`INFERRED`): tested under 24/53/64-bit precision. | resolved (harness) |
| 7 | Optional, hash-tied, reversible, no disk / DRM changes | `[timer] Enabled=0` by default; file SHA-256 + image base + 30-byte context + `_ftol` prologue; in memory only, after the DRM stub has finished; the DLL is pinned once the call points into it; deleting the DLL or setting `Enabled=0` restores the original game on the next launch. | design resolved |
| 8 | Isolated harness first | `tests/native/timer_test.cpp`: the game's 30-byte sequence and `_ftol` run in the test process (only the 1024.0f address and rel32 relocated) against a fake timer object; unpatched it reproduces the bug (17 ticks/frame at 60 fps, 0 at 3000 fps); patched via the same `RedirectCall` it is within 1 tick of `floor(sum(dt*1024))` at 24-5000 fps with jitter. | resolved |

## Patch (unchanged target, revised mechanism)

- Site hitman2.exe VA **0x417B40** `E8 67 86 18 00` (`call _ftol`); context 0x417B32..0x417B4F must equal
  `D8 8E 3C 19 00 00 D9 05 F8 9B 63 00 D8 C9 E8 67 86 18 00 8B 4E 28 03 C8 89 4E 28 D9 5E 2C`; `_ftol` at 0x5A01AC
  must start `55 8B EC 83 C4 F4 9B D9 7D FE 9B 66 8B 45 FE 80 CC 0C`.
- New rel32 -> `h2e_TimerTickStub` (proxy DLL). Swap: aligned qword `E8 67 86 18 00 8B 4E 28` ->
  `E8 r0 r1 r2 r3 8B 4E 28` with `InterlockedCompareExchange64`.
- Stub and accumulator: `src/fixes/timer.cpp`. Any failed check: log the reason, write nothing, game unchanged.

## Test plan and results

| Test | Where | Result |
|---|---|---|
| `FtolLow32` == executed original `_ftol` (23 values x 24/53/64-bit precision) | harness | PASS |
| Unpatched sequence: 17 ticks/frame at 60 fps, 0 at 3000 fps | harness | PASS (bug reproduced) |
| `RedirectCall` refuses context / target / alignment mismatches, writes nothing; restores protection; only rel32 changes; idempotent | harness | PASS |
| Patched: within 1 tick of `floor(sum(dt*1024))` at 24, 25, 30, 60, 144, 240, 1000, 3000, 5000 fps, +-30 % jitter | harness | PASS |
| Time scale 0.5 (half speed) and 0 (clock and fraction hold); reset drops the fraction | harness | PASS |
| dt stored at +0x2C bit-identical to the unpatched code | harness | PASS |
| ABI: EBX/ESI/EDI/EBP, XMM0-7, x87 CW, MXCSR, empty x87 stack, ST(1) kept, exceptional EAX | harness | PASS |
| **Baseline (read-only)**: clock rate vs wall time at uncapped / 30 / 60 / 144 fps | game, owner run | **DONE: matches the truncation prediction (144 fps 0.984 vs 0.9846; >1300 fps ≈ 0)** |
| Thread layout at the patch moment | game | DONE: 8 other threads, none in hitman2.exe code (live test 1) |
| Resolution-patch ordering vs `Direct3DCreate8` | game | OPEN (only needed to move `resolution` out of `DllMain`) |
| With `timer` on: clock rate 1.00 +- 0.01 at every fps, rendering fps unchanged | game | **PASS** at 30, 60 and uncapped 610-850 fps (live test 1); steady >1024 fps and 144 fps still to measure |
| Behaviour on/off uncapped: cutscene sync, Esc, animation-cancel trick, NPC patrol timing, mission loads (GB-008) | game, owner | after approval |

### Baseline procedure (no patch, read-only)

1. Proxy installed **without fixes**, log level 2 (rendering fps every 5 s): `python -I tools/d3d8_logger_install.py install .. --log-level 2`.
2. Watcher started before the game:
   `python -I tools/watch_values.py samples/local/logs/exp004-baseline.log --chain clock=0x6A6C5C>+0x28 --chain dt:f=0x6A6C5C>+0x2C --interval 0.25 --seconds 900 --wait 600`
3. Owner, in one mission, ~60 s per segment, standing still: uncapped, then RivaTuner caps 30, 60, 144 (note the
   order/times).
4. `python -I tools/watch_values.py --summarize samples/local/logs/exp004-baseline.log`: expected without the fix
   `clock_rate ≈ trunc(1024/fps)·fps/1024` (≈ 0 above 1024 fps), cross-checked with the proxy's fps lines.

## Regression / remaining issue (owner, 2026-11-11)

- With `timer` on, **above ~1024 fps the character's running animation breaks**. Not caused by the patch itself (the
  clock is correct), but the clock was only one of **139** `_ftol(x * 1024)` conversions in the engine
  (TIMING_AND_FPS.md). `HYPOTHESIS`: the animation system advances by its own `trunc(dt*1024)` per frame. Next: the
  `framelimit` fallback (cap the frame rate, e.g. 60) and, separately, identify the animation site.

## Not covered by this fix (tested separately, not assumed)

Very small float `dt` (~0.0003 s at 3000 fps) in physics/animation, logic that runs per frame instead of per time
(input edge detection, Esc), the 0.7/0.3 smoothing lag, the 0.1 s clamp, the black screen after the
animation-cancel trick, and the loading crash GB-008.
