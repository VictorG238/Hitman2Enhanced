#include "resolution.h"

#include "fix.h"
#include "game_build.h"

namespace h2e {
namespace {

// The same 6 bytes in every known RenderD3D build, at a build-specific RVA (game_build.h).
void OnAttach(const FixContext& ctx) {
    const RenderBuild* b = RunningRenderBuild();
    if (!b) {
        ctx.log("resolution: unknown or unloaded RenderD3D.dll - not applied");
        return;
    }
    const BytePatch patch = {"RenderD3D.dll", b->sha256, b->snapBranchRva, kSnapBranchOriginal, kSnapBranchPatched, 6};
    ApplyBytePatch(patch, ctx.log);
}

}  // namespace

extern const Fix kResolutionFix = {"resolution", "fullscreen keeps the configured Resolution (GB-003)", OnAttach,
                                   nullptr};

}  // namespace h2e
