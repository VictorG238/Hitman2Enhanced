// End-to-end test of the `settings` page through the real proxy and a real D3D8 device.
// Usage: settings_smoke <path-to-proxy-d3d8.dll> <log-path>
// Writes h2e.ini ([settings] Enabled=1) next to the proxy, opens the page from the start (H2E_SETTINGS_OPEN=1),
// presents a few frames and checks: no crash, the page was drawn (log), the device state set before Present is
// restored after it, and the dinput8 keyboard vtable now points at the proxy's hooks.
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../../src/common/d3d8_min.h"

namespace {
using CreateFn = void*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(WINAPI*)(void*, UINT, DWORD, HWND, DWORD, d3d8::PresentParameters*, void**);
using ReleaseFn = ULONG(WINAPI*)(void*);
using PresentFn = HRESULT(WINAPI*)(void*, const RECT*, const RECT*, HWND, const void*);
using ClearFn = HRESULT(WINAPI*)(void*, DWORD, const void*, DWORD, DWORD, float, DWORD);
using SetRenderStateFn = HRESULT(WINAPI*)(void*, DWORD, DWORD);
using GetRenderStateFn = HRESULT(WINAPI*)(void*, DWORD, DWORD*);
using DI8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, const GUID&, void**, void*);
using DICreateDeviceFn = HRESULT(WINAPI*)(void*, const GUID&, void**, void*);
constexpr int kDev_Clear = 36, kDev_GetRenderState = 51;

template <typename F>
F Slot(void* obj, int i) {
    return reinterpret_cast<F>((*static_cast<void***>(obj))[i]);
}

int Fail(const char* what) {
    std::printf("FAIL: %s\n", what);
    return 1;
}

