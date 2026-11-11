// Fix "framelimit" (GB-001, EXP-004). Caps the frame rate by pacing Present.
//
// The engine converts per-frame time to its 1/1024 s unit with truncation at many sites (139 `_ftol(x*1024)` calls,
// TIMING_AND_FPS.md). The `timer` fix makes the main clock exact; the other conversions still lose up to one unit per
// frame and stop above 1024 fps (owner: running animation breaks). Capping the frame rate keeps them in the range the
// game was built for. Default cap: the refresh rate of the monitor showing the game ([framelimit] MaxFps=0); a fixed
// value can be set instead (e.g. MaxFps=60).
#pragma once
#include <cstdint>

namespace h2e {

constexpr int kFallbackFps = 60;
constexpr int kMinFps = 15;
constexpr int kMaxFps = 1000;

// Frame cap from the configuration and the monitor: configured > 0 wins (clamped to [kMinFps, kMaxFps]); otherwise
// the refresh rate (Windows reports 0 or 1 for "default hardware rate" - then kFallbackFps).
int ResolveMaxFps(int configured, int monitorHz);

// Cap value meaning "no cap" (settings page "Unlimited").
constexpr int kUnlimitedFps = -1;

// Live change of the cap (settings page): 0 = monitor refresh rate, kUnlimitedFps = no cap, otherwise a fixed value.
// Takes effect on the next frame; only has an effect while the framelimit fix is enabled.
void SetFrameLimit(int maxFps);
bool FrameLimitActive();  // the framelimit fix has run at least one frame (it is enabled)

// Deadline bookkeeping in clock ticks: each frame is due one period after the previous deadline; when the game is
// more than one period late the schedule restarts from `now`, so a slow frame is not followed by a burst.
struct FramePacer {
    int64_t next = 0;  // 0 = not started
    // Ticks to wait before continuing (0 = none); advances the deadline.
    int64_t Advance(int64_t now, int64_t period);
};

}  // namespace h2e
