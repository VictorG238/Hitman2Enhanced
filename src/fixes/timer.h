// Fix "timer" (GB-001, EXP-004). EXPERIMENTAL, off by default; approved by the owner for controlled testing.
//
// hitman2.exe 0x4179F0 (frame timer, `this` in ESI) adds `_ftol(dt * 1024)` to the integer clock [this+0x28]; the
// truncation loses the fraction every frame (0 ticks per frame above 1024 fps). The fix redirects the single
// `call _ftol` at 0x417B40 to h2e_TimerTickStub, which returns whole ticks and carries the fraction.
#pragma once
#include <cstddef>
#include <cstdint>

namespace h2e {

// --- binary identity (hitman2.exe 1.0.0.277 Steam, file SHA-256 below; analysis image f59d353e...) ------------
constexpr const char* kHitman2Sha256 = "f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136";
constexpr uintptr_t kHitman2Base = 0x400000;  // no relocations
constexpr uint32_t kTimerContextRva = 0x17B32;  // 30 bytes 0x417B32..0x417B4F
constexpr size_t kTimerCallOffset = 14;         // call _ftol at 0x417B40 (8-byte aligned)
constexpr uint32_t kFtolRva = 0x1A01AC;         // _ftol at 0x5A01AC
constexpr uint8_t kTimerContext[30] = {
    0xD8, 0x8E, 0x3C, 0x19, 0x00, 0x00,  // fmul dword [esi+193Ch]   (time scale)
    0xD9, 0x05, 0xF8, 0x9B, 0x63, 0x00,  // fld  dword [639BF8h]     (1024.0f)
    0xD8, 0xC9,                          // fmul st, st(1)
    0xE8, 0x67, 0x86, 0x18, 0x00,        // call _ftol (5A01ACh)
    0x8B, 0x4E, 0x28,                    // mov  ecx, [esi+28h]
    0x03, 0xC8,                          // add  ecx, eax
    0x89, 0x4E, 0x28,                    // mov  [esi+28h], ecx
    0xD9, 0x5E, 0x2C};                   // fstp dword [esi+2Ch]
// The CRT `_ftol` of this build: sets rounding control to truncate, fistp qword, restores the control word.
constexpr uint8_t kFtolCode[39] = {0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xF4, 0x9B, 0xD9, 0x7D, 0xFE, 0x9B, 0x66, 0x8B,
                                   0x45, 0xFE, 0x80, 0xCC, 0x0C, 0x66, 0x89, 0x45, 0xFC, 0xD9, 0x6D, 0xFC, 0xDF,
                                   0x7D, 0xF4, 0xD9, 0x6D, 0xFE, 0x8B, 0x45, 0xF4, 0x8B, 0x55, 0xF8, 0xC9, 0xC3};
constexpr uint32_t kClockOffset = 0x28;  // int32 game clock, 1/1024 s

// Low 32 bits of what `_ftol` returns: x truncated toward zero to int64 (fistp with RC = truncate), or the
// integer indefinite 0x8000000000000000 (low dword 0) for NaN, infinities and values outside int64.
uint32_t FtolLow32(double x);

// Whole ticks per frame with the fraction carried, tied to one timer instance and to the clock value it produced.
// The fraction is dropped (and the frame behaves exactly like the original code) when:
//   - the timer instance (`self`) changes,
//   - the clock was changed by someone else since the last frame (engine reset via vtable 0xEC, load, set),
//   - `ticks` is not a normal value in [0, kMaxNormalTicks] (NaN, infinity, negative, absurd) - then the result is
//     FtolLow32(ticks), bit-identical to the original.
// Time-scale changes need no special case: `ticks` already includes the scale and the carried fraction is game
// time that was earned but not yet credited (< 1 tick). Scale 0 (pause) adds 0 and keeps the fraction.
class TickAccumulator {
public:
    static constexpr double kMaxNormalTicks = 1048576.0;  // 1024 s per frame; the engine clamps dt to 0.1 s
    int32_t Next(const void* self, int32_t clockNow, double ticks);
    double fraction() const { return fraction_; }

private:
    const void* self_ = nullptr;
    uint32_t expectedClock_ = 0;
    bool haveExpected_ = false;
    double fraction_ = 0.0;
};

#if defined(_M_IX86)
// Drop-in replacement for the `call _ftol` at 0x417B40. Entry: ST(0) = ticks, ST(1) = dt, ESI = timer instance.
// Exit: ST(0) popped (ST(1) unchanged, now ST(0)), EAX = ticks to add. EBX/ESI/EDI/EBP, XMM0-XMM7, the x87 control
// word and MXCSR are preserved; ECX/EDX/EFLAGS are clobbered (the game overwrites ECX next and never reads EDX).
extern "C" void h2e_TimerTickStub();
// Accumulator instance used by the stub (exposed for tests).
TickAccumulator& TimerAccumulator();
#endif

}  // namespace h2e
