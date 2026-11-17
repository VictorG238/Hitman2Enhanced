#include "keyhole.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "exe_redirect.h"
#include "timer.h"
#include "../common/sha256.h"
#include "game_build.h"
#include "menuscale.h"

namespace h2e {
bool IsKeyholeSceneViewport(void* device, const d3d8::Viewport& vp, unsigned width, unsigned height) {
    if (!device || width < 100 || height < 100 ||
        std::fabs(double(vp.X) - width * .35) > 2 ||
        std::fabs(double(vp.Width) - width * .30) > 2 ||
        vp.X >= width || vp.Width > width - vp.X || vp.Y >= height || vp.Height > height - vp.Y) return false;
    const bool expanded = vp.Y == 0 && vp.Height == height;
    // Still collect collision dimensions if the height patch could not apply.
    const bool original = std::fabs(double(vp.Y) - height * .15) <= 2 &&
        std::fabs(double(vp.Height) - height * .85) <= 2;
    if (!expanded && !original) return false;
    auto** v = *static_cast<void***>(device);
    using GetBack = HRESULT(WINAPI*)(void*, UINT, DWORD, void**);
    using GetTarget = HRESULT(WINAPI*)(void*, void**);
    using Release = ULONG(WINAPI*)(void*);
    void* back = nullptr;
    void* target = nullptr;
    const HRESULT backHr = reinterpret_cast<GetBack>(v[16])(device, 0, 0, &back);
    const HRESULT targetHr = reinterpret_cast<GetTarget>(v[32])(device, &target);
    const bool isBack = SUCCEEDED(backHr) && SUCCEEDED(targetHr) && back && back == target;
    if (back) reinterpret_cast<Release>((*static_cast<void***>(back))[2])(back);
    if (target) reinterpret_cast<Release>((*static_cast<void***>(target))[2])(target);
    return isBack;
}

void KeyholeLook::Add(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    // Engine scale is pi/14 radians per unit; 49/18 units permit about 35 degrees.
    // Extend mouse travel without changing sensitivity or the controller path.
    constexpr float limit = 49.0f / 18.0f;
    axes[1] = std::clamp(axes[1] + dx * 0.01f, -limit, limit);
    axes[3] = std::clamp(axes[3] + dy * 0.01f, -limit, limit);
}

float KeyholeSafeFraction(const KeyholeLook& current, const KeyholeLook& desired,
                          bool (*blocked)(void*, float), void* context) {
    const float dx = desired.axes[1] - current.axes[1];
    const float dy = desired.axes[3] - current.axes[3];
    const float distance = std::hypot(dx, dy);
    if (!std::isfinite(distance) || distance == 0) return 0;
    // Test roughly every four degrees for large mouse movements, then refine
    // the first blocked interval. Ordinary movements use only one sample.
    const int steps = static_cast<int>(std::fmin(25.0f, std::ceil(distance / (4.0f * 14 / 180))));
    float safe = 0;
    bool retreatingFromOverlap = false;
    for (int i = 1; i <= steps; ++i) {
        float test = float(i) / steps;
        if (!blocked(context, test)) {
            safe = test;
            retreatingFromOverlap = false;
            continue;
        }
        // If geometry moved into the last good pose, allow retreat as long as
        // the motion continues toward the center over the entire sample.
        const float x = current.axes[1] + dx * test, y = current.axes[3] + dy * test;
        if (i == 1 && x * dx + y * dy < 0) retreatingFromOverlap = blocked(context, 0);
        if (retreatingFromOverlap && x * dx + y * dy < 0) {
            safe = test;
            continue;
        }
        for (int j = 0; j < 4; ++j) {
            const float mid = (safe + test) * .5f;
            if (blocked(context, mid)) test = mid;
            else safe = mid;
        }
        return safe;
    }
    return 1;
}

KeyholeLook SlideKeyholeLook(const KeyholeLook& current, const KeyholeLook& desired,
                            bool (*blocked)(void*, const KeyholeLook&), void* context) {
    struct Sweep {
        const KeyholeLook& from;
        const KeyholeLook& to;
        bool (*blocked)(void*, const KeyholeLook&);
        void* context;

        KeyholeLook Pose(float fraction) const {
            if (fraction == 1) return to;
            auto pose = from;
            for (int axis : {1, 3})
                pose.axes[axis] += (to.axes[axis] - from.axes[axis]) * fraction;
            return pose;
        }
    };
    const auto sweepTo = [&](const KeyholeLook& from, const KeyholeLook& to) {
        Sweep sweep{from, to, blocked, context};
        const float fraction = KeyholeSafeFraction(from, to, [](void* data, float fraction) {
            const auto& path = *static_cast<const Sweep*>(data);
            return path.blocked(path.context, path.Pose(fraction));
        }, &sweep);
        return sweep.Pose(fraction);
    };

    auto result = sweepTo(current, desired);
    if (result.axes[1] != desired.axes[1] && result.axes[3] != desired.axes[3]) {
        // Both axes still have input left after the diagonal was stopped.
        for (int axis : {3, 1}) {
            auto target = result;
            target.axes[axis] = desired.axes[axis];
            result = sweepTo(result, target);
        }
    }

    // The previous pose can overlap a nearby edge after the projection or
    // geometry changes. Try a small inward correction only if it clears
    // the edge and lets the requested single-axis movement resume.
    const bool yawOnly = current.axes[3] == desired.axes[3] && current.axes[1] != desired.axes[1];
    const bool pitchOnly = current.axes[1] == desired.axes[1] && current.axes[3] != desired.axes[3];
    if (yawOnly || pitchOnly) {
        const int axis = yawOnly ? 1 : 3, other = yawOnly ? 3 : 1;
        const float angle = current.axes[other];
        if (result.axes[axis] == current.axes[axis] && angle != 0 && blocked(context, current)) {
            auto clear = current;
            constexpr float oneDegree = 14.0f / 180;
            clear.axes[other] -= std::copysign(std::fmin(std::fabs(angle), oneDegree), angle);
            if (!blocked(context, clear)) {
                auto target = clear;
                target.axes[axis] = desired.axes[axis];
                const auto slide = sweepTo(clear, target);
                if (slide.axes[axis] != current.axes[axis]) return slide;
            }
        }
    }
    return result;
}

namespace {
struct KeyholeEngine {
    uintptr_t dispatcher, context, dispatcherEnd, update, suffix, viewport;
    uintptr_t queryBase, rotate, lookBasis;
    const char* updateHash;
    const char* queryHash;
    const char* rotateHash;
    const char* basisHash;
};
constexpr KeyholeEngine kSteam{0x4eeca0, 0x4eed48, 0x4eed7d, 0x4eee80, 0x4eeebd, 0x4ef520,
    0x5ca2c0, 0x5c2380, 0x5c2d60,
    "8b6d171ef716653b2304f7eb3c71fece63d43a35ce954edd23de75dcf0b5bd81",
    "300df860adb8c298c1193eea1e855a67993eae685e5b944379c1b889e9b0fc6e",
    "0704e9ce224a9517fc1094f0e2467085fee210f1f3bdd71299b47ba034a7e105",
    "d80d6c87215142101aa27db132377d266b13ee1db3fefcf4e3a791c9e1c39051"};
constexpr KeyholeEngine kRetail{0x4ed360, 0x4ed408, 0x4ed43d, 0x4ed540, 0x4ed57d, 0x4edbe0,
    0x5c67c0, 0x5be870, 0x5bf250,
    "ed1042a210c8c3ce877e2ff0ba3e237f88e6fefaa45ea55bd9588d30af8960f1",
    "6fa199868d4ff1da4627eadca955ee5e22b9fc9dd18e1e44e07c814ad54b3197",
    "d2117dc5e64f4348f05dd9da1e8dac11383932fc141b70c6de5e6ecb783fecfe",
    "fa6fc3c63492c718889d69e1366d08457c7398fe8c90553eba18e35b53e2341f"};
const KeyholeEngine* keyholeEngine = nullptr;
uintptr_t sysInterfacePtr = 0, bridgeSuffix = 0;
KeyholeLook look;
uint64_t frame = 0, lastFrame = 0;
const void* lastSelf = nullptr;
uint32_t lastDoor = 0;
LogFn logger = nullptr;
bool viewedThisFrame = false, viewportLogged = false;
bool collisionReady = false, collisionLogged = false;

struct CollisionProjection {
    float nearDepth = 0, halfWidth = 0, halfHeight = 0;
    uint64_t frame = 0;
} collisionProjection;

template<class T> T Read(const void* p, size_t off) {
    T value;
    std::memcpy(&value, static_cast<const uint8_t*>(p) + off, sizeof value);
    return value;
}

struct Vec3 { float x, y, z; };
using Basis = Vec3[3];

// The original camera uses this query to gather nearby collision
// triangles once, then intersect several short segments against them.
struct CameraCollisionQuery {
    explicit CameraCollisionQuery(void* world) {
        using Init = void*(__thiscall*)(void*, void*, int, int, int);
        reinterpret_cast<Init>(keyholeEngine->queryBase + 0x1f0)(state, world, 0, 1, 1);
    }
    ~CameraCollisionQuery() {
        reinterpret_cast<void(__thiscall*)(void*)>(keyholeEngine->queryBase + 0x2a0)(state);
    }
    CameraCollisionQuery(const CameraCollisionQuery&) = delete;
    CameraCollisionQuery& operator=(const CameraCollisionQuery&) = delete;

