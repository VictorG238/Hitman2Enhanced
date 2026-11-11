// End-to-end test of the texture hooks and the `hdfonts` fix through the real proxy and a real D3D8 device.
// Usage: hdfonts_smoke <path-to-proxy-d3d8.dll> <log-path>
// Writes a one-page pack (128x128 -> 512x512) and h2e.ini ([hdfonts] Enabled=1) next to the proxy, creates a
// device, fills a 128x128 A8R8G8B8 texture with the page the way RenderD3D does (LockRect level 0 / UnlockRect),
// binds it with SetTexture and checks that the device holds the 512x512 replacement; a texture with other content
// stays as it is; releasing the original releases the replacement (log lines checked).
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/common/d3d8_min.h"
#include "../../src/fixes/hdfonts.h"

namespace {
using CreateFn = void*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(WINAPI*)(void*, UINT, DWORD, HWND, DWORD, d3d8::PresentParameters*, void**);
using ReleaseFn = ULONG(WINAPI*)(void*);
using CreateTextureFn = HRESULT(WINAPI*)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**);
using SetTextureFn = HRESULT(WINAPI*)(void*, DWORD, void*);
using GetTextureFn = HRESULT(WINAPI*)(void*, DWORD, void**);
using LockRectFn = HRESULT(WINAPI*)(void*, UINT, d3d8::LockedRect*, const RECT*, DWORD);
using UnlockRectFn = HRESULT(WINAPI*)(void*, UINT);
using GetLevelDescFn = HRESULT(WINAPI*)(void*, UINT, d3d8::SurfaceDesc*);
constexpr int kDev_GetTexture = 60;

template <typename F>
F Slot(void* obj, int i) {
    return reinterpret_cast<F>((*static_cast<void***>(obj))[i]);
}

int Fail(const char* what) {
    std::printf("FAIL: %s\n", what);
    return 1;
}

// 128x128 page: 4-bit alpha pattern (as RenderD3D writes it from a PALN palette: alpha 0x00..0xF0).
uint8_t PageAlpha4(unsigned x, unsigned y, int variant) { return static_cast<uint8_t>((x / 8 + y / 16 + variant) & 15); }

void Fill(void* tex, int variant) {
    d3d8::LockedRect lr{};
    Slot<LockRectFn>(tex, d3d8::kTex_LockRect)(tex, 0, &lr, nullptr, 0);
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 0; x < 128; ++x) {
            const uint32_t v = uint32_t(PageAlpha4(x, y, variant) << 4) << 24 | 0xFEFFFF;
            std::memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch + 4 * x, &v, 4);
        }
    Slot<UnlockRectFn>(tex, d3d8::kTex_UnlockRect)(tex, 0);
}

void WritePack(const std::string& path) {
    std::vector<uint8_t> a4(128 * 128);
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 0; x < 128; ++x) a4[y * 128 + x] = PageAlpha4(x, y, 0);
    const uint64_t key = h2e::hdfonts::PageKey(a4.data(), a4.size());
    std::vector<uint8_t> packed;  // PackBits: 512*512 zero bytes as runs of 128
    for (unsigned i = 0; i < 512 * 512 / 128; ++i) packed.insert(packed.end(), {0x81, 0x00});
    std::ofstream f(path, std::ios::binary);
    const uint32_t version = 1, count = 1, rgb = 0xFEFFFF, size = static_cast<uint32_t>(packed.size());
    const uint16_t dims[4] = {128, 128, 512, 512};
    const uint8_t nameLen = 4;
    f.write("H2EF", 4);
    f.write(reinterpret_cast<const char*>(&version), 4);
    f.write(reinterpret_cast<const char*>(&count), 4);
    f.write(reinterpret_cast<const char*>(&key), 8);
    f.write(reinterpret_cast<const char*>(dims), 8);
    f.write(reinterpret_cast<const char*>(&rgb), 4);
    f.write(reinterpret_cast<const char*>(&nameLen), 1);
    f.write("T#00", 4);
    f.write(reinterpret_cast<const char*>(&size), 4);
    f.write(reinterpret_cast<const char*>(packed.data()), size);
}

