#include "exe_redirect.h"

#include <windows.h>

#include <cstring>

#include "../common/code_patch.h"
#include "game_build.h"
#include "timer.h"  // kHitman2Base

namespace h2e {

namespace {

bool InModule(uintptr_t addr, const char* name) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(addr), &mod))
        return false;
    return mod == GetModuleHandleA(name);
}

}  // namespace

bool RedirectExeCall(const FixContext& ctx, uintptr_t caller, const ExeCallSite& site, const void* stub) {
    const LogFn log = ctx.log;
    const char* fix = site.fix;
    // 1. Initialisation point: the call must come from RenderD3D (engine thread, renderer setup).
    if (!InModule(caller, "RenderD3D.dll")) {
        log("%s: Direct3DCreate8 not called from RenderD3D.dll (caller 0x%08X) - not applied", fix,
            static_cast<unsigned>(caller));
        return false;
    }
    // 2. Binary identity.
    HMODULE exe = GetModuleHandleA(nullptr);
    if (reinterpret_cast<uintptr_t>(exe) != kHitman2Base) {
        log("%s: image base 0x%08X is not 0x400000 - not applied", fix,
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(exe)));
        return false;
    }
    char hex[65] = "";
    const ExeBuild* build = ModuleFileSha256(exe, hex) ? FindExeBuild(hex) : nullptr;
    if (!build || std::strcmp(build->sha256, site.buildSha256) != 0) {
        log("%s: hitman2.exe SHA-256 %s is not a known build - not applied", fix, hex);
        return false;
    }
    // 3. Memory: committed image page of hitman2.exe covering the whole context. Executable in the Steam build; the
    // retail 1.0.0.265 exe has its section headers rewritten to data (READWRITE), so the loader maps the code as
    // WRITECOPY and it runs only because the process has no DEP - accepted too (the context bytes are still checked).
    MEMORY_BASIC_INFORMATION mbi{};
    auto* context = reinterpret_cast<uint8_t*>(site.contextVa);
    if (!VirtualQuery(context, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE ||
        mbi.AllocationBase != exe ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY |
                         PAGE_READWRITE | PAGE_WRITECOPY)) ||
        reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize < site.contextVa + site.contextSize) {
        log("%s: unexpected memory at 0x%08X (state 0x%lX type 0x%lX protect 0x%lX) - not applied", fix,
            static_cast<unsigned>(site.contextVa), mbi.State, mbi.Type, mbi.Protect);
        return false;
    }
    // 4. Keep this DLL loaded for the rest of the process once the game may call into it.
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCSTR>(stub), &self)) {
        log("%s: cannot pin the proxy DLL (%lu) - not applied", fix, GetLastError());
        return false;
    }
    // 5. Freeze every other thread; refuse if one is executing the context bytes. 6. Verified atomic redirect.
    const CallRedirect redirect = {context, site.context, site.contextSize, site.callOffset, site.targetPrologue,
                                   site.targetPrologueSize};
    ThreadFreeze freeze;
    char why[160] = {};
    if (!freeze.Freeze(site.contextVa, site.contextVa + site.contextSize, why, sizeof why)) {
        log("%s: %s - not applied", fix, why);
        return false;
    }
    const RedirectResult result = RedirectCall(redirect, stub);
    // thread inventory is copied before resuming, logged after (no I/O while threads are suspended)
    size_t threads = freeze.count(), inExe = 0, inFunction = 0;
    for (size_t i = 0; i < threads; ++i) {
        const uintptr_t eip = freeze.eip(i);
        if (eip >= kHitman2Base + 0x1000 && eip < build->codeEnd) ++inExe;
        if (eip >= site.functionLo && eip < site.functionHi) ++inFunction;
    }
    freeze.Resume();
    log("%s: %u other threads were suspended during the write (%u executing hitman2.exe code, %u inside the %s)", fix,
        static_cast<unsigned>(threads), static_cast<unsigned>(inExe), static_cast<unsigned>(inFunction),
        site.functionName);
    const uintptr_t siteVa = site.contextVa + site.callOffset;
    if (result == RedirectResult::Applied || result == RedirectResult::AlreadyApplied) {
        log("%s: call at 0x%08X redirected to %s 0x%08X (%s; hitman2.exe %s, sha256 %s)", fix,
            static_cast<unsigned>(siteVa), site.stubName, static_cast<unsigned>(reinterpret_cast<uintptr_t>(stub)),
            ToString(result), build->label, hex);
        return true;
    }
    log("%s: %s - not applied", fix, ToString(result));
    return false;
}

}  // namespace h2e
