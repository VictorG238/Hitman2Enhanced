// Fix "menuscale" (GB-006). EXPERIMENTAL: enlarges the menu screens (main menu, level list, briefing, load).
//
// The menus are 2D objects in front of a UI camera (RenderD3D+0x33DD, centred LH perspective, zn 5, zf ~3000) and a
// few sub-panels drawn in sub-viewports inside the centred 640x480 canvas (EXP-003 run-006..009, uiscale and Hor+
// tests). Scaling the UI camera's _11/_22 by s enlarges everything it draws by s about the screen centre; scaling
// the sub-viewports by s about the centre keeps the sub-panels consistent. Applied only on frames where no 3D scene
// camera (zf > 4000) has been used recently, so gameplay, HUD and the in-game pause menu are untouched. The only game
// code change is one verified call redirect (menu cursor, below), applied from onDirect3DCreate; if any check fails
// the menus are still scaled and the cursor keeps the engine's behaviour.
#pragma once
#include <cstddef>
#include <cstdint>

#include "../common/d3d8_min.h"

namespace h2e {

constexpr unsigned kCanvasWidth = 640, kCanvasHeight = 480;
constexpr double kMaxUiFarPlane = 4000.0;  // UI cameras zf ~3000; scene cameras zf >= 11999 (EXP-003)
// The zn 0.5 / zf 10, 90 deg utility camera (load screens: `_11` 1.0, zf 10 next to the 45 deg / zf 3000 UI camera)
// is not a 640x480-layout camera: scaling it zoomed the load screens ("super zoomed in", owner 2026-11-11).
// HYPOTHESIS that it draws the full-screen load layer; under test.
constexpr double kMinUiFarPlane = 100.0;

// Largest factor that fits the 640x480 canvas on the back buffer (1920x1080 -> 2.25), or `configured` (> 0) clamped
// to it. <= 1 means nothing to enlarge.
double MenuScaleFactor(unsigned bbWidth, unsigned bbHeight, double configured);

// True for the UI camera: centred LH perspective with kMinUiFarPlane <= zf <= kMaxUiFarPlane on a full back-buffer
// viewport.
bool IsUiCamera(const d3d8::Matrix& m, const d3d8::Viewport& vp, unsigned bbWidth, unsigned bbHeight);

// True for a scene camera (centred LH perspective with zf > kMaxUiFarPlane).
bool IsSceneCamera(const d3d8::Matrix& m);

// Multiplies _11 and _22 by `scale` (enlarges about the viewport centre).
void ScaleProjection(d3d8::Matrix& m, double scale);

// If `vp` lies inside the centred 640x480 canvas (and is smaller than the back buffer), scales it by `scale` about
// the back-buffer centre, clamped to the back buffer, and returns true.
bool ScaleCanvasViewport(d3d8::Viewport& vp, unsigned bbWidth, unsigned bbHeight, double scale);

// --- menu cursor (hitman2.exe 1.0.0.277, SHA-256 kHitman2Sha256; analysis image f59d353e...05266a) -------------
// The cursor is g_pSysInput (ZSysInputWintel, vtable 0x639C10) +0x18 / +0x1C: floats in unscaled screen pixels.
// Each frame 0x40F7C0 (from vtbl+0x24 = 0x410E10) adds the mouse delta returned by `call 0x40F9D0` (at 0x40F830,
// WintelMouse+0x3A: dx, dy) and clamps the result to 0..W, 0..H. The cursor is drawn through the UI camera, so with
// the camera scaled by s it moves s times faster and its clamp lies off screen. While menuscale scales the frame,
// the call at 0x40F830 is answered with a delta that divides the motion by s and keeps the cursor in the part of the
// unscaled screen that the scaled camera shows.
constexpr uint32_t kCursorContextVa = 0x40F82C;  // 34 bytes 0x40F82C..0x40F84D
constexpr size_t kCursorCallOffset = 4;          // call 0x40F9D0 at 0x40F830 (8-byte aligned)
constexpr uint8_t kCursorContext[34] = {
    0x6A, 0x00,                          // push 0                  (device index)
    0x8B, 0xCE,                          // mov  ecx, esi           (g_pSysInput)
    0xE8, 0x9B, 0x01, 0x00, 0x00,        // call 0x40F9D0           (returns float* {dx, dy})
    0x85, 0xC0,                          // test eax, eax
    0x0F, 0x84, 0xC6, 0x00, 0x00, 0x00,  // jz   0x40F903
    0xD9, 0x46, 0x18,                    // fld  dword [esi+18h]    (cursor x)
    0xD8, 0x00,                          // fadd dword [eax]
    0xD9, 0x5E, 0x18,                    // fstp dword [esi+18h]
    0xD9, 0x40, 0x04,                    // fld  dword [eax+4]
    0xD8, 0x46, 0x1C,                    // fadd dword [esi+1Ch]    (cursor y)
    0xD9, 0x5E, 0x1C};                   // fstp dword [esi+1Ch]
constexpr uintptr_t kMouseDeltaVa = 0x40F9D0;  // thiscall float* (int device), ret 4
constexpr uint8_t kMouseDeltaPrologue[11] = {0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x0C, 0x8B, 0x01, 0xFF, 0x50, 0x14};
constexpr uintptr_t kInputFrameVa = 0x40F7C0, kInputFrameEnd = 0x40F906;  // enclosing function
constexpr uint32_t kCursorX = 0x18, kCursorY = 0x1C;

// Unscaled screen region shown by the scaled UI camera: centre +/- (W/(2s), H/(2s)), within 0..W x 0..H.
struct CursorBounds {
    float x0, y0, x1, y1;
};
CursorBounds MenuCursorBounds(unsigned bbWidth, unsigned bbHeight, double scale);

// The delta to add to (x, y) so that the cursor ends at clamp((x, y) + (dx, dy) / scale, bounds): normal on-screen
// speed and the screen edges as limits. A cursor outside the bounds (menu just opened) is moved inside.
void MenuCursorDelta(float x, float y, const float delta[2], double scale, const CursorBounds& b, float out[2]);

// Second cursor (main menu, briefing; runtime 2026-11-11: the g_pSysInput cursor stayed confined there while the owner
// still saw the old behaviour): ZWINDOWS keeps its own position at +0x70/+0x74 (getter 0x4BBDC0 = vtbl+0x2EC, setter
// 0x4BBDE0 = vtbl+0x2E8, clamps to 0..W/H). Its update 0x4BC1F0 adds (raw - last) * k, with raw =
// g_pSysInterface+0x7E4/+0x7E8 (unbounded sum of the DirectInput deltas, WintelMouse::Update 0x411810), last =
// ZWINDOWS+0x78/+0x7C (raw seen last time) and k = ZWINDOWS+0x90. Runs in MainLoop, after the input update, so the
// hook can set raw such that this step ends confined too.
constexpr uintptr_t kSysInterfacePtr = 0x6A6C5C;
constexpr uint32_t kRawCursor = 0x7E4;           // float x, y
constexpr uint32_t kDirectInputMouseFlag = 0x62;  // byte; 0 = raw is not the DirectInput sum (0x40F7D4) - INFERRED
constexpr uint32_t kZwCursor = 0x70, kZwLastRaw = 0x78, kZwCursorSpeed = 0x90;

// New raw value (one axis) so that ZWINDOWS moves from `pos` to clamp(pos + (raw - last) * k / scale, lo..hi).
// Returns `raw` unchanged if k is 0 or not finite.
float ZWindowsRawTarget(float raw, float last, float pos, float k, double scale, float lo, float hi);

// Menu decision with the ZWINDOWS probe. listCount: open windows (-1: no ZWINDOWS instance known - load screens,
// logos: never scaled). A menu is an open window that is not the in-game map, or a ZWINDOWS screen without any 3D
// scene: the briefing shows its photos and text with an empty window list (owner 2026-11-11: photos were enlarged
// for ~1 s while a transient window was listed, then shrank back), or a paused menu (PausedMenu below): the in-game
// briefing (key B) shows its photos over the mission scene with an empty list too (owner 2026-11-11, both builds).
bool ProbeSaysMenu(int listCount, bool mapOpen, bool noSceneCamera, bool pausedMenu = false);

// "Paused menu": the game is paused and a window other than the in-game map was open at some point of this pause.
// The in-game briefing (key B) opens a window, then empties the list while a photo is shown; the weapon/inventory
// menu pauses the game with an empty list throughout and must not be scaled (0.8.1 regression: the weapon model was
// drawn 2.25x, owner 2026-11-12, Steam log frames 12453-13442).
struct PausedMenu {
    bool active = false;
    bool Update(bool paused, int listCount, bool mapOpen) {
        if (!paused) active = false;
        else if (listCount > 0 && !mapOpen) active = true;
        return active;
    }
};

// ZEngineDataBase::IsPaused (Steam 0x4014C0, retail 0x4015C0, same code): byte [db+0xB9C], db = [g_pSysInterface+0x98]
// (main loop 0x4169E4 reads it the same way). CONFIRMED static, both builds.
constexpr uint32_t kSysEngineDb = 0x98, kEngineDbPaused = 0xB9C;

// True while menuscale runs (fix enabled, ZWINDOWS probe active) and the last frame was a menu frame. Used by the
// settings page for its "F10" hint.
bool MenuScaleMenuOpen();

// Frame bookkeeping: menu mode = no scene camera in the last `kSceneGraceFrames` frames.
class MenuDetector {
public:
    static constexpr uint32_t kSceneGraceFrames = 3;
    void SceneCameraSeen() { lastScene_ = frame_; seenAny_ = true; }
    void EndFrame() { ++frame_; }
    bool InMenu() const { return !seenAny_ || frame_ - lastScene_ > kSceneGraceFrames; }

private:
    uint32_t frame_ = 0, lastScene_ = 0;
    bool seenAny_ = false;
};

}  // namespace h2e
