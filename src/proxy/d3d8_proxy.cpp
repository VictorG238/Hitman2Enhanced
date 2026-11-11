// Hitman 2 Enhanced - d3d8.dll proxy: hosts the fixes (src/fixes, D-009) and the research logger (EXP-003).
//
// Placed next to the game as d3d8.dll, it loads the real system d3d8.dll,
// forwards Direct3DCreate8 and wraps a few vtable slots of IDirect3D8 and
// IDirect3DDevice8. With no fix enabled every wrapper calls the original with
// identical arguments and returns its result unchanged.
//
// Configuration: h2e.ini next to this DLL (all fixes off by default)
//   [log]    Level=0|1|2   0 = no log file, 1 = setup and fix outcomes (default), 2 = research logging below
//   [<fix>]  Enabled=0|1   one section per fix id in src/fixes/registry.cpp
// Nothing is written to disk except the log; removing the DLL (and h2e.ini) restores the game.
//
// Research logging (Level=2): adapter modes, CreateDevice/Reset parameters, viewports and
// projection matrices (deduplicated), frame counts, and for each event the
// caller as module+offset; and (0.4.0, EXP-003 run-005):
//   * screen-space draws: DrawPrimitiveUP / DrawIndexedPrimitiveUP with a
//     pre-transformed FVF (D3DFVF_XYZRHW) - per call site the draw count and the
//     bounding box of the vertex x/y positions (pixels); buffered draws with that
//     FVF are counted (their vertices are not read);
//   * every 5 s, per projection call site, the latest matrix as fovX/fovY/aspect
//     with its viewport (so a busy cutscene cannot hide the gameplay camera);
//   * distinct SetRenderTarget call sites.
//
// Control (environment variables, read once at load):
//   H2E_D3D8LOG=0          pure pass-through, nothing is logged or hooked
//   H2E_D3D8LOG_PATH=file  log file (default: h2e.log next to this DLL)
#include <windows.h>
#include <intrin.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <share.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstring>

#include "../common/d3d8_min.h"
#include "../fixes/fix.h"
#include "../version.h"

#pragma intrinsic(_ReturnAddress)

namespace {

using CreateFn = void*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(WINAPI*)(void*, UINT, DWORD, HWND, DWORD, d3d8::PresentParameters*, void**);
using ModeCountFn = UINT(WINAPI*)(void*, UINT);
using EnumModesFn = HRESULT(WINAPI*)(void*, UINT, UINT, d3d8::DisplayMode*);
using DisplayModeFn = HRESULT(WINAPI*)(void*, UINT, d3d8::DisplayMode*);
using ResetFn = HRESULT(WINAPI*)(void*, d3d8::PresentParameters*);
using PresentFn = HRESULT(WINAPI*)(void*, const RECT*, const RECT*, HWND, const void*);
using SetTransformFn = HRESULT(WINAPI*)(void*, DWORD, const d3d8::Matrix*);
using SetViewportFn = HRESULT(WINAPI*)(void*, const d3d8::Viewport*);
using SetRenderTargetFn = HRESULT(WINAPI*)(void*, void*, void*);
using SetVertexShaderFn = HRESULT(WINAPI*)(void*, DWORD);
using DrawPrimitiveFn = HRESULT(WINAPI*)(void*, DWORD, UINT, UINT);
using DrawIndexedPrimitiveFn = HRESULT(WINAPI*)(void*, DWORD, UINT, UINT, UINT, UINT);
using DrawPrimitiveUPFn = HRESULT(WINAPI*)(void*, DWORD, UINT, const void*, UINT);
using DrawIndexedPrimitiveUPFn = HRESULT(WINAPI*)(void*, DWORD, UINT, UINT, UINT, const void*, DWORD, const void*,
                                                  UINT);
using CreateTextureFn = HRESULT(WINAPI*)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**);
using SetTextureFn = HRESULT(WINAPI*)(void*, DWORD, void*);
using ReleaseFn = ULONG(WINAPI*)(void*);
using GetLevelDescFn = HRESULT(WINAPI*)(void*, UINT, d3d8::SurfaceDesc*);
using LockRectFn = HRESULT(WINAPI*)(void*, UINT, d3d8::LockedRect*, const RECT*, DWORD);
using UnlockRectFn = HRESULT(WINAPI*)(void*, UINT);

HMODULE g_self = nullptr;
HMODULE g_real = nullptr;
CreateFn g_realCreate = nullptr;
bool g_enabled = true;
FILE* g_log = nullptr;
CRITICAL_SECTION g_lock;
ULONGLONG g_t0 = 0;

CreateDeviceFn o_CreateDevice = nullptr;
ModeCountFn o_GetAdapterModeCount = nullptr;
EnumModesFn o_EnumAdapterModes = nullptr;
DisplayModeFn o_GetAdapterDisplayMode = nullptr;
ResetFn o_Reset = nullptr;
PresentFn o_Present = nullptr;
SetTransformFn o_SetTransform = nullptr;
SetViewportFn o_SetViewport = nullptr;
SetRenderTargetFn o_SetRenderTarget = nullptr;
SetVertexShaderFn o_SetVertexShader = nullptr;
DrawPrimitiveFn o_DrawPrimitive = nullptr;
DrawIndexedPrimitiveFn o_DrawIndexedPrimitive = nullptr;
DrawPrimitiveUPFn o_DrawPrimitiveUP = nullptr;
DrawIndexedPrimitiveUPFn o_DrawIndexedPrimitiveUP = nullptr;
SetTextureFn o_SetTexture = nullptr;
ReleaseFn o_TexRelease = nullptr;
LockRectFn o_TexLockRect = nullptr;
UnlockRectFn o_TexUnlockRect = nullptr;

