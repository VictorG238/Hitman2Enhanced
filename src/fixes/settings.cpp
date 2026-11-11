#define NOMINMAX  // before any windows.h (settings.h includes it via d3d8_min.h)
#include "settings.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fix.h"
#include "framelimit.h"
#include "horplus.h"    // kSceneProjectionRetRva, kMinSceneFarPlane
#include "menuscale.h"  // MenuScaleMenuOpen
#include "../version.h"  // version and author shown on the page

namespace h2e {
namespace settings {

const int kFpsChoices[] = {0, 30, 60, 75, 90, 120, 144, 165, 240, 360, -1};
const int kFpsChoiceCount = int(sizeof kFpsChoices / sizeof kFpsChoices[0]);

int FpsChoiceIndex(int maxFps) {
    for (int i = 0; i < kFpsChoiceCount; ++i)
        if (kFpsChoices[i] == maxFps) return i;
    if (maxFps <= 0) return 0;
    int best = 1;
    for (int i = 1; i < kFpsChoiceCount; ++i)
        if (kFpsChoices[i] > 0 && std::abs(kFpsChoices[i] - maxFps) < std::abs(kFpsChoices[best] - maxFps)) best = i;
    return best;
}

double FovDegrees(int percent) {
    const double pi = 3.14159265358979323846;
    return 2.0 * std::atan(std::tan(kBaseFovXDeg * pi / 360.0) * percent / 100.0) * 180.0 / pi;
}

bool ApplyFov(d3d8::Matrix& m, int percent) {
    if (percent == 100 || percent < kFovMin || percent > kFovMax) return false;
    float(*a)[4] = m.m;
    if (a[2][3] != 1.0f || a[3][3] != 0.0f || a[0][0] <= 0.0f || a[1][1] <= 0.0f || a[2][0] != 0.0f ||
        a[2][1] != 0.0f || a[2][2] == 1.0f || a[2][2] == 0.0f)
        return false;
    const double zn = -double(a[3][2]) / a[2][2];
    const double zf = double(a[2][2]) * zn / (double(a[2][2]) - 1.0);
    if (!(zf > kMinSceneFarPlane)) return false;
    const float k = float(100.0 / percent);
    a[0][0] *= k;
    a[1][1] *= k;
    return true;
}

bool ParseResolution(const std::string& text, Resolution* out) {
    unsigned w = 0, h = 0;
    char x = 0;
    if (sscanf_s(text.c_str(), " %u %c %u", &w, &x, 1u, &h) != 3 || (x != 'x' && x != 'X') || w < 640 || h < 480 ||
        w > 16384 || h > 16384)
        return false;
    out->width = w;
    out->height = h;
    return true;
}

std::vector<Resolution> ResolutionChoices(std::vector<Resolution> modes, Resolution current) {
    modes.erase(std::remove_if(modes.begin(), modes.end(),
                               [](const Resolution& r) { return r.width < 640 || r.height < 480; }),
                modes.end());
    if (current.width >= 640 && current.height >= 480) modes.push_back(current);
    std::sort(modes.begin(), modes.end());
    modes.erase(std::unique(modes.begin(), modes.end()), modes.end());
    return modes;
}

namespace {

// [begin, end) of each line without its terminator.
struct LineSpan {
    size_t begin, end, next;
};
std::vector<LineSpan> Lines(const std::string& t) {
    std::vector<LineSpan> out;
    size_t i = 0;
    while (i < t.size()) {
        size_t e = t.find('\n', i);
        const size_t next = e == std::string::npos ? t.size() : e + 1;
        if (e == std::string::npos) e = t.size();
        size_t end = e;
        if (end > i && t[end - 1] == '\r') --end;
        out.push_back({i, end, next});
        i = next;
    }
    return out;
}

bool KeyMatches(const std::string& t, const LineSpan& l, const char* key, size_t* valueBegin) {
    size_t p = l.begin;
    while (p < l.end && (t[p] == ' ' || t[p] == '\t')) ++p;
    const size_t start = p;
    while (p < l.end && t[p] != ' ' && t[p] != '\t' && t[p] != '=') ++p;
    const size_t n = std::strlen(key);
    if (p - start != n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(t[start + i])) != std::tolower(static_cast<unsigned char>(key[i])))
            return false;
    while (p < l.end && (t[p] == ' ' || t[p] == '\t' || t[p] == '=')) ++p;
    *valueBegin = p;
    return true;
}

}  // namespace

bool FindIniLine(const std::string& text, const char* key, std::string* value) {
    for (const LineSpan& l : Lines(text)) {
        size_t v = 0;
        if (!KeyMatches(text, l, key, &v)) continue;
        if (value) {
            size_t e = l.end;
            while (e > v && (text[e - 1] == ' ' || text[e - 1] == '\t')) --e;
            value->assign(text, v, e - v);
        }
        return true;
    }
    return false;
}

std::string SetIniLine(const std::string& text, const char* key, const std::string& value, bool remove) {
    // CRLF (Windows, Hitman2.ini) unless the file uses bare LF.
    const std::string eol = text.find('\n') != std::string::npos && text.find("\r\n") == std::string::npos ? "\n" : "\r\n";
    const std::string line = value.empty() ? std::string(key) : std::string(key) + " " + value;
    std::string out;
    bool done = false;
    for (const LineSpan& l : Lines(text)) {
        size_t v = 0;
        if (KeyMatches(text, l, key, &v)) {
            if (!remove && !done) {
                out += line;
                out.append(text, l.end, l.next - l.end);  // original terminator
            }
            done = true;
            continue;
        }
        out.append(text, l.begin, l.next - l.begin);
    }
    if (!remove && !done) {
        if (!out.empty() && out.back() != '\n') out += eol;
        out += line + eol;
    }
    return out;
}

const char* RowLabel(int row) {
    switch (row) {
        case kRowResolution: return "Resolution";
        case kRowDisplayMode: return "Display mode";
        case kRowFov: return "Field of view";
        case kRowFrameLimit: return "Frame rate limit";
        case kRowDefaults: return "Restore defaults";
        case kRowClose: return "Close";
        default: return "";
    }
}

const char* RowHelp(int row) {
    switch (row) {
        case kRowResolution:
            return "Screen resolution of the game. Pick your monitor's native resolution for the sharpest image. "
                   "Takes effect after restarting the game.";
        case kRowDisplayMode:
            return "Fullscreen or windowed. Takes effect after restarting the game.";
        case kRowFov:
            return "Widens or narrows the gameplay camera; 100% is the original. Applies immediately. "
                   "Experimental: parts of the HUD are drawn with the same camera and move with it.";
        case kRowFrameLimit:
            return "Caps the frame rate. Default: your monitor's refresh rate. The game's timing was made for low "
                   "frame rates: above 1024 FPS animations, physics and cutscenes break (frozen game, broken running "
                   "animation). \"Unlimited\" is not recommended.";
        case kRowDefaults:
            return "Field of view 100% and frame rate limit = monitor refresh rate. Resolution and display mode "
                   "stay as they are.";
        case kRowClose:
            return "Close this page (F10 or Esc).";
        default: return "";
    }
}

bool KeyRepeat::Update(bool isDown, unsigned long nowMs, unsigned long delay, unsigned long rate) {
    if (!isDown) {
        down = false;
        return false;
    }
    if (!down) {
        down = true;
        since = last = nowMs;
        return true;
    }
    if (nowMs - since >= delay && nowMs - last >= rate) {
        last = nowMs;
        return true;
    }
    return false;
}

}  // namespace settings

