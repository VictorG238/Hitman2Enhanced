#include "hdfonts.h"

#include <cstring>
#include <unordered_map>

#include "fix.h"

namespace h2e::hdfonts {

bool Alpha4FromLevel(DWORD format, const void* bits, int pitch, unsigned width, unsigned height,
                     std::vector<uint8_t>& out) {
    if (!bits) return false;
    out.resize(size_t(width) * height);
    const auto* base = static_cast<const uint8_t*>(bits);
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t* row = base + ptrdiff_t(y) * pitch;
        uint8_t* dst = out.data() + size_t(y) * width;
        switch (format) {
            case d3d8::kFmt_A8R8G8B8:
                for (unsigned x = 0; x < width; ++x) dst[x] = row[4 * x + 3] >> 4;
                break;
            case d3d8::kFmt_A4R4G4B4:
                for (unsigned x = 0; x < width; ++x) dst[x] = row[2 * x + 1] >> 4;
                break;
            case d3d8::kFmt_A8:
                for (unsigned x = 0; x < width; ++x) dst[x] = row[x] >> 4;
                break;
            default:
                return false;
        }
    }
    return true;
}

uint64_t PageKey(const uint8_t* alpha4, size_t count) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < count; ++i) h = (h ^ alpha4[i]) * 0x100000001B3ULL;
    return h;
}

namespace {

template <typename T>
bool Read(const std::vector<uint8_t>& b, size_t& p, T& v) {
    if (p + sizeof(T) > b.size()) return false;
    std::memcpy(&v, b.data() + p, sizeof(T));
    p += sizeof(T);
    return true;
}

}  // namespace

bool ParsePack(const std::vector<uint8_t>& blob, std::vector<PackPage>& pages, std::string& error) {
    pages.clear();
    size_t p = 0;
    uint32_t magic = 0, version = 0, count = 0;
    if (!Read(blob, p, magic) || std::memcmp(&magic, "H2EF", 4) != 0) return error = "not an hdfonts pack", false;
    if (!Read(blob, p, version) || version != 1) return error = "unsupported pack version", false;
    if (!Read(blob, p, count) || count > 4096) return error = "bad page count", false;
    for (uint32_t i = 0; i < count; ++i) {
        PackPage pg{};
        uint16_t sw = 0, sh = 0, dw = 0, dh = 0;
        uint32_t rgb = 0, size = 0;
        uint8_t nameLen = 0;
        if (!Read(blob, p, pg.key) || !Read(blob, p, sw) || !Read(blob, p, sh) || !Read(blob, p, dw) ||
            !Read(blob, p, dh) || !Read(blob, p, rgb) || !Read(blob, p, nameLen) || p + nameLen > blob.size())
            return error = "truncated page header", false;
        pg.name.assign(reinterpret_cast<const char*>(blob.data() + p), nameLen);
        p += nameLen;
        if (!Read(blob, p, size) || p + size > blob.size()) return error = "truncated page data", false;
        // Destination must be a whole multiple of the source (UVs unchanged) and a power of two up to 2048.
        if (!sw || !sh || dw % sw || dh % sh || dw / sw != dh / sh || dw > 2048 || dh > 2048 || (dw & (dw - 1)) ||
            (dh & (dh - 1)))
            return error = "bad page size", false;
        pg.srcWidth = sw;
        pg.srcHeight = sh;
        pg.dstWidth = dw;
        pg.dstHeight = dh;
        pg.rgb = rgb & 0xFFFFFF;
        pg.packed = blob.data() + p;
        pg.packedSize = size;
        p += size;
        pages.push_back(pg);
    }
    if (p != blob.size()) return error = "trailing bytes", false;
    return true;
}

bool UnpackBits(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t size) {
    size_t i = 0, o = 0;
    while (o < size) {
        if (i >= srcSize) return false;
        const uint8_t n = src[i++];
        if (n < 128) {
            const size_t len = size_t(n) + 1;
            if (i + len > srcSize || o + len > size) return false;
            std::memcpy(dst + o, src + i, len);
            i += len;
            o += len;
        } else if (n > 128) {
            const size_t len = 257 - size_t(n);
            if (i >= srcSize || o + len > size) return false;
            std::memset(dst + o, src[i++], len);
            o += len;
        }
    }
    return true;
}

void DownsampleAlpha(const uint8_t* src, unsigned width, unsigned height, uint8_t* dst) {
    const unsigned w2 = width > 1 ? width / 2 : 1, h2 = height > 1 ? height / 2 : 1;
    for (unsigned y = 0; y < h2; ++y)
        for (unsigned x = 0; x < w2; ++x) {
            const unsigned x0 = x * 2 < width ? x * 2 : width - 1, x1 = x * 2 + 1 < width ? x * 2 + 1 : x0;
            const unsigned y0 = y * 2 < height ? y * 2 : height - 1, y1 = y * 2 + 1 < height ? y * 2 + 1 : y0;
            const unsigned s = src[y0 * width + x0] + src[y0 * width + x1] + src[y1 * width + x0] + src[y1 * width + x1];
            dst[y * w2 + x] = static_cast<uint8_t>((s + 2) / 4);
        }
}

