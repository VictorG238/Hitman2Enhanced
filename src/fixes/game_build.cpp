#include "game_build.h"

#include <windows.h>

#include <cstring>

#include "../common/code_patch.h"  // ModuleFileSha256
#include "menuscale.h"             // Steam cursor constants
#include "resolution.h"            // kRenderD3DSha256
#include "timer.h"                 // Steam timer constants, kHitman2Sha256, kHitman2Base
#include "zwindows.h"              // Steam vtables

namespace h2e {

namespace {

// Retail 1.0.0.265: the same instruction sequences as the Steam build (time scale field 0x18B8 instead of 0x193C,
// other rel32/absolute operands). Found with masked-signature matching, then read back from the file.
constexpr uint8_t kTimerContext265[30] = {
    0xD8, 0x8E, 0xB8, 0x18, 0x00, 0x00,  // fmul dword [esi+18B8h]  (time scale)
    0xD9, 0x05, 0x10, 0x5C, 0x63, 0x00,  // fld  dword [635C10h]     (1024.0f)
    0xD8, 0xC9,                          // fmul st, st(1)
    0xE8, 0xD5, 0x59, 0x18, 0x00,        // call _ftol (59DD9Ch)     at 0x4183C2
    0x8B, 0x4E, 0x28,                    // mov  ecx, [esi+28h]
    0x03, 0xC8,                          // add  ecx, eax
    0x89, 0x4E, 0x28,                    // mov  [esi+28h], ecx
    0xD9, 0x5E, 0x2C};                   // fstp dword [esi+2Ch]
// Identical bytes to the Steam context: the call target 0x40FC40 is the same distance away.
constexpr uint8_t kCursorContext265[34] = {0x6A, 0x00, 0x8B, 0xCE, 0xE8, 0x9B, 0x01, 0x00, 0x00, 0x85, 0xC0, 0x0F,
                                           0x84, 0xC6, 0x00, 0x00, 0x00, 0xD9, 0x46, 0x18, 0xD8, 0x00, 0xD9, 0x5E,
                                           0x18, 0xD9, 0x40, 0x04, 0xD8, 0x46, 0x1C, 0xD9, 0x5E, 0x1C};

}  // namespace

const ExeBuild kExeBuilds[] = {
    {"Steam 1.0.0.277", kHitman2Sha256, 0x639000, kSysInterfacePtr,
     kHitman2Base + kTimerContextRva, kTimerContext, sizeof kTimerContext, kTimerCallOffset, 0x4179F0, 0x417C60,
     kCursorContextVa, kCursorContext, sizeof kCursorContext, kCursorCallOffset, kMouseDeltaVa, kInputFrameVa,
     kInputFrameEnd, kZWindowsVtable, kIngameMapVtable},
    {"retail 1.0.0.265", "0d7742c85e328886e91152036ae6615ca4c98bbb97c7a36d8f9e150235e57560", 0x635000, 0x69F054,
     0x4183B4, kTimerContext265, sizeof kTimerContext265, 14, 0x418250, 0x4184F0,
     0x40FA9C, kCursorContext265, sizeof kCursorContext265, 4, 0x40FC40, 0x40FA30, 0x40FB76,
     0x646928, 0x651DD8},
};
const size_t kExeBuildCount = sizeof kExeBuilds / sizeof kExeBuilds[0];

const RenderBuild kRenderBuilds[] = {
    {"Steam", kRenderD3DSha256, 0x197F7, 0x33DD},
    {"retail 1.0", "803c32e44e9441e2a430356f1fad8dd043dec67f81f4d6ba6070be0f52c7567a", 0x18767, 0x349D},
};
const size_t kRenderBuildCount = sizeof kRenderBuilds / sizeof kRenderBuilds[0];

const ExeBuild* FindExeBuild(const char* sha256) {
    for (size_t i = 0; i < kExeBuildCount; ++i)
        if (std::strcmp(kExeBuilds[i].sha256, sha256) == 0) return &kExeBuilds[i];
    return nullptr;
}

const RenderBuild* FindRenderBuild(const char* sha256) {
    for (size_t i = 0; i < kRenderBuildCount; ++i)
        if (std::strcmp(kRenderBuilds[i].sha256, sha256) == 0) return &kRenderBuilds[i];
    return nullptr;
}

const ExeBuild* RunningExeBuild() {
    static bool s_done = false;
    static const ExeBuild* s_build = nullptr;
    if (!s_done) {
        s_done = true;
        HMODULE exe = GetModuleHandleA(nullptr);
        char hex[65] = "";
        if (reinterpret_cast<uintptr_t>(exe) == kHitman2Base && ModuleFileSha256(exe, hex)) s_build = FindExeBuild(hex);
    }
    return s_build;
}

const RenderBuild* RunningRenderBuild() {
    static bool s_done = false;
    static const RenderBuild* s_build = nullptr;
    if (!s_done) {
        HMODULE rd = GetModuleHandleA("RenderD3D.dll");
        if (!rd) return nullptr;  // not loaded yet: try again later
        s_done = true;
        char hex[65] = "";
        if (ModuleFileSha256(rd, hex)) s_build = FindRenderBuild(hex);
    }
    return s_build;
}

bool IsCameraProjectionCall(uintptr_t renderD3DBase, uintptr_t returnAddress) {
    const RenderBuild* b = renderD3DBase ? RunningRenderBuild() : nullptr;
    return b && returnAddress == renderD3DBase + b->sceneProjectionRet;
}

}  // namespace h2e