namespace {

using namespace settings;

void* Method(void* obj, int slot) { return (*static_cast<void***>(obj))[slot]; }

// ---------------------------------------------------------------------------------------------------------------
// Stored choices (h2e_settings.ini next to h2e.ini) and Hitman2.ini.

char g_userIni[MAX_PATH] = {};
char g_gameIni[MAX_PATH] = {};
int g_fov = kFovDefault;
int g_fpsIndex = 0;
std::vector<Resolution> g_resolutions;
int g_resIndex = 0;
bool g_windowed = false;
bool g_resChanged = false, g_modeChanged = false;  // re-applied to Hitman2.ini at exit (the game rewrites it)
LogFn g_log = nullptr;

bool ReadFile(const char* path, std::string* out) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    const DWORD size = GetFileSize(f, nullptr);
    bool ok = size != INVALID_FILE_SIZE && size < (1u << 20);
    if (ok) {
        out->resize(size);
        DWORD got = 0;
        ok = size == 0 || (::ReadFile(f, &(*out)[0], size, &got, nullptr) && got == size);
    }
    CloseHandle(f);
    return ok;
}

// Writes next to the target, then replaces it (a crash cannot leave a half-written Hitman2.ini).
bool WriteFileAtomic(const char* path, const std::string& data) {
    char tmp[MAX_PATH];
    if (std::snprintf(tmp, sizeof tmp, "%s.h2e-tmp", path) >= int(sizeof tmp)) return false;
    HANDLE f = CreateFileA(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = data.empty() || (WriteFile(f, data.data(), DWORD(data.size()), &put, nullptr) && put == data.size());
    CloseHandle(f);
    if (ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileA(tmp);
    return false;
}

void WriteUserInt(const char* section, const char* key, int value) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d", value);
    WritePrivateProfileStringA(section, key, buf, g_userIni);
}

// Applies the pending resolution / display mode to Hitman2.ini. The first change keeps the original lines in
// h2e_settings.ini [original] (restore by hand if needed).
void WriteGameIni(const char* why) {
    if (!g_resChanged && !g_modeChanged) return;
    std::string text;
    if (!ReadFile(g_gameIni, &text)) {
        if (g_log) g_log("settings: %s: cannot read %s", why, g_gameIni);
        return;
    }
    char have[8] = {};
    if (!GetPrivateProfileStringA("original", "Saved", "", have, sizeof have, g_userIni) || !have[0]) {
        std::string res;
        const bool hasRes = FindIniLine(text, "Resolution", &res);
        WritePrivateProfileStringA("original", "Resolution", hasRes ? res.c_str() : "(none)", g_userIni);
        WritePrivateProfileStringA("original", "Window", FindIniLine(text, "Window", nullptr) ? "1" : "0", g_userIni);
        WritePrivateProfileStringA("original", "Saved", "1", g_userIni);
    }
    std::string out = text;
    if (g_resChanged && g_resIndex < int(g_resolutions.size())) {
        char v[32];
        std::snprintf(v, sizeof v, "%ux%u", g_resolutions[g_resIndex].width, g_resolutions[g_resIndex].height);
        out = SetIniLine(out, "Resolution", v);
    }
    if (g_modeChanged) out = SetIniLine(out, "Window", "", !g_windowed);
    if (out == text) return;
    const bool ok = WriteFileAtomic(g_gameIni, out);
    if (g_log) g_log("settings: %s: Hitman2.ini %s", why, ok ? "updated" : "NOT updated (write failed)");
}

void ApplyFps() {
    const int v = kFpsChoices[g_fpsIndex];
    SetFrameLimit(v);
    WriteUserInt("framelimit", "MaxFps", v);
}

// ---------------------------------------------------------------------------------------------------------------
// DirectInput: blank the game's keyboard/mouse data while the page is open, and read it for the page.

using GetStateFn = HRESULT(WINAPI*)(void* self, DWORD cb, void* data);
using GetDataFn = HRESULT(WINAPI*)(void* self, DWORD cbObj, void* rgdod, DWORD* inOut, DWORD flags);
using GetCapsFn = HRESULT(WINAPI*)(void* self, void* caps);

struct HookedVtbl {
    void** vtbl;
    GetStateFn state;
    GetDataFn data;
};
HookedVtbl g_vt[2] = {};
int g_vtCount = 0;

struct DevKind {
    void* dev;
    int kind;  // 1 mouse, 2 keyboard, 0 other
};
DevKind g_devs[8] = {};
int g_devCount = 0;

bool g_open = false;
bool g_holdKeys = false, g_holdMouse = false;  // after closing: keep blanking until released
unsigned char g_kb[256] = {};
long g_mouseDx = 0, g_mouseDy = 0, g_mouseWheel = 0;
bool g_mouseBtn[2] = {}, g_mouseSeen = false;

const HookedVtbl* FindVt(void* self) {
    void** vt = *static_cast<void***>(self);
    for (int i = 0; i < g_vtCount; ++i)
        if (g_vt[i].vtbl == vt) return &g_vt[i];
    return nullptr;
}

int Kind(void* dev) {
    for (int i = 0; i < g_devCount; ++i)
        if (g_devs[i].dev == dev) return g_devs[i].kind;
    struct {
        DWORD dwSize, dwFlags, dwDevType, dwAxes, dwButtons, dwPOVs, dwFFSamplePeriod, dwFFMinTimeResolution,
            dwFirmwareRevision, dwHardwareRevision, dwFFDriverVersion;
    } caps{};
    caps.dwSize = sizeof caps;
    int kind = 0;
    if (SUCCEEDED(reinterpret_cast<GetCapsFn>(Method(dev, 3))(dev, &caps))) {
        const DWORD t = caps.dwDevType & 0xFF;
        kind = t == 0x12 ? 1 : t == 0x13 ? 2 : 0;  // DI8DEVTYPE_MOUSE / DI8DEVTYPE_KEYBOARD
    }
    if (g_devCount < 8) g_devs[g_devCount++] = {dev, kind};
    return kind;
}

bool AnyKeyDown(const unsigned char* kb) {
    for (int i = 0; i < 256; ++i)
        if (kb[i] & 0x80) return true;
    return false;
}

HRESULT WINAPI H_GetDeviceState(void* self, DWORD cb, void* data) {
    const HookedVtbl* vt = FindVt(self);
    if (!vt) return E_FAIL;
    const HRESULT hr = vt->state(self, cb, data);
    if (FAILED(hr) || !data) return hr;
    if (cb == 256) {
        std::memcpy(g_kb, data, 256);
        if (g_holdKeys && !g_open && !AnyKeyDown(g_kb)) g_holdKeys = false;
        if (g_open || g_holdKeys) std::memset(data, 0, cb);
    } else if (cb == 16 || cb == 20) {  // DIMOUSESTATE / DIMOUSESTATE2
        const LONG* axes = static_cast<const LONG*>(data);
        const BYTE* buttons = static_cast<const BYTE*>(data) + 12;
        g_mouseSeen = true;
        g_mouseDx += axes[0];
        g_mouseDy += axes[1];
        g_mouseWheel += axes[2];
        g_mouseBtn[0] = (buttons[0] & 0x80) != 0;
        g_mouseBtn[1] = (buttons[1] & 0x80) != 0;
        if (g_holdMouse && !g_open && !g_mouseBtn[0] && !g_mouseBtn[1]) g_holdMouse = false;
        if (g_open || g_holdMouse) std::memset(data, 0, cb);
    }
    return hr;
}

HRESULT WINAPI H_GetDeviceData(void* self, DWORD cbObj, void* rgdod, DWORD* inOut, DWORD flags) {
    const HookedVtbl* vt = FindVt(self);
    if (!vt) return E_FAIL;
    const HRESULT hr = vt->data(self, cbObj, rgdod, inOut, flags);
    if (FAILED(hr) || !rgdod || !inOut || cbObj < 8 || (flags & 1 /*DIGDD_PEEK*/)) return hr;
    const int kind = Kind(self);
    const BYTE* p = static_cast<const BYTE*>(rgdod);
    for (DWORD i = 0; i < *inOut; ++i, p += cbObj) {
        DWORD ofs, value;
        std::memcpy(&ofs, p, 4);
        std::memcpy(&value, p + 4, 4);
        if (kind == 2 && ofs < 256) {
            g_kb[ofs] = BYTE(value & 0x80);
        } else if (kind == 1) {
            g_mouseSeen = true;
            if (ofs == 0) g_mouseDx += LONG(value);
            else if (ofs == 4) g_mouseDy += LONG(value);
            else if (ofs == 8) g_mouseWheel += LONG(value);
            else if (ofs == 12 || ofs == 13) g_mouseBtn[ofs - 12] = (value & 0x80) != 0;
        }
    }
    if (kind == 2) {
        if (g_holdKeys && !g_open && !AnyKeyDown(g_kb)) g_holdKeys = false;
        if (g_open || g_holdKeys) *inOut = 0;
    } else if (kind == 1) {
        if (g_holdMouse && !g_open && !g_mouseBtn[0] && !g_mouseBtn[1]) g_holdMouse = false;
        if (g_open || g_holdMouse) *inOut = 0;
    }
    return hr;
}

using DI8CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, const GUID&, void**, void*);
using DICreateDeviceFn = HRESULT(WINAPI*)(void* self, const GUID&, void** dev, void* outer);
using ReleaseFn = ULONG(WINAPI*)(void*);

