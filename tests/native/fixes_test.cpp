// Unit tests for src/common (SHA-256, verified byte patches) and src/fixes (registry, resolution, horplus).
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "../../src/common/code_patch.h"
#include "../../src/common/sha256.h"
#include "../../src/fixes/fix.h"
#include "../../src/fixes/horplus.h"
#include "../../src/fixes/framelimit.h"
#include "../../src/fixes/hdfonts.h"
#include "../../src/fixes/menuscale.h"
#include "../../src/fixes/resolution.h"
#include "../../src/fixes/settings.h"
#include "../../src/fixes/zwindows.h"

// Bytes the end-to-end patch test rewrites inside this executable's own image (.data, so writable anyway).
volatile uint8_t g_patchTarget[6] = {0x0F, 0x84, 0x05, 0x01, 0x00, 0x00};

namespace {
int g_failures = 0;
std::string g_lastLog;

void TestLog(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_lastLog = buf;
}

void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

bool Near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }

double Deg(double scale) { return 2.0 * std::atan(1.0 / scale) * 180.0 / 3.14159265358979; }

// LH perspective as RenderD3D builds it (Vert-): _11 from the horizontal FOV, _22 = _11 * viewport aspect.
d3d8::Matrix GameMatrix(double fovXDeg, double vpAspect, float zn = 5.0f, float zf = 25000.0f) {
    const float xs = float(1.0 / std::tan(fovXDeg * 3.14159265358979 / 360.0));
    const float ys = float(xs * vpAspect);
    return d3d8::Matrix{{{xs, 0, 0, 0}, {0, ys, 0, 0}, {0, 0, zf / (zf - zn), 1}, {0, 0, -zn * zf / (zf - zn), 0}}};
}
}  // namespace

