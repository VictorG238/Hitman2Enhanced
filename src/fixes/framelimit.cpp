#include "framelimit.h"

#include <windows.h>

#include "fix.h"

namespace h2e {

int ResolveMaxFps(int configured, int monitorHz) {
    int fps = configured > 0 ? configured : (monitorHz > 1 ? monitorHz : kFallbackFps);
    if (fps < kMinFps) fps = kMinFps;
    if (fps > kMaxFps) fps = kMaxFps;
    return fps;
}

int64_t FramePacer::Advance(int64_t now, int64_t period) {
    if (next == 0 || now - next > period) {
        next = now + period;
        return 0;
    }
    const int64_t wait = next > now ? next - now : 0;
    next += period;
    return wait;
}

namespace {

int g_configured = 0;
FramePacer g_pacer;
int64_t g_period = 0;  // QPC ticks per frame; 0 = not resolved yet
int64_t g_freq = 0;
HANDLE g_timer = nullptr;

int64_t Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

int MonitorRefreshHz(HWND window) {
    MONITORINFOEXA mi{};
    mi.cbSize = sizeof mi;
    HMONITOR mon = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    if (!mon || !GetMonitorInfoA(mon, &mi)) return 0;
    DEVMODEA dm{};
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return 0;
    return static_cast<int>(dm.dmDisplayFrequency);
}

void OnAttach(const FixContext& ctx) {
    g_configured = static_cast<int>(GetPrivateProfileIntA("framelimit", "MaxFps", 0, ctx.iniPath));
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    // High-resolution waitable timer (Windows 10 1803+); without it, Sleep(1) granularity plus spinning.
    g_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/,
                                     TIMER_ALL_ACCESS);
}

// Sleeps most of the wait on the timer, then spins the last ~1 ms for accuracy.
void Wait(int64_t ticks) {
    const int64_t deadline = Now() + ticks;
    const int64_t spin = g_freq / 1000;
    if (ticks > spin) {
        const int64_t sleepTicks = ticks - spin;
        if (g_timer) {
            LARGE_INTEGER due;
            due.QuadPart = -(sleepTicks * 10000000 / g_freq);  // relative, 100 ns units
            if (SetWaitableTimer(g_timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(g_timer, INFINITE);
        } else {
            Sleep(static_cast<DWORD>(sleepTicks * 1000 / g_freq));
        }
    }
    while (Now() < deadline) YieldProcessor();
}

bool g_active = false;

void OnPresent(const FixContext& ctx) {
    g_active = true;
    if (g_configured == kUnlimitedFps) {
        if (g_period != -1) ctx.log("framelimit: no cap (settings: Unlimited)");
        g_period = -1;
        g_pacer = FramePacer{};
        return;
    }
    if (g_period <= 0) {  // resolved on the first frame, when the device window exists, and after SetFrameLimit
        const int hz = MonitorRefreshHz(static_cast<HWND>(ctx.deviceWindow));
        const int fps = ResolveMaxFps(g_configured, hz);
        g_period = g_freq / fps;
        g_pacer = FramePacer{};
        ctx.log("framelimit: %d fps (%s; monitor %d Hz; high-resolution timer %s)", fps,
                g_configured > 0 ? "MaxFps" : "monitor refresh rate", hz, g_timer ? "yes" : "no");
    }
    const int64_t wait = g_pacer.Advance(Now(), g_period);
    if (wait > 0) Wait(wait);
}

}  // namespace

void SetFrameLimit(int maxFps) {
    g_configured = maxFps;
    g_period = 0;  // re-resolved on the next frame
}

bool FrameLimitActive() { return g_active; }

extern const Fix kFrameLimitFix = {"framelimit", "caps the frame rate (default: monitor refresh rate) (GB-001)",
                                   OnAttach, nullptr, nullptr, nullptr, OnPresent};

}  // namespace h2e
