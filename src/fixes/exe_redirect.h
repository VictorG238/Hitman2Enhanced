// Verified redirect of one `call rel32` inside hitman2.exe (pinned build), shared by the fixes that hook engine code
// (timer, menuscale). Must be called from Fix::onDirect3DCreate: the renderer is being set up on the engine thread,
// so the engine's main loop is not running.
#pragma once
#include <cstddef>
#include <cstdint>

#include "fix.h"

namespace h2e {

struct ExeCallSite {
    const char* fix;                // log prefix, e.g. "timer"
    uintptr_t contextVa;            // verified context (must contain the call)
    const uint8_t* context;         // expected bytes, including the original rel32
    size_t contextSize;
    size_t callOffset;              // offset of the E8 opcode inside the context (call must be 8-byte aligned)
    const uint8_t* targetPrologue;  // expected first bytes of the original call target
    size_t targetPrologueSize;
    uintptr_t functionLo, functionHi;  // enclosing engine function, reported in the thread inventory
    const char* functionName;          // e.g. "timer function 0x4179F0"
    const char* stubName;              // e.g. "the accumulator stub"
};

// Checks, in order: the caller is RenderD3D.dll, hitman2.exe is mapped at 0x400000 with the pinned SHA-256
// (kHitman2Sha256), the context lies in committed executable image memory of hitman2.exe; pins the proxy DLL;
// freezes every other thread (refusing if one executes the context); then RedirectCall. Every outcome is logged;
// any failure leaves the game untouched. True if the call now goes to `stub`.
bool RedirectExeCall(const FixContext& ctx, uintptr_t caller, const ExeCallSite& site, const void* stub);

}  // namespace h2e
