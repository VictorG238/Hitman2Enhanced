// Verified in-memory code patches for game modules.
//
// Every patch is pinned to one build of one module: the module file on disk must have the expected
// SHA-256 and the bytes in memory must be exactly the expected original bytes. Otherwise nothing is written.
// Patches only touch the process memory; files on disk are never modified.
#pragma once
#include <cstddef>
#include <cstdint>

namespace h2e {

using LogFn = void (*)(const char* fmt, ...);

struct BytePatch {
    const char* module;     // module name as loaded, e.g. "RenderD3D.dll"
    const char* sha256;     // expected SHA-256 of the module file (lowercase hex)
    uint32_t rva;           // offset of the bytes from the module base
    const uint8_t* original;
    const uint8_t* patched;
    size_t size;
};

enum class PatchState { Original, AlreadyPatched, Mismatch };

// Classifies `size` bytes at `code`; never writes.
PatchState ClassifyBytes(const uint8_t* code, const BytePatch& patch);

enum class PatchResult { Applied, AlreadyApplied, ModuleNotLoaded, FileUnreadable, HashMismatch, BytesMismatch,
                         ProtectFailed };

// Checks the module file hash and the bytes, then writes the patch. Logs the outcome through `log`.
PatchResult ApplyBytePatch(const BytePatch& patch, LogFn log);

// Redirection of one `call rel32` to new code, done as safely as x86 allows (EXP-004):
//   - the caller-supplied context bytes around the call must match memory exactly (including the original rel32),
//   - the original call target must start with the expected prologue,
//   - the call must sit at an 8-byte aligned address so the 5 bytes are replaced together with the next 3 by ONE
//     aligned 8-byte `lock cmpxchg8b`: another processor sees either the old or the new instruction, never a mix;
//     the exchange fails (nothing written) if memory changed since it was verified,
//   - page protection is changed only for those 8 bytes' page(s) and restored afterwards, the instruction cache is
//     flushed, and the result is read back.
// Intel SDM vol. 3 §8.1.3 still asks for the call site not to be executing on another thread during the change:
// callers must pick a moment when that is guaranteed (the timer fix patches from the render thread inside
// Direct3DCreate8, while the engine's main loop is not running).
struct CallRedirect {
    uint8_t* context;              // address of the verified context
    const uint8_t* expected;       // expected context bytes
    size_t contextSize;
    size_t callOffset;             // offset of the E8 opcode inside the context
    const uint8_t* targetPrologue; // expected first bytes of the original call target
    size_t targetPrologueSize;
};

enum class RedirectResult { Applied, AlreadyApplied, ContextMismatch, TargetMismatch, Misaligned, ProtectFailed,
                            ExchangeFailed, VerifyFailed };

RedirectResult RedirectCall(const CallRedirect& site, const void* newTarget);
const char* ToString(RedirectResult r);

// SHA-256 of the file a loaded module was mapped from (lowercase hex). False if it cannot be read.
bool ModuleFileSha256(void* module, char out[65]);

// Suspends every other thread of this process and records where each one is executing, so a patch can be written
// while no thread can run the modified bytes (Intel SDM 3 §8.1.3). Uses no heap and does no I/O between Freeze()
// and Resume(); log only after Resume(). Threads created after the snapshot are not covered (documented limitation).
class ThreadFreeze {
public:
    static constexpr size_t kMaxThreads = 256;
    ~ThreadFreeze() { Resume(); }
    // False (with everything already resumed and `why` set) if a thread could not be suspended or inspected, if
    // there are more than kMaxThreads, or if any thread's EIP lies in [lo, hi).
    bool Freeze(uintptr_t lo, uintptr_t hi, char* why, size_t whySize);
    void Resume();
    size_t count() const { return count_; }
    uintptr_t eip(size_t i) const { return eips_[i]; }
    unsigned long threadId(size_t i) const { return ids_[i]; }

private:
    void* handles_[kMaxThreads] = {};
    uintptr_t eips_[kMaxThreads] = {};
    unsigned long ids_[kMaxThreads] = {};
    size_t count_ = 0;
    bool frozen_ = false;
};

}  // namespace h2e
