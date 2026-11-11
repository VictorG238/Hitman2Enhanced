// Isolated harness for the "timer" fix (EXP-004). Nothing here touches the game.
//
// The game's 30-byte clock sequence (hitman2.exe 0x417B32) and its CRT `_ftol` (0x5A01AC) are copied from
// src/fixes/timer.h into executable memory of THIS process; only the absolute address of the 1024.0f constant and the
// call's rel32 are relocated. The copy runs for real against a fake timer object (+0x28 clock, +0x2C dt,
// +0x193C time scale): first unpatched (reproduces the truncation), then after h2e::RedirectCall pointed the call
// at h2e_TimerTickStub - the same code path the fix would use in the game.
#include <windows.h>
#include <xmmintrin.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

#include "../../src/common/code_patch.h"
#include "../../src/fixes/timer.h"

using h2e::h2e_TimerTickStub;

namespace {

int g_failures = 0;
void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

const float k1024 = 1024.0f;

struct Harness {
    uint8_t* page = nullptr;
    uint8_t* seq = nullptr;   // relocated 30-byte context + ret; seq + 14 is 8-byte aligned
    uint8_t* ftol = nullptr;  // copy of the original _ftol
    uint8_t expected[30] = {};
    uint8_t obj[0x2000] = {};

    bool Init() {
        page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!page) return false;
        ftol = page + 0x800;
        std::memcpy(ftol, h2e::kFtolCode, sizeof h2e::kFtolCode);
        seq = page + 0x102;  // 0x102 + 14 = 0x110
        std::memcpy(expected, h2e::kTimerContext, sizeof expected);
        const auto constAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&k1024));
        std::memcpy(expected + 8, &constAddr, 4);
        const auto rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(ftol) -
                                              reinterpret_cast<uintptr_t>(seq + h2e::kTimerCallOffset + 5));
        std::memcpy(expected + h2e::kTimerCallOffset + 1, &rel, 4);
        std::memcpy(seq, expected, sizeof expected);
        seq[30] = 0xC3;  // ret
        DWORD old;
        VirtualProtect(page, 0x2000, PAGE_EXECUTE_READ, &old);  // like the game's .text: not writable
        SetScale(1.0f);
        return true;
    }
    void SetScale(float s) { std::memcpy(obj + 0x193C, &s, 4); }
    int32_t Clock() const {
        int32_t c;
        std::memcpy(&c, obj + 0x28, 4);
        return c;
    }
    void SetClock(int32_t c) { std::memcpy(obj + 0x28, &c, 4); }
    float Dt() const {
        float f;
        std::memcpy(&f, obj + 0x2C, 4);
        return f;
    }
    h2e::CallRedirect Site() { return {seq, expected, sizeof expected, h2e::kTimerCallOffset, h2e::kFtolCode, 18}; }

    // One frame: ST(0) = smoothed dt, then the game's sequence. Returns the x87 tag word afterwards (0xFFFF = empty).
    unsigned Frame(double smooth) {
        uint8_t* code = seq;
        uint8_t* o = obj;
        unsigned short tag = 0;
        unsigned char env[28];
        __asm {
            push  esi
            mov   esi, o
            fld   qword ptr smooth
            call  code
            pop   esi
            fnstenv env
            fldenv env
        }
        std::memcpy(&tag, env + 8, 2);
        return tag;
    }
};

// The engine's smoothing as observed (TIMING_AND_FPS.md): raw <= 1.0, smooth = 0.7 * smooth + 0.3 * raw, <= 0.1.
struct EngineDt {
    double smooth = 0.0;
    double Step(double raw) {
        if (raw > 1.0) raw = 1.0;
        if (smooth == 0.0) smooth = raw;
        smooth = smooth * 0.699999988079071 + raw * 0.30000001192092896;
        if (smooth > 0.1) smooth = 0.1;
        return smooth;
    }
};

// Simulates `seconds` of play at `fps` (optionally +-jitter) and returns clock ticks gained.
int64_t Run(Harness& h, double fps, double seconds, double jitter = 0.0) {
    EngineDt e;
    const int32_t start = h.Clock();
    const auto frames = static_cast<long>(fps * seconds);
    double elapsed = 0.0;
    for (long i = 0; i < frames; ++i) {
        const double raw = (1.0 / fps) * (1.0 + jitter * ((i % 7) - 3) / 3.0);
        elapsed += raw;
        h.Frame(e.Step(raw));
    }
    (void)elapsed;
    return static_cast<int64_t>(h.Clock()) - start;
}

