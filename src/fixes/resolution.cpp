#include "resolution.h"

#include "fix.h"

namespace h2e {
namespace {

void OnAttach(const FixContext& ctx) { ApplyBytePatch(kSnapBranchPatch, ctx.log); }

}  // namespace

extern const Fix kResolutionFix = {"resolution", "fullscreen keeps the configured Resolution (GB-003)", OnAttach,
                                   nullptr};

}  // namespace h2e