// Whole-level texture locks in progress (texture, level) -> locked memory, for onTextureFilled (protected by g_lock).
struct PendingLock {
    void* texture;
    UINT level;
    d3d8::LockedRect rect;
};
PendingLock g_locks[16];
size_t g_lockCount = 0;

// Screen-space (pre-transformed) draw statistics per call site, for the current
// 5-second window and for the whole session.
struct Box {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    void Add(float x, float y) {
        if (x < x0) x0 = x;
        if (y < y0) y0 = y;
        if (x > x1) x1 = x;
        if (y > y1) y1 = y;
    }
    void Add(const Box& b) {
        if (b.x0 <= b.x1) {
            Add(b.x0, b.y0);
            Add(b.x1, b.y1);
        }
    }
    bool Valid() const { return x0 <= x1; }
};
struct ScreenStats {
    unsigned draws = 0, buffered = 0;
    Box box;
};
struct ScreenSite {
    ScreenStats window, total;
    DWORD fvf = 0;
};
struct ProjSample {
    d3d8::Matrix m{};
    d3d8::Viewport vp{};
    bool fresh = false;
};
std::unordered_map<void*, ScreenSite> g_screen;
std::unordered_map<void*, ProjSample> g_projLast;
std::unordered_set<void*> g_rtSeen;
std::unordered_map<void*, std::string> g_desc;  // call-site descriptions, cached while modules are loaded
DWORD g_shader = 0;

// Configuration and fix state.
int g_logLevel = 1;
bool g_fixEnabled[16] = {};  // indexed like h2e::kFixes
bool g_anyProjectionFix = false;
bool g_anyViewportFix = false;
bool g_anyPresentFix = false;
char g_iniPath[MAX_PATH] = {};
h2e::FixContext g_ctx{nullptr, 0, g_iniPath, nullptr, nullptr};
bool g_anyTextureFix = false;
UINT g_bbWidth = 0, g_bbHeight = 0;
unsigned g_projectionFixCalls = 0;

bool Research() { return g_logLevel >= 2; }  // last SetVertexShader handle (an FVF code when bit 0 is clear)

// Deduplication / rate limiting state (protected by g_lock).
// Each distinct (projection matrix, active viewport) pair and each distinct viewport is
// logged once; a 5-second summary line reports call counts.
// Per call site (return address): a busy pass such as the shadow pass cannot starve others.
std::unordered_map<void*, std::unordered_set<uint64_t>> g_projSeen;
size_t g_projDistinct = 0;
std::unordered_set<uint64_t> g_vpSeen;
unsigned g_projCalls = 0;
unsigned g_projCallsAtLastSummary = 0;
d3d8::Viewport g_curVp{};
bool g_haveVp = false;
unsigned g_frames = 0;
ULONGLONG g_lastFpsTick = 0;
unsigned g_framesAtLastFps = 0;
unsigned g_fpsLines = 0;
unsigned char g_modeSeen[2048] = {};

constexpr size_t kMaxDistinctProjPerCaller = 1500;
constexpr size_t kMaxDistinctVp = 500;
constexpr unsigned kMaxFpsLines = 1000;

uint64_t Fnv1a(const void* data, size_t n, uint64_t h = 1469598103934665603ULL) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ULL;
    return h;
}

void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_lock);
    fprintf(g_log, "[%8llu ms] ", GetTickCount64() - g_t0);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_lock);
}

// "module+0xOFFSET (va 0x...)" for a code address.
void Describe(void* addr, char* out, size_t n) {
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(addr), &mod)) {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(mod, path, MAX_PATH);
        const char* base = strrchr(path, '\\');
        base = base ? base + 1 : path;
        snprintf(out, n, "%s+0x%X (va 0x%08X, base 0x%08X)", base,
                 static_cast<unsigned>(reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod)),
                 static_cast<unsigned>(reinterpret_cast<uintptr_t>(addr)),
                 static_cast<unsigned>(reinterpret_cast<uintptr_t>(mod)));
    } else {
        snprintf(out, n, "va 0x%08X", static_cast<unsigned>(reinterpret_cast<uintptr_t>(addr)));
    }
}

bool IsShimOrSelf(void* addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCSTR>(addr), &mod))
        return false;
    if (mod == g_self) return true;
    char path[MAX_PATH] = {};
    GetModuleFileNameA(mod, path, MAX_PATH);
    const char* base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    // Windows application-compatibility shim modules interpose on d3d8.dll calls.
    return _stricmp(base, "apphelp.dll") == 0 || _strnicmp(base, "Ac", 2) == 0;
}

// Caller description: first stack frame outside this DLL and the shim layer,
// plus the raw chain of up to 4 frames for verification.
void Caller(void* immediate, char* out, size_t n) {
    void* frames[8] = {};
    USHORT got = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
    void* best = immediate;
    if (IsShimOrSelf(immediate)) {
        for (USHORT i = 0; i < got; ++i) {
            if (!IsShimOrSelf(frames[i])) {
                best = frames[i];
                break;
            }
        }
    }
    char a[160];
    Describe(best, a, sizeof a);
    size_t used = static_cast<size_t>(snprintf(out, n, "%s", a));
    if (best != immediate && used < n) {
        char b[160];
        Describe(immediate, b, sizeof b);
        snprintf(out + used, n - used, " [via %s]", b);
    }
}

void* Patch(void* obj, int slot, void* hook) {
    void** vtbl = *static_cast<void***>(obj);
    void* orig = vtbl[slot];
    if (orig == hook) return nullptr;
    DWORD old = 0;
    if (!VirtualProtect(&vtbl[slot], sizeof(void*), PAGE_READWRITE, &old)) return nullptr;
    vtbl[slot] = hook;
    VirtualProtect(&vtbl[slot], sizeof(void*), old, &old);
    return orig;
}