// Calls `target` with ST(1) = y, ST(0) = x and ESI = self; returns EAX and checks that y is left as ST(0).
uint32_t CallConversion(void* target, double x, double y, const void* self, bool* yIntact, unsigned short cw) {
    uint32_t r = 0;
    double back = 0;
    unsigned short saved;
    __asm {
        fnstcw saved
        fldcw  cw
        push   esi
        mov    esi, self
        fld    qword ptr y
        fld    qword ptr x
        call   target
        mov    r, eax
        fstp   qword ptr back
        pop    esi
        fldcw  saved
    }
    *yIntact = std::memcmp(&back, &y, sizeof y) == 0;
    return r;
}

// ABI probe: runs the sequence with sentinel values in the callee-saved registers. Naked so the compiler's own use
// of EBX/EBP as frame registers cannot interfere; inputs and outputs are globals.
uint8_t* g_probeCode;
uint8_t* g_probeObj;
double g_probeSmooth;
unsigned g_probeEbx, g_probeEdi, g_probeEbp, g_probeEsi;
unsigned g_xmmIn[32], g_xmmOut[32];  // XMM0-XMM7 sentinels before / after

__declspec(naked) void AbiProbe() {
    __asm {
        push  ebx
        push  edi
        push  esi
        push  ebp
        movups xmm0, g_xmmIn
        movups xmm1, g_xmmIn + 16
        movups xmm2, g_xmmIn + 32
        movups xmm3, g_xmmIn + 48
        movups xmm4, g_xmmIn + 64
        movups xmm5, g_xmmIn + 80
        movups xmm6, g_xmmIn + 96
        movups xmm7, g_xmmIn + 112
        mov   ebx, 0x11111111
        mov   edi, 0x22222222
        mov   esi, g_probeObj
        fld   qword ptr g_probeSmooth
        mov   eax, g_probeCode
        mov   ebp, 0x33333333
        call  eax
        mov   g_probeEbx, ebx
        mov   g_probeEdi, edi
        mov   g_probeEbp, ebp
        mov   g_probeEsi, esi
        movups g_xmmOut, xmm0
        movups g_xmmOut + 16, xmm1
        movups g_xmmOut + 32, xmm2
        movups g_xmmOut + 48, xmm3
        movups g_xmmOut + 64, xmm4
        movups g_xmmOut + 80, xmm5
        movups g_xmmOut + 96, xmm6
        movups g_xmmOut + 112, xmm7
        pop   ebp
        pop   esi
        pop   edi
        pop   ebx
        ret
    }
}

// Worker thread for the ThreadFreeze test: spins inside SpinLoop (a known 16-byte code range) until g_stop is set.
volatile long g_stop = 0;
volatile long g_spinning = 0;
__declspec(naked) void SpinLoop() {
    __asm {
    again:
        mov   eax, g_stop
        test  eax, eax
        jz    again
        ret
    }
}
DWORD WINAPI SpinThread(void*) {
    InterlockedExchange(&g_spinning, 1);
    SpinLoop();
    return 0;
}

}  // namespace

