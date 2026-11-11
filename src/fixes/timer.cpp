#include "timer.h"

#include <windows.h>

#include <cmath>
#include <cstring>

#include "exe_redirect.h"
#include "fix.h"

namespace h2e {

uint32_t FtolLow32(double x) {
    // fistp qword: NaN, +-inf and |trunc(x)| >= 2^63 raise the (masked) invalid exception and store the integer
    // indefinite 0x8000000000000000. -2^63 itself is representable.
    if (!(x > -9223372036854775809.0 && x < 9223372036854775808.0)) return 0;
    return static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(x)));  // C++ cast truncates toward 0
}

int32_t TickAccumulator::Next(const void* self, int32_t clockNow, double ticks) {
    if (self != self_ || !haveExpected_ || static_cast<uint32_t>(clockNow) != expectedClock_) {
        self_ = self;
        fraction_ = 0.0;
    }
    if (!(ticks >= 0.0 && ticks <= kMaxNormalTicks)) {
        fraction_ = 0.0;
        haveExpected_ = false;
        return static_cast<int32_t>(FtolLow32(ticks));
    }
    const double total = fraction_ + ticks;
    const double whole = std::floor(total);
    fraction_ = total - whole;
    const auto result = static_cast<int32_t>(whole);
    expectedClock_ = static_cast<uint32_t>(clockNow) + static_cast<uint32_t>(result);  // the game adds with wrap
    haveExpected_ = true;
    return result;
}

#if defined(_M_IX86)

TickAccumulator& TimerAccumulator() {
    static TickAccumulator acc;
    return acc;
}

extern "C" int __cdecl h2e_TimerAccumulate(const uint8_t* self, double ticks) {
    int32_t clock;
    std::memcpy(&clock, self + kClockOffset, sizeof clock);
    return TimerAccumulator().Next(self, clock, ticks);
}

// x87 stack on entry: ST(0) = ticks, ST(1) = dt. Both are stored at full 80-bit precision (fstp tbyte) so dt is
// restored bit-exactly; ticks is converted to double for the accumulator. The C++ part runs with an empty x87
// stack, as the 32-bit calling convention requires, and is compiled with SSE2 (no x87 control-word changes).
// The original _ftol never touches XMM registers, so XMM0-XMM7 are saved and restored too (found by the harness:
// a caller keeping values in XMM across the call would otherwise see them clobbered).
extern "C" __declspec(naked) void h2e_TimerTickStub() {
    __asm {
        sub    esp, 156               ; [0] ticks (10) [10] dt (10) [20] ticks double (8) [28] xmm0-7 (128)
        movups [esp+28], xmm0
        movups [esp+44], xmm1
        movups [esp+60], xmm2
        movups [esp+76], xmm3
        movups [esp+92], xmm4
        movups [esp+108], xmm5
        movups [esp+124], xmm6
        movups [esp+140], xmm7
        fstp   tbyte ptr [esp]        ; pop ticks
        fstp   tbyte ptr [esp+10]     ; pop dt -> x87 stack empty
        fld    tbyte ptr [esp]
        fstp   qword ptr [esp+20]     ; ticks as double (x87 stack empty again)
        push   dword ptr [esp+24]     ; double argument: high dword ...
        push   dword ptr [esp+24]     ; ... then low dword (offsets shift by 4 after the first push)
        push   esi                    ; timer instance
        call   h2e_TimerAccumulate    ; cdecl, EAX = ticks to add
        add    esp, 12
        movups xmm0, [esp+28]
        movups xmm1, [esp+44]
        movups xmm2, [esp+60]
        movups xmm3, [esp+76]
        movups xmm4, [esp+92]
        movups xmm5, [esp+108]
        movups xmm6, [esp+124]
        movups xmm7, [esp+140]
        fld    tbyte ptr [esp+10]     ; restore dt as ST(0), bit-exact
        add    esp, 156
        ret
    }
}

namespace {

// The call _ftol at 0x417B40 inside the frame timer update 0x4179F0 (next known function 0x417C60, vtable 0xEC).
// Every check logs its outcome; any failure leaves the game untouched (exe_redirect.h).
void OnDirect3DCreate(const FixContext& ctx, uintptr_t caller) {
    const ExeCallSite site = {"timer",           kHitman2Base + kTimerContextRva, kTimerContext, sizeof kTimerContext,
                              kTimerCallOffset,  kFtolCode,  18,  0x4179F0,  0x417C60,
                              "timer function 0x4179F0", "the accumulator stub"};
    RedirectExeCall(ctx, caller, site, reinterpret_cast<const void*>(&h2e_TimerTickStub));
}

}  // namespace

extern const Fix kTimerFix = {"timer",
                              "EXPERIMENTAL frame-rate independent game clock (fraction-carrying tick accumulator, EXP-004)",
                              nullptr, nullptr, OnDirect3DCreate};

#endif

}  // namespace h2e