bool InModule(const void* p, HMODULE m) {
    HMODULE owner = nullptr;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCSTR>(p), &owner) &&
           owner == m;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) return Fail("usage: settings_smoke <proxy.dll> <log>");
    DeleteFileA(argv[2]);
    SetEnvironmentVariableA("H2E_D3D8LOG_PATH", argv[2]);
    SetEnvironmentVariableA("H2E_SETTINGS_OPEN", "1");
    std::string dir = argv[1];
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    const std::string ini = dir + "h2e.ini", user = dir + "h2e_settings.ini";
    DeleteFileA(user.c_str());
    {
        std::ofstream cfg(ini, std::ios::binary);
        cfg << "[log]\r\nLevel=1\r\n[settings]\r\nEnabled=1\r\n";
    }
    HMODULE di = LoadLibraryA("dinput8.dll");  // the game imports it; the hook needs it loaded
    if (!di) return Fail("LoadLibrary(dinput8)");

    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "h2e_settings_smoke";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "h2e", WS_OVERLAPPEDWINDOW, 0, 0, 1280, 720, nullptr, nullptr,
                              wc.hInstance, nullptr);
    if (!hwnd) return Fail("CreateWindow");
    HMODULE proxy = LoadLibraryA(argv[1]);
    if (!proxy) return Fail("LoadLibrary(proxy)");
    auto create = reinterpret_cast<CreateFn>(GetProcAddress(proxy, "Direct3DCreate8"));
    void* d3d = create ? create(220) : nullptr;
    if (!d3d) return Fail("Direct3DCreate8");
    using DisplayModeFn = HRESULT(WINAPI*)(void*, UINT, d3d8::DisplayMode*);
    d3d8::DisplayMode desktop{};
    Slot<DisplayModeFn>(d3d, d3d8::kD3D_GetAdapterDisplayMode)(d3d, 0, &desktop);
    d3d8::PresentParameters pp{};
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.BackBufferFormat = desktop.Format;
    pp.SwapEffect = 3;  // COPY: the back buffer survives Present (screenshot below)
    pp.Flags = 1;       // D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    void* dev = nullptr;
    HRESULT hr = Slot<CreateDeviceFn>(d3d, d3d8::kD3D_CreateDevice)(d3d, 0, 1, hwnd, 0x20, &pp, &dev);
    if (FAILED(hr)) hr = Slot<CreateDeviceFn>(d3d, d3d8::kD3D_CreateDevice)(d3d, 0, 2, hwnd, 0x20, &pp, &dev);
    if (FAILED(hr) || !dev) return Fail("CreateDevice");

    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    };
    Slot<SetRenderStateFn>(dev, d3d8::kDev_SetRenderState)(dev, d3d8::kRS_ZEnable, 1);
    Slot<SetRenderStateFn>(dev, d3d8::kDev_SetRenderState)(dev, d3d8::kRS_AlphaBlendEnable, 0);
    for (int i = 0; i < 5; ++i) {
        Slot<ClearFn>(dev, kDev_Clear)(dev, 0, nullptr, 1 /*TARGET*/, 0xFF202040, 1.0f, 0);
        hr = Slot<PresentFn>(dev, d3d8::kDev_Present)(dev, nullptr, nullptr, nullptr, nullptr);
        check(SUCCEEDED(hr), "Present");
    }
    // Screenshot of the last frame (COPY swap effect keeps the back buffer): <log>.bmp, for a visual check.
    {
        using GetBackBufferFn = HRESULT(WINAPI*)(void*, UINT, DWORD, void**);
        using SurfLockFn = HRESULT(WINAPI*)(void*, d3d8::LockedRect*, const RECT*, DWORD);
        using SurfUnlockFn = HRESULT(WINAPI*)(void*);
        void* bb = nullptr;
        if (SUCCEEDED(Slot<GetBackBufferFn>(dev, 16)(dev, 0, 0, &bb)) && bb) {
            d3d8::LockedRect lr{};
            if (SUCCEEDED(Slot<SurfLockFn>(bb, 9)(bb, &lr, nullptr, d3d8::kLock_ReadOnly))) {
                const std::string shot = std::string(argv[2]) + ".bmp";
                std::ofstream f(shot, std::ios::binary);
                BITMAPFILEHEADER fh{};
                BITMAPINFOHEADER ih{};
                ih.biSize = sizeof ih;
                ih.biWidth = 1280;
                ih.biHeight = -720;
                ih.biPlanes = 1;
                ih.biBitCount = 32;
                fh.bfType = 0x4D42;
                fh.bfOffBits = sizeof fh + sizeof ih;
                fh.bfSize = fh.bfOffBits + 1280 * 720 * 4;
                f.write(reinterpret_cast<const char*>(&fh), sizeof fh);
                f.write(reinterpret_cast<const char*>(&ih), sizeof ih);
                for (int y = 0; y < 720; ++y) f.write(static_cast<const char*>(lr.pBits) + y * lr.Pitch, 1280 * 4);
                Slot<SurfUnlockFn>(bb, 10)(bb);
            }
            Slot<ReleaseFn>(bb, 2)(bb);
        }
    }
    DWORD z = 0, blend = 1;
    Slot<GetRenderStateFn>(dev, kDev_GetRenderState)(dev, d3d8::kRS_ZEnable, &z);
    Slot<GetRenderStateFn>(dev, kDev_GetRenderState)(dev, d3d8::kRS_AlphaBlendEnable, &blend);
    check(z == 1 && blend == 0, "device state restored after the page was drawn");

    // The keyboard vtable of dinput8 (A flavour) is hooked by the proxy.
    static const GUID kIidA = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
    static const GUID kSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
    auto diCreate = reinterpret_cast<DI8CreateFn>(GetProcAddress(di, "DirectInput8Create"));
    void* dinput = nullptr;
    void* kbd = nullptr;
    if (diCreate && SUCCEEDED(diCreate(GetModuleHandleA(nullptr), 0x0800, kIidA, &dinput, nullptr)) &&
        SUCCEEDED(Slot<DICreateDeviceFn>(dinput, 3)(dinput, kSysKeyboard, &kbd, nullptr)) && kbd) {
        void** vt = *static_cast<void***>(kbd);
        check(InModule(vt[9], proxy) && InModule(vt[10], proxy), "dinput8 GetDeviceState/GetDeviceData hooked");
        Slot<ReleaseFn>(kbd, 2)(kbd);
        Slot<ReleaseFn>(dinput, 2)(dinput);
    } else {
        check(false, "DirectInput keyboard for the vtable check");
    }

    Slot<ReleaseFn>(dev, 2)(dev);
    Slot<ReleaseFn>(d3d, 2)(d3d);
    FreeLibrary(proxy);  // DLL_PROCESS_DETACH is not sent for a FreeLibrary at refcount > 0 (pinned) - fine
    std::ifstream lf(argv[2]);
    std::stringstream log;
    log << lf.rdbuf();
    const std::string text = log.str();
    check(text.find("fix settings     ON") != std::string::npos, "settings enabled in the log");
    check(text.find("settings: DirectInput A device vtable") != std::string::npos, "hook logged");
    DeleteFileA(ini.c_str());
    DeleteFileA(user.c_str());
    if (failures == 0) std::printf("PASS\n");
    return failures == 0 ? 0 : 1;
}
