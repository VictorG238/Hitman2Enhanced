#include "horplus.h"

#include "fix.h"

namespace h2e {

bool ApplyHorPlus(d3d8::Matrix& m, const d3d8::Viewport& vp, unsigned bbWidth, unsigned bbHeight) {
    if (bbWidth == 0 || bbHeight == 0 || vp.X != 0 || vp.Width != bbWidth) return false;
    float(*a)[4] = m.m;
    // centred LH perspective: _34 == 1, _44 == 0, no off-centre terms
    if (a[2][3] != 1.0f || a[3][3] != 0.0f || a[0][0] <= 0.0f || a[1][1] <= 0.0f || a[2][0] != 0.0f ||
        a[2][1] != 0.0f)
        return false;
    const double zn = -double(a[3][2]) / a[2][2];
    const double zf = double(a[2][2]) * zn / (double(a[2][2]) - 1.0);
    if (!(zf > kMinSceneFarPlane)) return false;
    const double bbAspect = double(bbWidth) / bbHeight;
    if (bbAspect <= 4.0 / 3.0 + 1e-6) return false;
    const float k = float((4.0 / 3.0) / bbAspect);
    a[0][0] *= k;
    a[1][1] *= k;
    return true;
}

namespace {

bool OnProjection(const FixContext& ctx, uintptr_t returnAddress, d3d8::Matrix& m, const d3d8::Viewport& vp,
                  unsigned bbWidth, unsigned bbHeight) {
    if (!ctx.renderD3DBase || returnAddress != ctx.renderD3DBase + kSceneProjectionRetRva) return false;
    return ApplyHorPlus(m, vp, bbWidth, bbHeight);
}

}  // namespace

extern const Fix kHorPlusFix = {"horplus", "EXPERIMENTAL Hor+ FOV by projection rescaling - moves the HUD (run-008)",
                                nullptr, OnProjection};

}  // namespace h2e