    void Gather(const Vec3& origin, float radius) {
        const Basis identity{{1,0,0}, {0,1,0}, {0,0,1}};
        const Vec3 extents{radius, radius, radius};
        using GatherFn = void(__thiscall*)(void*, const Vec3*, const Basis*, const Vec3*, int);
        if (Read<void*>(state, 0x14))
            reinterpret_cast<GatherFn>(keyholeEngine->queryBase + 0x2b0)(state, &origin, &identity, &extents, 1);
    }
    bool Hit(const Vec3& origin, const Vec3& segment) {
        using Trace = bool(__thiscall*)(void*, const Vec3*, const Vec3*, int);
        return reinterpret_cast<Trace>(keyholeEngine->queryBase + 0x530)(state, &origin, &segment, 1);
    }
    // Stack reservation observed in the original camera caller (EXP-007).
    alignas(4) uint8_t state[0x130]{};
};

struct KeyholeSweep {
    CameraCollisionQuery& query;
    const Vec3 origin;
    const Basis& door;
    const CollisionProjection& projection;

    bool Blocked(const KeyholeLook& pose) {
        constexpr float angleScale = 0.2243995f;
        const Vec3 angles{
            pose.axes[3] * angleScale, pose.axes[1] * angleScale, 0};
        Vec3 direction{0, 0, 1};
        reinterpret_cast<void(__fastcall*)(Vec3*, const Vec3*)>(keyholeEngine->rotate)(&direction, &angles);
        const Vec3 f{
            direction.x * door[2].x + direction.y * door[1].x + direction.z * door[0].x,
            direction.x * door[2].y + direction.y * door[1].y + direction.z * door[0].y,
            direction.x * door[2].z + direction.y * door[1].z + direction.z * door[0].z};
        Basis view{};
        reinterpret_cast<void(__fastcall*)(Basis*, const Vec3*, const Vec3*)>(keyholeEngine->lookBasis)(&view, &f, nullptr);
        const auto& forward = view[0];
        const auto& up = view[1];
        const auto& right = view[2];
        // Project the center and four near-plane corners a small distance
        // beyond the clip plane. Longer sight rays would block normal peeks.
        constexpr float offsets[][2] = {{0,0}, {-1,-1}, {-1,1}, {1,-1}, {1,1}};
        for (const auto& offset : offsets) {
            const float x = projection.halfWidth * offset[0], y = projection.halfHeight * offset[1];
            const float z = projection.nearDepth + .5f;
            const Vec3 segment{
                forward.x*z + right.x*x + up.x*y,
                forward.y*z + right.y*x + up.y*y,
                forward.z*z + right.z*x + up.z*y};
            if (query.Hit(origin, segment)) return true;
        }
        return false;
    }
};

void LimitByGeometry(const void* self, const void* sys, KeyholeLook& desired) {
    if (!collisionReady || frame - collisionProjection.frame != 1 || collisionProjection.nearDepth <= 0 ||
        (desired.axes[1] == look.axes[1] && desired.axes[3] == look.axes[3])) return;
    const auto engine = Read<const void*>(sys, 0x98);
    if (!engine) return;
    void* world = Read<void*>(engine, 0xad8);
    if (!world) return;
    const Vec3 origin = Read<Vec3>(self, 0x116);
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return;
    Basis door{};
    std::memcpy(door, static_cast<const uint8_t*>(self) + 0xf2, sizeof door);
    CameraCollisionQuery query(world);
    const auto& p = collisionProjection;
    query.Gather(origin, p.nearDepth + p.halfWidth + p.halfHeight + 1.0f);
    KeyholeSweep sweep{query, origin, door, p};
    const auto clamped = SlideKeyholeLook(look, desired, [](void* context, const KeyholeLook& pose) {
        return static_cast<KeyholeSweep*>(context)->Blocked(pose);
    }, &sweep);
    if ((clamped.axes[1] != desired.axes[1] || clamped.axes[3] != desired.axes[3]) && !collisionLogged && logger) {
        logger("keyhole: nearby geometry limited camera movement");
        collisionLogged = true;
    }
    desired = clamped;
}

}  // namespace

// EXP-007. Both verified builds use this stack layout. The thunk recreates the
// original update's stack frame, then enters its existing rotation/exit code.
// ECX = scope object, EDX = four floats laid out like the controller state.
__declspec(naked) void __fastcall ApplyMouseLook(void*, const float*) {
    __asm {
        sub esp, 44h
        push esi
        mov esi, ecx
        mov eax, edx
        mov dword ptr [esp+0ch], 0
        mov dword ptr [esp+10h], 0
        mov dword ptr [esp+14h], 3f800000h
        mov edx, bridgeSuffix
        jmp edx
    }
}

namespace {
void __fastcall Update(void* self, void*) {
    viewedThisFrame = true;
    const auto sys = *reinterpret_cast<uint8_t**>(sysInterfacePtr);
    if (sys[0x60]) {
        lastSelf = nullptr;
        reinterpret_cast<void(__fastcall*)(void*, void*)>(keyholeEngine->update)(self, nullptr);
        return;
    }
    const uint32_t door = Read<uint32_t>(self, 0x86);
    const bool entering = self != lastSelf || door != lastDoor || frame - lastFrame > 1;
    if (entering) {
        look.Reset();
        collisionProjection.nearDepth = 0;
        collisionLogged = false;
        if (logger) logger("keyhole: mouse view entered (door 0x%08X)", door);
    }
    lastSelf = self;
    lastDoor = door;
    lastFrame = frame;
    // Same per-frame mouse counts populated by WintelMouse::Update for normal
    // gameplay. Skip the entry frame to avoid carrying the interaction movement.
    if (!entering) {
        KeyholeLook desired = look;
        desired.Add(Read<float>(sys, 0x7f0), Read<float>(sys, 0x7f4));
        LimitByGeometry(self, sys, desired);
        look = desired;
    }
    ApplyMouseLook(self, look.axes);
    if (!Read<uint8_t>(self, 0x1a9) || Read<uint8_t>(self, 0x1b1)) {
        lastSelf = nullptr;
        look.Reset();
        if (logger) logger("keyhole: mouse view exited");
    }
}

template<size_t Size>
bool EngineBytesMatch(uintptr_t address, const char* expectedHash) {
    uint8_t original[Size];
    SIZE_T read = 0;
    char hash[65];
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                           original, sizeof original, &read) || read != sizeof original) return false;
    Sha256Hex(original, sizeof original, hash);
    return std::strcmp(hash, expectedHash) == 0;
}

