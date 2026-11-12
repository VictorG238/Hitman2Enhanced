#include "code_patch.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "sha256.h"

namespace h2e {

PatchState ClassifyBytes(const uint8_t* code, const BytePatch& patch) {
    if (std::memcmp(code, patch.original, patch.size) == 0) return PatchState::Original;
    if (std::memcmp(code, patch.patched, patch.size) == 0) return PatchState::AlreadyPatched;
    return PatchState::Mismatch;
}

namespace {

bool ReadWholeFile(const wchar_t* path, std::string& data) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (64 << 20);
    if (ok) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        ok = ReadFile(f, &data[0], static_cast<DWORD>(data.size()), &got, nullptr) && got == data.size();
    }
    CloseHandle(f);
    return ok;
}

}  // namespace

bool ModuleFileSha256(void* module, char out[65]) {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(static_cast<HMODULE>(module), path, MAX_PATH)) return false;
    std::string data;
    if (!ReadWholeFile(path, data)) return false;
    Sha256Hex(data.data(), data.size(), out);
    return true;
}

bool ThreadFreeze::Freeze(uintptr_t lo, uintptr_t hi, char* why, size_t whySize) {
    Resume();
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        snprintf(why, whySize, "thread snapshot failed (%lu)", GetLastError());
        return false;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof te;
    // While threads are suspended only kernel calls are made; the failure is formatted after Resume().
    enum { kOk, kTooMany, kOpen, kSuspend, kContext, kInRange } fail = kOk;
    DWORD failTid = 0, failErr = 0;
    uintptr_t failEip = 0;
    frozen_ = true;
    for (BOOL more = Thread32First(snap, &te); more && fail == kOk; more = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        failTid = te.th32ThreadID;
        if (count_ == kMaxThreads) {
            fail = kTooMany;
            break;
        }
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                              te.th32ThreadID);
        if (!t) {
            fail = kOpen;
            failErr = GetLastError();
            break;
        }
        if (SuspendThread(t) == static_cast<DWORD>(-1)) {
            fail = kSuspend;
            failErr = GetLastError();
            CloseHandle(t);
            break;
        }
        handles_[count_] = t;
        ids_[count_] = te.th32ThreadID;
        ++count_;
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(t, &ctx)) {  // also waits until the suspension is effective
            fail = kContext;
            failErr = GetLastError();
            break;
        }
#if defined(_M_IX86)
        eips_[count_ - 1] = ctx.Eip;
#else
        eips_[count_ - 1] = static_cast<uintptr_t>(ctx.Rip);
#endif
        if (eips_[count_ - 1] >= lo && eips_[count_ - 1] < hi) {
            fail = kInRange;
            failEip = eips_[count_ - 1];
        }
    }
    CloseHandle(snap);
    if (fail == kOk) return true;
    Resume();
    switch (fail) {
        case kTooMany: snprintf(why, whySize, "more than %u threads", static_cast<unsigned>(kMaxThreads)); break;
        case kOpen: snprintf(why, whySize, "cannot open thread %lu (error %lu)", failTid, failErr); break;
        case kSuspend: snprintf(why, whySize, "cannot suspend thread %lu (error %lu)", failTid, failErr); break;
        case kContext: snprintf(why, whySize, "cannot read the context of thread %lu (error %lu)", failTid, failErr); break;
        default:
            snprintf(why, whySize, "thread %lu is executing the target bytes (eip 0x%08X)", failTid,
                     static_cast<unsigned>(failEip));
    }
    return false;
}

void ThreadFreeze::Resume() {
    for (size_t i = 0; i < count_; ++i) {
        if (frozen_) ResumeThread(static_cast<HANDLE>(handles_[i]));
        CloseHandle(static_cast<HANDLE>(handles_[i]));
    }
    count_ = 0;
    frozen_ = false;
}

