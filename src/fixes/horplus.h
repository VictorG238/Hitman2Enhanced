// Fix "horplus" (GB-002, EXP-003 run-005..008, docs/FIXES.md). EXPERIMENTAL - known broken.
//
// Rescales the scene projection so the vertical FOV stays as at 4:3 and the horizontal FOV widens. run-008 showed
// that the HUD and parts of the map are drawn with the gameplay projection itself, so they move towards the screen
// centre too. Kept (off by default) as the base for a redesign at the FOV source.
#pragma once
#include <cstdint>

#include "../common/d3d8_min.h"

namespace h2e {

// Scene projection call site (return address of SetTransform(PROJECTION)), RVA in RenderD3D.
constexpr uint32_t kSceneProjectionRetRva = 0x33DD;

// UI/2D cameras use the same call site with zn 5, zf ~3000 (run-007); every 3D scene/cutscene camera logged so far
// has zf >= 11999. Projections at or below this far plane are left alone. INFERRED from logs.
constexpr double kMinSceneFarPlane = 4000.0;

// For a centred LH perspective with zf > kMinSceneFarPlane on a viewport spanning the full back-buffer width and a
// back buffer wider than 4:3: multiplies _11 and _22 by (4/3) / backBufferAspect. Returns false and leaves `m`
// unchanged otherwise.
bool ApplyHorPlus(d3d8::Matrix& m, const d3d8::Viewport& vp, unsigned bbWidth, unsigned bbHeight);

}  // namespace h2e