void LogPresentParams(const char* tag, const d3d8::PresentParameters* p) {
    if (!p) {
        Log("%s pPresentationParameters=NULL", tag);
        return;
    }
    Log("%s BackBuffer=%ux%u fmt=%lu(%s) count=%u MultiSample=%lu SwapEffect=%lu hDeviceWindow=%p Windowed=%d "
        "AutoDepthStencil=%d DepthFmt=%lu(%s) Flags=0x%lX RefreshHz=%u PresentInterval=0x%X",
        tag, p->BackBufferWidth, p->BackBufferHeight, p->BackBufferFormat, d3d8::FormatName(p->BackBufferFormat),
        p->BackBufferCount, p->MultiSampleType, p->SwapEffect, static_cast<void*>(p->hDeviceWindow), p->Windowed,
        p->EnableAutoDepthStencil, p->AutoDepthStencilFormat, d3d8::FormatName(p->AutoDepthStencilFormat), p->Flags,
        p->FullScreen_RefreshRateInHz, p->FullScreen_PresentationInterval);
}

void LogWindow(HWND hwnd) {
    if (!hwnd) return;
    RECT wr{}, cr{};
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    Log("  window %p rect=(%ld,%ld)-(%ld,%ld) client=%ldx%ld style=0x%08lX", static_cast<void*>(hwnd), wr.left,
        wr.top, wr.right, wr.bottom, cr.right - cr.left, cr.bottom - cr.top,
        static_cast<unsigned long>(GetWindowLongA(hwnd, GWL_STYLE)));
}

// "fovX=.. fovY=.. aspect=.." for a standard LH perspective matrix, else "non-perspective".
void ProjText(const d3d8::Matrix& m, char* out, size_t n) {
    const float(*a)[4] = m.m;
    if (a[2][3] == 1.0f && a[3][3] == 0.0f && a[0][0] != 0.0f && a[1][1] != 0.0f) {
        const double k = 180.0 / 3.14159265358979;
        snprintf(out, n, "fovX=%.3f fovY=%.3f aspect(x/y)=%.5f", 2.0 * atan(1.0 / a[0][0]) * k,
                 2.0 * atan(1.0 / a[1][1]) * k, double(a[1][1]) / a[0][0]);
    } else {
        snprintf(out, n, "non-perspective _11=%.6f _22=%.6f _41=%.4f _42=%.4f _44=%.4f", a[0][0], a[1][1], a[3][0],
                 a[3][1], a[3][3]);
    }
}

// Description of a call site, cached: at process exit RenderD3D may already be unloaded.
std::string SiteName(void* site) {
    EnterCriticalSection(&g_lock);
    auto it = g_desc.find(site);
    std::string cached = it != g_desc.end() ? it->second : std::string();
    LeaveCriticalSection(&g_lock);
    if (!cached.empty()) return cached;
    char who[400];
    Describe(site, who, sizeof who);  // outside g_lock: takes the loader lock
    EnterCriticalSection(&g_lock);
    g_desc[site] = who;
    LeaveCriticalSection(&g_lock);
    return who;
}

// Periodic (and final) summaries of screen-space draws and of the latest projection per call site.
void LogSummaries(bool final) {
    struct Row {
        void* site;
        ScreenSite s;
    };
    struct PRow {
        void* site;
        ProjSample p;
    };
    Row rows[64];
    PRow prows[16];
    size_t nr = 0, np = 0;
    EnterCriticalSection(&g_lock);
    for (auto& kv : g_screen) {
        if (nr < 64 && (final ? kv.second.total.draws + kv.second.total.buffered
                              : kv.second.window.draws + kv.second.window.buffered))
            rows[nr++] = {kv.first, kv.second};
        kv.second.window = ScreenStats{};
    }
    for (auto& kv : g_projLast) {
        if (np < 16 && (final || kv.second.fresh)) prows[np++] = {kv.first, kv.second};
        kv.second.fresh = false;
    }
    LeaveCriticalSection(&g_lock);
    const char* tag = final ? "total" : "5s";
    for (size_t i = 0; i < nr; ++i) {
        const ScreenStats& st = final ? rows[i].s.total : rows[i].s.window;
        const std::string who = SiteName(rows[i].site);
        if (st.box.Valid())
            Log("Screen2D[%s] caller=%s fvf=0x%lX draws=%u buffered=%u bbox=(%.1f,%.1f)-(%.1f,%.1f)", tag, who.c_str(),
                rows[i].s.fvf, st.draws, st.buffered, st.box.x0, st.box.y0, st.box.x1, st.box.y1);
        else
            Log("Screen2D[%s] caller=%s fvf=0x%lX draws=%u buffered=%u bbox=none", tag, who.c_str(), rows[i].s.fvf, st.draws,
                st.buffered);
    }
    for (size_t i = 0; i < np; ++i) {
        char txt[160];
        const std::string who = SiteName(prows[i].site);
        ProjText(prows[i].p.m, txt, sizeof txt);
        const d3d8::Viewport& vp = prows[i].p.vp;
        Log("ProjSample[%s] caller=%s %s viewport=%lux%lu+%lu+%lu", tag, who.c_str(), txt, vp.Width, vp.Height, vp.X, vp.Y);
    }
}

bool IsPretransformed(DWORD shader) {
    return (shader & 1) == 0 && (shader & d3d8::kFVF_PositionMask) == d3d8::kFVF_XYZRHW;
}