void ExpandKeyholeViewport(LogFn log) {
    // EXP-007. The mode-3 setup passes {.35,0,.30,.85} to the
    // camera's viewport setter. Change the height to 1.0 before frustum setup.
    const uintptr_t viewport = keyholeEngine->viewport;
    if (!EngineBytesMatch<0x52>(viewport, "f808853278d57a6a75e1a155f45a950034f5fe040119e0022f07439daa2c8940")) {
        log("keyhole: viewport setup bytes differ - height patch not applied");
        return;
    }
    ThreadFreeze freeze;
    char why[160]{};
    if (!freeze.Freeze(viewport, viewport + 0x134, why, sizeof why)) {
        log("keyhole: viewport height patch refused: %s", why);
        return;
    }
    // .85f is 9a 99 59 3f, 1.0f is 00 00 80 3f. Only the first
    // three bytes change, all inside the same aligned dword.
    auto* word = reinterpret_cast<volatile LONG*>(viewport + 0x44);
    auto* bytes = const_cast<LONG*>(word);
    DWORD oldProtect = 0;
    bool applied = false;
    if (VirtualProtect(bytes, sizeof(LONG), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        constexpr LONG original = 0x59999a18;
        constexpr LONG expanded = static_cast<LONG>(0x80000018u);
        applied = InterlockedCompareExchange(word, expanded, original) == original;
        DWORD ignored = 0;
        VirtualProtect(bytes, sizeof(LONG), oldProtect, &ignored);
        if (applied) FlushInstructionCache(GetCurrentProcess(), bytes, sizeof(LONG));
    }
    freeze.Resume();
    log("keyhole: full-height scene viewport %s", applied ? "enabled" : "not applied");
}

void OnCreate(const FixContext& ctx, uintptr_t caller) {
    const auto build = RunningExeBuild();
    if (!build) return;
    if (std::strcmp(build->sha256, kHitman2Sha256) == 0) keyholeEngine = &kSteam;
    else if (std::strcmp(build->sha256, kRetailExeSha256) == 0) keyholeEngine = &kRetail;
    else return;
    // Validate the entire engine suffix used by the bridge, not just its entry.
    if (!EngineBytesMatch<0x133>(keyholeEngine->update, keyholeEngine->updateHash)) {
        ctx.log("keyhole: engine update bytes differ - not applied");
        return;
    }
    // Tail jump starts at qword+1, so the existing atomic rel32 redirect fits.
    static constexpr uint8_t context[] = {0x5e,0xe9,0x32,0x01,0x00,0x00,0x83,0xf8};
    uint8_t prologue[] = {0xa1,0,0,0,0,0x83,0xec,0x44,0x56,0x8b,0xf1,0x8a,0x48,0x60,0x84,0xc9};
    sysInterfacePtr = build->sysInterfacePtr;
    bridgeSuffix = keyholeEngine->suffix;
    std::memcpy(prologue + 1, &sysInterfacePtr, sizeof sysInterfacePtr);
    const ExeCallSite site = {"keyhole", build->sha256, keyholeEngine->context, context, sizeof context, 1,
        prologue, sizeof prologue, keyholeEngine->dispatcher, keyholeEngine->dispatcherEnd,
        "scope update", "mouse keyhole update"};
    if (!RedirectExeCall(ctx, caller, site, reinterpret_cast<const void*>(&Update))) return;
    logger = ctx.log;
    ExpandKeyholeViewport(logger);
    collisionReady =
        EngineBytesMatch<0xa1e>(keyholeEngine->queryBase, keyholeEngine->queryHash) &&
        EngineBytesMatch<0xbb>(keyholeEngine->rotate, keyholeEngine->rotateHash) &&
        EngineBytesMatch<0x390>(keyholeEngine->lookBasis, keyholeEngine->basisHash);
    ctx.log("keyhole: geometry limit %s", collisionReady ? "enabled" : "unavailable (unexpected engine code)");
}
bool OnProjection(const FixContext& ctx, uintptr_t caller, d3d8::Matrix& m,
                  const d3d8::Viewport& vp, unsigned width, unsigned height) {
    if (!viewedThisFrame || (collisionProjection.nearDepth > 0 && collisionProjection.frame == frame) ||
        !IsCameraProjectionCall(ctx.renderD3DBase, caller) || !IsSceneCamera(m) ||
        !IsKeyholeSceneViewport(ctx.device, vp, width, height)) return false;
    if (collisionReady) {
        const float depth = -m.m[3][2] / m.m[2][2];
        const float w = depth / m.m[0][0], h = depth / m.m[1][1];
        if (depth > 0 && depth <= 50 && w > 0 && w <= 100 && h > 0 && h <= 100)
            collisionProjection = {depth, w, h, frame};
    }
    if (!viewportLogged) {
        ctx.log("keyhole: scene viewport %lu,%lu %lux%lu", vp.X, vp.Y, vp.Width, vp.Height);
        viewportLogged = true;
    }
    return false;  // projection unchanged
}
void OnPresent(const FixContext&) {
    ++frame;
    viewedThisFrame = false;
}
}  // namespace

extern const Fix kKeyholeFix = {"keyhole", "EXPERIMENTAL mouse steering through door keyholes (EXP-007)",
    nullptr, OnProjection, OnCreate, nullptr, OnPresent};
}  // namespace h2e