// dinput8 shares one vtable per interface flavour (A/W) between all devices: a keyboard created here exposes the
// vtable the game's devices use. Slots 9 GetDeviceState, 10 GetDeviceData (dinput.h).
void HookDirectInput(const FixContext& ctx) {
    HMODULE di = GetModuleHandleA("dinput8.dll");
    auto create = di ? reinterpret_cast<DI8CreateFn>(GetProcAddress(di, "DirectInput8Create")) : nullptr;
    if (!create) {
        ctx.log("settings: dinput8.dll not loaded - the game's input is not blocked while the page is open");
        return;
    }
    static const GUID kIidA = {0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
    static const GUID kIidW = {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
    static const GUID kSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
    for (const GUID* iid : {&kIidA, &kIidW}) {
        void* dinput = nullptr;
        if (FAILED(create(GetModuleHandleA(nullptr), 0x0800, *iid, &dinput, nullptr)) || !dinput) continue;
        void* dev = nullptr;
        if (SUCCEEDED(reinterpret_cast<DICreateDeviceFn>(Method(dinput, 3))(dinput, kSysKeyboard, &dev, nullptr)) &&
            dev) {
            void** vt = *static_cast<void***>(dev);
            bool seen = false;
            for (int i = 0; i < g_vtCount; ++i) seen = seen || g_vt[i].vtbl == vt;
            DWORD old = 0;
            if (!seen && g_vtCount < 2 && VirtualProtect(vt + 9, 2 * sizeof(void*), PAGE_READWRITE, &old)) {
                g_vt[g_vtCount] = {vt, reinterpret_cast<GetStateFn>(vt[9]), reinterpret_cast<GetDataFn>(vt[10])};
                ++g_vtCount;  // published before the slots point at the hooks
                vt[9] = reinterpret_cast<void*>(&H_GetDeviceState);
                vt[10] = reinterpret_cast<void*>(&H_GetDeviceData);
                VirtualProtect(vt + 9, 2 * sizeof(void*), old, &old);
                FlushInstructionCache(GetCurrentProcess(), vt + 9, 2 * sizeof(void*));
                ctx.log("settings: DirectInput %s device vtable %p hooked (GetDeviceState, GetDeviceData)",
                        iid == &kIidA ? "A" : "W", static_cast<void*>(vt));
            }
            reinterpret_cast<ReleaseFn>(Method(dev, 2))(dev);
        }
        reinterpret_cast<ReleaseFn>(Method(dinput, 2))(dinput);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Page drawing: GDI renders the page into a 32-bit DIB, uploaded to a managed texture when it changes; the page,
// the "F10" hint and the mouse cursor are drawn as pre-transformed quads over the finished frame.

using CreateTextureFn = HRESULT(WINAPI*)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**);
using LockRectFn = HRESULT(WINAPI*)(void*, UINT, d3d8::LockedRect*, const RECT*, DWORD);
using UnlockRectFn = HRESULT(WINAPI*)(void*, UINT);
using DevFn0 = HRESULT(WINAPI*)(void*);
using DevFn1 = HRESULT(WINAPI*)(void*, DWORD);
using DevFn2 = HRESULT(WINAPI*)(void*, DWORD, DWORD);
using DevFn3 = HRESULT(WINAPI*)(void*, DWORD, DWORD, DWORD);
using StateBlockFn = HRESULT(WINAPI*)(void*, DWORD, DWORD*);
using SetTextureFn = HRESULT(WINAPI*)(void*, DWORD, void*);
using SetViewportFn = HRESULT(WINAPI*)(void*, const d3d8::Viewport*);
using DrawUPFn = HRESULT(WINAPI*)(void*, DWORD, UINT, const void*, UINT);

struct Surface {  // a GDI page and its texture
    void* tex = nullptr;
    void* device = nullptr;
    unsigned w = 0, h = 0, texW = 0, texH = 0;
    HDC dc = nullptr;
    HBITMAP bmp = nullptr, oldBmp = nullptr;
    uint32_t* bits = nullptr;
};
Surface g_page, g_hint;

unsigned Pow2(unsigned v) {
    unsigned p = 1;
    while (p < v) p <<= 1;
    return p;
}

void FreeSurface(Surface& s) {
    if (s.tex) reinterpret_cast<ReleaseFn>(Method(s.tex, d3d8::kTex_Release))(s.tex);
    if (s.dc) {
        SelectObject(s.dc, s.oldBmp);
        DeleteObject(s.bmp);
        DeleteDC(s.dc);
    }
    s = Surface{};
}

bool EnsureSurface(Surface& s, void* device, unsigned w, unsigned h) {
    if (s.dc && s.device == device && s.w == w && s.h == h) return true;
    FreeSurface(s);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = LONG(w);
    bi.bmiHeader.biHeight = -LONG(h);  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    s.dc = CreateCompatibleDC(nullptr);
    s.bmp = s.dc ? CreateDIBSection(s.dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!s.bmp) {
        FreeSurface(s);
        return false;
    }
    s.oldBmp = static_cast<HBITMAP>(SelectObject(s.dc, s.bmp));
    s.bits = static_cast<uint32_t*>(bits);
    s.device = device;
    s.w = w;
    s.h = h;
    s.texW = Pow2(w);
    s.texH = Pow2(h);
    auto create = reinterpret_cast<CreateTextureFn>(Method(device, d3d8::kDev_CreateTexture));
    if (FAILED(create(device, s.texW, s.texH, 1, 0, d3d8::kFmt_A8R8G8B8, d3d8::kPool_Managed, &s.tex)) || !s.tex) {
        s.tex = nullptr;
        FreeSurface(s);
        return false;
    }
    return true;
}

// GDI leaves alpha 0: `alpha` is set for the whole page (an opaque-looking panel), text colours stay as drawn.
bool Upload(Surface& s, uint8_t alpha) {
    const uint32_t a = uint32_t(alpha) << 24;
    for (unsigned i = 0, n = s.w * s.h; i < n; ++i) s.bits[i] = (s.bits[i] & 0x00FFFFFFu) | a;
    d3d8::LockedRect lr{};
    if (FAILED(reinterpret_cast<LockRectFn>(Method(s.tex, d3d8::kTex_LockRect))(s.tex, 0, &lr, nullptr, 0)))
        return false;
    for (unsigned y = 0; y < s.texH; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch);
        if (y < s.h) {
            std::memcpy(row, s.bits + size_t(y) * s.w, s.w * 4);
            std::memset(row + s.w, 0, (s.texW - s.w) * 4);
        } else {
            std::memset(row, 0, s.texW * 4);
        }
    }
    reinterpret_cast<UnlockRectFn>(Method(s.tex, d3d8::kTex_UnlockRect))(s.tex, 0);
    return true;
}

HFONT MakeFont(int px, bool bold) {
    // Copperplate Gothic is the menus' typeface; Windows may not have it - GDI then substitutes.
    HFONT f = CreateFontA(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS,
                          CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Copperplate Gothic Bold");
    HDC dc = CreateCompatibleDC(nullptr);
    char face[64] = {};
    HGDIOBJ old = SelectObject(dc, f);
    GetTextFaceA(dc, sizeof face, face);
    SelectObject(dc, old);
    DeleteDC(dc);
    if (_stricmp(face, "Copperplate Gothic Bold") == 0) return f;
    DeleteObject(f);
    return CreateFontA(-px, 0, 0, 0, bold ? FW_BOLD : FW_SEMIBOLD, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
}

// Page geometry in page pixels (scale u = back-buffer height / 1080).
struct Layout {
    double u = 1;
    int w = 0, h = 0, pad = 0, titleY = 0, rowsY = 0, rowH = 0, valueX0 = 0, valueX1 = 0, helpY = 0, footerY = 0;
    RECT Row(int r) const { return RECT{pad / 2, rowsY + r * rowH, w - pad / 2, rowsY + (r + 1) * rowH}; }
};
Layout MakeLayout(unsigned bbH) {
    Layout l;
    l.u = std::max(0.55, bbH / 1080.0);
    auto px = [&](double v) { return int(std::lround(v * l.u)); };
    l.w = px(1120);
    l.h = px(720);
    l.pad = px(56);
    l.titleY = px(36);
    l.rowsY = px(130);
    l.rowH = px(62);
    l.valueX0 = px(500);
    l.valueX1 = l.w - l.pad;
    l.helpY = l.rowsY + kRowCount * l.rowH + px(18);
    l.footerY = l.h - px(46);
    return l;
}

int g_sel = 0;
bool g_dirty = true;
unsigned g_projSeen = 0, g_projChanged = 0;
int g_cursorX = 0, g_cursorY = 0;  // screen pixels
std::string g_message;             // last action, shown in the footer area

std::string ValueText(int row) {
    char buf[96];
    switch (row) {
        case kRowResolution:
            if (g_resolutions.empty()) return "(unknown)";
            std::snprintf(buf, sizeof buf, "%u x %u%s", g_resolutions[g_resIndex].width,
                          g_resolutions[g_resIndex].height, g_resChanged ? "  *" : "");
            return buf;
        case kRowDisplayMode: return std::string(g_windowed ? "Windowed" : "Fullscreen") + (g_modeChanged ? "  *" : "");
        case kRowFov:
            std::snprintf(buf, sizeof buf, "%d%%  (%.0f\xB0)%s", g_fov, FovDegrees(g_fov),
                          g_fov == kFovDefault ? "  original" : "");
            return buf;
        case kRowFrameLimit: {
            const int v = kFpsChoices[g_fpsIndex];
            if (v == 0) return "Monitor refresh rate";
            if (v < 0) return "Unlimited (risky)";
            std::snprintf(buf, sizeof buf, "%d FPS", v);
            return buf;
        }
        default: return "";
    }
}

bool HasValue(int row) { return row <= kRowFrameLimit; }

void RenderPage(const Layout& l) {
    Surface& s = g_page;
    HDC dc = s.dc;
    RECT all{0, 0, l.w, l.h};
    HBRUSH bg = CreateSolidBrush(RGB(14, 14, 16));
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    auto px = [&](double v) { return int(std::lround(v * l.u)); };

    HFONT title = MakeFont(px(40), true), item = MakeFont(px(30), true), helpFont = MakeFont(px(21), false);
    HGDIOBJ old = SelectObject(dc, title);
    SetTextColor(dc, RGB(235, 235, 235));
    RECT tr{l.pad, l.titleY, l.w - l.pad, l.titleY + px(56)};
    DrawTextA(dc, "Hitman 2 Enhanced - Settings", -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT line{l.pad, l.titleY + px(64), l.w - l.pad, l.titleY + px(67)};
    HBRUSH red = CreateSolidBrush(RGB(150, 22, 22));
    FillRect(dc, &line, red);

    SelectObject(dc, item);
    for (int r = 0; r < kRowCount; ++r) {
        RECT rr = l.Row(r);
        if (r == g_sel) {
            RECT bar{rr.left, rr.top + px(4), rr.right, rr.bottom - px(4)};
            FillRect(dc, &bar, red);
        }
        SetTextColor(dc, r == g_sel ? RGB(255, 255, 255) : RGB(170, 175, 180));
        RECT label{l.pad, rr.top, l.valueX0 - px(10), rr.bottom};
        DrawTextA(dc, RowLabel(r), -1, &label, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        if (HasValue(r)) {
            RECT lt{l.valueX0, rr.top, l.valueX0 + px(30), rr.bottom}, rt{l.valueX1 - px(30), rr.top, l.valueX1, rr.bottom};
            DrawTextA(dc, "<", -1, &lt, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            DrawTextA(dc, ">", -1, &rt, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            RECT vr{l.valueX0 + px(30), rr.top, l.valueX1 - px(30), rr.bottom};
            const std::string v = ValueText(r);
            DrawTextA(dc, v.c_str(), -1, &vr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }
    DeleteObject(red);

    SelectObject(dc, helpFont);
    SetTextColor(dc, RGB(200, 200, 200));
    RECT help{l.pad, l.helpY, l.w - l.pad, l.footerY - px(40)};
    std::string h = RowHelp(g_sel);
    if (g_resChanged || g_modeChanged) h += "\n\n* = applies after restarting the game.";
    DrawTextA(dc, h.c_str(), -1, &help, DT_LEFT | DT_WORDBREAK);
    SetTextColor(dc, RGB(150, 40, 40));  // about: version, author, link
    RECT about{l.pad, l.footerY - px(34), l.w - l.pad, l.footerY - px(4)};
    DrawTextA(dc, "v" H2E_VERSION "  -  by " H2E_AUTHOR "  -  " H2E_REPO_URL, -1, &about,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SetTextColor(dc, RGB(130, 130, 130));
    RECT foot{l.pad, l.footerY, l.w - l.pad, l.h - px(12)};
    const std::string f = g_message.empty()
                              ? "Up/Down: select    Left/Right or click: change    F10 / Esc: close"
                              : g_message;
    DrawTextA(dc, f.c_str(), -1, &foot, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(dc, old);
    DeleteObject(title);
    DeleteObject(item);
    DeleteObject(helpFont);
    GdiFlush();
}

void RenderHint(unsigned w, unsigned h, double u) {
    HDC dc = g_hint.dc;
    RECT all{0, 0, LONG(w), LONG(h)};
    HBRUSH bg = CreateSolidBrush(RGB(14, 14, 16));
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    HFONT f = MakeFont(int(std::lround(20 * u)), false);
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, RGB(210, 210, 210));
    DrawTextA(dc, "F10:  Hitman 2 Enhanced settings", -1, &all, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, old);
    DeleteObject(f);
    GdiFlush();
}

struct TexVertex {
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};
struct ColorVertex {
    float x, y, z, rhw;
    DWORD color;
};

struct Draw {
    void* d;
    void Rs(DWORD s, DWORD v) { reinterpret_cast<DevFn2>(Method(d, d3d8::kDev_SetRenderState))(d, s, v); }
    void Tss(DWORD st, DWORD t, DWORD v) {
        reinterpret_cast<DevFn3>(Method(d, d3d8::kDev_SetTextureStageState))(d, st, t, v);
    }
    void Tex(void* t) { reinterpret_cast<SetTextureFn>(Method(d, d3d8::kDev_SetTexture))(d, 0, t); }
    void Fvf(DWORD f) { reinterpret_cast<DevFn1>(Method(d, d3d8::kDev_SetVertexShader))(d, f); }
    void Up(DWORD type, UINT count, const void* v, UINT stride) {
        reinterpret_cast<DrawUPFn>(Method(d, d3d8::kDev_DrawPrimitiveUP))(d, type, count, v, stride);
    }
    void Quad(const Surface& s, float x, float y) {
        const float x0 = x - 0.5f, y0 = y - 0.5f, x1 = x0 + s.w, y1 = y0 + s.h;
        const float u1 = float(s.w) / s.texW, v1 = float(s.h) / s.texH;
        const TexVertex v[4] = {{x0, y0, 0, 1, 0xFFFFFFFF, 0, 0},
                                {x1, y0, 0, 1, 0xFFFFFFFF, u1, 0},
                                {x0, y1, 0, 1, 0xFFFFFFFF, 0, v1},
                                {x1, y1, 0, 1, 0xFFFFFFFF, u1, v1}};
        Tex(s.tex);
        Fvf(d3d8::kFVF_XYZRHW | d3d8::kFVF_Diffuse | d3d8::kFVF_Tex1);
        Tss(0, d3d8::kTSS_ColorOp, d3d8::kTOP_Modulate);
        Tss(0, d3d8::kTSS_AlphaOp, d3d8::kTOP_Modulate);
        Up(d3d8::kPT_TriangleStrip, 2, v, sizeof(TexVertex));
    }
    void Cursor(float x, float y, double u) {
        const float s = float(22 * u);
        const ColorVertex outline[3] = {{x - 2, y - 3, 0, 1, 0xFF000000},
                                        {x - 2, y + s + 3, 0, 1, 0xFF000000},
                                        {x + s * 0.75f + 3, y + s * 0.75f, 0, 1, 0xFF000000}};
        const ColorVertex fill[3] = {{x, y, 0, 1, 0xFFFFFFFF},
                                     {x, y + s, 0, 1, 0xFFFFFFFF},
                                     {x + s * 0.7f, y + s * 0.7f, 0, 1, 0xFFFFFFFF}};
        Tex(nullptr);
        Fvf(d3d8::kFVF_XYZRHW | d3d8::kFVF_Diffuse);
        Tss(0, d3d8::kTSS_ColorOp, d3d8::kTOP_SelectArg1);
        Tss(0, d3d8::kTSS_ColorArg1, d3d8::kTA_Diffuse);
        Tss(0, d3d8::kTSS_AlphaOp, d3d8::kTOP_SelectArg1);
        Tss(0, d3d8::kTSS_AlphaArg1, d3d8::kTA_Diffuse);
        Up(d3d8::kPT_TriangleList, 1, outline, sizeof(ColorVertex));
        Up(d3d8::kPT_TriangleList, 1, fill, sizeof(ColorVertex));
    }
};

// Saves all device state, sets a plain 2D pipeline, runs `fn`, restores the state.
template <class Fn>
void WithOverlayState(void* dev, unsigned bbW, unsigned bbH, Fn fn) {
    DWORD block = 0;
    if (FAILED(reinterpret_cast<StateBlockFn>(Method(dev, d3d8::kDev_CreateStateBlock))(dev, d3d8::kSBT_All, &block)))
        return;
    if (SUCCEEDED(reinterpret_cast<DevFn0>(Method(dev, d3d8::kDev_BeginScene))(dev))) {
        Draw d{dev};
        const d3d8::Viewport vp{0, 0, bbW, bbH, 0.0f, 1.0f};
        reinterpret_cast<SetViewportFn>(Method(dev, d3d8::kDev_SetViewport))(dev, &vp);
        reinterpret_cast<DevFn1>(Method(dev, d3d8::kDev_SetPixelShader))(dev, 0);
        d.Rs(d3d8::kRS_ZEnable, 0);
        d.Rs(d3d8::kRS_ZWriteEnable, 0);
        d.Rs(d3d8::kRS_FillMode, d3d8::kFill_Solid);
        d.Rs(d3d8::kRS_AlphaTestEnable, 0);
        d.Rs(d3d8::kRS_AlphaBlendEnable, 1);
        d.Rs(d3d8::kRS_SrcBlend, d3d8::kBlend_SrcAlpha);
        d.Rs(d3d8::kRS_DestBlend, d3d8::kBlend_InvSrcAlpha);
        d.Rs(d3d8::kRS_CullMode, d3d8::kCull_None);
        d.Rs(d3d8::kRS_FogEnable, 0);
        d.Rs(d3d8::kRS_StencilEnable, 0);
        d.Rs(d3d8::kRS_Lighting, 0);
        d.Rs(d3d8::kRS_ColorWriteEnable, 0xF);
        d.Tss(0, d3d8::kTSS_ColorArg1, d3d8::kTA_Texture);
        d.Tss(0, d3d8::kTSS_ColorArg2, d3d8::kTA_Diffuse);
        d.Tss(0, d3d8::kTSS_AlphaArg1, d3d8::kTA_Texture);
        d.Tss(0, d3d8::kTSS_AlphaArg2, d3d8::kTA_Diffuse);
        d.Tss(0, d3d8::kTSS_TexCoordIndex, 0);
        d.Tss(0, d3d8::kTSS_TextureTransformFlags, 0);
        d.Tss(0, d3d8::kTSS_AddressU, d3d8::kTAddress_Clamp);
        d.Tss(0, d3d8::kTSS_AddressV, d3d8::kTAddress_Clamp);
        d.Tss(0, d3d8::kTSS_MagFilter, d3d8::kTexF_Point);
        d.Tss(0, d3d8::kTSS_MinFilter, d3d8::kTexF_Point);
        d.Tss(0, d3d8::kTSS_MipFilter, d3d8::kTexF_None);
        d.Tss(1, d3d8::kTSS_ColorOp, d3d8::kTOP_Disable);
        d.Tss(1, d3d8::kTSS_AlphaOp, d3d8::kTOP_Disable);
        fn(d);
        reinterpret_cast<DevFn0>(Method(dev, d3d8::kDev_EndScene))(dev);
    }
    reinterpret_cast<DevFn1>(Method(dev, d3d8::kDev_ApplyStateBlock))(dev, block);
    reinterpret_cast<DevFn1>(Method(dev, d3d8::kDev_DeleteStateBlock))(dev, block);
}

// ---------------------------------------------------------------------------------------------------------------
// Page logic.

void Change(int row, int dir) {
    g_message.clear();
    switch (row) {
        case kRowResolution:
            if (g_resolutions.empty()) return;
            g_resIndex = (g_resIndex + dir + int(g_resolutions.size())) % int(g_resolutions.size());
            g_resChanged = true;
            WriteGameIni("resolution changed");
            break;
        case kRowDisplayMode:
            g_windowed = !g_windowed;
            g_modeChanged = true;
            WriteGameIni("display mode changed");
            break;
        case kRowFov:
            g_fov = std::min(kFovMax, std::max(kFovMin, g_fov + dir * kFovStep));
            WriteUserInt("settings", "FovPercent", g_fov);
            break;
        case kRowFrameLimit:
            g_fpsIndex = (g_fpsIndex + dir + kFpsChoiceCount) % kFpsChoiceCount;
            ApplyFps();
            if (!FrameLimitActive()) g_message = "The framelimit fix is off in h2e.ini: the limit is saved but not active.";
            break;
        case kRowDefaults:
            g_fov = kFovDefault;
            WriteUserInt("settings", "FovPercent", g_fov);
            g_fpsIndex = 0;
            ApplyFps();
            g_message = "Defaults restored.";
            break;
        default: break;
    }
    g_dirty = true;
}

void Open(const FixContext& ctx, const Layout& l) {
    g_open = true;
    g_dirty = true;
    g_message.clear();
    g_cursorX = int(ctx.bbWidth / 2);
    g_cursorY = int((ctx.bbHeight - l.h) / 2 + l.rowsY + l.rowH / 2);
    g_mouseDx = g_mouseDy = g_mouseWheel = 0;
    ctx.log("settings: page opened");
}

void Close(const FixContext& ctx) {
    g_open = false;
    g_holdKeys = g_holdMouse = true;  // the closing key/click must not reach the game
    ctx.log("settings: projections at the scene call site since start: %u, FOV applied to %u", g_projSeen,
            g_projChanged);
    ctx.log("settings: page closed (fov %d%%, frame limit %d, resolution %s, display mode %s)", g_fov,
            kFpsChoices[g_fpsIndex], g_resChanged ? "changed" : "unchanged", g_modeChanged ? "changed" : "unchanged");
}

bool KeyDown(int vk, int dik) { return (g_kb[dik] & 0x80) || (GetAsyncKeyState(vk) & 0x8000); }

KeyRepeat g_kF10, g_kEsc, g_kUp, g_kDown, g_kLeft, g_kRight, g_kEnter;
bool g_prevBtn[2] = {};

void OnBeforePresent(const FixContext& ctx) {
    if (!ctx.device || !ctx.bbWidth || !ctx.bbHeight) return;
    HWND wnd = static_cast<HWND>(ctx.deviceWindow);
    const bool focused = !wnd || GetForegroundWindow() == wnd || GetForegroundWindow() == GetAncestor(wnd, GA_ROOT);
    const unsigned long now = GetTickCount();
    const Layout l = MakeLayout(ctx.bbHeight);

    if (g_kF10.Update(focused && KeyDown(VK_F10, 0x44), now, 1u << 30) || (g_open && g_kEsc.Update(focused && KeyDown(VK_ESCAPE, 0x01), now, 1u << 30))) {
        if (g_open) Close(ctx);
        else Open(ctx, l);
    }
    if (!g_open) {
        g_kEsc.Update(false, now);
        g_mouseDx = g_mouseDy = g_mouseWheel = 0;
        if (!MenuScaleMenuOpen()) return;
        const double u = l.u;
        const unsigned hw = unsigned(std::lround(380 * u)), hh = unsigned(std::lround(34 * u));
        if (!EnsureSurface(g_hint, ctx.device, hw, hh)) return;
        static unsigned s_hintFor = 0;
        if (s_hintFor != hh) {
            RenderHint(hw, hh, u);
            if (!Upload(g_hint, 170)) return;
            s_hintFor = hh;
        }
        WithOverlayState(ctx.device, ctx.bbWidth, ctx.bbHeight,
                         [&](Draw& d) { d.Quad(g_hint, float(ctx.bbWidth - hw - 16 * u), float(ctx.bbHeight - hh - 14 * u)); });
        return;
    }

    // Keyboard.
    const int before = g_sel;
    if (g_kUp.Update(focused && KeyDown(VK_UP, 0xC8), now)) g_sel = (g_sel + kRowCount - 1) % kRowCount;
    if (g_kDown.Update(focused && KeyDown(VK_DOWN, 0xD0), now)) g_sel = (g_sel + 1) % kRowCount;
    if (g_kLeft.Update(focused && KeyDown(VK_LEFT, 0xCB), now) && HasValue(g_sel)) Change(g_sel, -1);
    if (g_kRight.Update(focused && KeyDown(VK_RIGHT, 0xCD), now) && HasValue(g_sel)) Change(g_sel, +1);
    if (g_kEnter.Update(focused && KeyDown(VK_RETURN, 0x1C), now, 1u << 30)) {
        if (g_sel == kRowClose) Close(ctx);
        else Change(g_sel, +1);
    }

    // Mouse (DirectInput deltas; the page has its own cursor).
    const int pageX = int(ctx.bbWidth - l.w) / 2, pageY = int(ctx.bbHeight - l.h) / 2;
    g_cursorX = std::min(int(ctx.bbWidth) - 1, std::max(0, g_cursorX + int(g_mouseDx)));
    g_cursorY = std::min(int(ctx.bbHeight) - 1, std::max(0, g_cursorY + int(g_mouseDy)));
    const long wheel = g_mouseWheel;
    const bool moved = g_mouseDx || g_mouseDy;
    g_mouseDx = g_mouseDy = g_mouseWheel = 0;
    const int mx = g_cursorX - pageX, my = g_cursorY - pageY;
    int hover = -1;
    for (int r = 0; r < kRowCount; ++r) {
        const RECT rr = l.Row(r);
        if (mx >= rr.left && mx < rr.right && my >= rr.top && my < rr.bottom) hover = r;
    }
    if (hover >= 0 && moved) g_sel = hover;  // a still mouse does not override the keyboard
    const bool click = focused && g_mouseBtn[0] && !g_prevBtn[0], rclick = focused && g_mouseBtn[1] && !g_prevBtn[1];
    g_prevBtn[0] = g_mouseBtn[0];
    g_prevBtn[1] = g_mouseBtn[1];
    if (hover >= 0 && (click || rclick)) {
        if (hover == kRowClose) {
            Close(ctx);
        } else if (HasValue(hover)) {
            const bool left = mx < (l.valueX0 + l.valueX1) / 2;
            Change(hover, rclick || (click && left && mx >= l.valueX0) ? -1 : +1);
        } else {
            Change(hover, +1);
        }
    }
    if (hover >= 0 && wheel && HasValue(hover)) Change(hover, wheel > 0 ? +1 : -1);
    if (g_sel != before) g_dirty = true;
    if (!g_open) return;

    if (!EnsureSurface(g_page, ctx.device, unsigned(l.w), unsigned(l.h))) return;
    if (g_dirty) {
        RenderPage(l);
        if (!Upload(g_page, 236)) return;
        g_dirty = false;
    }
    WithOverlayState(ctx.device, ctx.bbWidth, ctx.bbHeight, [&](Draw& d) {
        d.Quad(g_page, float(pageX), float(pageY));
        if (g_mouseSeen) d.Cursor(float(g_cursorX), float(g_cursorY), l.u);
    });
}

// ---------------------------------------------------------------------------------------------------------------

void OnAttach(const FixContext& ctx) {
    g_log = ctx.log;
    strcpy_s(g_userIni, ctx.iniPath);
    if (char* slash = std::strrchr(g_userIni, '\\'))
        strcpy_s(slash + 1, sizeof g_userIni - size_t(slash + 1 - g_userIni), "h2e_settings.ini");
    GetModuleFileNameA(nullptr, g_gameIni, MAX_PATH);
    if (char* slash = std::strrchr(g_gameIni, '\\'))
        strcpy_s(slash + 1, sizeof g_gameIni - size_t(slash + 1 - g_gameIni), "Hitman2.ini");

    g_fov = int(GetPrivateProfileIntA("settings", "FovPercent", kFovDefault, g_userIni));
    if (g_fov < kFovMin || g_fov > kFovMax) g_fov = kFovDefault;
    char buf[16] = {};
    if (GetPrivateProfileStringA("framelimit", "MaxFps", "", buf, sizeof buf, g_userIni) && buf[0]) {
        g_fpsIndex = FpsChoiceIndex(std::atoi(buf));
        SetFrameLimit(kFpsChoices[g_fpsIndex]);  // overrides h2e.ini [framelimit] MaxFps (framelimit attached first)
    }

    std::string text;
    Resolution current;
    if (ReadFile(g_gameIni, &text)) {
        std::string v;
        if (FindIniLine(text, "Resolution", &v)) ParseResolution(v, &current);
        g_windowed = FindIniLine(text, "Window", nullptr);
    }
    std::vector<Resolution> modes;
    DEVMODEA dm{};
    dm.dmSize = sizeof dm;
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); ++i)
        if (dm.dmBitsPerPel >= 16) modes.push_back({dm.dmPelsWidth, dm.dmPelsHeight});
    g_resolutions = ResolutionChoices(modes, current);
    g_resIndex = 0;
    for (size_t i = 0; i < g_resolutions.size(); ++i)
        if (g_resolutions[i] == current) g_resIndex = int(i);
    char test[4] = {};
    if (GetEnvironmentVariableA("H2E_SETTINGS_OPEN", test, sizeof test) && test[0] == '1') g_open = true;  // smoke test
    ctx.log("settings: fov %d%%, frame limit %d, Hitman2.ini resolution %ux%u %s, %u display modes; F10 opens the page",
            g_fov, kFpsChoices[g_fpsIndex], current.width, current.height, g_windowed ? "windowed" : "fullscreen",
            unsigned(g_resolutions.size()));
}

void OnDirect3DCreate(const FixContext& ctx, uintptr_t) { HookDirectInput(ctx); }

bool OnProjection(const FixContext& ctx, uintptr_t returnAddress, d3d8::Matrix& m, const d3d8::Viewport&, unsigned,
                  unsigned) {
    if (!ctx.renderD3DBase || returnAddress != ctx.renderD3DBase + kSceneProjectionRetRva) return false;
    // Diagnostics (owner 2026-11-11: "FOV changes nothing"): scene-site projections seen / changed, logged at close.
    ++g_projSeen;
    const bool changed = ApplyFov(m, g_fov);
    if (changed) ++g_projChanged;
    return changed;
}

void OnDetach(const FixContext&) { WriteGameIni("exit"); }  // the game rewrites Hitman2.ini when it shuts down

}  // namespace

extern const Fix kSettingsFix = {"settings",
                                 "EXPERIMENTAL in-game settings page (F10): resolution, display mode, FOV, frame limit",
                                 OnAttach,
                                 OnProjection,
                                 OnDirect3DCreate,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 OnBeforePresent,
                                 OnDetach};

}  // namespace h2e