// Records one screen-space draw; vertices may be null (buffered draw: counted only).
void NoteScreenDraw(void* ra, const void* verts, UINT first, UINT count, UINT stride) {
    Box b;
    if (verts && stride >= 8) {
        const auto* p = static_cast<const unsigned char*>(verts) + size_t(first) * stride;
        for (UINT i = 0; i < count; ++i, p += stride) {
            float xy[2];
            memcpy(xy, p, sizeof xy);
            b.Add(xy[0], xy[1]);
        }
    }
    EnterCriticalSection(&g_lock);
    ScreenSite& s = g_screen[ra];
    s.fvf = g_shader;
    ScreenStats* both[2] = {&s.window, &s.total};
    for (ScreenStats* st : both) {
        if (verts) {
            ++st->draws;
            st->box.Add(b);
        } else {
            ++st->buffered;
        }
    }
    LeaveCriticalSection(&g_lock);
}

// ---- IDirect3DDevice8 wrappers ---------------------------------------------

HRESULT WINAPI H_SetVertexShader(void* self, DWORD handle) {
    g_shader = handle;  // draws run on the render thread; a plain store is enough
    return o_SetVertexShader(self, handle);
}

HRESULT WINAPI H_DrawPrimitiveUP(void* self, DWORD type, UINT count, const void* verts, UINT stride) {
    if (IsPretransformed(g_shader)) NoteScreenDraw(_ReturnAddress(), verts, 0, d3d8::VertexCount(type, count), stride);
    return o_DrawPrimitiveUP(self, type, count, verts, stride);
}

HRESULT WINAPI H_DrawIndexedPrimitiveUP(void* self, DWORD type, UINT minIndex, UINT numVerts, UINT count,
                                        const void* indices, DWORD indexFmt, const void* verts, UINT stride) {
    if (IsPretransformed(g_shader)) NoteScreenDraw(_ReturnAddress(), verts, minIndex, numVerts, stride);
    return o_DrawIndexedPrimitiveUP(self, type, minIndex, numVerts, count, indices, indexFmt, verts, stride);
}

HRESULT WINAPI H_DrawPrimitive(void* self, DWORD type, UINT start, UINT count) {
    if (IsPretransformed(g_shader)) NoteScreenDraw(_ReturnAddress(), nullptr, 0, 0, 0);
    return o_DrawPrimitive(self, type, start, count);
}

HRESULT WINAPI H_DrawIndexedPrimitive(void* self, DWORD type, UINT minIndex, UINT numVerts, UINT start, UINT count) {
    if (IsPretransformed(g_shader)) NoteScreenDraw(_ReturnAddress(), nullptr, 0, 0, 0);
    return o_DrawIndexedPrimitive(self, type, minIndex, numVerts, start, count);
}

HRESULT WINAPI H_SetRenderTarget(void* self, void* rt, void* zs) {
    void* ra = _ReturnAddress();
    EnterCriticalSection(&g_lock);
    bool log = g_rtSeen.size() < 64 && g_rtSeen.insert(ra).second;
    LeaveCriticalSection(&g_lock);
    if (log) {
        char who[400];
        Caller(ra, who, sizeof who);
        Log("SetRenderTarget rt=%p zs=%p caller=%s", rt, zs, who);
    }
    return o_SetRenderTarget(self, rt, zs);
}

HRESULT WINAPI H_Reset(void* self, d3d8::PresentParameters* pp) {
    char who[400];
    Caller(_ReturnAddress(), who, sizeof who);
    LogPresentParams("Reset (in) ", pp);
    HRESULT hr = o_Reset(self, pp);
    if (SUCCEEDED(hr) && pp) {
        EnterCriticalSection(&g_lock);
        g_bbWidth = pp->BackBufferWidth;
        g_bbHeight = pp->BackBufferHeight;
        g_ctx.bbWidth = g_bbWidth;
        g_ctx.bbHeight = g_bbHeight;
        LeaveCriticalSection(&g_lock);
    }
    Log("Reset -> hr=0x%08lX caller=%s", hr, who);
    return hr;
}

HRESULT WINAPI H_Present(void* self, const RECT* src, const RECT* dst, HWND wnd, const void* dirty) {
    if (g_anyPresentFix)
        for (size_t i = 0; i < h2e::kFixCount; ++i)
            if (g_fixEnabled[i] && h2e::kFixes[i]->onBeforePresent) h2e::kFixes[i]->onBeforePresent(g_ctx);
    HRESULT hr = o_Present(self, src, dst, wnd, dirty);
    if (g_anyPresentFix)
        for (size_t i = 0; i < h2e::kFixCount; ++i)
            if (g_fixEnabled[i] && h2e::kFixes[i]->onPresent) h2e::kFixes[i]->onPresent(g_ctx);
    if (!Research()) return hr;
    ULONGLONG now = GetTickCount64();
    EnterCriticalSection(&g_lock);
    ++g_frames;
    bool report = false;
    unsigned frames = 0, projCalls = 0;
    double secs = 0;
    d3d8::Viewport vp{};
    size_t distinctProj = 0, distinctVp = 0;
    if (g_lastFpsTick == 0) {
        g_lastFpsTick = now;
    } else if (now - g_lastFpsTick >= 5000 && g_fpsLines < kMaxFpsLines) {
        frames = g_frames - g_framesAtLastFps;
        secs = (now - g_lastFpsTick) / 1000.0;
        projCalls = g_projCalls - g_projCallsAtLastSummary;
        g_projCallsAtLastSummary = g_projCalls;
        g_framesAtLastFps = g_frames;
        g_lastFpsTick = now;
        ++g_fpsLines;
        vp = g_curVp;
        distinctProj = g_projDistinct;
        distinctVp = g_vpSeen.size();
        report = true;
    }
    unsigned total = g_frames;
    LeaveCriticalSection(&g_lock);
    if (report)
        Log("Present: %u frames in %.2fs = %.1f fps (total %u); projection calls %u; viewport now %lux%lu+%lu+%lu; "
            "distinct proj=%zu vp=%zu",
            frames, secs, frames / secs, total, projCalls, vp.Width, vp.Height, vp.X, vp.Y, distinctProj, distinctVp);
    if (report) {
        if (g_anyProjectionFix) Log("projection fixes: %u projections adjusted so far", g_projectionFixCalls);
        LogSummaries(false);
    }
    return hr;
}