UINT Width(void* tex) {
    d3d8::SurfaceDesc d{};
    Slot<GetLevelDescFn>(tex, d3d8::kTex_GetLevelDesc)(tex, 0, &d);
    return d.Width;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) return Fail("usage: hdfonts_smoke <proxy.dll> <log>");
    DeleteFileA(argv[2]);
    SetEnvironmentVariableA("H2E_D3D8LOG_PATH", argv[2]);
    std::string dir = argv[1];
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    const std::string ini = dir + "h2e.ini", pack = dir + "hdfonts_smoke.bin";
    WritePack(pack);
    {
        std::ofstream cfg(ini, std::ios::binary);
        cfg << "[log]\r\nLevel=1\r\n[hdfonts]\r\nEnabled=1\r\nPack=hdfonts_smoke.bin\r\n";
    }

    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "h2e_hdfonts_smoke";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "h2e", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr, nullptr,
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
    pp.BackBufferWidth = 320;
    pp.BackBufferHeight = 240;
    pp.BackBufferFormat = desktop.Format;
    pp.SwapEffect = 1;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    void* dev = nullptr;
    HRESULT hr = Slot<CreateDeviceFn>(d3d, d3d8::kD3D_CreateDevice)(d3d, 0, 1, hwnd, 0x20, &pp, &dev);
    if (FAILED(hr)) hr = Slot<CreateDeviceFn>(d3d, d3d8::kD3D_CreateDevice)(d3d, 0, 2, hwnd, 0x20, &pp, &dev);
    if (FAILED(hr) || !dev) return Fail("CreateDevice");

    void* page = nullptr;
    void* other = nullptr;
    auto createTex = Slot<CreateTextureFn>(dev, d3d8::kDev_CreateTexture);
    if (FAILED(createTex(dev, 128, 128, 1, 0, d3d8::kFmt_A8R8G8B8, d3d8::kPool_Managed, &page)) ||
        FAILED(createTex(dev, 128, 128, 1, 0, d3d8::kFmt_A8R8G8B8, d3d8::kPool_Managed, &other)))
        return Fail("CreateTexture");
    Fill(page, 0);
    Fill(other, 1);
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    };
    void* bound = nullptr;
    Slot<SetTextureFn>(dev, d3d8::kDev_SetTexture)(dev, 0, page);
    Slot<GetTextureFn>(dev, kDev_GetTexture)(dev, 0, &bound);
    check(bound && bound != page && Width(bound) == 512, "the page binds its 512x512 replacement");
    if (bound) Slot<ReleaseFn>(bound, 2)(bound);
    Slot<SetTextureFn>(dev, d3d8::kDev_SetTexture)(dev, 0, other);
    Slot<GetTextureFn>(dev, kDev_GetTexture)(dev, 0, &bound);
    check(bound == other, "a texture with other content is bound unchanged");
    if (bound) Slot<ReleaseFn>(bound, 2)(bound);
    Slot<SetTextureFn>(dev, d3d8::kDev_SetTexture)(dev, 0, nullptr);
    Slot<ReleaseFn>(page, 2)(page);
    Slot<ReleaseFn>(other, 2)(other);
    Slot<ReleaseFn>(dev, 2)(dev);
    Slot<ReleaseFn>(d3d, 2)(d3d);
    FreeLibrary(proxy);
    DestroyWindow(hwnd);
    DeleteFileA(ini.c_str());
    DeleteFileA(pack.c_str());

    std::ifstream in(argv[2]);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string log = ss.str();
    const char* expected[] = {"fix hdfonts      ON", "hdfonts: 1 pages from", "(128x128 -> 512x512)",
                              "texture hooks: texture vtable", "LockRect ok, UnlockRect ok, Release ok), SetTexture ok",
                              "hdfonts: T#00 -> 512x512 (A8R8G8B8), 1 live", "not in the pack",
                              "hdfonts: original released, HD copy freed, 0 live"};
    for (const char* e : expected)
        if (log.find(e) == std::string::npos) {
            std::printf("missing log line \"%s\"\n", e);
            ++failures;
        }
    if (failures) {
        std::printf("log was:\n%s\n", log.c_str());
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}
