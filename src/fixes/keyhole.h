#pragma once
#include <cstdint>
#include "../common/d3d8_min.h"

namespace h2e {
// Internal x86 bridge; exposed for the isolated ABI harness (EXP-007).
void __fastcall ApplyMouseLook(void* self, const float* axes);
// Recognizes the keyhole scene viewport for collision projection sampling.
bool IsKeyholeSceneViewport(void* device, const d3d8::Viewport& vp, unsigned width, unsigned height);
// Absolute stick deflection relative to the door, accumulated from relative mouse counts.
struct KeyholeLook {
    float axes[4]{};
    void Add(float dx, float dy);
    void Reset() { axes[0] = axes[1] = axes[2] = axes[3] = 0; }
};

// Sweeps between two mouse poses and stops just before the first blocked sample.
// The callback receives the interpolation fraction (0 = current, 1 = requested).
float KeyholeSafeFraction(const KeyholeLook& current, const KeyholeLook& desired,
                          bool (*blocked)(void*, float), void* context);

// Resolves a blocked diagonal along the remaining horizontal and vertical paths.
KeyholeLook SlideKeyholeLook(const KeyholeLook& current, const KeyholeLook& desired,
                            bool (*blocked)(void*, const KeyholeLook&), void* context);
}  // namespace h2e
