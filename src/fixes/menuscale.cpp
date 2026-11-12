#include "menuscale.h"

#include <windows.h>

#include <cmath>

#include <cstring>
#include <initializer_list>

#include "../common/code_patch.h"  // ModuleFileSha256
#include "exe_redirect.h"
#include "fix.h"
#include "game_build.h"
#include "zwindows.h"

namespace h2e {

namespace {

bool CentredPerspective(const d3d8::Matrix& m, double* zf) {
    const float(*a)[4] = m.m;
    if (a[2][3] != 1.0f || a[3][3] != 0.0f || a[0][0] <= 0.0f || a[1][1] <= 0.0f || a[2][0] != 0.0f ||
        a[2][1] != 0.0f || a[2][2] == 1.0f || a[2][2] == 0.0f)
        return false;
    const double zn = -double(a[3][2]) / a[2][2];
    *zf = double(a[2][2]) * zn / (double(a[2][2]) - 1.0);
    return true;
}

}  // namespace

double MenuScaleFactor(unsigned bbWidth, unsigned bbHeight, double configured) {
    const double fit = std::fmin(double(bbWidth) / kCanvasWidth, double(bbHeight) / kCanvasHeight);
    return configured > 0.0 ? std::fmin(configured, fit) : fit;
}

bool IsUiCamera(const d3d8::Matrix& m, const d3d8::Viewport& vp, unsigned bbWidth, unsigned bbHeight) {
    double zf = 0;
    return vp.X == 0 && vp.Y == 0 && vp.Width == bbWidth && vp.Height == bbHeight && CentredPerspective(m, &zf) &&
           zf >= kMinUiFarPlane && zf <= kMaxUiFarPlane;
}

bool IsSceneCamera(const d3d8::Matrix& m) {
    double zf = 0;
    return CentredPerspective(m, &zf) && zf > kMaxUiFarPlane;
}

void ScaleProjection(d3d8::Matrix& m, double scale) {
    m.m[0][0] = float(m.m[0][0] * scale);
    m.m[1][1] = float(m.m[1][1] * scale);
}

bool ScaleCanvasViewport(d3d8::Viewport& vp, unsigned bbWidth, unsigned bbHeight, double scale) {
    if (scale <= 1.0 || bbWidth < kCanvasWidth || bbHeight < kCanvasHeight) return false;
    const unsigned px = (bbWidth - kCanvasWidth) / 2, py = (bbHeight - kCanvasHeight) / 2;
    if (vp.Width == 0 || vp.Height == 0 || (vp.Width == bbWidth && vp.Height == bbHeight) || vp.X < px ||
        vp.Y < py || vp.X + vp.Width > px + kCanvasWidth || vp.Y + vp.Height > py + kCanvasHeight)
        return false;
    const double cx = bbWidth / 2.0, cy = bbHeight / 2.0;
    long x0 = std::lround(cx + (vp.X - cx) * scale), x1 = std::lround(cx + (double(vp.X) + vp.Width - cx) * scale);
    long y0 = std::lround(cy + (vp.Y - cy) * scale), y1 = std::lround(cy + (double(vp.Y) + vp.Height - cy) * scale);
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > long(bbWidth) ? long(bbWidth) : x1;
    y1 = y1 > long(bbHeight) ? long(bbHeight) : y1;
    if (x1 <= x0 || y1 <= y0) return false;
    vp.X = DWORD(x0);
    vp.Y = DWORD(y0);
    vp.Width = DWORD(x1 - x0);
    vp.Height = DWORD(y1 - y0);
    return true;
}

CursorBounds MenuCursorBounds(unsigned bbWidth, unsigned bbHeight, double scale) {
    const double w = bbWidth, h = bbHeight;
    if (!(scale > 1.0)) return CursorBounds{0.0f, 0.0f, float(w), float(h)};
    const double hw = w / (2.0 * scale), hh = h / (2.0 * scale);
    return CursorBounds{float(std::fmax(0.0, w / 2 - hw)), float(std::fmax(0.0, h / 2 - hh)),
                        float(std::fmin(w, w / 2 + hw)), float(std::fmin(h, h / 2 + hh))};
}

void MenuCursorDelta(float x, float y, const float delta[2], double scale, const CursorBounds& b, float out[2]) {
    const double s = scale > 1.0 ? scale : 1.0;
    const double tx = std::fmin(std::fmax(double(x) + delta[0] / s, double(b.x0)), double(b.x1));
    const double ty = std::fmin(std::fmax(double(y) + delta[1] / s, double(b.y0)), double(b.y1));
    out[0] = float(tx - x);
    out[1] = float(ty - y);
}

float ZWindowsRawTarget(float raw, float last, float pos, float k, double scale, float lo, float hi) {
    if (!(std::isfinite(k) && k != 0.0f)) return raw;
    const double s = scale > 1.0 ? scale : 1.0;
    const double step = (double(raw) - last) * k / s;
    const double target = std::fmin(std::fmax(double(pos) + step, double(lo)), double(hi));
    return float(double(last) + (target - pos) / k);
}

bool ProbeSaysMenu(int listCount, bool mapOpen, bool noSceneCamera, bool paused) {
    if (listCount < 0 || mapOpen) return false;
    return listCount > 0 || noSceneCamera || paused;
}

namespace {

// Cursor state shared with the hook. The engine runs the input update (0x410E10) and the frame that ends in
// Present on one thread (main loop 0x416810: g_pSysInput->vtbl[0x24], then ZEngineDataBase::MainLoop) - INFERRED -
// so plain globals are enough. g_cursorScale <= 1: the hook passes the engine's delta through unchanged.
double g_cursorScale = 0.0;
CursorBounds g_cursorBounds{};
float g_cursorOut[3] = {};
uint32_t g_cursorCalls = 0, g_cursorAdjusted = 0, g_zwAdjusted = 0, g_zwSkipped = 0;
uintptr_t g_cursorWindows = 0;  // ZWINDOWS instance from the last Present (0: none known)
// Build-specific addresses (game_build.h), set in OnAttach / OnDirect3DCreate before the hook can run.
uintptr_t g_mouseDeltaVa = kMouseDeltaVa, g_sysInterfacePtr = kSysInterfacePtr;
uint32_t g_ingameMapVtable = kIngameMapVtable;

bool ReadF32(uintptr_t addr, float* value) {
    uint32_t bits = 0;
    if (!ReadU32(addr, &bits)) return false;
    std::memcpy(value, &bits, sizeof *value);
    return true;
}

// The ZWINDOWS cursor (main menu, briefing): rewrites the raw accumulator so that this frame's ZWINDOWS step is
// divided by the scale and ends inside the bounds. Reads go through ReadU32 (a freed instance fails instead of
// faulting); the only write is to g_pSysInterface, a static engine object.
void AdjustZWindowsCursor() {
    uint32_t sys = 0, flag = 0;
    const uintptr_t zw = g_cursorWindows;
    if (!zw || !IsZWindows(zw) || !ReadU32(g_sysInterfacePtr, &sys) || !sys ||
        !ReadU32(sys + kDirectInputMouseFlag, &flag) || (flag & 0xFF) == 0) {
        ++g_zwSkipped;
        return;
    }
    // A new instance's `last` may not match raw yet (scene start): leave its first step to the engine.
    static uintptr_t s_seen = 0;
    if (zw != s_seen) {
        s_seen = zw;
        ++g_zwSkipped;
        return;
    }
    float raw[2], last[2], pos[2], k = 0;
    if (!ReadF32(sys + kRawCursor, &raw[0]) || !ReadF32(sys + kRawCursor + 4, &raw[1]) ||
        !ReadF32(zw + kZwLastRaw, &last[0]) || !ReadF32(zw + kZwLastRaw + 4, &last[1]) ||
        !ReadF32(zw + kZwCursor, &pos[0]) || !ReadF32(zw + kZwCursor + 4, &pos[1]) ||
        !ReadF32(zw + kZwCursorSpeed, &k)) {
        ++g_zwSkipped;
        return;
    }
    for (float v : {raw[0], raw[1], last[0], last[1], pos[0], pos[1], k})
        if (!std::isfinite(v)) {
            ++g_zwSkipped;
            return;
        }
    const CursorBounds& b = g_cursorBounds;
    const float out[2] = {ZWindowsRawTarget(raw[0], last[0], pos[0], k, g_cursorScale, b.x0, b.x1),
                          ZWindowsRawTarget(raw[1], last[1], pos[1], k, g_cursorScale, b.y0, b.y1)};
    std::memcpy(reinterpret_cast<void*>(sys + kRawCursor), out, sizeof out);
    ++g_zwAdjusted;
}
bool g_cursorHooked = false;
unsigned g_bbWidth = 0, g_bbHeight = 0;

}  // namespace

#if defined(_M_IX86)

using MouseDeltaFn = const float*(__fastcall*)(void* self, void* edx, int device);  // = thiscall, ret 4

extern "C" const float* __cdecl h2e_MenuCursorDelta(uint8_t* self, int device) {
    const float* d = reinterpret_cast<MouseDeltaFn>(g_mouseDeltaVa)(self, nullptr, device);
    ++g_cursorCalls;
    if (device != 0 || !(g_cursorScale > 1.0)) return d;
    AdjustZWindowsCursor();
    if (!d) return d;
    float x = 0, y = 0;
    std::memcpy(&x, self + kCursorX, sizeof x);
    std::memcpy(&y, self + kCursorY, sizeof y);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(d[0]) || !std::isfinite(d[1])) return d;
    MenuCursorDelta(x, y, d, g_cursorScale, g_cursorBounds, g_cursorOut);
    g_cursorOut[2] = d[2];  // wheel, unchanged
    ++g_cursorAdjusted;
    return g_cursorOut;
}

