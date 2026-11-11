// Fix framework (D-009).
//
// A fix is a `Fix` record with optional callbacks for the events the proxy offers. To add a fix:
//   1. create src/fixes/<id>.cpp defining `extern const Fix k<Name>Fix = {...};` (+ a header for testable logic),
//   2. add it to kFixes in src/fixes/registry.cpp and to FIX_IDS in tools/d3d8_logger_install.py,
//   3. add unit tests (tests/native) and an entry in docs/FIXES.md.
// The id is the section name in h2e.ini (`[<id>] Enabled=1`). Every fix is off unless enabled there.
#pragma once
#include <cstddef>
#include <cstdint>

#include "../common/code_patch.h"
#include "../common/d3d8_min.h"

namespace h2e {

struct FixContext {
    LogFn log;
    uintptr_t renderD3DBase;  // 0 if RenderD3D.dll is not loaded
    const char* iniPath;      // h2e.ini; a fix reads its own keys from its [<id>] section
    void* deviceWindow;       // HWND of the Direct3D device (null before CreateDevice)
    void* device;             // IDirect3DDevice8 (null before CreateDevice)
    unsigned bbWidth = 0, bbHeight = 0;  // back buffer of the device (CreateDevice/Reset)
};

// One mip level of a texture the game has just filled (whole-level IDirect3DTexture8::LockRect), seen before
// UnlockRect. `bits`/`pitch` are the locked memory: valid only during the callback.
struct TextureLevel {
    unsigned level, width, height;
    DWORD format;  // D3DFORMAT
    const void* bits;
    int pitch;
};

struct Fix {
    const char* id;
    const char* summary;
    // DllMain(PROCESS_ATTACH), after the config is read: code patches. Must not load libraries.
    void (*onAttach)(const FixContext& ctx);
    // SetTransform(PROJECTION). May change `m`; returns true if it did.
    bool (*onProjection)(const FixContext& ctx, uintptr_t returnAddress, d3d8::Matrix& m, const d3d8::Viewport& vp,
                         unsigned bbWidth, unsigned bbHeight);
    // First Direct3DCreate8 call (from RenderD3D's device setup, outside the loader lock): patches that must not be
    // applied from DllMain. `caller` is the return address of that call.
    void (*onDirect3DCreate)(const FixContext& ctx, uintptr_t caller);
    // SetViewport. May change `vp`; returns true if it did.
    bool (*onViewport)(const FixContext& ctx, d3d8::Viewport& vp, unsigned bbWidth, unsigned bbHeight);
    // IDirect3DDevice8::Present (end of a frame).
    void (*onPresent)(const FixContext& ctx);
    // Texture hooks (installed only when a fix with one of them is enabled). Texture filled by the game: see
    // TextureLevel. A fix that creates or locks textures itself re-enters these callbacks.
    void (*onTextureFilled)(const FixContext& ctx, void* texture, const TextureLevel& level);
    // IDirect3DDevice8::SetTexture: returns the texture to bind instead of `texture` (or `texture` itself).
    void* (*onSetTexture)(const FixContext& ctx, unsigned stage, void* texture);
    // A texture's last reference was released: `texture` is dangling, usable only as a key.
    void (*onTextureReleased)(const FixContext& ctx, void* texture);
    // IDirect3DDevice8::Present, before the frame is shown: last chance to draw over it (own BeginScene/EndScene).
    void (*onBeforePresent)(const FixContext& ctx);
    // DllMain(PROCESS_DETACH) at process exit, after the engine has shut down: file writes only.
    void (*onDetach)(const FixContext& ctx);
};

extern const Fix* const kFixes[];
extern const size_t kFixCount;

}  // namespace h2e