bool WritePixels(DWORD format, uint32_t rgb, const uint8_t* alpha, unsigned width, unsigned height, void* bits,
                 int pitch) {
    auto* base = static_cast<uint8_t*>(bits);
    const uint16_t rgb4 = static_cast<uint16_t>(((rgb >> 20) & 0xF) << 8 | ((rgb >> 12) & 0xF) << 4 | ((rgb >> 4) & 0xF));
    for (unsigned y = 0; y < height; ++y) {
        uint8_t* row = base + ptrdiff_t(y) * pitch;
        const uint8_t* a = alpha + size_t(y) * width;
        switch (format) {
            case d3d8::kFmt_A8R8G8B8:
                for (unsigned x = 0; x < width; ++x) {
                    const uint32_t v = uint32_t(a[x]) << 24 | (rgb & 0xFFFFFF);
                    std::memcpy(row + 4 * x, &v, 4);
                }
                break;
            case d3d8::kFmt_A4R4G4B4:
                for (unsigned x = 0; x < width; ++x) {
                    const uint16_t v = static_cast<uint16_t>((a[x] >> 4) << 12 | rgb4);
                    std::memcpy(row + 2 * x, &v, 2);
                }
                break;
            case d3d8::kFmt_A8:
                std::memcpy(row, a, width);
                break;
            default:
                return false;
        }
    }
    return true;
}

}  // namespace h2e::hdfonts