// Replaces `call 0x40F9D0` at 0x40F830. Entry: ECX = g_pSysInput, [esp+4] = device index; returns EAX = float*,
// pops the argument (thiscall). EBX/ESI/EDI/EBP are preserved by the C++ callee; XMM0-XMM7 are saved and restored
// as in the timer stub (the original function does not touch them).
extern "C" __declspec(naked) void h2e_MenuCursorStub() {
    __asm {
        sub    esp, 128
        movups [esp], xmm0
        movups [esp+16], xmm1
        movups [esp+32], xmm2
        movups [esp+48], xmm3
        movups [esp+64], xmm4
        movups [esp+80], xmm5
        movups [esp+96], xmm6
        movups [esp+112], xmm7
        push   dword ptr [esp+132]    ; device index
        push   ecx                    ; g_pSysInput
        call   h2e_MenuCursorDelta    ; cdecl, EAX = float*
        add    esp, 8
        movups xmm0, [esp]
        movups xmm1, [esp+16]
        movups xmm2, [esp+32]
        movups xmm3, [esp+48]
        movups xmm4, [esp+64]
        movups xmm5, [esp+80]
        movups xmm6, [esp+96]
        movups xmm7, [esp+112]
        add    esp, 128
        ret    4
    }
}

#endif

namespace {

MenuDetector g_detector;
double g_configured = 0.0;

// Per-frame classification, logged on change (level 1, capped): which cameras the frame used and whether it was
// scaled. Used to see which screens (load screen, pause, map) are treated as menus.
struct FrameKind {
    bool scene = false, ui = false, scaled = false;
    int list = -1;  // open ZWINDOWS windows (-1: no instance known)
    bool operator!=(const FrameKind& o) const {
        return scene != o.scene || ui != o.ui || scaled != o.scaled || list != o.list;
    }
};
FrameKind g_frame, g_lastLogged;
uint32_t g_frameNo = 0, g_kindSince = 0, g_kindLines = 0;
DWORD g_kindSinceTick = 0;
bool g_anyLogged = false;
constexpr uint32_t kMaxKindLines = 400;
// Canvas viewports scaled in the current span, and how many of them while the scaled UI projection was still the
// active projection (content enlarged twice - suspected cause of the over-zoomed load screen).
uint32_t g_spanCanvas = 0, g_spanDouble = 0;
bool g_projScaled = false;
// UI camera parameters seen in the span (before scaling): _11 and zf ranges, number of UI-camera projections.
struct UiCamStats {
    float min11 = 1e30f, max11 = 0.0f;
    double minZf = 1e30, maxZf = 0.0;
    uint32_t calls = 0;
    void Add(const d3d8::Matrix& m) {
        const float a = m.m[0][0];
        min11 = a < min11 ? a : min11;
        max11 = a > max11 ? a : max11;
        const double zn = -double(m.m[3][2]) / m.m[2][2], zf = double(m.m[2][2]) * zn / (double(m.m[2][2]) - 1.0);
        minZf = zf < minZf ? zf : minZf;
        maxZf = zf > maxZf ? zf : maxZf;
        ++calls;
    }
};
UiCamStats g_spanUi;

// Read-only ZWINDOWS probe (zwindows.h), diagnostic: logs the open-window list on change.
bool g_probeOk = false;
uintptr_t g_windows = 0, g_lastWindows = 0;
uint32_t g_probeLines = 0, g_missingSince = 0;
constexpr uint32_t kScanEveryFrames = 288;  // ~2 s at 144 fps while the instance is unknown or gone
constexpr uint32_t kMaxProbeLines = 1500;
constexpr size_t kScanBudget = 8u << 20;  // bytes per frame for the incremental ZWINDOWS search
IncrementalFinder g_finder;
WindowList g_lastList;
bool g_haveList = false;

// Open windows in the current ZWINDOWS list, or -1 while no ZWINDOWS instance is known (logos, load screens, the
// first ~2 s of a scene). Updated once per frame in OnPresent, used by the next frame.
int g_listCount = -1;

// The ZWINDOW_IngameMap object of the current scene (missions only) and whether it is open
// ([map+0x20] & 0x10, zwindows.h). The in-game map already adapts to the screen, so it must not be scaled.
uintptr_t g_map = 0, g_mapOwner = 0;
uint32_t g_mapNextScan = 0, g_mapTries = 0, g_mapLines = 0;
constexpr uint32_t kMapScanTries = 3;
constexpr uint32_t kMaxMapLines = 200;
bool g_mapOpen = false;
bool g_paused = false;  // ZEngineDataBase::IsPaused, read once per frame with the probe
uint32_t g_pauseLines = 0;

void ProbePaused(const FixContext& ctx) {
    uint32_t sys = 0, db = 0, v = 0;
    const bool paused = ReadU32(g_sysInterfacePtr, &sys) && sys && ReadU32(sys + kSysEngineDb, &db) && db &&
                        ReadU32(db + kEngineDbPaused, &v) && (v & 0xFF) != 0;
    if (paused != g_paused && g_pauseLines < kMaxMapLines) {
        ++g_pauseLines;
        ctx.log("menuscale: frame %u game %s", g_frameNo, paused ? "paused" : "running");
    }
    g_paused = paused;
}

// Menu frame decision: with the probe, "a window is open" (list not empty) and it is not the in-game map - covers
// the main menu and the pause menu, which draw over a 3D scene - or a ZWINDOWS screen without a scene camera (the
// briefing, whose list is often empty while its photos are shown). While the probe runs but no ZWINDOWS
// instance is known (level load screens, logos, the first frames of a scene) nothing is scaled: the load screens
// already fill the screen (owner 2026-11-11: "super zoomed in" when scaled; their progress bar sits at the bottom of
// the screen). Without the probe (unknown hitman2.exe), the old rule (no scene camera in the last frames).
bool InMenuNow() {
    if (g_listCount >= 0) return ProbeSaysMenu(g_listCount, g_mapOpen, g_detector.InMenu(), g_paused);
    return g_probeOk ? false : g_detector.InMenu();
}

void ProbeIngameMap(const FixContext& ctx) {
    const bool canLog = g_mapLines < kMaxMapLines;
    if (g_mapOwner != g_windows) {  // new scene: forget the old object
        g_mapOwner = g_windows;
        g_map = 0;
        g_mapNextScan = 0;
        g_mapTries = 0;
        g_mapOpen = false;
    }
    uint32_t vt = 0;
    if (g_map && !(ReadU32(g_map, &vt) && vt == g_ingameMapVtable)) {
        g_map = 0;
        g_mapOpen = false;
    }
    if (!g_map) {
        // At most kMapScanTries full scans per scene, ~2 s apart (menu scenes have no map: 0 matches).
        if (g_mapTries >= kMapScanTries || g_frameNo < g_mapNextScan) return;
        g_mapNextScan = g_frameNo + kScanEveryFrames;
        ++g_mapTries;
        WindowObjectScan info;
        g_map = FindWindowObject(g_ingameMapVtable, g_windows, &info);
        if (canLog && (g_map || info.matches)) {
            ctx.log("menuscale: IngameMap scan %u: %u heap match(es) -> %s 0x%08X", g_mapTries, info.matches,
                    g_map ? "using" : "not found", unsigned(g_map));
            ++g_mapLines;
        }
        if (!g_map) return;
    }
    uint32_t flags = 0;
    const bool open = ReadU32(g_map + kIngameMapFlags, &flags) && (flags & kIngameMapOpenBit);
    if (open != g_mapOpen && canLog) {
        ctx.log("menuscale: frame %u in-game map %s (flags 0x%02X)", g_frameNo, open ? "open - not scaled" : "closed",
                unsigned(flags & 0xFF));
        ++g_mapLines;
    }
    g_mapOpen = open;
}

void ProbeWindows(const FixContext& ctx) {
    if (!g_probeOk) return;
    const bool canLog = g_probeLines < kMaxProbeLines;
    if (!IsZWindows(g_windows)) {
        g_listCount = -1;
        if (g_windows) {
            if (canLog) ctx.log("menuscale: ZWINDOWS 0x%08X gone (frame %u)", unsigned(g_windows), g_frameNo);
            ++g_probeLines;
            g_windows = 0;
            g_haveList = false;
            g_missingSince = g_frameNo;
        }
        // Every frame while missing: the region of the last instance, then the next kScanBudget bytes of the
        // incremental scan (a full pass takes a few dozen frames instead of one 15-60 ms hitch every 2 s).
        if (g_lastWindows) g_windows = FindZWindowsInRegion(g_lastWindows);
        if (!g_windows) g_windows = g_finder.Step(kScanBudget);
        if (!g_windows) return;
        g_lastWindows = g_windows;
        if (canLog)
            ctx.log("menuscale: ZWINDOWS found at 0x%08X (frame %u, %u frames after the previous one went away)",
                    unsigned(g_windows), g_frameNo, g_frameNo - g_missingSince);
        ++g_probeLines;
    }
    // Open-window list: logged whenever its content (node values / resolved vtables) changes.
    WindowList list;
    if (!ReadWindowList(g_windows, &list)) {
        g_listCount = -1;
        return;
    }
    g_listCount = int(list.count);
    ProbeIngameMap(ctx);
    ProbePaused(ctx);
    if (g_probeLines >= kMaxProbeLines) return;
    if (g_haveList && list.count == g_lastList.count && list.complete == g_lastList.complete &&
        std::memcmp(list.vtable, g_lastList.vtable, sizeof list.vtable) == 0 &&
        std::memcmp(list.node, g_lastList.node, sizeof list.node) == 0)
        return;
    g_lastList = list;
    g_haveList = true;
    ctx.log("menuscale: frame %u window list: %u node(s)%s", g_frameNo, list.count, list.complete ? "" : " (truncated)");
    ++g_probeLines;
    for (unsigned i = 0; i < list.count && g_probeLines < kMaxProbeLines; ++i, ++g_probeLines)
        ctx.log("menuscale:   [%u] node prev 0x%08X next 0x%08X value 0x%08X -> object 0x%08X vtbl 0x%08X", i,
                unsigned(list.node[i][0]), unsigned(list.node[i][1]), unsigned(list.node[i][2]),
                unsigned(list.object[i]), unsigned(list.vtable[i]));
    if (g_probeLines >= kMaxProbeLines) ctx.log("menuscale: probe log limit reached");
}

void OnAttach(const FixContext& ctx) {
    g_configured = GetPrivateProfileIntA("menuscale", "ScalePercent", 0, ctx.iniPath) / 100.0;
    ctx.log("menuscale: %s", g_configured > 0.0 ? "scale from ScalePercent" : "scale fits the screen");
    const ExeBuild* b = RunningExeBuild();
    g_probeOk = b != nullptr;
    if (b) {
        g_sysInterfacePtr = b->sysInterfacePtr;
        g_ingameMapVtable = b->ingameMapVtable;
        SetZWindowsBuild(b->zwindowsVtable, b->sysInterfacePtr);
    }
    ctx.log("menuscale: ZWINDOWS probe %s%s", g_probeOk ? "enabled (read-only), hitman2.exe " : "disabled",
            b ? b->label : " (unknown hitman2.exe)");
}

bool OnProjection(const FixContext& ctx, uintptr_t returnAddress, d3d8::Matrix& m, const d3d8::Viewport& vp,
                  unsigned bbWidth, unsigned bbHeight) {
    if (!IsCameraProjectionCall(ctx.renderD3DBase, returnAddress)) return false;
    g_projScaled = false;
    if (IsSceneCamera(m)) {
        g_detector.SceneCameraSeen();
        g_frame.scene = true;
        return false;
    }
    if (!IsUiCamera(m, vp, bbWidth, bbHeight)) return false;
    g_frame.ui = true;
    g_spanUi.Add(m);
    if (!InMenuNow()) return false;
    const double s = MenuScaleFactor(bbWidth, bbHeight, g_configured);
    if (s <= 1.0) return false;
    g_bbWidth = bbWidth;
    g_bbHeight = bbHeight;
    ScaleProjection(m, s);
    g_frame.scaled = true;
    g_projScaled = true;
    return true;
}

bool OnViewport(const FixContext&, d3d8::Viewport& vp, unsigned bbWidth, unsigned bbHeight) {
    if (!InMenuNow()) return false;
    if (!ScaleCanvasViewport(vp, bbWidth, bbHeight, MenuScaleFactor(bbWidth, bbHeight, g_configured))) return false;
    ++g_spanCanvas;
    if (g_projScaled) ++g_spanDouble;
    return true;
}

// Cursor confinement for the next frame's input update: on while this frame's UI camera was scaled. Logged on change.
uint32_t g_cursorLines = 0;
constexpr uint32_t kMaxCursorLines = 200;

void UpdateCursor(const FixContext& ctx) {
    if (!g_cursorHooked) return;
    g_cursorWindows = g_windows;
    const double s = g_frame.scaled ? MenuScaleFactor(g_bbWidth, g_bbHeight, g_configured) : 0.0;
    const bool was = g_cursorScale > 1.0, now = s > 1.0;
    g_cursorScale = s;
    if (now) g_cursorBounds = MenuCursorBounds(g_bbWidth, g_bbHeight, s);
    if (was == now || g_cursorLines >= kMaxCursorLines) return;
    ++g_cursorLines;
    if (now)
        ctx.log("menuscale: frame %u cursor confined to (%.1f,%.1f)-(%.1f,%.1f), speed 1/%.3f (hook calls %u, adjusted "
                "%u, ZWINDOWS adjusted %u skipped %u)",
                g_frameNo, g_cursorBounds.x0, g_cursorBounds.y0, g_cursorBounds.x1, g_cursorBounds.y1, s,
                g_cursorCalls, g_cursorAdjusted, g_zwAdjusted, g_zwSkipped);
    else
        ctx.log("menuscale: frame %u cursor released (hook calls %u, adjusted %u, ZWINDOWS adjusted %u skipped %u)",
                g_frameNo, g_cursorCalls, g_cursorAdjusted, g_zwAdjusted, g_zwSkipped);
}

void OnDirect3DCreate(const FixContext& ctx, uintptr_t caller) {
#if defined(_M_IX86)
    if (g_cursorHooked) return;
    const ExeBuild* b = RunningExeBuild();
    if (!b) {
        ctx.log("menuscale: unknown hitman2.exe build - menu cursor not confined");
        return;
    }
    g_mouseDeltaVa = b->mouseDeltaVa;
    const ExeCallSite site = {"menuscale",
                              b->sha256,
                              b->cursorContextVa,
                              b->cursorContext,
                              b->cursorContextSize,
                              b->cursorCallOffset,
                              kMouseDeltaPrologue,
                              sizeof kMouseDeltaPrologue,
                              b->inputFnLo,
                              b->inputFnHi,
                              "input frame function",
                              "the menu cursor stub"};
    g_cursorHooked = RedirectExeCall(ctx, caller, site, reinterpret_cast<const void*>(&h2e_MenuCursorStub));
    if (!g_cursorHooked) ctx.log("menuscale: menu cursor not confined (menus are still scaled)");
#else
    (void)ctx;
    (void)caller;
#endif
}

void OnPresent(const FixContext& ctx) {
    g_detector.EndFrame();
    ProbeWindows(ctx);
    UpdateCursor(ctx);
    g_frame.list = g_listCount;
    if (!g_anyLogged || g_frame != g_lastLogged) {
        if (g_kindLines < kMaxKindLines) {
            if (g_anyLogged)
                ctx.log("menuscale: frames %u-%u (%u frames, %u ms) scene=%d ui=%d scaled=%d windows=%d canvasVp=%u "
                        "double=%u uiCam=%u _11=%.4f..%.4f zf=%.1f..%.1f",
                        g_kindSince, g_frameNo - 1, g_frameNo - g_kindSince, GetTickCount() - g_kindSinceTick,
                        g_lastLogged.scene, g_lastLogged.ui, g_lastLogged.scaled, g_lastLogged.list, g_spanCanvas,
                        g_spanDouble, g_spanUi.calls, g_spanUi.calls ? g_spanUi.min11 : 0.0f,
                        g_spanUi.calls ? g_spanUi.max11 : 0.0f, g_spanUi.calls ? g_spanUi.minZf : 0.0,
                        g_spanUi.calls ? g_spanUi.maxZf : 0.0);
            if (++g_kindLines == kMaxKindLines) ctx.log("menuscale: frame-kind log limit reached");
        }
        g_lastLogged = g_frame;
        g_kindSince = g_frameNo;
        g_kindSinceTick = GetTickCount();
        g_anyLogged = true;
        g_spanCanvas = g_spanDouble = 0;
        g_spanUi = UiCamStats{};
    }
    g_frame = FrameKind{};
    ++g_frameNo;
}

}  // namespace

bool MenuScaleMenuOpen() { return g_frameNo > 0 && g_probeOk && InMenuNow(); }

extern const Fix kMenuScaleFix = {"menuscale",
                                  "EXPERIMENTAL enlarges menu screens (UI camera + canvas sub-viewports, menus only) (GB-006)",
                                  OnAttach, OnProjection, OnDirect3DCreate, OnViewport, OnPresent};

}  // namespace h2e
