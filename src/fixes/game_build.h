// Known builds of the game files the fixes patch or read (docs/EXECUTABLE_ANALYSIS.md "Binary identity").
//
// Every engine address a fix uses depends on the exact hitman2.exe / RenderD3D.dll. A build is identified by the
// SHA-256 of the file the module was loaded from; an unknown file gets no build and the fixes that need one stay
// off (logged). Supported:
//   hitman2.exe   Steam 1.0.0.277  f85765f1...5136  (Steam DRM wrapper; addresses from the runtime capture, EXP-002)
//                 retail 1.0.0.265 0d7742c8...7560  (no Steam wrapper; addresses ported 2026-11-11 by matching the
//                                                     Steam code with absolute addresses and rel32 masked)
//   RenderD3D.dll Steam            e9aff449...871d
//                 retail 1.0       803c32e4...567a
#pragma once
#include <cstddef>
#include <cstdint>

namespace h2e {

struct ExeBuild {
    const char* label;   // for the log, e.g. "Steam 1.0.0.277"
    const char* sha256;  // of hitman2.exe on disk
    uintptr_t codeEnd;   // end of hitman2.exe engine code (thread inventory of exe_redirect)
    uintptr_t sysInterfacePtr;  // g_pSysInterface
    // timer: `call _ftol` in the frame timer update (context bytes include the original rel32)
    uintptr_t timerContextVa;
    const uint8_t* timerContext;
    size_t timerContextSize, timerCallOffset;
    uintptr_t timerFnLo, timerFnHi;
    // menuscale: `call GetMouseDelta` in the input frame function
    uintptr_t cursorContextVa;
    const uint8_t* cursorContext;
    size_t cursorContextSize, cursorCallOffset;
    uintptr_t mouseDeltaVa, inputFnLo, inputFnHi;
    // ZWINDOWS window manager and the in-game map window (object layouts are the same in both builds)
    uint32_t zwindowsVtable, ingameMapVtable;
};

struct RenderBuild {
    const char* label;
    const char* sha256;           // of RenderD3D.dll on disk
    uint32_t snapBranchRva;       // `jz` skipping the fullscreen width snapping (fix resolution)
    uint32_t sceneProjectionRet;  // return address RVA of SetTransform(PROJECTION) for cameras (HUD/menus/scenes)
};

extern const ExeBuild kExeBuilds[];
extern const size_t kExeBuildCount;
extern const RenderBuild kRenderBuilds[];
extern const size_t kRenderBuildCount;

const ExeBuild* FindExeBuild(const char* sha256);
const RenderBuild* FindRenderBuild(const char* sha256);

// The running hitman2.exe / the loaded RenderD3D.dll (file hashed once, then cached). Null if unknown, not loaded,
// or (exe) not mapped at its preferred base 0x400000.
const ExeBuild* RunningExeBuild();
const RenderBuild* RunningRenderBuild();

// True if `returnAddress` is RenderD3D's camera SetTransform(PROJECTION) call site of a known build.
bool IsCameraProjectionCall(uintptr_t renderD3DBase, uintptr_t returnAddress);

}  // namespace h2e
