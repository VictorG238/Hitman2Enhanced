#include "axebreak.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "exe_redirect.h"
#include "game_build.h"
#include "timer.h"

namespace h2e {

AxeBreakScene GetAxeBreakScene(std::string_view scene) {
    const auto separator = scene.find_last_of("/\\");
    if (separator != std::string_view::npos) scene.remove_prefix(separator + 1);
    const auto extension = scene.rfind('.');
    if (extension != std::string_view::npos) scene = scene.substr(0, extension);
    constexpr auto equals = [](std::string_view name, std::string_view expected) {
        if (name.size() != expected.size()) return false;
        for (size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            if ((c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c) != expected[i]) return false;
        }
        return true;
    };
    if (equals(scene, "C4-1__MAIN")) return AxeBreakScene::Basement;
    if (equals(scene, "C4-2__MAIN")) return AxeBreakScene::Graveyard;
    return AxeBreakScene::Other;
}

const char* AxeBreakTarget(AxeBreakScene scene, uint32_t model, uint32_t sceneTag) {
    // These pairs are unique in both verified mission versions (EXP-006).
    if (scene == AxeBreakScene::Basement && model == 0x6bb8fe && sceneTag == 0x1438)
        return "Screen_SurvCam_01";
    if (scene != AxeBreakScene::Graveyard) return nullptr;
    if (model == 0x754d4 && sceneTag == 0x392) return "ENABLED_Win_01";
    if (model == 0x7ddf4 && sceneTag == 0x396) return "ENABLED_Win_02";
    if (model == 0x7dca4 && sceneTag == 0x394) return "ENABLED_Win_03";
    return nullptr;
}

std::array<AxePoint, 7> AxeSweepOffsets(AxePoint start, AxePoint end, AxePoint actor, float radius) {
    // The unshifted sample keeps the original contact. The remaining samples
    // cover the sides and the near edge of the native sweep radius.
    const float x = (start.x + end.x) * 0.5f - actor.x;
    const float z = (start.z + end.z) * 0.5f - actor.z;
    const float distance = std::hypot(x, z);
    if (!(distance > 0) || !std::isfinite(distance) || !(radius > 0) || !std::isfinite(radius)) return {};

    const float nearX = -x / distance * radius;
    const float nearZ = -z / distance * radius;
    const float sideX = -nearZ * 0.5f;
    const float sideZ = nearX * 0.5f;
    const float halfNearX = nearX * 0.5f;
    const float halfNearZ = nearZ * 0.5f;
    return {{{}, {sideX, 0, sideZ}, {-sideX, 0, -sideZ}, {halfNearX, 0, halfNearZ},
             {halfNearX + sideX, 0, halfNearZ + sideZ}, {halfNearX - sideX, 0, halfNearZ - sideZ},
             {nearX, 0, nearZ}}};
}

std::array<AxeSegment, 15> AxeMonitorSegments(AxePoint start, AxePoint end, AxePoint actor, AxePoint nearOffset) {
    std::array<AxeSegment, 15> segments{};
    const float radius = std::hypot(nearOffset.x, nearOffset.z);
    if (!(radius > 0) || !std::isfinite(radius)) return segments;
    const float forwardX = -nearOffset.x / radius;
    const float forwardZ = -nearOffset.z / radius;
    const float startDepth = (start.x - actor.x) * forwardX + (start.z - actor.z) * forwardZ;
    const float endDepth = (end.x - actor.x) * forwardX + (end.z - actor.z) * forwardZ;
    const float nearDepth = (std::max)(0.0f, (std::min)(startDepth, endDepth) - radius);
    const float farDepth = (std::max)(startDepth, endDepth);
    const float reach = farDepth - nearDepth;
    if (!(reach > 0) || !std::isfinite(reach)) return segments;

    // Sample separate heights so depth and aim do not determine which
    // height of the small monitor the diagonal stroke can intersect.
    constexpr float heights[] = {0.5f, 1.0f / 3, 2.0f / 3, 1.0f / 6, 5.0f / 6};
    constexpr float lanes[] = {0.0f, -0.5f, 0.5f};
    size_t index = 0;
    for (const float height : heights) {
        for (const float lane : lanes) {
            const float side = lane * radius;
            segments[index++] = {{actor.x + forwardX * nearDepth - forwardZ * side,
                                  start.y + (end.y - start.y) * height,
                                  actor.z + forwardZ * nearDepth + forwardX * side},
                                 {forwardX * reach, 0, forwardZ * reach}};
        }
    }
    return segments;
}

namespace {
// Private ABI, checked separately for each executable in EXP-006.
struct AxeEngine {
    uintptr_t actionVtable, hitmanClass, roomVtable;
    uintptr_t sceneName, trace, refToBase, toRef, send, impactMessage;
    uintptr_t sweep, sweepContext, update, updateEnd;
};
constexpr AxeEngine kSteam{0x657528, 0x6add40, 0x643830,
    0x41b880, 0x4547a0, 0x422af0, 0x422ae0, 0x43cbe0, 0x6abaac,
    0x526550, 0x526170, 0x525ef0, 0x526216};
constexpr AxeEngine kRetail{0x6502a8, 0x6a6138, 0x63c5c8,
    0x41c050, 0x4534c0, 0x422030, 0x422020, 0x43b880, 0x6a3ea4,
    0x5248a0, 0x5244c0, 0x524240, 0x524566};
const AxeEngine* engine = nullptr;
uintptr_t sysInterfacePtr = 0;

using Vec3 = AxePoint;
struct SceneTrace {
    Vec3 origin;
    Vec3 displacement;
    uint8_t reserved18[0x28 - 0x18];
    Vec3 contact;
    float fractionLimit;
    uint8_t reserved38[0x44 - 0x38];
    uint32_t geometry;
    uint8_t reserved48[0x6c - 0x48];
    uint8_t reportSurface;
};
static_assert(sizeof(SceneTrace) == 0x70 && offsetof(SceneTrace, contact) == 0x28 &&
              offsetof(SceneTrace, fractionLimit) == 0x34 && offsetof(SceneTrace, geometry) == 0x44 &&
              offsetof(SceneTrace, reportSurface) == 0x6c);

struct ImpactMessage {
    SceneTrace* trace;
    uint32_t projectile;
    uint32_t weapon;
    uint32_t attacker;
    uint32_t attackerRoot;
};
static_assert(sizeof(ImpactMessage) == 20);

template<class T>
T Read(const void* object, size_t offset = 0) {
    T value;
    std::memcpy(&value, static_cast<const uint8_t*>(object) + offset, sizeof value);
    return value;
}

LogFn logger = nullptr;

void TryBreak(const Vec3& start, const Vec3& end, float radius, void* actor) {
    if (!actor || Read<uintptr_t>(actor, 0x0c) != engine->hitmanClass) return;
    const auto sys = *reinterpret_cast<void**>(sysInterfacePtr);
    const auto db = sys ? Read<void*>(sys, 0x98) : nullptr;
    if (!db) return;

    using SceneName = const char*(__thiscall*)(void*);
    const auto name = reinterpret_cast<SceneName>(engine->sceneName)(db);
    const auto scene = name ? GetAxeBreakScene(name) : AxeBreakScene::Other;
    if (scene == AxeBreakScene::Other) return;

    const auto world = Read<void*>(db, 0xad8);
    if (!world || Read<uintptr_t>(world) != engine->roomVtable) return;
    const auto message = *reinterpret_cast<const uint16_t*>(engine->impactMessage);
    if (!message) return;

    const Vec3 d{end.x - start.x, end.y - start.y, end.z - start.z};
    const float lengthSquared = d.x * d.x + d.y * d.y + d.z * d.z;
    if (!(lengthSquared > 0) || !std::isfinite(lengthSquared)) return;

    Vec3 actorPosition{};
    using PointToWorld = void(__thiscall*)(void*, Vec3*);
    const auto actorVtable = Read<const void*>(actor);
    Read<PointToWorld>(actorVtable, 0x88)(actor, &actorPosition);

    using Trace = bool(__thiscall*)(void*, SceneTrace*, int, int, int, int, int);
    using RefToBase = void*(__thiscall*)(void*, uint32_t);
    using ToRef = uint32_t(__thiscall*)(void*, void*);
    using Send = void(__thiscall*)(void*, void*, uint16_t, ImpactMessage*);
    const auto tryImpact = [&](Vec3 origin, Vec3 displacement, const char* sweep, size_t index) {
        SceneTrace trace{};
        trace.origin = origin;
        trace.displacement = displacement;
        trace.fractionLimit = 1.0f;
        trace.reportSurface = 1;
        // Stop at the first collision on this segment, including obstructions.
        if (!reinterpret_cast<Trace>(engine->trace)(world, &trace, 0, 2, -1, 1, 1) || !trace.geometry) return false;

        const auto hit = reinterpret_cast<RefToBase>(engine->refToBase)(db, trace.geometry);
        const auto receiver = hit ? Read<void*>(hit, 0x54) : nullptr;
        const auto base = receiver ? Read<void*>(receiver, 8) : nullptr;
        if (!base) return false;
        const auto target = AxeBreakTarget(scene, Read<uint32_t>(base, 0x68), Read<uint32_t>(base, 0x6c));
        if (!target) return false;

        const uint32_t attacker = reinterpret_cast<ToRef>(engine->toRef)(db, actor);
        if (!attacker) return false;
        // The prop routines consume the native hit record without firearm metadata.
        ImpactMessage payload{&trace, 0, 0, attacker, 0};
        reinterpret_cast<Send>(engine->send)(actor, receiver, message, &payload);
        if (logger) logger("axebreak: sent scene impact to %s (%s sample %u)", target, sweep,
                           static_cast<unsigned>(index));
        return true;
    };

    const auto offsets = AxeSweepOffsets(start, end, actorPosition, radius);
    for (size_t i = 0; i < offsets.size(); ++i) {
        const auto& offset = offsets[i];
        if (i && offset.x == 0 && offset.z == 0) break;
        if (tryImpact({start.x + offset.x, start.y, start.z + offset.z}, d, "sweep", i)) return;
    }
    if (scene != AxeBreakScene::Basement) return;
    const auto monitorSegments = AxeMonitorSegments(start, end, actorPosition, offsets.back());
    for (size_t i = 0; i < monitorSegments.size(); ++i) {
        const auto& segment = monitorSegments[i];
        if (segment.displacement.x == 0 && segment.displacement.z == 0) return;
        if (tryImpact(segment.origin, segment.displacement, "monitor", i)) return;
    }
}

bool __fastcall Sweep(void* action, void*, const Vec3* start, const Vec3* end,
                      float radius, uintptr_t callback, void* actor) {
    using Original = bool(__thiscall*)(void*, const Vec3*, const Vec3*, float, uintptr_t, void*);
    const bool hit = reinterpret_cast<Original>(engine->sweep)(action, start, end, radius, callback, actor);
    // Keep the vanilla character result and animation selection.
    if (!hit && Read<uintptr_t>(action) == engine->actionVtable) TryBreak(*start, *end, radius, actor);
    return hit;
}

void OnCreate(const FixContext& ctx, uintptr_t caller) {
    const auto build = RunningExeBuild();
    if (!build) return;
    if (std::strcmp(build->sha256, kHitman2Sha256) == 0) engine = &kSteam;
    else if (std::strcmp(build->sha256, kRetailExeSha256) == 0) engine = &kRetail;
    else return;
    sysInterfacePtr = build->sysInterfacePtr;
    static constexpr uint8_t context[] = {0x3f, 0xe8, 0xda, 0x03, 0, 0, 0x84, 0xc0};
    static constexpr uint8_t prologue[] = {0x81, 0xec, 0x90, 0x04, 0, 0, 0x53, 0x55, 0x56};
    const ExeCallSite site = {"axebreak", build->sha256, engine->sweepContext, context, sizeof context, 1,
        prologue, sizeof prologue, engine->update, engine->updateEnd, "close-combat update", "axe scene impact"};
    if (RedirectExeCall(ctx, caller, site, reinterpret_cast<const void*>(&Sweep))) logger = ctx.log;
}
}  // namespace

extern const Fix kAxeBreakFix = {"axebreak", "EXPERIMENTAL axe impacts on C4 mission props (EXP-006)",
    nullptr, nullptr, OnCreate};
}  // namespace h2e