HRESULT WINAPI H_SetTransform(void* self, DWORD state, const d3d8::Matrix* m) {
    if (Research() && state == d3d8::kTS_Projection && m) {
        void* ra = _ReturnAddress();
        EnterCriticalSection(&g_lock);
        ++g_projCalls;
        const d3d8::Viewport vp = g_curVp;
        const bool haveVp = g_haveVp;
        const uint64_t key = Fnv1a(&vp, sizeof vp, Fnv1a(m, sizeof *m));
        auto& seen = g_projSeen[ra];
        bool log = seen.size() < kMaxDistinctProjPerCaller && seen.insert(key).second;
        ProjSample& last = g_projLast[ra];
        last.m = *m;
        last.vp = vp;
        last.fresh = true;
        if (log) ++g_projDistinct;
        unsigned calls = g_projCalls;
        LeaveCriticalSection(&g_lock);
        if (log) {
            char who[400];
            Caller(ra, who, sizeof who);
            const float(*a)[4] = m->m;
            Log("SetTransform(PROJECTION) #%u caller=%s", calls, who);
            if (haveVp)
                Log("  active viewport x=%lu y=%lu w=%lu h=%lu aspect(w/h)=%.5f", vp.X, vp.Y, vp.Width, vp.Height,
                    vp.Height ? double(vp.Width) / vp.Height : 0.0);
            for (int r = 0; r < 4; ++r) Log("  [% .6f % .6f % .6f % .6f]", a[r][0], a[r][1], a[r][2], a[r][3]);
            // Interpretation valid only for a standard left-handed perspective matrix
            // (_12.._14,_21,_23,_24,_31,_32,_41,_42,_44 == 0 and _34 == 1).
            if (a[2][3] == 1.0f && a[3][3] == 0.0f && a[0][0] != 0.0f && a[1][1] != 0.0f && a[2][2] != 1.0f) {
                double yscale = a[1][1], xscale = a[0][0];
                double fovy = 2.0 * atan(1.0 / yscale) * 180.0 / 3.14159265358979;
                double fovx = 2.0 * atan(1.0 / xscale) * 180.0 / 3.14159265358979;
                double zn = -a[3][2] / a[2][2];
                double zf = a[2][2] * zn / (a[2][2] - 1.0);
                Log("  as LH perspective: fovY=%.3f deg fovX=%.3f deg aspect(x/y)=%.5f zn=%.4f zf=%.2f "
                    "offcenter=(%.4f,%.4f)",
                    fovy, fovx, yscale / xscale, zn, zf, a[2][0], a[2][1]);
            }
        }
    }
    if (g_anyProjectionFix && state == d3d8::kTS_Projection && m) {
        EnterCriticalSection(&g_lock);
        const d3d8::Viewport vp = g_curVp;
        const UINT w = g_bbWidth, h = g_bbHeight;
        LeaveCriticalSection(&g_lock);
        const auto ra = reinterpret_cast<uintptr_t>(_ReturnAddress());
        d3d8::Matrix fixed = *m;
        bool changed = false;
        for (size_t i = 0; i < h2e::kFixCount; ++i)
            if (g_fixEnabled[i] && h2e::kFixes[i]->onProjection)
                changed |= h2e::kFixes[i]->onProjection(g_ctx, ra, fixed, vp, w, h);
        if (changed) {
            EnterCriticalSection(&g_lock);
            ++g_projectionFixCalls;
            LeaveCriticalSection(&g_lock);
            return o_SetTransform(self, state, &fixed);
        }
    }
    return o_SetTransform(self, state, m);
}

HRESULT WINAPI H_SetViewport(void* self, const d3d8::Viewport* vp) {
    if (vp && g_anyViewportFix) {  // when a fix changes the viewport, research logging is skipped for that call
        EnterCriticalSection(&g_lock);
        const UINT w = g_bbWidth, h = g_bbHeight;
        LeaveCriticalSection(&g_lock);
        d3d8::Viewport fixed = *vp;
        bool changed = false;
        for (size_t i = 0; i < h2e::kFixCount; ++i)
            if (g_fixEnabled[i] && h2e::kFixes[i]->onViewport) changed |= h2e::kFixes[i]->onViewport(g_ctx, fixed, w, h);
        if (changed) {
            EnterCriticalSection(&g_lock);
            g_curVp = fixed;
            g_haveVp = true;
            LeaveCriticalSection(&g_lock);
            return o_SetViewport(self, &fixed);
        }
    }
    if (vp) {
        void* ra = _ReturnAddress();
        EnterCriticalSection(&g_lock);
        g_curVp = *vp;
        g_haveVp = true;
        bool log = Research() && g_vpSeen.size() < kMaxDistinctVp && g_vpSeen.insert(Fnv1a(vp, sizeof *vp)).second;
        LeaveCriticalSection(&g_lock);
        if (log) {
            char who[400];
            Caller(ra, who, sizeof who);
            Log("SetViewport x=%lu y=%lu w=%lu h=%lu minZ=%.3f maxZ=%.3f aspect(w/h)=%.5f caller=%s", vp->X, vp->Y,
                vp->Width, vp->Height, vp->MinZ, vp->MaxZ, vp->Height ? double(vp->Width) / vp->Height : 0.0, who);
        }
    }
    return o_SetViewport(self, vp);
}

