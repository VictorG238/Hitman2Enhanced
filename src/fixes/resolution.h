// Fix "resolution" (GB-003, EXP-003 run-004/007, docs/FIXES.md).
//
// RenderD3D FUN_100197b0 at RVA 0x197F7 has `jz +0x105` (0F 84 05 01 00 00) that skips the fullscreen width
// snapping only in windowed mode. Replacing it with `jmp +0x106; nop` (E9 06 01 00 00 90) always skips it, so
// fullscreen keeps the configured `Resolution`. Same target (RVA 0x19902). Status: CONFIRMED in a mission.
#pragma once
#include "../common/code_patch.h"

namespace h2e {

constexpr const char* kRenderD3DSha256 = "e9aff44931aa6c361fdcc49d1f5969d19777923c8f6c2c2e18e439bfd53c871d";
constexpr uint8_t kSnapBranchOriginal[6] = {0x0F, 0x84, 0x05, 0x01, 0x00, 0x00};
constexpr uint8_t kSnapBranchPatched[6] = {0xE9, 0x06, 0x01, 0x00, 0x00, 0x90};
constexpr BytePatch kSnapBranchPatch = {"RenderD3D.dll", kRenderD3DSha256, 0x197F7, kSnapBranchOriginal,
                                        kSnapBranchPatched, 6};

}  // namespace h2e
