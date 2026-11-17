// Isolated bridge/rel32 harness. No game process or game files are touched.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "../../src/fixes/keyhole.h"
#include "../../src/common/code_patch.h"

namespace {
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
}
unsigned seenSelf, seenAxes, vector[3], seenEsi, seenEbx, seenEdi, seenEbp;
// Synthetic continuation checks the exact stack/register inputs expected by
// the original suffix, then uses its original epilogue to return to C++.
__declspec(naked) void Continuation() {
    __asm {
        mov seenSelf, esi
        mov seenAxes, eax
        mov eax, [esp+0ch]
        mov vector, eax
        mov eax, [esp+10h]
        mov vector+4, eax
        mov eax, [esp+14h]
        mov vector+8, eax
        pop esi
        add esp, 44h
        ret
    }
}
float axes[4] = {0, .5f, 0, -.5f};
uintptr_t continuation = reinterpret_cast<uintptr_t>(&Continuation);
void* bridge;
__declspec(naked) void Probe() {
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ebx, 11111111h
        mov esi, 22222222h
        mov edi, 33333333h
        mov ebp, 44444444h
        mov ecx, 55555555h
        lea edx, axes
        call bridge
        mov seenEsi, esi
        mov seenEbx, ebx
        mov seenEdi, edi
        mov seenEbp, ebp
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}
void Jump(uint8_t* at, const void* target) {
    at[0] = 0xe9;
    const uint32_t rel = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) -
                                              reinterpret_cast<uintptr_t>(at + 5));
    std::memcpy(at + 1, &rel, 4);
}
int __cdecl Original() { return 17; }
int __cdecl Replacement() { return 29; }
}
int main() {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x10000,
                       MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Check(page != nullptr, "allocate isolated bridge");
    if (!page) return 1;
    // Copy the actual compiled bridge, relocating only its final engine address
    // to our synthetic suffix. No fixed addresses are reserved in the test.
    const auto* source = reinterpret_cast<const uint8_t*>(&h2e::ApplyMouseLook);
    size_t end = 0;
    while (end < 64 && !(source[end] == 0x8b && source[end + 1] == 0x15 &&
                         source[end + 6] == 0xff && source[end + 7] == 0xe2)) ++end;
    Check(end < 64, "compiled bridge ends with expected engine jump");
    if (end == 64) { VirtualFree(page, 0, MEM_RELEASE); return 1; }
    std::memcpy(page, source, end + 8);
    const auto address = reinterpret_cast<uintptr_t>(&continuation);
    std::memcpy(page + end + 2, &address, sizeof address);
    bridge = page;
    auto* branch = page + 0x101;
    page[0x100] = 0x90;
    Jump(branch, &Original);
    page[0x106] = page[0x107] = 0x90;
    uint8_t expected[8];
    std::memcpy(expected, page + 0x100, 8);
    uint8_t prologue[5];
    std::memcpy(prologue, reinterpret_cast<const void*>(&Original), 5);
    DWORD old;
    VirtualProtect(page, 0x10000, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), page, 0x10000);
    Probe();
    Check(seenSelf == 0x55555555 && seenAxes == reinterpret_cast<uintptr_t>(axes), "bridge inputs");
    Check(vector[0] == 0 && vector[1] == 0 && vector[2] == 0x3f800000, "bridge initial direction");
    Check(seenEsi == 0x22222222 && seenEbx == 0x11111111 && seenEdi == 0x33333333 && seenEbp == 0x44444444,
          "bridge restores stack and callee-saved registers");
    const h2e::CallRedirect site{page + 0x100, expected, 8, 1, prologue, 5};
    auto run = reinterpret_cast<int(__cdecl*)()>(branch);
    Check(run() == 17, "original tail target");
    Check(h2e::RedirectCall(site, &Replacement) == h2e::RedirectResult::Applied, "E9 redirect applied");
    Check(run() == 29, "E9 replacement executes and returns");
    Check(h2e::RedirectCall(site, &Replacement) == h2e::RedirectResult::AlreadyApplied, "E9 idempotent");
    Check(page[0x100] == 0x90 && branch[0] == 0xe9 && page[0x106] == 0x90 && page[0x107] == 0x90,
          "E9 opcode and surrounding bytes preserved");
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(page, &mbi, sizeof mbi);
    Check(mbi.Protect == PAGE_EXECUTE_READ, "E9 protection restored");
    VirtualFree(page, 0, MEM_RELEASE);
    return failures ? 1 : 0;
}