namespace h2e {
namespace {

using CreateTextureFn = HRESULT(WINAPI*)(void*, UINT, UINT, UINT, DWORD, DWORD, DWORD, void**);
using LockRectFn = HRESULT(WINAPI*)(void*, UINT, d3d8::LockedRect*, const RECT*, DWORD);
using UnlockRectFn = HRESULT(WINAPI*)(void*, UINT);
using ReleaseFn = ULONG(WINAPI*)(void*);

void* Method(void* obj, int slot) { return (*static_cast<void***>(obj))[slot]; }
void ReleaseTexture(void* tex) { reinterpret_cast<ReleaseFn>(Method(tex, d3d8::kTex_Release))(tex); }

// Pack, loaded once (onDirect3DCreate, outside the loader lock).
std::vector<uint8_t> g_blob;
std::vector<hdfonts::PackPage> g_pages;
std::unordered_map<uint64_t, size_t> g_byKey;  // page key -> index in g_pages
unsigned g_srcWidth = 0, g_srcHeight = 0;       // source page size in the pack (all pages share it)
bool g_loaded = false;

// Original texture -> HD texture (one owned reference each). Render thread only in practice; locked anyway.
SRWLOCK g_mapLock = SRWLOCK_INIT;
std::unordered_map<void*, void*> g_hd;
volatile LONG g_mapSize = 0;
unsigned g_replaced = 0, g_unmatched = 0, g_released = 0;

void OnDirect3DCreate(const FixContext& ctx, uintptr_t) {
    char name[MAX_PATH] = {}, path[MAX_PATH] = {};
    GetPrivateProfileStringA("hdfonts", "Pack", "h2e_hdfonts.bin", name, MAX_PATH, ctx.iniPath);
    if (strchr(name, ':') || name[0] == '\\') {
        strcpy_s(path, name);
    } else {  // relative to h2e.ini
        strcpy_s(path, ctx.iniPath);
        char* slash = strrchr(path, '\\');
        if (slash) slash[1] = 0;
        strcat_s(path, name);
    }
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        ctx.log("hdfonts: no pack at %s (build it with tools/hdfonts_build.py); fix inactive", path);
        return;
    }
    LARGE_INTEGER size{};
    DWORD got = 0;
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (256 << 20);
    if (ok) {
        g_blob.resize(static_cast<size_t>(size.QuadPart));
        ok = ReadFile(f, g_blob.data(), static_cast<DWORD>(g_blob.size()), &got, nullptr) && got == g_blob.size();
    }
    CloseHandle(f);
    std::string error;
    if (!ok || !hdfonts::ParsePack(g_blob, g_pages, error)) {
        ctx.log("hdfonts: cannot use %s (%s); fix inactive", path, ok ? error.c_str() : "read failed");
        g_pages.clear();
        g_blob.clear();
        return;
    }
    for (size_t i = 0; i < g_pages.size(); ++i) {
        const hdfonts::PackPage& p = g_pages[i];
        if (i == 0) {
            g_srcWidth = p.srcWidth;
            g_srcHeight = p.srcHeight;
        }
        if (p.srcWidth == g_srcWidth && p.srcHeight == g_srcHeight) g_byKey[p.key] = i;
    }
    g_loaded = !g_byKey.empty();
    ctx.log("hdfonts: %zu pages from %s (%ux%u -> %ux%u)", g_byKey.size(), path, g_srcWidth, g_srcHeight,
            g_pages.empty() ? 0 : g_pages[0].dstWidth, g_pages.empty() ? 0 : g_pages[0].dstHeight);
}

// Creates and fills the HD texture of a page in `format` (full mip chain). Null on failure.
void* CreateHd(const FixContext& ctx, const hdfonts::PackPage& page, DWORD format) {
    std::vector<uint8_t> alpha(size_t(page.dstWidth) * page.dstHeight), next;
    if (!hdfonts::UnpackBits(page.packed, page.packedSize, alpha.data(), alpha.size())) return nullptr;
    void* tex = nullptr;
    auto create = reinterpret_cast<CreateTextureFn>(Method(ctx.device, d3d8::kDev_CreateTexture));
    if (FAILED(create(ctx.device, page.dstWidth, page.dstHeight, 0, 0, format, d3d8::kPool_Managed, &tex)) || !tex)
        return nullptr;
    auto lock = reinterpret_cast<LockRectFn>(Method(tex, d3d8::kTex_LockRect));
    auto unlock = reinterpret_cast<UnlockRectFn>(Method(tex, d3d8::kTex_UnlockRect));
    const UINT levels = reinterpret_cast<UINT(WINAPI*)(void*)>(Method(tex, d3d8::kTex_GetLevelCount))(tex);
    unsigned w = page.dstWidth, h = page.dstHeight;
    for (UINT level = 0; level < levels; ++level) {
        d3d8::LockedRect lr{};
        if (FAILED(lock(tex, level, &lr, nullptr, 0))) {
            ReleaseTexture(tex);
            return nullptr;
        }
        const bool ok = hdfonts::WritePixels(format, page.rgb, alpha.data(), w, h, lr.pBits, lr.Pitch);
        unlock(tex, level);
        if (!ok) {
            ReleaseTexture(tex);
            return nullptr;
        }
        if (w == 1 && h == 1) break;
        next.resize(size_t(w > 1 ? w / 2 : 1) * (h > 1 ? h / 2 : 1));
        hdfonts::DownsampleAlpha(alpha.data(), w, h, next.data());
        alpha.swap(next);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return tex;
}

void OnTextureFilled(const FixContext& ctx, void* texture, const TextureLevel& lv) {
    if (!g_loaded || !ctx.device || lv.level != 0 || lv.width != g_srcWidth || lv.height != g_srcHeight) return;
    std::vector<uint8_t> a4;
    if (!hdfonts::Alpha4FromLevel(lv.format, lv.bits, lv.pitch, lv.width, lv.height, a4)) return;
    const uint64_t key = hdfonts::PageKey(a4.data(), a4.size());
    auto it = g_byKey.find(key);
    if (it == g_byKey.end()) {
        if (g_unmatched++ < 8)
            ctx.log("hdfonts: %ux%u texture (%s) key %016llx not in the pack", lv.width, lv.height,
                    d3d8::FormatName(lv.format), static_cast<unsigned long long>(key));
        return;
    }
    const hdfonts::PackPage& page = g_pages[it->second];
    void* hd = CreateHd(ctx, page, lv.format);
    if (!hd) {
        ctx.log("hdfonts: ERROR creating the HD texture of %s (%s); original kept", page.name.c_str(),
                d3d8::FormatName(lv.format));
        return;
    }
    void* old = nullptr;
    AcquireSRWLockExclusive(&g_mapLock);
    auto [pos, inserted] = g_hd.emplace(texture, hd);
    if (!inserted) {  // refilled: replace
        old = pos->second;
        pos->second = hd;
    }
    g_mapSize = static_cast<LONG>(g_hd.size());
    ReleaseSRWLockExclusive(&g_mapLock);
    if (old) ReleaseTexture(old);
    if (g_replaced++ < 256)
        ctx.log("hdfonts: %s -> %ux%u (%s), %ld live", page.name.c_str(), page.dstWidth, page.dstHeight,
                d3d8::FormatName(lv.format), g_mapSize);
}

void* OnSetTexture(const FixContext&, unsigned, void* texture) {
    if (!g_mapSize) return texture;
    AcquireSRWLockShared(&g_mapLock);
    auto it = g_hd.find(texture);
    void* out = it != g_hd.end() ? it->second : texture;
    ReleaseSRWLockShared(&g_mapLock);
    return out;
}

void OnTextureReleased(const FixContext& ctx, void* texture) {
    if (!g_mapSize) return;
    void* hd = nullptr;
    AcquireSRWLockExclusive(&g_mapLock);
    auto it = g_hd.find(texture);
    if (it != g_hd.end()) {
        hd = it->second;
        g_hd.erase(it);
        g_mapSize = static_cast<LONG>(g_hd.size());
    }
    ReleaseSRWLockExclusive(&g_mapLock);
    if (!hd) return;
    ReleaseTexture(hd);  // the device keeps its own reference while the HD texture is bound
    if (g_released++ < 256) ctx.log("hdfonts: original released, HD copy freed, %ld live", g_mapSize);
}

}  // namespace

extern const Fix kHdFontsFix = {"hdfonts",
                                "EXPERIMENTAL high-resolution UI font pages from the embedded TrueType font (GB-010)",
                                nullptr, nullptr, OnDirect3DCreate, nullptr, nullptr,
                                OnTextureFilled, OnSetTexture, OnTextureReleased};

}  // namespace h2e