// ---- Texture hooks (fixes with texture callbacks) ----------------------------
// IDirect3DTexture8 methods are patched in the vtable shared by the runtime's textures (found through a 1x1
// texture created right after the device). RenderD3D fills each level with LockRect(level, rect=NULL) + copy +
// UnlockRect (RenderD3D.dll RVA 0x11417/0x1150C, docs/RENDERING.md); fixes see the level before it is unlocked.

HRESULT WINAPI H_TexLockRect(void* self, UINT level, d3d8::LockedRect* out, const RECT* rect, DWORD flags) {
    HRESULT hr = o_TexLockRect(self, level, out, rect, flags);
    if (SUCCEEDED(hr) && out && !rect && !(flags & d3d8::kLock_ReadOnly)) {
        EnterCriticalSection(&g_lock);
        if (g_lockCount < sizeof g_locks / sizeof g_locks[0]) g_locks[g_lockCount++] = {self, level, *out};
        LeaveCriticalSection(&g_lock);
    }
    return hr;
}

HRESULT WINAPI H_TexUnlockRect(void* self, UINT level) {
    d3d8::LockedRect rect{};
    bool found = false;
    EnterCriticalSection(&g_lock);
    for (size_t i = 0; i < g_lockCount; ++i)
        if (g_locks[i].texture == self && g_locks[i].level == level) {
            rect = g_locks[i].rect;
            g_locks[i] = g_locks[--g_lockCount];
            found = true;
            break;
        }
    LeaveCriticalSection(&g_lock);
    if (found) {
        d3d8::SurfaceDesc desc{};
        auto getDesc = reinterpret_cast<GetLevelDescFn>((*static_cast<void***>(self))[d3d8::kTex_GetLevelDesc]);
        if (SUCCEEDED(getDesc(self, level, &desc))) {
            const h2e::TextureLevel tl{level, desc.Width, desc.Height, desc.Format, rect.pBits, rect.Pitch};
            for (size_t i = 0; i < h2e::kFixCount; ++i)
                if (g_fixEnabled[i] && h2e::kFixes[i]->onTextureFilled) h2e::kFixes[i]->onTextureFilled(g_ctx, self, tl);
        }
    }
    return o_TexUnlockRect(self, level);
}

ULONG WINAPI H_TexRelease(void* self) {
    const ULONG refs = o_TexRelease(self);
    if (refs == 0)
        for (size_t i = 0; i < h2e::kFixCount; ++i)
            if (g_fixEnabled[i] && h2e::kFixes[i]->onTextureReleased) h2e::kFixes[i]->onTextureReleased(g_ctx, self);
    return refs;
}

HRESULT WINAPI H_SetTexture(void* self, DWORD stage, void* texture) {
    if (texture)
        for (size_t i = 0; i < h2e::kFixCount; ++i)
            if (g_fixEnabled[i] && h2e::kFixes[i]->onSetTexture) texture = h2e::kFixes[i]->onSetTexture(g_ctx, stage, texture);
    return o_SetTexture(self, stage, texture);
}

void InstallTextureHooks(void* dev) {
    if (void* p = Patch(dev, d3d8::kDev_SetTexture, reinterpret_cast<void*>(&H_SetTexture)))
        o_SetTexture = reinterpret_cast<SetTextureFn>(p);
    if (o_TexRelease) return;  // the texture vtable is already patched (device re-created)
    auto create = reinterpret_cast<CreateTextureFn>((*static_cast<void***>(dev))[d3d8::kDev_CreateTexture]);
    void* probe = nullptr;
    const DWORD formats[] = {d3d8::kFmt_A8R8G8B8, d3d8::kFmt_A4R4G4B4, d3d8::kFmt_X8R8G8B8};
    for (DWORD f : formats)
        if (SUCCEEDED(create(dev, 1, 1, 1, 0, f, d3d8::kPool_Managed, &probe)) && probe) break;
    if (!probe) {
        Log("texture hooks: ERROR cannot create a probe texture; texture fixes inactive");
        return;
    }
    void** vtbl = *static_cast<void***>(probe);
    auto release = reinterpret_cast<ReleaseFn>(vtbl[d3d8::kTex_Release]);
    void* lock = Patch(probe, d3d8::kTex_LockRect, reinterpret_cast<void*>(&H_TexLockRect));
    void* unlock = Patch(probe, d3d8::kTex_UnlockRect, reinterpret_cast<void*>(&H_TexUnlockRect));
    void* rel = Patch(probe, d3d8::kTex_Release, reinterpret_cast<void*>(&H_TexRelease));
    if (lock) o_TexLockRect = reinterpret_cast<LockRectFn>(lock);
    if (unlock) o_TexUnlockRect = reinterpret_cast<UnlockRectFn>(unlock);
    if (rel) o_TexRelease = reinterpret_cast<ReleaseFn>(rel);
    release(probe);
    Log("texture hooks: texture vtable %p patched (LockRect %s, UnlockRect %s, Release %s), SetTexture %s",
        static_cast<void*>(vtbl), lock ? "ok" : "FAILED", unlock ? "ok" : "FAILED", rel ? "ok" : "FAILED",
        o_SetTexture ? "ok" : "FAILED");
}

// ---- IDirect3D8 wrappers -----------------------------------------------------

UINT WINAPI H_GetAdapterModeCount(void* self, UINT adapter) {
    UINT n = o_GetAdapterModeCount(self, adapter);
    static bool once = false;
    if (!once) {
        once = true;
        char who[400];
        Caller(_ReturnAddress(), who, sizeof who);
        Log("GetAdapterModeCount(adapter=%u) -> %u caller=%s", adapter, n, who);
    }
    return n;
}