PatchResult ApplyBytePatch(const BytePatch& patch, LogFn log) {
    HMODULE mod = GetModuleHandleA(patch.module);
    if (!mod) {
        log("patch %s+0x%X: module not loaded - not applied", patch.module, patch.rva);
        return PatchResult::ModuleNotLoaded;
    }
    char hex[65];
    if (!ModuleFileSha256(mod, hex)) {
        log("patch %s+0x%X: cannot read the module file - not applied", patch.module, patch.rva);
        return PatchResult::FileUnreadable;
    }
    if (std::strcmp(hex, patch.sha256) != 0) {
        log("patch %s+0x%X: file SHA-256 %s is not the pinned build - not applied", patch.module, patch.rva, hex);
        return PatchResult::HashMismatch;
    }
    auto* code = reinterpret_cast<uint8_t*>(mod) + patch.rva;
    switch (ClassifyBytes(code, patch)) {
        case PatchState::AlreadyPatched:
            log("patch %s+0x%X: already applied", patch.module, patch.rva);
            return PatchResult::AlreadyApplied;
        case PatchState::Mismatch:
            log("patch %s+0x%X: unexpected bytes in memory - not applied", patch.module, patch.rva);
            return PatchResult::BytesMismatch;
        case PatchState::Original:
            break;
    }
    DWORD old = 0;
    if (!VirtualProtect(code, patch.size, PAGE_EXECUTE_READWRITE, &old)) {
        log("patch %s+0x%X: VirtualProtect failed (%lu) - not applied", patch.module, patch.rva, GetLastError());
        return PatchResult::ProtectFailed;
    }
    std::memcpy(code, patch.patched, patch.size);
    VirtualProtect(code, patch.size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), code, patch.size);
    log("patch %s+0x%X: applied (base 0x%08X, sha256 %s)", patch.module, patch.rva,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(mod)), hex);
    return PatchResult::Applied;
}

namespace {

int32_t Rel32At(const uint8_t* p) {
    int32_t rel;
    std::memcpy(&rel, p, sizeof rel);
    return rel;
}

// Context equal to `expected` everywhere except the 4 rel32 bytes of the call.
bool SameExceptRel(const CallRedirect& s) {
    for (size_t i = 0; i < s.contextSize; ++i) {
        if (i > s.callOffset && i <= s.callOffset + 4) continue;
        if (s.context[i] != s.expected[i]) return false;
    }
    return true;
}

}  // namespace

RedirectResult RedirectCall(const CallRedirect& s, const void* newTarget) {
    uint8_t* call = s.context + s.callOffset;
    // The 5-byte call must lie inside one aligned qword that is itself inside the verified context, so a single
    // lock cmpxchg8b replaces it atomically (Steam build: call at the qword start; retail 1.0.0.265: at +2).
    auto* block = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(call) & ~uintptr_t(7));
    const size_t lead = size_t(call - block);
    if (lead > 3 || block < s.context || block + 8 > s.context + s.contextSize || s.expected[s.callOffset] != 0xE8)
        return RedirectResult::Misaligned;
    const uintptr_t next = reinterpret_cast<uintptr_t>(call) + 5;
    if (call[0] == 0xE8 && next + static_cast<uintptr_t>(Rel32At(call + 1)) == reinterpret_cast<uintptr_t>(newTarget) &&
        SameExceptRel(s))
        return RedirectResult::AlreadyApplied;
    if (std::memcmp(s.context, s.expected, s.contextSize) != 0) return RedirectResult::ContextMismatch;
    const auto* oldTarget =
        reinterpret_cast<const uint8_t*>(next + static_cast<uintptr_t>(Rel32At(s.expected + s.callOffset + 1)));
    if (std::memcmp(oldTarget, s.targetPrologue, s.targetPrologueSize) != 0) return RedirectResult::TargetMismatch;

    LONGLONG oldQ, newQ;
    std::memcpy(&oldQ, block, 8);
    uint8_t bytes[8];
    std::memcpy(bytes, block, 8);
    const auto newRel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(newTarget) - next);
    std::memcpy(bytes + lead + 1, &newRel, 4);
    std::memcpy(&newQ, bytes, 8);

    DWORD oldProtect = 0;
    if (!VirtualProtect(block, 8, PAGE_EXECUTE_READWRITE, &oldProtect)) return RedirectResult::ProtectFailed;
    const LONGLONG seen = InterlockedCompareExchange64(reinterpret_cast<volatile LONGLONG*>(block), newQ, oldQ);
    DWORD ignored = 0;
    VirtualProtect(block, 8, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), block, 8);
    if (seen != oldQ) return RedirectResult::ExchangeFailed;
    LONGLONG now;
    std::memcpy(&now, block, 8);
    return now == newQ ? RedirectResult::Applied : RedirectResult::VerifyFailed;
}

const char* ToString(RedirectResult r) {
    switch (r) {
        case RedirectResult::Applied: return "applied";
        case RedirectResult::AlreadyApplied: return "already applied";
        case RedirectResult::ContextMismatch: return "context bytes differ";
        case RedirectResult::TargetMismatch: return "call target differs";
        case RedirectResult::Misaligned: return "call site not 8-byte aligned";
        case RedirectResult::ProtectFailed: return "VirtualProtect failed";
        case RedirectResult::ExchangeFailed: return "memory changed during the exchange";
        case RedirectResult::VerifyFailed: return "read-back differs";
    }
    return "?";
}

}  // namespace h2e