int main() {
    // --- registry: unique ids, every fix has at least one callback
    std::set<std::string> ids;
    for (size_t i = 0; i < h2e::kFixCount; ++i) {
        Check(ids.insert(h2e::kFixes[i]->id).second, "fix ids are unique");
        Check(h2e::kFixes[i]->onAttach || h2e::kFixes[i]->onProjection || h2e::kFixes[i]->onDirect3DCreate ||
                  h2e::kFixes[i]->onViewport || h2e::kFixes[i]->onPresent || h2e::kFixes[i]->onTextureFilled ||
                  h2e::kFixes[i]->onSetTexture || h2e::kFixes[i]->onTextureReleased,
              "fix has a callback");
    }
    Check(ids.count("resolution") == 1 && ids.count("horplus") == 1 && ids.count("timer") == 1,
          "resolution, horplus and timer registered");

    // --- resolution patch definition
    const h2e::BytePatch& rp = h2e::kSnapBranchPatch;
    Check(h2e::ClassifyBytes(h2e::kSnapBranchOriginal, rp) == h2e::PatchState::Original, "original bytes patchable");
    Check(h2e::ClassifyBytes(h2e::kSnapBranchPatched, rp) == h2e::PatchState::AlreadyPatched, "patched bytes seen");
    const uint8_t other[6] = {0x0F, 0x84, 0x05, 0x01, 0x00, 0x01};
    Check(h2e::ClassifyBytes(other, rp) == h2e::PatchState::Mismatch, "unknown bytes refused");
    // jz rel32 at RVA 0x197F7 and jmp rel32 must reach the same target (RVA 0x19902)
    int32_t jz = 0, jmp = 0;
    std::memcpy(&jz, h2e::kSnapBranchOriginal + 2, 4);
    std::memcpy(&jmp, h2e::kSnapBranchPatched + 1, 4);
    Check(rp.rva + 6 + jz == 0x19902, "jz target is RVA 0x19902");
    Check(rp.rva + 5 + jmp == 0x19902, "jmp target is RVA 0x19902");

    // --- ApplyBytePatch end to end on this executable: hash check, byte check, write, idempotence
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::ifstream f(exePath, std::ios::binary);
        std::stringstream data;
        data << f.rdbuf();
        const std::string bytes = data.str();
        char exeHash[65];
        h2e::Sha256Hex(bytes.data(), bytes.size(), exeHash);
        const char* exeName = std::strrchr(exePath, '\\') + 1;
        const auto rva = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_patchTarget) -
                                               reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));
        h2e::BytePatch wrongHash = {exeName, h2e::kRenderD3DSha256, rva, h2e::kSnapBranchOriginal,
                                    h2e::kSnapBranchPatched, 6};
        Check(h2e::ApplyBytePatch(wrongHash, TestLog) == h2e::PatchResult::HashMismatch, "wrong file hash refused");
        Check(g_patchTarget[0] == 0x0F, "nothing written on hash mismatch");
        h2e::BytePatch ok = {exeName, exeHash, rva, h2e::kSnapBranchOriginal, h2e::kSnapBranchPatched, 6};
        Check(h2e::ApplyBytePatch(ok, TestLog) == h2e::PatchResult::Applied, "patch applied");
        Check(g_patchTarget[0] == 0xE9 && g_patchTarget[5] == 0x90, "patched bytes written");
        Check(h2e::ApplyBytePatch(ok, TestLog) == h2e::PatchResult::AlreadyApplied, "second apply is a no-op");
        h2e::BytePatch wrongBytes = {exeName, exeHash, rva + 1, h2e::kSnapBranchOriginal, h2e::kSnapBranchPatched, 6};
        Check(h2e::ApplyBytePatch(wrongBytes, TestLog) == h2e::PatchResult::BytesMismatch, "wrong bytes refused");
        h2e::BytePatch missing = {"no_such_module.dll", exeHash, rva, h2e::kSnapBranchOriginal,
                                  h2e::kSnapBranchPatched, 6};
        Check(h2e::ApplyBytePatch(missing, TestLog) == h2e::PatchResult::ModuleNotLoaded, "missing module refused");
    }

    // --- SHA-256 (FIPS 180-2 vectors)
    char hex[65];
    h2e::Sha256Hex("", 0, hex);
    Check(std::strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0, "sha256('')");
    h2e::Sha256Hex("abc", 3, hex);
    Check(std::strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "sha256(abc)");
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";  // 56 bytes: two-block padding
    h2e::Sha256Hex(two, std::strlen(two), hex);
    Check(std::strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0, "sha256(448 bit)");
    std::string million(1000000, 'a');
    h2e::Sha256Hex(million.data(), million.size(), hex);
    Check(std::strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0, "sha256(1M a)");

    // --- Hor+ at 1920x1080 (run-006: gameplay fovX 67.4, fovY 41.126 at 16:9)
    const d3d8::Viewport full{0, 0, 1920, 1080, 0.0f, 1.0f};
    d3d8::Matrix m = GameMatrix(67.4, 1920.0 / 1080.0);
    Check(Near(Deg(m.m[1][1]), 41.126), "game matrix reproduces run-006 fovY 41.126");
    const d3d8::Matrix before = m;
    Check(h2e::ApplyHorPlus(m, full, 1920, 1080), "hor+ applies to full-screen 16:9");
    Check(Near(Deg(m.m[1][1]), Deg(GameMatrix(67.4, 4.0 / 3.0).m[1][1])), "vertical FOV equals the 4:3 one (53.15)");
    Check(Near(Deg(m.m[1][1]), 53.147, 0.01), "fovY 53.147 deg");
    Check(Near(Deg(m.m[0][0]), 83.29, 0.02), "fovX widened to ~83.3 deg");
    Check(Near(m.m[1][1] / m.m[0][0], before.m[1][1] / before.m[0][0], 1e-5), "matrix aspect unchanged");
    Check(m.m[2][2] == before.m[2][2] && m.m[3][2] == before.m[3][2] && m.m[2][3] == 1.0f, "depth terms unchanged");

    // letterbox cutscene 1920x810 at y=135: vertical FOV as the 4:3 game would show it (letterbox 16:9)
    const d3d8::Viewport letterbox{0, 135, 1920, 810, 0.0f, 1.0f};
    d3d8::Matrix c = GameMatrix(22.0, 1920.0 / 810.0);
    Check(h2e::ApplyHorPlus(c, letterbox, 1920, 1080), "hor+ applies to full-width letterbox");
    Check(Near(Deg(c.m[1][1]), Deg(GameMatrix(22.0, 16.0 / 9.0).m[1][1])), "letterbox keeps the 4:3-era vertical FOV");

    // refused cases: sub-viewport, 4:3 and 5:4 back buffers, off-centre (shadow-like) and orthographic matrices
    d3d8::Matrix s = GameMatrix(45.0, 310.0 / 156.0);
    const d3d8::Matrix sCopy = s;
    Check(!h2e::ApplyHorPlus(s, d3d8::Viewport{671, 351, 310, 156, 0, 1}, 1920, 1080), "sub-viewport untouched");
    Check(std::memcmp(&s, &sCopy, sizeof s) == 0, "refused matrix unchanged");
    d3d8::Matrix q = GameMatrix(67.4, 4.0 / 3.0);
    Check(!h2e::ApplyHorPlus(q, d3d8::Viewport{0, 0, 800, 600, 0, 1}, 800, 600), "4:3 untouched");
    d3d8::Matrix f = GameMatrix(65.0, 1.25);
    Check(!h2e::ApplyHorPlus(f, d3d8::Viewport{0, 0, 1280, 1024, 0, 1}, 1280, 1024), "5:4 untouched");
    d3d8::Matrix off = GameMatrix(9.0, 1.0);
    off.m[2][0] = -0.0061f;
    Check(!h2e::ApplyHorPlus(off, full, 1920, 1080), "off-centre matrix untouched");
    // run-007: the HUD/menu camera (45 deg, zn 5, zf 3000.08) shares the call site and must stay unchanged
    d3d8::Matrix ui = GameMatrix(45.0, 1920.0 / 1080.0, 5.0f, 3000.08f);
    Check(!h2e::ApplyHorPlus(ui, full, 1920, 1080), "UI camera (zf 3000) untouched");
    d3d8::Matrix util = GameMatrix(90.0, 1.0, 0.5f, 10.0f);
    Check(!h2e::ApplyHorPlus(util, full, 1920, 1080), "zn 0.5 / zf 10 utility pass untouched");
    d3d8::Matrix cut = GameMatrix(60.903, 1920.0 / 877.0, 5.0f, 11999.0f);
    Check(h2e::ApplyHorPlus(cut, d3d8::Viewport{0, 101, 1920, 877, 0, 1}, 1920, 1080), "cutscene zf 11999 adjusted");
    d3d8::Matrix id{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    Check(!h2e::ApplyHorPlus(id, full, 1920, 1080), "identity (2D layer, RenderD3D+0x47F1) untouched");

    // --- framelimit
    Check(h2e::ResolveMaxFps(0, 144) == 144, "default cap = monitor refresh rate");
    Check(h2e::ResolveMaxFps(0, 1) == 60 && h2e::ResolveMaxFps(0, 0) == 60, "unknown refresh rate -> 60");
    Check(h2e::ResolveMaxFps(60, 144) == 60, "MaxFps overrides the monitor");
    Check(h2e::ResolveMaxFps(5, 144) == 15 && h2e::ResolveMaxFps(5000, 144) == 1000, "MaxFps clamped to 15..1000");
    {
        h2e::FramePacer p;
        const int64_t period = 1000;  // ticks per frame
        Check(p.Advance(5000, period) == 0 && p.next == 6000, "first frame: no wait, schedule starts");
        Check(p.Advance(5200, period) == 800 && p.next == 7000, "fast frame waits until its deadline");
        Check(p.Advance(7000, period) == 0 && p.next == 8000, "on-time frame: no wait");
        Check(p.Advance(7600, period) == 400, "deadlines stay on the grid");
        Check(p.Advance(12000, period) == 0 && p.next == 13000, "more than one period late: schedule restarts");
        // steady fast game: average period equals the cap
        h2e::FramePacer steady;
        int64_t now = 0, total = 0;
        for (int i = 0; i < 1000; ++i) {
            now += 10;  // the game itself needs 10 ticks per frame
            const int64_t w = steady.Advance(now, period);
            now += w;
            total += w;
        }
        Check(now >= 999 * period && now <= 1000 * period + 10, "1000 fast frames take 1000 periods");
    }

    // --- menuscale (1920x1080: x2.25)
    Check(Near(h2e::MenuScaleFactor(1920, 1080, 0.0), 2.25) && Near(h2e::MenuScaleFactor(1920, 1080, 1.5), 1.5) &&
              Near(h2e::MenuScaleFactor(1920, 1080, 9.0), 2.25),
          "menu scale factor: fit, configured, clamped");
    const d3d8::Viewport screenVp{0, 0, 1920, 1080, 0.0f, 1.0f};
    const d3d8::Matrix uiCam = GameMatrix(45.0, 1920.0 / 1080.0, 5.0f, 3000.08f);
    const d3d8::Matrix sceneCam = GameMatrix(67.4, 1920.0 / 1080.0, 5.0f, 20000.0f);
    Check(h2e::IsUiCamera(uiCam, screenVp, 1920, 1080) && !h2e::IsUiCamera(sceneCam, screenVp, 1920, 1080),
          "UI camera recognised, scene camera not");
    Check(!h2e::IsUiCamera(uiCam, d3d8::Viewport{671, 351, 310, 156, 0, 1}, 1920, 1080), "UI camera needs full viewport");
    Check(h2e::IsSceneCamera(sceneCam) && !h2e::IsSceneCamera(uiCam), "scene camera recognised by zf");
    Check(!h2e::IsUiCamera(GameMatrix(90.0, 1920.0 / 1080.0, 0.5f, 10.0f), screenVp, 1920, 1080),
          "zf 10 utility camera (load screens) is not a UI camera");
    d3d8::Matrix scaled = uiCam;
    h2e::ScaleProjection(scaled, 2.25);
    // a point projected at NDC x=0.2 lands at 0.45: x2.25 about the centre
    Check(Near(scaled.m[0][0] / uiCam.m[0][0], 2.25, 1e-5) && Near(scaled.m[1][1] / uiCam.m[1][1], 2.25, 1e-5) &&
              scaled.m[2][2] == uiCam.m[2][2] && scaled.m[3][2] == uiCam.m[3][2],
          "projection scaled in x/y only");
    d3d8::Viewport canvas{640, 300, 640, 480, 0.0f, 1.0f};
    Check(h2e::ScaleCanvasViewport(canvas, 1920, 1080, 2.25) && canvas.X == 240 && canvas.Y == 0 &&
              canvas.Width == 1440 && canvas.Height == 1080,
          "640x480 canvas viewport -> 1440x1080 at (240,0)");
    d3d8::Viewport scroller{671, 351, 310, 156, 0.0f, 1.0f};
    Check(h2e::ScaleCanvasViewport(scroller, 1920, 1080, 2.25) && scroller.X == 310 && scroller.Y == 115 &&
              scroller.Width == 697 && scroller.Height == 351,
          "briefing scroller sub-viewport scaled with the canvas");
    d3d8::Viewport fullVp = screenVp, rtt{0, 0, 128, 128, 0.0f, 1.0f};
    Check(!h2e::ScaleCanvasViewport(fullVp, 1920, 1080, 2.25) && !h2e::ScaleCanvasViewport(rtt, 1920, 1080, 2.25),
          "full-screen and render-to-texture viewports untouched");
    // menu cursor: visible region at 1080p x2.25 and the delta the hook returns
    const h2e::CursorBounds cb = h2e::MenuCursorBounds(1920, 1080, 2.25);
    Check(Near(cb.x0, 960.0 - 1920.0 / 4.5, 1e-3) && Near(cb.x1, 960.0 + 1920.0 / 4.5, 1e-3) && Near(cb.y0, 300.0) &&
              Near(cb.y1, 780.0),
          "cursor bounds = centre +/- (W/2s, H/2s): (533.3,300)-(1386.7,780)");
    Check(Near(960.0 + (cb.x1 - 960.0) * 2.25, 1920.0, 1e-3) && Near(540.0 + (cb.y0 - 540.0) * 2.25, 0.0, 1e-3),
          "cursor bounds are the screen edges once drawn through the scaled camera");
    const h2e::CursorBounds whole = h2e::MenuCursorBounds(1920, 1080, 1.0);
    Check(whole.x0 == 0.0f && whole.y0 == 0.0f && whole.x1 == 1920.0f && whole.y1 == 1080.0f, "no scale: whole screen");
    float out[2] = {};
    const float move[2] = {90.0f, -45.0f};
    h2e::MenuCursorDelta(960.0f, 540.0f, move, 2.25, cb, out);
    Check(Near(out[0], 40.0, 1e-4) && Near(out[1], -20.0, 1e-4), "cursor motion divided by the scale");
    const float push[2] = {500.0f, 500.0f};
    h2e::MenuCursorDelta(1380.0f, 775.0f, push, 2.25, cb, out);
    Check(Near(1380.0 + out[0], cb.x1, 1e-3) && Near(775.0 + out[1], cb.y1, 1e-3), "cursor stops at the edges");
    const float none[2] = {0.0f, 0.0f};
    h2e::MenuCursorDelta(1919.0f, 0.0f, none, 2.25, cb, out);
    Check(Near(1919.0 + out[0], cb.x1, 1e-3) && Near(0.0 + out[1], cb.y0, 1e-3),
          "cursor outside the region (menu just opened) is moved inside");
    h2e::MenuCursorDelta(100.0f, 100.0f, move, 1.0, whole, out);
    Check(out[0] == 90.0f && out[1] == -45.0f, "no scale: engine delta unchanged");
    // ZWINDOWS cursor: pos += (raw - last) * k after the hook; the new raw makes that step /s and confined
    {
        const float last = -35000.0f, k = 1.0f;
        float raw = h2e::ZWindowsRawTarget(last + 90.0f, last, 960.0f, k, 2.25, cb.x0, cb.x1);
        Check(Near(960.0 + (raw - last) * k, 1000.0, 1e-2), "ZWINDOWS step divided by the scale");
        raw = h2e::ZWindowsRawTarget(last + 5000.0f, last, 1300.0f, 0.5f, 2.25, cb.x0, cb.x1);
        Check(Near(1300.0 + (raw - last) * 0.5, cb.x1, 1e-2), "ZWINDOWS cursor stops at the edge (speed k = 0.5)");
        raw = h2e::ZWindowsRawTarget(last, last, 1900.0f, k, 2.25, cb.x0, cb.x1);
        Check(Near(1900.0 + (raw - last) * k, cb.x1, 1e-2), "ZWINDOWS cursor outside the region moved inside");
        Check(h2e::ZWindowsRawTarget(123.0f, last, 960.0f, 0.0f, 2.25, cb.x0, cb.x1) == 123.0f,
              "ZWINDOWS speed 0: raw unchanged");
    }
    int32_t rel = 0;
    std::memcpy(&rel, h2e::kCursorContext + h2e::kCursorCallOffset + 1, sizeof rel);
    const uintptr_t callVa = h2e::kCursorContextVa + h2e::kCursorCallOffset;
    Check(h2e::kCursorContext[h2e::kCursorCallOffset] == 0xE8 && callVa + 5 + rel == h2e::kMouseDeltaVa &&
              callVa % 8 == 0 && callVa == 0x40F830,
          "cursor context: aligned call 0x40F9D0 at 0x40F830");

    h2e::MenuDetector det;
    Check(det.InMenu(), "menu mode before any scene camera");
    det.SceneCameraSeen();
    det.EndFrame();
    Check(!det.InMenu(), "not in menu right after a scene camera");
    for (uint32_t i = 0; i < h2e::MenuDetector::kSceneGraceFrames; ++i) det.EndFrame();
    Check(det.InMenu(), "menu mode after the grace frames without scene camera");

    Check(!h2e::ProbeSaysMenu(-1, false, true), "no ZWINDOWS (load screen): not a menu");
    Check(h2e::ProbeSaysMenu(1, false, false), "open window over a 3D scene (main menu, pause): menu");
    Check(!h2e::ProbeSaysMenu(1, true, true), "in-game map open: not scaled");
    Check(h2e::ProbeSaysMenu(0, false, true), "empty list without scene camera (briefing photos): menu");
    Check(!h2e::ProbeSaysMenu(0, false, false), "empty list with a scene camera (gameplay HUD): not a menu");

    // ZWINDOWS probe: a fake instance on the heap is found by its vtable + self-cache; a decoy without the
    // self-cache is rejected.
    auto* fake = static_cast<uint32_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    fake[0] = h2e::kZWindowsVtable;
    fake[h2e::kZWinObjWindowsCache / 4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fake));
    fake[0x400 / 4] = h2e::kZWindowsVtable;  // decoy
    unsigned count = 0;
    Check(h2e::IsZWindows(reinterpret_cast<uintptr_t>(fake)) &&
              !h2e::IsZWindows(reinterpret_cast<uintptr_t>(fake) + 0x400),
          "IsZWindows: vtable + self-cache required");
    Check(h2e::FindZWindows(&count) == reinterpret_cast<uintptr_t>(fake) && count == 1,
          "FindZWindows finds the single valid instance");
    {
        h2e::IncrementalFinder finder;
        uintptr_t hit = 0;
        while (!hit && finder.Passes() < 2) hit = finder.Step(1u << 20);
        Check(hit == reinterpret_cast<uintptr_t>(fake), "IncrementalFinder finds the instance in 1 MB steps");
    }
    uint32_t v = 0;
    Check(!h2e::ReadU32(0x10, &v), "ReadU32 fails safely on an unmapped address");
    // Window list: two nodes {prev, next, value}; values point at fake objects whose vtable lies in the exe range.
    auto addr = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };
    uint32_t objA[1] = {0x0064AAAA}, objB[1] = {0x0064BBBB};
    uint32_t nodeA[3] = {0, 0, addr(objA)}, nodeB[3] = {addr(nodeA), 0, addr(objB)};
    nodeA[1] = addr(nodeB);
    fake[h2e::kWindowListHead / 4] = addr(nodeA);
    fake[h2e::kWindowListTail / 4] = addr(nodeB);
    h2e::WindowList wl;
    Check(h2e::ReadWindowList(reinterpret_cast<uintptr_t>(fake), &wl) && wl.count == 2 && wl.complete &&
              wl.object[0] == addr(objA) && wl.vtable[0] == 0x0064AAAA && wl.vtable[1] == 0x0064BBBB,
          "ReadWindowList walks head -> tail and resolves object pointers");
    fake[h2e::kWindowListHead / 4] = fake[h2e::kWindowListTail / 4] = 0;
    Check(h2e::ReadWindowList(reinterpret_cast<uintptr_t>(fake), &wl) && wl.count == 0 && wl.complete,
          "empty window list");
    Check(h2e::ResolveGeom(0x40000005) == 0, "static references are not resolved");
    VirtualFree(fake, 0, MEM_RELEASE);

    // --- hdfonts: key, pixel formats, pack parsing (vectors generated by tools/hdfonts_build.py)
    {
        namespace hf = h2e::hdfonts;
        uint8_t ramp[16];
        for (int i = 0; i < 16; ++i) ramp[i] = static_cast<uint8_t>(i);
        Check(hf::PageKey(ramp, 16) == 0x7c84dc9477851775ULL, "PageKey matches page_key of the Python builder");

        // 2x2 level with padded rows: alpha 0x00, 0xF0 / 0xF7, 0x0F -> 0, 15 / 15, 0
        uint8_t argb[2][12] = {};
        argb[0][3] = 0x00, argb[0][7] = 0xF0, argb[1][3] = 0xF7, argb[1][7] = 0x0F;
        std::vector<uint8_t> a4;
        Check(hf::Alpha4FromLevel(d3d8::kFmt_A8R8G8B8, argb, 12, 2, 2, a4) && a4 == std::vector<uint8_t>{0, 15, 15, 0},
              "Alpha4FromLevel A8R8G8B8 with pitch");
        const uint16_t argb4[4] = {0x0FFF, 0xFFFF, 0xF000, 0x0000};
        Check(hf::Alpha4FromLevel(d3d8::kFmt_A4R4G4B4, argb4, 4, 2, 2, a4) && a4 == std::vector<uint8_t>{0, 15, 15, 0},
              "Alpha4FromLevel A4R4G4B4");
        Check(!hf::Alpha4FromLevel(25 /*A1R5G5B5*/, argb4, 4, 2, 2, a4), "Alpha4FromLevel refuses A1R5G5B5");

        const std::vector<uint8_t> pack = {
            0x48, 0x32, 0x45, 0x46, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x57, 0x77, 0x61, 0x54, 0x7f,
            0x3b, 0x6a, 0xcb, 0x02, 0x00, 0x02, 0x00, 0x08, 0x00, 0x08, 0x00, 0xff, 0xff, 0xfe, 0x00, 0x03, 0x54,
            0x23, 0x30, 0x0a, 0x00, 0x00, 0x00, 0xed, 0x00, 0xfe, 0xff, 0x02, 0x07, 0x09, 0x0b, 0xdb, 0x00};
        std::vector<hf::PackPage> pages;
        std::string err;
        Check(hf::ParsePack(pack, pages, err) && pages.size() == 1, "ParsePack reads the Python pack");
        if (pages.size() == 1) {
            const hf::PackPage& p = pages[0];
            const uint8_t key4[4] = {0, 15, 15, 0};
            Check(p.key == hf::PageKey(key4, 4) && p.srcWidth == 2 && p.dstWidth == 8 && p.rgb == 0xFEFFFF &&
                      p.name == "T#0",
                  "pack page fields");
            uint8_t alpha[64];
            Check(hf::UnpackBits(p.packed, p.packedSize, alpha, 64) && alpha[19] == 0 && alpha[20] == 255 &&
                      alpha[22] == 255 && alpha[23] == 7 && alpha[25] == 11 && alpha[26] == 0 && alpha[63] == 0,
                  "UnpackBits restores the alpha");
            Check(!hf::UnpackBits(p.packed, p.packedSize - 1, alpha, 64), "UnpackBits refuses truncated data");
        }
        std::vector<uint8_t> bad = pack;
        bad[24] = 6;  // dst width not a power of two / multiple
        Check(!hf::ParsePack(bad, pages, err), "ParsePack refuses a bad page size");
        bad = pack;
        bad.pop_back();
        Check(!hf::ParsePack(bad, pages, err), "ParsePack refuses a truncated pack");

        const uint8_t src[4 * 2] = {0, 255, 100, 100, 255, 0, 100, 101};
        uint8_t mip[2];
        hf::DownsampleAlpha(src, 4, 2, mip);
        Check(mip[0] == 128 && mip[1] == 100, "DownsampleAlpha 2x2 box (rounded)");
        uint8_t one;
        hf::DownsampleAlpha(src, 1, 1, &one);
        Check(one == 0, "DownsampleAlpha 1x1");

        const uint8_t al[2] = {0x80, 0xFF};
        uint32_t out32[2] = {};
        Check(hf::WritePixels(d3d8::kFmt_A8R8G8B8, 0xFEFFFF, al, 2, 1, out32, 8) && out32[0] == 0x80FEFFFF &&
                  out32[1] == 0xFFFEFFFF,
              "WritePixels A8R8G8B8");
        uint16_t out16[2] = {};
        Check(hf::WritePixels(d3d8::kFmt_A4R4G4B4, 0xFEFFFF, al, 2, 1, out16, 4) && out16[0] == 0x8FFF &&
                  out16[1] == 0xFFFF,
              "WritePixels A4R4G4B4");
    }

    // --- settings page: Hitman2.ini line editing, choices, FOV
    {
        namespace st = h2e::settings;
        const std::string ini = "DefaultScene=AllLevels/logos.gms\r\n\r\nresolution 1280x1024\r\nColorDepth 32\r\n";
        std::string iv;
        Check(st::FindIniLine(ini, "Resolution", &iv) && iv == "1280x1024", "FindIniLine case-insensitive value");
        Check(st::FindIniLine(ini, "DefaultScene", &iv) && iv == "AllLevels/logos.gms", "FindIniLine Key=Value line");
        Check(!st::FindIniLine(ini, "Window", nullptr), "FindIniLine missing key");
        Check(!st::FindIniLine("Windowed 1\r\n", "Window", nullptr), "FindIniLine matches the whole first token");
        const std::string set = st::SetIniLine(ini, "Resolution", "1920x1080");
        Check(set == "DefaultScene=AllLevels/logos.gms\r\n\r\nResolution 1920x1080\r\nColorDepth 32\r\n",
              "SetIniLine replaces in place, keeps CRLF");
        const std::string win = st::SetIniLine(ini, "Window", "");
        Check(win == ini + "Window\r\n", "SetIniLine appends a bare flag");
        Check(st::SetIniLine(win, "Window", "", true) == ini, "SetIniLine removes a flag");
        Check(st::SetIniLine("A 1", "B", "2") == "A 1\r\nB 2\r\n", "SetIniLine on a file without final newline");

        st::Resolution r;
        Check(st::ParseResolution("1920x1080", &r) && r.width == 1920 && r.height == 1080, "ParseResolution");
        Check(st::ParseResolution(" 800 X 600 ", &r) && r.width == 800, "ParseResolution spaces/upper X");
        Check(!st::ParseResolution("320x200", &r) && !st::ParseResolution("1920", &r), "ParseResolution rejects");
        const auto list = st::ResolutionChoices({{1920, 1080}, {640, 400}, {1280, 720}, {1920, 1080}}, {2560, 1440});
        Check(list.size() == 3 && list[0].width == 1280 && list[2].width == 2560, "ResolutionChoices sort/unique/add");

        Check(st::kFpsChoices[0] == 0 && st::kFpsChoices[st::kFpsChoiceCount - 1] == h2e::kUnlimitedFps,
              "fps choices: monitor first, unlimited last");
        Check(st::FpsChoiceIndex(0) == 0 && st::kFpsChoices[st::FpsChoiceIndex(144)] == 144 &&
                  st::kFpsChoices[st::FpsChoiceIndex(70)] == 75 && st::FpsChoiceIndex(-1) == st::kFpsChoiceCount - 1,
              "FpsChoiceIndex exact / nearest / unlimited");
        Check(std::fabs(st::FovDegrees(100) - st::kBaseFovXDeg) < 1e-9 && st::FovDegrees(120) > st::kBaseFovXDeg,
              "FovDegrees");

        d3d8::Matrix scene{};  // LH perspective zn 10, zf 12000
        scene.m[0][0] = 1.5f;
        scene.m[1][1] = 2.0f;
        scene.m[2][2] = float(12000.0 / (12000.0 - 10.0));
        scene.m[2][3] = 1.0f;
        scene.m[3][2] = float(-10.0 * 12000.0 / (12000.0 - 10.0));
        d3d8::Matrix uiProj = scene;  // zf 3000: UI camera, left alone
        uiProj.m[2][2] = float(3000.0 / (3000.0 - 5.0));
        uiProj.m[3][2] = float(-5.0 * 3000.0 / (3000.0 - 5.0));
        d3d8::Matrix fov = scene;
        Check(!st::ApplyFov(fov, 100) && fov.m[0][0] == 1.5f, "ApplyFov 100% leaves the matrix");
        Check(st::ApplyFov(fov, 125) && std::fabs(fov.m[0][0] - 1.2f) < 1e-6 && std::fabs(fov.m[1][1] - 1.6f) < 1e-6,
              "ApplyFov 125% scales _11/_22 by 100/125");
        Check(!st::ApplyFov(uiProj, 125), "ApplyFov skips the UI camera");

        st::KeyRepeat k;
        Check(k.Update(true, 1000) && !k.Update(true, 1100) && k.Update(true, 1450) && !k.Update(true, 1480) &&
                  k.Update(true, 1530) && !k.Update(false, 1600) && k.Update(true, 1610),
              "KeyRepeat press, delay, rate, release");
    }

    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