HRESULT WINAPI H_EnumAdapterModes(void* self, UINT adapter, UINT mode, d3d8::DisplayMode* out) {
    HRESULT hr = o_EnumAdapterModes(self, adapter, mode, out);
    if (SUCCEEDED(hr) && out && mode < sizeof g_modeSeen && !g_modeSeen[mode]) {
        g_modeSeen[mode] = 1;
        Log("EnumAdapterModes #%u: %ux%u @%uHz fmt=%lu(%s)", mode, out->Width, out->Height, out->RefreshRate,
            out->Format, d3d8::FormatName(out->Format));
    }
    return hr;
}

HRESULT WINAPI H_GetAdapterDisplayMode(void* self, UINT adapter, d3d8::DisplayMode* out) {
    HRESULT hr = o_GetAdapterDisplayMode(self, adapter, out);
    static d3d8::DisplayMode last{};
    if (SUCCEEDED(hr) && out && memcmp(&last, out, sizeof last) != 0) {
        last = *out;
        Log("GetAdapterDisplayMode(adapter=%u) -> %ux%u @%uHz fmt=%lu(%s)", adapter, out->Width, out->Height,
            out->RefreshRate, out->Format, d3d8::FormatName(out->Format));
    }
    return hr;
}

HRESULT WINAPI H_CreateDevice(void* self, UINT adapter, DWORD devType, HWND focus, DWORD flags,
                              d3d8::PresentParameters* pp, void** outDev) {
    char who[400];
    Caller(_ReturnAddress(), who, sizeof who);
    Log("CreateDevice adapter=%u devType=%lu hFocusWindow=%p BehaviorFlags=0x%08lX caller=%s", adapter, devType,
        static_cast<void*>(focus), flags, who);
    LogPresentParams("CreateDevice (in) ", pp);
    LogWindow(focus);
    HRESULT hr = o_CreateDevice(self, adapter, devType, focus, flags, pp, outDev);
    Log("CreateDevice -> hr=0x%08lX device=%p", hr, outDev ? *outDev : nullptr);
    LogPresentParams("CreateDevice (out)", pp);
    if (SUCCEEDED(hr) && outDev && *outDev) {
        void* dev = *outDev;
        if (pp) {
            EnterCriticalSection(&g_lock);
            g_bbWidth = pp->BackBufferWidth;
            g_bbHeight = pp->BackBufferHeight;
            g_ctx.bbWidth = g_bbWidth;
            g_ctx.bbHeight = g_bbHeight;
            LeaveCriticalSection(&g_lock);
        }
        if (!g_ctx.renderD3DBase) g_ctx.renderD3DBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("RenderD3D.dll"));
        g_ctx.deviceWindow = (pp && pp->hDeviceWindow) ? pp->hDeviceWindow : focus;
        g_ctx.device = dev;
        if (g_anyTextureFix) InstallTextureHooks(dev);
        // Reset keeps the back-buffer size current; projection/viewport hooks serve research and projection fixes.
        if (void* p = Patch(dev, d3d8::kDev_Reset, reinterpret_cast<void*>(&H_Reset))) o_Reset = reinterpret_cast<ResetFn>(p);
        if (Research() || g_anyProjectionFix || g_anyViewportFix) {
            if (void* p = Patch(dev, d3d8::kDev_SetTransform, reinterpret_cast<void*>(&H_SetTransform)))
                o_SetTransform = reinterpret_cast<SetTransformFn>(p);
            if (void* p = Patch(dev, d3d8::kDev_SetViewport, reinterpret_cast<void*>(&H_SetViewport)))
                o_SetViewport = reinterpret_cast<SetViewportFn>(p);
        }
        if (Research() || g_anyPresentFix) {
            if (void* p = Patch(dev, d3d8::kDev_Present, reinterpret_cast<void*>(&H_Present)))
                o_Present = reinterpret_cast<PresentFn>(p);
        }
        if (!Research()) {
            LogWindow(focus);
            return hr;
        }
        if (void* p = Patch(dev, d3d8::kDev_SetRenderTarget, reinterpret_cast<void*>(&H_SetRenderTarget)))
            o_SetRenderTarget = reinterpret_cast<SetRenderTargetFn>(p);
        if (void* p = Patch(dev, d3d8::kDev_SetVertexShader, reinterpret_cast<void*>(&H_SetVertexShader)))
            o_SetVertexShader = reinterpret_cast<SetVertexShaderFn>(p);
        if (void* p = Patch(dev, d3d8::kDev_DrawPrimitive, reinterpret_cast<void*>(&H_DrawPrimitive)))
            o_DrawPrimitive = reinterpret_cast<DrawPrimitiveFn>(p);
        if (void* p = Patch(dev, d3d8::kDev_DrawIndexedPrimitive, reinterpret_cast<void*>(&H_DrawIndexedPrimitive)))
            o_DrawIndexedPrimitive = reinterpret_cast<DrawIndexedPrimitiveFn>(p);
        if (void* p = Patch(dev, d3d8::kDev_DrawPrimitiveUP, reinterpret_cast<void*>(&H_DrawPrimitiveUP)))
            o_DrawPrimitiveUP = reinterpret_cast<DrawPrimitiveUPFn>(p);
        if (void* p = Patch(dev, d3d8::kDev_DrawIndexedPrimitiveUP,
                            reinterpret_cast<void*>(&H_DrawIndexedPrimitiveUP)))
            o_DrawIndexedPrimitiveUP = reinterpret_cast<DrawIndexedPrimitiveUPFn>(p);
        LogWindow(focus);
    }
    return hr;
}

// Path of a file next to this DLL.
void SiblingPath(const char* name, char* out) {
    GetModuleFileNameA(g_self, out, MAX_PATH);
    char* slash = strrchr(out, '\\');
    if (slash) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - out), name);
}

