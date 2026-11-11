// Smoke test for the d3d8 proxy (research logging level, no fix enabled).
// Usage: proxy_smoke <path-to-proxy-d3d8.dll> <log-path>
// Writes h2e.ini ([log] Level=2) next to the proxy, loads it explicitly, creates a real windowed
// HAL (fallback REF) device, issues SetViewport / SetTransform(PROJECTION) / a screen-space draw /
// Present, then checks that the calls succeeded (forwarding works) and the log has the expected lines.
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../../src/common/d3d8_min.h"
#include "../../src/version.h"

namespace {
using CreateFn = void*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(WINAPI*)(void*, UINT, DWORD, HWND, DWORD, d3d8::PresentParameters*, void**);
using ReleaseFn = ULONG(WINAPI*)(void*);
using SetTransformFn = HRESULT(WINAPI*)(void*, DWORD, const d3d8::Matrix*);
using SetViewportFn = HRESULT(WINAPI*)(void*, const d3d8::Viewport*);
using PresentFn = HRESULT(WINAPI*)(void*, const RECT*, const RECT*, HWND, const void*);
using SceneFn = HRESULT(WINAPI*)(void*);
using SetVertexShaderFn = HRESULT(WINAPI*)(void*, DWORD);
using DrawPrimitiveUPFn = HRESULT(WINAPI*)(void*, DWORD, UINT, const void*, UINT);

template <typename F>
F Slot(void* obj, int i) {
    return reinterpret_cast<F>((*static_cast<void***>(obj))[i]);
}

int Fail(const char* what) {
    std::printf("FAIL: %s\n", what);
    return 1;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) return Fail("usage: proxy_smoke <proxy.dll> <log>");
    DeleteFileA(argv[2]);
    SetEnvironmentVariableA("H2E_D3D8LOG_PATH", argv[2]);
    std::string ini = argv[1];
    ini = ini.substr(0, ini.find_last_of("\\/") + 1) + "h2e.ini";
    {
        std::ofstream cfg(ini, std::ios::binary);
        cfg << "[log]\r\nLevel=2\r\n";
    }

    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "h2e_smoke";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("h2e_smoke", "h2e", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, nullptr, nullptr,
                              wc.hInstance, nullptr);
    if (!hwnd) return Fail("CreateWindow");

    HMODULE proxy = LoadLibraryA(argv[1]);
    if (!proxy) return Fail("LoadLibrary(proxy)");
    auto create = reinterpret_cast<CreateFn>(GetProcAddress(proxy, "Direct3DCreate8"));
    if (!create) return Fail("GetProcAddress(Direct3DCreate8)");
    void* d3d = create(220);  // D3D_SDK_VERSION for DirectX 8.1
    if (!d3d) return Fail("Direct3DCreate8 returned NULL");

    // D3D8 windowed mode needs the back buffer in the current desktop format.
    using DisplayModeFn = HRESULT(WINAPI*)(void*, UINT, d3d8::DisplayMode*);
    d3d8::DisplayMode desktop{};
    if (FAILED(Slot<DisplayModeFn>(d3d, d3d8::kD3D_GetAdapterDisplayMode)(d3d, 0, &desktop)))
        return Fail("GetAdapterDisplayMode");
    d3d8::PresentParameters pp{};
    pp.BackBufferWidth = 320;
    pp.BackBufferHeight = 240;
    pp.BackBufferFormat = desktop.Format;
    pp.SwapEffect = 1;        // DISCARD
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    void* dev = nullptr;
    const DWORD kSoftwareVP = 0x20;  // D3DCREATE_SOFTWARE_VERTEXPROCESSING
    HRESULT hr = Slot<CreateDeviceFn>(d3d, d3d8::kD3D_CreateDevice)(d3d, 0, 1, hwnd, kSoftwareVP, &pp, &dev);
    if (FAILED(hr)) hr = Slot<CreateDeviceFn>(d3d, d3d8::kD3D_CreateDevice)(d3d, 0, 2, hwnd, kSoftwareVP, &pp, &dev);
    if (FAILED(hr) || !dev) return Fail("CreateDevice");

