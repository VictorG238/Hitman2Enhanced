#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace h2e {

enum class AxeBreakScene { Other, Basement, Graveyard };

AxeBreakScene GetAxeBreakScene(std::string_view scene);
const char* AxeBreakTarget(AxeBreakScene scene, uint32_t model, uint32_t sceneTag);

struct AxePoint { float x, y, z; };

std::array<AxePoint, 7> AxeSweepOffsets(AxePoint start, AxePoint end, AxePoint actor, float radius);

struct AxeSegment { AxePoint origin, displacement; };

std::array<AxeSegment, 15> AxeMonitorSegments(AxePoint start, AxePoint end, AxePoint actor, AxePoint nearOffset);

}  // namespace h2e
