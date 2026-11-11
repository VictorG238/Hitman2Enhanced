// Fix "hdfonts" (GB-010, EXP-005). Sharp UI text: replaces the engine's 128x128 bitmap font pages with
// high-resolution pages built offline from the TrueType font embedded in the scenes (tools/hdfonts_build.py).
//
// Each scene's .TEX holds palettised font pages (`Fonts\COPRGTB.TTF-<px>#<n>`) whose glyph boxes the engine
// addresses with normalized UVs (glyph tables in the scene's .PRM). A page re-rendered at N x the size with every
// glyph in its N x box keeps those UVs valid, so the replacement is a plain texture swap:
//   1. RenderD3D fills the page texture (LockRect / copy / UnlockRect): the proxy hashes the level's 4-bit alpha
//      (PageKey) and, if the pack has that page, creates the HD texture (same format, full mip chain).
//   2. SetTexture(original) binds the HD texture instead.
//   3. When the original is released, the HD texture is released too.
// Pack file: [hdfonts] Pack=<file> (default h2e_hdfonts.bin next to the DLL). Without a pack the fix does nothing.
#pragma once
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace h2e::hdfonts {

// 4-bit alpha (0..15, one byte per pixel) of a locked level; false for formats without a usable alpha channel.
// Supported: A8R8G8B8, A4R4G4B4, A8.
bool Alpha4FromLevel(DWORD format, const void* bits, int pitch, unsigned width, unsigned height,
                     std::vector<uint8_t>& out);

// 64-bit FNV-1a over the 4-bit alpha bytes (same as page_key in tools/hdfonts_build.py).
uint64_t PageKey(const uint8_t* alpha4, size_t count);

struct PackPage {
    uint64_t key;
    unsigned srcWidth, srcHeight, dstWidth, dstHeight;
    uint32_t rgb;  // page colour, alpha comes from the pack
    std::string name;
    const uint8_t* packed;  // PackBits alpha, dstWidth * dstHeight bytes once unpacked (points into the pack blob)
    size_t packedSize;
};

// Parses a pack ("H2EF" v1, see tools/hdfonts_build.py write_pack). False with `error` set on any inconsistency.
bool ParsePack(const std::vector<uint8_t>& blob, std::vector<PackPage>& pages, std::string& error);

// PackBits decoding into exactly `size` bytes; false on malformed or short input.
bool UnpackBits(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t size);

// Next mip level of an alpha image (2x2 box filter; odd edges clamp). Writes max(w/2,1) x max(h/2,1) bytes.
void DownsampleAlpha(const uint8_t* src, unsigned width, unsigned height, uint8_t* dst);

// Writes alpha + constant colour into a locked level of `format` (A8R8G8B8, A4R4G4B4 or A8); false otherwise.
bool WritePixels(DWORD format, uint32_t rgb, const uint8_t* alpha, unsigned width, unsigned height, void* bits,
                 int pitch);

}  // namespace h2e::hdfonts