    d3d8::Viewport vp{0, 0, 320, 240, 0.0f, 1.0f};
    if (FAILED(Slot<SetViewportFn>(dev, d3d8::kDev_SetViewport)(dev, &vp))) return Fail("SetViewport");
    // LH perspective: fovY = 90 deg (yscale 1), aspect 4/3, zn = 1, zf = 100
    const float zn = 1.0f, zf = 100.0f, ys = 1.0f, xs = ys / (4.0f / 3.0f);
    d3d8::Matrix proj{{{xs, 0, 0, 0}, {0, ys, 0, 0}, {0, 0, zf / (zf - zn), 1}, {0, 0, -zn * zf / (zf - zn), 0}}};
    // Same call site twice with an identical matrix: both forwarded, logged once
    // (dedup is per call site, so the repeat must come from the same instruction).
    for (int i = 0; i < 2; ++i) {
        if (FAILED(Slot<SetTransformFn>(dev, d3d8::kDev_SetTransform)(dev, d3d8::kTS_Projection, &proj)))
            return Fail("SetTransform");
    }
    // Screen-space quad (XYZRHW | DIFFUSE) covering x 10..110, y 20..70.
    struct Vtx {
        float x, y, z, rhw;
        DWORD color;
    };
    const Vtx quad[4] = {{10, 20, 0, 1, 0xffffffff}, {110, 20, 0, 1, 0xffffffff}, {10, 70, 0, 1, 0xffffffff},
                         {110, 70, 0, 1, 0xffffffff}};
    if (FAILED(Slot<SceneFn>(dev, d3d8::kDev_BeginScene)(dev))) return Fail("BeginScene");
    if (FAILED(Slot<SetVertexShaderFn>(dev, d3d8::kDev_SetVertexShader)(dev, d3d8::kFVF_XYZRHW | 0x40)))
        return Fail("SetVertexShader");
    if (FAILED(Slot<DrawPrimitiveUPFn>(dev, d3d8::kDev_DrawPrimitiveUP)(dev, d3d8::kPT_TriangleStrip, 2, quad,
                                                                        sizeof(Vtx))))
        return Fail("DrawPrimitiveUP");
    if (FAILED(Slot<SceneFn>(dev, d3d8::kDev_EndScene)(dev))) return Fail("EndScene");
    Slot<PresentFn>(dev, d3d8::kDev_Present)(dev, nullptr, nullptr, nullptr, nullptr);
    Slot<ReleaseFn>(dev, 2)(dev);
    Slot<ReleaseFn>(d3d, 2)(d3d);
    FreeLibrary(proxy);  // last reference: DLL_PROCESS_DETACH closes the log (lines are flushed anyway)
    DestroyWindow(hwnd);
    DeleteFileA(ini.c_str());

    std::ifstream in(argv[2]);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string log = ss.str();
    const char* expected[] = {"Direct3DCreate8(SDKVersion=220)", "CreateDevice adapter=0",
                              "CreateDevice (in)  BackBuffer=320x240", "CreateDevice -> hr=0x00000000",
                              "SetViewport x=0 y=0 w=320 h=240", "SetTransform(PROJECTION) #1",
                              "fovY=90.000 deg", "aspect(x/y)=1.33333", "zn=1.0000 zf=100.00",
                              "active viewport x=0 y=0 w=320 h=240 aspect(w/h)=1.33333",
                              "2 projection SetTransform calls", "proxy_smoke.exe+0x",
                              "Hitman 2 Enhanced " H2E_VERSION " (d3d8 proxy) attached", "log level 2",
                              "fix resolution   off", "fix horplus      off", "fix timer        off", "fix menuscale    off", "fix framelimit   off",
                              "fix hdfonts      off", "fix settings     off",
                              "Screen2D[total] caller=proxy_smoke.exe+0x",
                              "fvf=0x44 draws=1 buffered=0 bbox=(10.0,20.0)-(110.0,70.0)",
                              "ProjSample[total] caller=proxy_smoke.exe+0x",
                              "fovX=106.260 fovY=90.000 aspect(x/y)=1.33333 viewport=320x240+0+0"};
    for (const char* e : expected) {
        if (log.find(e) == std::string::npos) {
            std::printf("log was:\n%s\n", log.c_str());
            std::printf("FAIL: missing \"%s\"\n", e);
            return 1;
        }
    }
    if (log.find("SetTransform(PROJECTION) #2") != std::string::npos) {
        std::printf("log was:\n%s\n", log.c_str());
        return Fail("duplicate projection was logged twice");
    }
    std::printf("PASS\n");
    return 0;
}
