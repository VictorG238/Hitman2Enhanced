// Real D3D8 coverage and scene classification for the full-height keyhole viewport.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "../../src/fixes/keyhole.h"
template<class F> F Slot(void* p, int n) { return reinterpret_cast<F>((*static_cast<void***>(p))[n]); }
using Release = ULONG(WINAPI*)(void*);
using Clear = HRESULT(WINAPI*)(void*, DWORD, const RECT*, DWORD, DWORD, float, DWORD);
using SetVp = HRESULT(WINAPI*)(void*, const d3d8::Viewport*);
using Lock = HRESULT(WINAPI*)(void*, d3d8::LockedRect*, const RECT*, DWORD);
using Unlock = HRESULT(WINAPI*)(void*);
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* msg) { if (!ok) { std::printf("FAIL: %s\n", msg); ++failures; } };
    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\d3d8.dll");
    HMODULE lib = LoadLibraryW(path);
    if (!lib) return 1;
    auto create = reinterpret_cast<void*(WINAPI*)(UINT)>(GetProcAddress(lib, "Direct3DCreate8"));
    void* api = create(220);
    if (!api) return 1;
    HWND window = CreateWindowA("STATIC", "keyhole test", WS_POPUP, 0, 0, 320, 240,
                                nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    d3d8::DisplayMode mode{};
    Slot<HRESULT(WINAPI*)(void*, UINT, d3d8::DisplayMode*)>(api, 8)(api, 0, &mode);
    d3d8::PresentParameters pp{};
    pp.BackBufferWidth = 320; pp.BackBufferHeight = 240; pp.BackBufferFormat = mode.Format;
    pp.SwapEffect = 1; pp.hDeviceWindow = window; pp.Windowed = TRUE; pp.Flags = 1; // lockable
    void* dev = nullptr;
    using CreateDevice = HRESULT(WINAPI*)(void*, UINT, DWORD, HWND, DWORD, d3d8::PresentParameters*, void**);
    if (FAILED(Slot<CreateDevice>(api, 15)(api, 0, 1, window, 0x20, &pp, &dev))) return 1;
    const d3d8::Viewport full{0,0,320,240,0,1}, keyhole{112,0,96,240,0,1};
    const d3d8::Viewport original{112,36,96,204,0,1};
    check(!h2e::IsKeyholeSceneViewport(dev, full, 320, 240), "full-screen/UI viewport excluded");
    const d3d8::Viewport shadow{0,0,128,128,0,1};
    check(!h2e::IsKeyholeSceneViewport(dev, shadow, 320, 240), "shadow viewport excluded");
    check(h2e::IsKeyholeSceneViewport(dev, original, 320, 240), "original crop supports collision fallback");
    check(h2e::IsKeyholeSceneViewport(dev, keyhole, 320, 240), "expanded keyhole still feeds collision projection");
    auto clear = Slot<Clear>(dev, 36);
    check(SUCCEEDED(clear(dev, 0, nullptr, 1, 0xffcc1122, 1, 0)), "fill original frame");
    check(SUCCEEDED(Slot<SetVp>(dev, 40)(dev, &keyhole)), "set expanded viewport");
    check(SUCCEEDED(clear(dev, 0, nullptr, 1, 0xff2244cc, 1, 0)), "color entire expanded keyhole viewport");
    void* back = nullptr;
    Slot<HRESULT(WINAPI*)(void*, UINT, DWORD, void**)>(dev,16)(dev,0,0,&back);
    d3d8::LockedRect lr{};
    const HRESULT locked = Slot<Lock>(back,9)(back,&lr,nullptr,0x10);
    check(SUCCEEDED(locked), "read back pixels");
    if (SUCCEEDED(locked)) {
        check(mode.Format == 21 || mode.Format == 22, "32-bit pixel format");
        if (mode.Format == 21 || mode.Format == 22) {
            bool pixels = true;
            for (unsigned y=0; y<240; ++y) for (unsigned x=0; x<320; ++x) {
                DWORD pixel;
                std::memcpy(&pixel, static_cast<const char*>(lr.pBits)+y*lr.Pitch+x*4, 4);
                const DWORD expected = x>=112 && x<208 ? 0x2244cc : 0xcc1122;
                pixels &= (pixel & 0xffffff) == expected;
            }
            check(pixels, "expanded viewport covers former strip and preserves pixels beside the scene");
        }
        Slot<Unlock>(back,10)(back);
    }
    void* offscreen = nullptr;
    using CreateRT = HRESULT(WINAPI*)(void*,UINT,UINT,DWORD,DWORD,BOOL,void**);
    check(SUCCEEDED(Slot<CreateRT>(dev,25)(dev,320,240,mode.Format,0,TRUE,&offscreen)), "offscreen target");
    if (offscreen) {
        using SetRT = HRESULT(WINAPI*)(void*,void*,void*);
        check(SUCCEEDED(Slot<SetRT>(dev,31)(dev,offscreen,nullptr)), "bind offscreen target");
        check(!h2e::IsKeyholeSceneViewport(dev,keyhole,320,240), "matching offscreen viewport excluded");
        Slot<SetRT>(dev,31)(dev,back,nullptr);
        Slot<Release>(offscreen,2)(offscreen);
    }
    Slot<Release>(back,2)(back);
    Slot<Release>(dev,2)(dev);
    Slot<Release>(api,2)(api);
    DestroyWindow(window);
    FreeLibrary(lib);
    return failures ? 1 : 0;
}
