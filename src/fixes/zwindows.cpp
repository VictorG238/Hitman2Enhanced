#include "zwindows.h"

#include <windows.h>


namespace h2e {

bool ReadU32(uintptr_t addr, uint32_t* value) {
    SIZE_T n = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(addr), value, sizeof *value, &n) &&
           n == sizeof *value;
}

bool IsZWindows(uintptr_t p) {
    uint32_t vtbl = 0, self = 0;
    return p && ReadU32(p, &vtbl) && vtbl == kZWindowsVtable && ReadU32(p + kZWinObjWindowsCache, &self) &&
           self == p;
}

namespace {
// Scans are done in fixed 64 KB chunks into a static buffer: no heap allocation inside the game process (a
// region-sized buffer could be tens of MB and starve a 32-bit game that is loading a level). Render thread only.
constexpr size_t kChunkDwords = 0x4000;
uint32_t g_chunk[kChunkDwords];

bool Scannable(const MEMORY_BASIC_INFORMATION& mbi) {
    return mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE;
}

// Calls visit(address) for every dword equal to `value` in one region; stops when visit returns true.
template <typename Visit>
bool ScanRegionFor(const MEMORY_BASIC_INFORMATION& mbi, uint32_t value, Visit visit) {
    if (!Scannable(mbi)) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress), end = base + mbi.RegionSize;
    for (uintptr_t a = base; a < end; a += sizeof g_chunk) {
        const size_t want = (end - a) < sizeof g_chunk ? size_t(end - a) : sizeof g_chunk;
        SIZE_T n = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(a), g_chunk, want, &n)) continue;
        for (size_t i = 0; i < n / 4; ++i)
            if (g_chunk[i] == value && visit(a + i * 4)) return true;
    }
    return false;
}

// Same over all committed private RW memory.
template <typename Visit>
void ScanAllFor(uint32_t value, Visit visit) {
    MEMORY_BASIC_INFORMATION mbi{};
    for (uintptr_t a = 0x10000; a < 0x7FFE0000 && VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof mbi);
         a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize)
        if (ScanRegionFor(mbi, value, visit)) return;
}
}  // namespace

uintptr_t FindWindowObject(uint32_t vtable, uintptr_t windows, WindowObjectScan* info) {
    WindowObjectScan local;
    WindowObjectScan& s = info ? *info : local;
    s = WindowObjectScan{};
    ULONG_PTR stackLo = 0, stackHi = 0;
    GetCurrentThreadStackLimits(&stackLo, &stackHi);
    uintptr_t owned = 0, unowned = 0;
    unsigned unownedCount = 0;
    ScanAllFor(vtable, [&](uintptr_t p) {
        if (p >= stackLo && p < stackHi) return false;
        uint32_t cache = 0;
        ReadU32(p + kZWinObjWindowsCache, &cache);
        if (s.matches < kMaxScanReport) {
            s.at[s.matches] = p;
            s.cache[s.matches] = cache;
        }
        ++s.matches;
        if (cache == windows && !owned) owned = p;
        if (cache == 0 && ++unownedCount == 1) unowned = p;
        return false;  // count all matches
    });
    if (owned) return owned;
    if (unownedCount == 1) return unowned;
    return s.matches == 1 ? s.at[0] : 0;  // a single heap object of this class (runtime: [+0x5C] is not the cache)
}

uintptr_t IncrementalFinder::Step(size_t budgetBytes) {
    size_t done = 0;
    while (done < budgetBytes) {
        if (pos_ >= end_) {  // next scannable region (wrap at the top of user space)
            MEMORY_BASIC_INFORMATION mbi{};
            if (next_ >= 0x7FFE0000 || !VirtualQuery(reinterpret_cast<void*>(next_), &mbi, sizeof mbi)) {
                next_ = 0x10000;
                ++passes_;
                return 0;
            }
            next_ = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (!Scannable(mbi)) continue;
            pos_ = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            end_ = next_;
        }
        const size_t want = (end_ - pos_) < sizeof g_chunk ? size_t(end_ - pos_) : sizeof g_chunk;
        SIZE_T n = 0;
        const uintptr_t at = pos_;
        pos_ += want;
        done += want;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(at), g_chunk, want, &n)) continue;
        for (size_t i = 0; i < n / 4; ++i)
            if (g_chunk[i] == kZWindowsVtable && IsZWindows(at + i * 4)) return at + i * 4;
    }
    return 0;
}

uintptr_t FindZWindowsInRegion(uintptr_t hint) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void*>(hint), &mbi, sizeof mbi)) return 0;
    uintptr_t found = 0;
    ScanRegionFor(mbi, kZWindowsVtable, [&](uintptr_t p) { return IsZWindows(p) ? (found = p, true) : false; });
    return found;
}

uintptr_t FindZWindows(unsigned* count, uintptr_t hint) {
    *count = 0;
    if (hint) {
        if (const uintptr_t p = FindZWindowsInRegion(hint)) {
            *count = 1;
            return p;
        }
    }
    uintptr_t found = 0;
    ScanAllFor(kZWindowsVtable, [&](uintptr_t p) {
        if (IsZWindows(p)) {
            ++*count;
            if (!found) found = p;
        }
        return false;
    });
    return found;
}

namespace {
bool InExeImage(uint32_t v) { return v >= 0x401000 && v < 0x700000; }
}  // namespace

uint32_t ResolveGeom(uint32_t value) {
    uint32_t vt = 0;
    if (value >= 0x10000 && ReadU32(value, &vt) && InExeImage(vt)) return value;
    if (!value) return 0;
    uint32_t sys = 0, db = 0, table = 0, mask = 0, arr = 0, entry = 0, obj = 0;
    if (!ReadU32(0x6A6C5C, &sys) || !ReadU32(sys + 0x98, &db)) return 0;
    if (value & 0x40000000) {
        // Static refs: entry = [[db+0x14]+4] + (ref & 0x3FFFFFFF); object = [entry+0x54] (0x426B9B-0x426BB5).
        uint32_t pool = 0, base = 0;
        if (!ReadU32(db + 0x14, &pool) || !ReadU32(pool + 4, &base) ||
            !ReadU32(base + (value & 0x3FFFFFFF) + 0x54, &obj))
            return 0;
        return ReadU32(obj, &vt) && InExeImage(vt) ? obj : 0;
    }
    if (!ReadU32(db + 0x8, &table) ||
        !ReadU32(table + 0x10, &mask) || !ReadU32(db + 0xC, &arr) || !ReadU32(arr + 4 * (value & mask), &entry) ||
        !ReadU32(entry + 0x54, &obj))
        return 0;
    return ReadU32(obj, &vt) && InExeImage(vt) ? obj : 0;
}

bool ReadWindowList(uintptr_t windows, WindowList* out) {
    *out = WindowList{};
    uint32_t head = 0, tail = 0;
    if (!ReadU32(windows + kWindowListHead, &head) || !ReadU32(windows + kWindowListTail, &tail)) return false;
    uint32_t node = head;
    while (node && out->count < kMaxListedWindows) {
        uint32_t* n = out->node[out->count];
        if (!ReadU32(node, &n[0]) || !ReadU32(node + 4, &n[1]) || !ReadU32(node + 8, &n[2])) break;
        out->object[out->count] = ResolveGeom(n[2]);
        if (out->object[out->count]) ReadU32(out->object[out->count], &out->vtable[out->count]);
        ++out->count;
        if (node == tail || !n[1]) {
            out->complete = true;
            break;
        }
        node = n[1];
    }
    if (!head) out->complete = true;
    return true;
}

}  // namespace h2e