// Reads h2e.ini: [log] Level and [<fix id>] Enabled for every registered fix.
void ReadConfig(char* path) {
    SiblingPath("h2e.ini", path);
    g_logLevel = static_cast<int>(GetPrivateProfileIntA("log", "Level", 1, path));
    for (size_t i = 0; i < h2e::kFixCount && i < sizeof g_fixEnabled; ++i) {
        g_fixEnabled[i] = GetPrivateProfileIntA(h2e::kFixes[i]->id, "Enabled", 0, path) != 0;
        if (g_fixEnabled[i] && h2e::kFixes[i]->onProjection) g_anyProjectionFix = true;
        if (g_fixEnabled[i] && h2e::kFixes[i]->onViewport) g_anyViewportFix = true;
        if (g_fixEnabled[i] && (h2e::kFixes[i]->onPresent || h2e::kFixes[i]->onBeforePresent)) g_anyPresentFix = true;
        if (g_fixEnabled[i] && (h2e::kFixes[i]->onTextureFilled || h2e::kFixes[i]->onSetTexture ||
                                h2e::kFixes[i]->onTextureReleased))
            g_anyTextureFix = true;
    }
}

void LogConfig(const char* path) {
    Log("config %s: log level %d", path, g_logLevel);
    for (size_t i = 0; i < h2e::kFixCount; ++i)
        Log("  fix %-12s %s  (%s)", h2e::kFixes[i]->id, g_fixEnabled[i] ? "ON " : "off", h2e::kFixes[i]->summary);
}

bool LoadReal() {
    if (g_realCreate) return true;
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);  // SysWOW64 for a 32-bit process (WOW64 redirection)
    if (n == 0 || n + 10 >= MAX_PATH) return false;
    wcscat_s(path, L"\\d3d8.dll");
    g_real = LoadLibraryW(path);
    if (!g_real) return false;
    g_realCreate = reinterpret_cast<CreateFn>(GetProcAddress(g_real, "Direct3DCreate8"));
    return g_realCreate != nullptr;
}

void OpenLog() {
    if (g_logLevel <= 0) return;
    char path[MAX_PATH] = {};
    if (!GetEnvironmentVariableA("H2E_D3D8LOG_PATH", path, MAX_PATH)) SiblingPath("h2e.log", path);
    g_log = _fsopen(path, "a", _SH_DENYNO);  // shared: the log can be read while the game runs
    g_t0 = GetTickCount64();
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    Log("=== Hitman 2 Enhanced " H2E_VERSION " (d3d8 proxy) attached, process %lu (%s) ===", GetCurrentProcessId(),
        exe);
}

}  // namespace

extern "C" void* WINAPI Direct3DCreate8(UINT sdkVersion) {
    if (!LoadReal()) {
        Log("ERROR: cannot load system d3d8.dll");
        return nullptr;
    }
    void* d3d = g_realCreate(sdkVersion);
    if (!g_enabled) return d3d;
    char who[400];
    Caller(_ReturnAddress(), who, sizeof who);
    Log("Direct3DCreate8(SDKVersion=%u) -> %p caller=%s", sdkVersion, d3d, who);
    static bool first = true;
    if (first) {  // outside the loader lock, on the engine thread that sets up the renderer
        first = false;
        if (!g_ctx.renderD3DBase) g_ctx.renderD3DBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("RenderD3D.dll"));
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        for (size_t i = 0; i < h2e::kFixCount; ++i)
            if (g_fixEnabled[i] && h2e::kFixes[i]->onDirect3DCreate) h2e::kFixes[i]->onDirect3DCreate(g_ctx, caller);
    }
    if (d3d) {
        if (void* p = Patch(d3d, d3d8::kD3D_CreateDevice, reinterpret_cast<void*>(&H_CreateDevice)))
            o_CreateDevice = reinterpret_cast<CreateDeviceFn>(p);
        if (!Research()) return d3d;
        if (void* p = Patch(d3d, d3d8::kD3D_GetAdapterModeCount, reinterpret_cast<void*>(&H_GetAdapterModeCount)))
            o_GetAdapterModeCount = reinterpret_cast<ModeCountFn>(p);
        if (void* p = Patch(d3d, d3d8::kD3D_EnumAdapterModes, reinterpret_cast<void*>(&H_EnumAdapterModes)))
            o_EnumAdapterModes = reinterpret_cast<EnumModesFn>(p);
        if (void* p = Patch(d3d, d3d8::kD3D_GetAdapterDisplayMode, reinterpret_cast<void*>(&H_GetAdapterDisplayMode)))
            o_GetAdapterDisplayMode = reinterpret_cast<DisplayModeFn>(p);
    }
    return d3d;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_lock);
        char flag[8] = {};
        if (GetEnvironmentVariableA("H2E_D3D8LOG", flag, sizeof flag) && flag[0] == '0') {
            g_enabled = false;  // emergency switch: pure pass-through, no fixes, no log
            return TRUE;
        }
        char* config = g_iniPath;  // kept for the fixes (FixContext::iniPath)
        ReadConfig(config);
        OpenLog();
        LogConfig(config);
        // RenderD3D imports this DLL, so it is mapped but none of its code has run yet: patch now.
        g_ctx.log = &Log;
        g_ctx.renderD3DBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("RenderD3D.dll"));
        for (size_t i = 0; i < h2e::kFixCount; ++i)
            if (g_fixEnabled[i] && h2e::kFixes[i]->onAttach) h2e::kFixes[i]->onAttach(g_ctx);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_enabled)
            for (size_t i = 0; i < h2e::kFixCount; ++i)
                if (g_fixEnabled[i] && h2e::kFixes[i]->onDetach) h2e::kFixes[i]->onDetach(g_ctx);
        if (g_log) {
            if (Research()) LogSummaries(true);
            Log("=== detach: %u frames presented, %u projection SetTransform calls ===", g_frames, g_projCalls);
            fclose(g_log);
            g_log = nullptr;
        }
    }
    return TRUE;
}
