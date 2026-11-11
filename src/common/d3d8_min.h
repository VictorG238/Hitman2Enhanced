// Minimal Direct3D 8 definitions needed by the logger.
//
// Layouts and vtable indices were verified on 2026-11-10 against the public
// mingw-w64 headers (mingw-w64-headers/include/d3d8.h and d3d8types.h); see
// docs/RENDERING.md "Direct3D 8 interface layout". Only what the logger reads
// is declared; the real interfaces are used through their vtables.
#pragma once
#include <windows.h>

namespace d3d8 {

// IDirect3D8 vtable slots (IUnknown = 0..2)
constexpr int kD3D_GetAdapterModeCount = 6;
constexpr int kD3D_EnumAdapterModes = 7;
constexpr int kD3D_GetAdapterDisplayMode = 8;
constexpr int kD3D_CreateDevice = 15;

// IDirect3DDevice8 vtable slots
constexpr int kDev_Reset = 14;
constexpr int kDev_Present = 15;
constexpr int kDev_CreateTexture = 20;
constexpr int kDev_SetTexture = 61;
constexpr int kDev_SetTransform = 37;
constexpr int kDev_SetRenderTarget = 31;
constexpr int kDev_BeginScene = 34;
constexpr int kDev_EndScene = 35;
constexpr int kDev_SetViewport = 40;
constexpr int kDev_DrawPrimitive = 70;
constexpr int kDev_DrawIndexedPrimitive = 71;
constexpr int kDev_DrawPrimitiveUP = 72;
constexpr int kDev_DrawIndexedPrimitiveUP = 73;
constexpr int kDev_SetVertexShader = 76;
// Used by the settings page to draw over the frame (mingw-w64 d3d8.h order).
constexpr int kDev_GetViewport = 41;
constexpr int kDev_SetRenderState = 50;
constexpr int kDev_ApplyStateBlock = 54;
constexpr int kDev_DeleteStateBlock = 56;
constexpr int kDev_CreateStateBlock = 57;
constexpr int kDev_SetTextureStageState = 63;
constexpr int kDev_SetPixelShader = 88;

// D3DSTATEBLOCKTYPE, D3DRENDERSTATETYPE, D3DTEXTURESTAGESTATETYPE and values (d3d8types.h)
constexpr DWORD kSBT_All = 1;
constexpr DWORD kRS_ZEnable = 7, kRS_FillMode = 8, kRS_ZWriteEnable = 14, kRS_AlphaTestEnable = 15, kRS_SrcBlend = 19,
                kRS_DestBlend = 20, kRS_CullMode = 22, kRS_AlphaBlendEnable = 27, kRS_FogEnable = 28,
                kRS_StencilEnable = 52, kRS_Lighting = 137, kRS_ColorWriteEnable = 168;
constexpr DWORD kBlend_SrcAlpha = 5, kBlend_InvSrcAlpha = 6, kCull_None = 1, kFill_Solid = 3;
constexpr DWORD kTSS_ColorOp = 1, kTSS_ColorArg1 = 2, kTSS_ColorArg2 = 3, kTSS_AlphaOp = 4, kTSS_AlphaArg1 = 5,
                kTSS_AlphaArg2 = 6, kTSS_TexCoordIndex = 11, kTSS_AddressU = 13, kTSS_AddressV = 14,
                kTSS_MagFilter = 16, kTSS_MinFilter = 17, kTSS_MipFilter = 18, kTSS_TextureTransformFlags = 24;
constexpr DWORD kTOP_Disable = 1, kTOP_SelectArg1 = 2, kTOP_Modulate = 4;
constexpr DWORD kTA_Diffuse = 0, kTA_Texture = 2;
constexpr DWORD kTAddress_Clamp = 3, kTexF_None = 0, kTexF_Point = 1, kTexF_Linear = 2;
constexpr DWORD kFVF_Diffuse = 0x40, kFVF_Tex1 = 0x100;

// IDirect3DTexture8 vtable slots (IUnknown 0..2, IDirect3DResource8 3..10, IDirect3DBaseTexture8 11..13)
constexpr int kTex_Release = 2;
constexpr int kTex_GetLevelCount = 13;
constexpr int kTex_GetLevelDesc = 14;
constexpr int kTex_LockRect = 16;
constexpr int kTex_UnlockRect = 17;

// D3DFORMAT, D3DPOOL, D3DLOCK
constexpr DWORD kFmt_A8R8G8B8 = 21, kFmt_X8R8G8B8 = 22, kFmt_A4R4G4B4 = 26, kFmt_A8 = 28;
constexpr DWORD kPool_Managed = 1;
constexpr DWORD kLock_ReadOnly = 0x10;

struct SurfaceDesc {  // D3DSURFACE_DESC (D3D8), 8 x 4 bytes
    DWORD Format;
    DWORD Type;
    DWORD Usage;
    DWORD Pool;
    UINT Size;
    DWORD MultiSampleType;
    UINT Width;
    UINT Height;
};
static_assert(sizeof(SurfaceDesc) == 32, "D3D8 surface desc layout");

struct LockedRect {  // D3DLOCKED_RECT
    INT Pitch;
    void* pBits;
};

// FVF bits (d3d8types.h)
constexpr DWORD kFVF_PositionMask = 0x000E;
constexpr DWORD kFVF_XYZRHW = 0x0004;

// D3DPRIMITIVETYPE
constexpr DWORD kPT_PointList = 1, kPT_LineList = 2, kPT_LineStrip = 3, kPT_TriangleList = 4,
                kPT_TriangleStrip = 5, kPT_TriangleFan = 6;

// Vertices referenced by a non-indexed draw of `count` primitives (0 for an unknown type).
inline UINT VertexCount(DWORD type, UINT count) {
    switch (type) {
        case kPT_PointList: return count;
        case kPT_LineList: return count * 2;
        case kPT_LineStrip: return count + 1;
        case kPT_TriangleList: return count * 3;
        case kPT_TriangleStrip:
        case kPT_TriangleFan: return count + 2;
        default: return 0;
    }
}

constexpr DWORD kTS_View = 2;
constexpr DWORD kTS_Projection = 3;

struct PresentParameters {  // D3DPRESENT_PARAMETERS (D3D8), 13 x 4 bytes
    UINT BackBufferWidth;
    UINT BackBufferHeight;
    DWORD BackBufferFormat;  // D3DFORMAT
    UINT BackBufferCount;
    DWORD MultiSampleType;
    DWORD SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    DWORD AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;
};
static_assert(sizeof(PresentParameters) == 52, "D3D8 present parameters layout");

struct Viewport {  // D3DVIEWPORT8
    DWORD X, Y, Width, Height;
    float MinZ, MaxZ;
};

struct DisplayMode {  // D3DDISPLAYMODE
    UINT Width, Height, RefreshRate;
    DWORD Format;
};

struct Matrix {  // D3DMATRIX, row-major _11.._44
    float m[4][4];
};

inline const char* FormatName(DWORD f) {
    switch (f) {
        case 0: return "UNKNOWN";
        case 20: return "R8G8B8";
        case 21: return "A8R8G8B8";
        case 22: return "X8R8G8B8";
        case 23: return "R5G6B5";
        case 24: return "X1R5G5B5";
        case 25: return "A1R5G5B5";
        case 26: return "A4R4G4B4";
        case 28: return "A8";
        case 75: return "D24S8";
        case 77: return "D24X8";
        case 80: return "D16";
        default: return "?";
    }
}

}  // namespace d3d8