int main() {
    Harness h;
    Check(h.Init(), "harness allocated");

    // --- 1. FtolLow32 reproduces the original _ftol bytes (executed) for normal and exceptional values,
    //        under 24-, 53- and 64-bit precision control.
    const double inf = std::numeric_limits<double>::infinity();
    const double values[] = {0.0, -0.0, 0.5, -0.5, 1.9999, 17.07, 1048575.9, 2147483647.5, 2147483648.0,
                             4294967299.7, -2147483649.0, 1e15 + 0.5, 9223372036854774784.0, 9223372036854775808.0,
                             -9223372036854775808.0, -9223372036854777856.0, 1e300, -1e300, inf, -inf,
                             std::numeric_limits<double>::quiet_NaN(), 1e-310, -1e-310};
    const unsigned short controlWords[] = {0x007F, 0x027F, 0x037F};
    bool allMatch = true, yKept = true;
    for (unsigned short cw : controlWords)
        for (double v : values) {
            bool intact = false;
            const uint32_t orig = CallConversion(h.ftol, v, 3.25, h.obj, &intact, cw);
            yKept &= intact;
            if (orig != h2e::FtolLow32(v)) {
                std::printf("  mismatch cw=%04X x=%.17g original=%08X ours=%08X\n", cw, v, orig, h2e::FtolLow32(v));
                allMatch = false;
            }
        }
    Check(allMatch, "FtolLow32 == original _ftol for all values and precision modes");
    Check(yKept, "original _ftol leaves ST(1) as ST(0)");

    // --- 2. Unpatched harness reproduces the bug.
    h.SetClock(0);
    Check(Run(h, 60.0, 10.0) == 17 * 600, "unpatched 60 fps: 17 ticks per frame");
    h.SetClock(0);
    Check(Run(h, 3000.0, 5.0) == 0, "unpatched 3000 fps: clock does not advance");
    h.SetClock(0);
    Run(h, 60.0, 1.0);
    const float dtUnpatched = h.Dt();

    // --- 3. RedirectCall refuses unsafe cases and patches the harness copy.
    const h2e::CallRedirect site = h.Site();
    uint8_t wrong[30];
    std::memcpy(wrong, h.expected, 30);
    wrong[2] ^= 1;
    h2e::CallRedirect badContext = site;
    badContext.expected = wrong;
    Check(h2e::RedirectCall(badContext, &h2e_TimerTickStub) == h2e::RedirectResult::ContextMismatch,
          "context mismatch refused");
    const uint8_t notFtol[18] = {0x90};
    h2e::CallRedirect badTarget = site;
    badTarget.targetPrologue = notFtol;
    Check(h2e::RedirectCall(badTarget, &h2e_TimerTickStub) == h2e::RedirectResult::TargetMismatch,
          "unexpected call target refused");
    h2e::CallRedirect misaligned = site;
    misaligned.context = h.seq - 1;
    Check(h2e::RedirectCall(misaligned, &h2e_TimerTickStub) == h2e::RedirectResult::Misaligned,
          "misaligned call site refused");
    Check(std::memcmp(h.seq, h.expected, 30) == 0, "nothing written by refused attempts");
    MEMORY_BASIC_INFORMATION before{};
    VirtualQuery(h.seq + 14, &before, sizeof before);
    Check(h2e::RedirectCall(site, &h2e_TimerTickStub) == h2e::RedirectResult::Applied, "redirect applied");
    Check(h2e::RedirectCall(site, &h2e_TimerTickStub) == h2e::RedirectResult::AlreadyApplied, "second apply no-op");
    MEMORY_BASIC_INFORMATION after{};
    VirtualQuery(h.seq + 14, &after, sizeof after);
    Check(after.Protect == before.Protect, "page protection restored");
    Check(std::memcmp(h.seq + 19, h.expected + 19, 11) == 0 && std::memcmp(h.seq, h.expected, 14) == 0,
          "only the rel32 changed");

    // --- 4. Patched: exact clock at any frame rate, same dt store, ABI preserved.
    h.SetClock(0);
    Run(h, 60.0, 1.0);
    const float dtPatched = h.Dt();
    Check(std::memcmp(&dtUnpatched, &dtPatched, 4) == 0, "dt stored at +0x2C is bit-identical to the unpatched code");
    const double rates[] = {24, 25, 30, 60, 144, 240, 1000, 3000, 5000};
    for (double fps : rates)
        for (double jitter : {0.0, 0.3}) {
            h.SetClock(0);  // a clock change resets the carried fraction, as an engine reset would
            const int64_t got = Run(h, fps, 10.0, jitter);
            // expected = floor(sum of the engine's own dt*1024); the smoothing settles within a few frames
            EngineDt e;
            double sum = 0;
            for (long i = 0; i < static_cast<long>(fps * 10.0); ++i)
                sum += e.Step((1.0 / fps) * (1.0 + jitter * ((i % 7) - 3) / 3.0)) * 1024.0;
            char what[96];
            std::snprintf(what, sizeof what, "patched %.0f fps jitter %.1f: |clock - sum(dt*1024)| < 1", fps, jitter);
            Check(std::fabs(static_cast<double>(got) - std::floor(sum)) <= 1.0, what);
            if (std::fabs(static_cast<double>(got) - 10240.0) > 0.01 * 10240.0)
                std::printf("  note: %.0f fps jitter %.1f gained %lld ticks over 10 s (engine smoothing)\n", fps,
                            jitter, static_cast<long long>(got));
        }
    // time scale 0.5 and 0 (pause)
    h.SetClock(0);
    h.SetScale(0.5f);
    Check(std::llabs(Run(h, 3000.0, 10.0) - 5120) <= 2, "scale 0.5 at 3000 fps: half speed");
    const double fracBefore = h2e::TimerAccumulator().fraction();
    h.SetScale(0.0f);
    const int32_t paused = h.Clock();
    Run(h, 3000.0, 1.0);
    Check(h.Clock() == paused, "scale 0: clock holds");
    Check(h2e::TimerAccumulator().fraction() == fracBefore, "scale 0: fraction kept");
    h.SetScale(1.0f);
    // engine reset (vtable 0xEC zeroes the clock): the carried fraction must be dropped on the next frame
    Run(h, 3000.0, 0.01);
    h.SetClock(0);
    EngineDt e;
    h.Frame(e.Step(1.0 / 3000.0));
    Check(h.Clock() == 0 && h2e::TimerAccumulator().fraction() > 0.0 && h2e::TimerAccumulator().fraction() < 1.0,
          "after a reset the fraction restarts from 0");

    // ABI: callee-saved registers, x87 control word, MXCSR, empty x87 stack after the sequence
    bool abiOk = true;
    g_probeCode = h.seq;
    g_probeObj = h.obj;
    g_probeSmooth = 1.0 / 144.0;
    for (unsigned i = 0; i < 32; ++i) g_xmmIn[i] = 0xA5A50000u + i;
    for (unsigned short cw : controlWords) {
        std::memset(g_xmmOut, 0, sizeof g_xmmOut);
        unsigned short cwBefore = cw, cwAfter = 0, saved;
        const unsigned mxcsr = _mm_getcsr();
        unsigned char env[28];
        __asm {
            fnstcw saved
            fldcw  cwBefore
        }
        AbiProbe();
        __asm {
            fnstcw cwAfter
            fnstenv env
            fldenv env
            fldcw  saved
        }
        unsigned short tag;
        std::memcpy(&tag, env + 8, 2);
        abiOk &= g_probeEbx == 0x11111111 && g_probeEdi == 0x22222222 && g_probeEbp == 0x33333333 &&
                 g_probeEsi == reinterpret_cast<uintptr_t>(h.obj);
        abiOk &= cwAfter == cwBefore && _mm_getcsr() == mxcsr && tag == 0xFFFF;
        abiOk &= std::memcmp(g_xmmIn, g_xmmOut, sizeof g_xmmIn) == 0;
    }
    Check(abiOk, "EBX/ESI/EDI/EBP, XMM0-7, x87 control word, MXCSR, empty x87 stack preserved (24/53/64-bit)");

    // the stub returns exactly FtolLow32 for exceptional values and keeps ST(1)
    bool excOk = true;
    for (double v : {std::numeric_limits<double>::quiet_NaN(), inf, -inf, -1.0, 1e300, 2097152.0}) {
        bool intact = false;
        const uint32_t r = CallConversion(reinterpret_cast<void*>(&h2e_TimerTickStub), v, 3.25, h.obj, &intact, 0x027F);
        excOk &= intact && r == h2e::FtolLow32(v);
    }
    Check(excOk, "stub == original semantics for NaN/inf/negative/huge, ST(1) kept");

    // --- 5. ThreadFreeze: refuses while a thread executes the target range, records EIPs, resumes everything.
    {
        HANDLE worker = CreateThread(nullptr, 0, SpinThread, nullptr, 0, nullptr);
        while (!g_spinning) Sleep(1);
        Sleep(20);
        const auto lo = reinterpret_cast<uintptr_t>(&SpinLoop), hi = lo + 16;
        h2e::ThreadFreeze f;
        char why[160] = {};
        Check(!f.Freeze(lo, hi, why, sizeof why) && std::strstr(why, "executing the target bytes"),
              "freeze refused while a thread runs the target range");
        Check(f.count() == 0, "refused freeze resumed and released every thread");
        Check(f.Freeze(lo + 0x100000, hi + 0x100000, why, sizeof why), "freeze succeeds when no thread is in range");
        bool sawWorker = false;
        for (size_t i = 0; i < f.count(); ++i) sawWorker |= f.eip(i) >= lo && f.eip(i) < hi;
        Check(sawWorker, "frozen worker's EIP recorded inside SpinLoop");
        f.Resume();
        InterlockedExchange(&g_stop, 1);
        Check(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, "worker resumed and finished");
        CloseHandle(worker);
    }

    if (g_failures == 0) std::printf("PASS\n");
    return g_failures == 0 ? 0 : 1;
}
