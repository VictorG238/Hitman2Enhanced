// Read-only probe of the engine's 2D window manager (ZWINDOWS, RENDERING.md "UI system (ZWINDOWS)").
//
// hitman2.exe 1.0.0.277 (SHA-256 kHitman2Sha256), analysis image f59d353e...05266a:
//   - ZWINDOWS vtable 0x64DBA8 (constructor 0x4BADF0, size 0x5F7, class id 0x100030); no global pointer, the instance
//     is only registered by name ("rWindows") in the scene variables, so it is located by scanning private
//     read/write memory for its vtable.
//   - ZWINOBJ::GetWindows (0x4B76F0) caches the ZWINDOWS root in [obj+0x5C]; on the root itself [+0x5C] == obj.
//   - [ZWINDOWS+0x1D4]: geometry pointer with getter 0x4BB580 / setter 0x4BB590 (vtable slots 0x32C / 0x328; the
//     setter calls the previous object's vtbl+0x1FC(1) first). HYPOTHESIS: the window that has the focus.
// Nothing is written; every read goes through ReadProcessMemory on the own process, so a stale pointer fails
// instead of faulting.
#pragma once
#include <cstdint>

namespace h2e {

constexpr uint32_t kZWindowsVtable = 0x0064DBA8;
constexpr uint32_t kZWinObjWindowsCache = 0x5C;
constexpr uint32_t kZWindowsFocus = 0x1D4;

// True if [addr] reads 4 bytes into *value.
bool ReadU32(uintptr_t addr, uint32_t* value);

// True if `p` looks like the ZWINDOWS instance: vtable and self-cache match.
bool IsZWindows(uintptr_t p);

// Scans committed private read/write memory for ZWINDOWS instances. Returns the first valid one (0 if none) and the
// number of valid candidates in *count. If `hint` is given, the region containing it is scanned first and a hit there
// returns at once (the engine tends to allocate the next scene's instance in the same heap region).
uintptr_t FindZWindows(unsigned* count, uintptr_t hint = 0);

// ZWINDOW_IngameMap (registration 0x53D4F3, factory 0x540F80, constructor 0x53D5F0, size 0x303): vtable 0x659058;
// the ZWINDOW base constructor (inlined there) sets 0x659120.
constexpr uint32_t kIngameMapVtable = 0x00659058;
constexpr uint32_t kIngameMapSize = 0x303;
// Runtime field diff 2026-11-11 (samples/local/logs/h2e-20261111-mapdiff.log): [map+0x20] is 0x37 while the map is
// open and 0x27 while closed (two open/close cycles; unchanged while the pause menu was open); [map+0x24] 0x10/0.
constexpr uint32_t kIngameMapFlags = 0x20;
constexpr uint32_t kIngameMapOpenBit = 0x10;

// Finds a window object of the given vtable that belongs to `windows`: [obj+0x5C] == windows, or - the cache is
// filled lazily by GetWindows - the only heap match with [obj+0x5C] == 0. The calling thread's stack is skipped
// (runtime 2026-11-11: a stack copy of the vtable at 0x001AF5F0 with [+0x5C] = 0xFFFFFFFF). Otherwise the single
// heap match, if there is exactly one (runtime: the IngameMap object exists only in mission scenes and its [+0x5C]
// is not the ZWINDOWS cache, e.g. 0x02990000). 0 if none.
// `info` (optional) receives the number of matches and the first kMaxScanReport of them with their [+0x5C].
constexpr unsigned kMaxScanReport = 4;
struct WindowObjectScan {
    unsigned matches = 0;
    uintptr_t at[kMaxScanReport] = {};
    uint32_t cache[kMaxScanReport] = {};
};
uintptr_t FindWindowObject(uint32_t vtable, uintptr_t windows, WindowObjectScan* info = nullptr);

// Incremental ZWINDOWS search: each Step reads at most `budgetBytes` of private RW memory (64 KB chunks, static
// buffer) continuing where the previous call stopped, wrapping at the top of user space. Returns the instance when
// it is met, else 0. Spreads a full scan (15-60 ms) over many frames so a new scene's instance is found within a
// fraction of a second without a hitch. Render thread only.
class IncrementalFinder {
public:
    uintptr_t Step(size_t budgetBytes);
    unsigned Passes() const { return passes_; }

private:
    uintptr_t next_ = 0x10000, pos_ = 0, end_ = 0;
    unsigned passes_ = 0;
};

// Scans only the region containing `hint` (cheap; used every frame while the instance is missing).
uintptr_t FindZWindowsInRegion(uintptr_t hint);

// Open-window list (runtime field diff 2026-11-11 + container constructor 0x5B13E0, RENDERING.md): a node list at
// ZWINDOWS+0xC4 (block size 8, 3 dwords per node) with head at +0xE0 and tail at +0xE4; empty while playing, one
// node while the in-game map or pause menu is open, 1 -> 2 nodes for Options over the main menu. Node layout
// INFERRED: [0] previous, [1] next, [2] value (window reference or pointer, see ResolveGeom).
constexpr uint32_t kWindowListHead = 0xE0;
constexpr uint32_t kWindowListTail = 0xE4;
constexpr unsigned kMaxListedWindows = 8;

struct WindowList {
    unsigned count = 0;     // nodes walked (capped at kMaxListedWindows)
    bool complete = false;  // walk ended at the tail / a null next
    uint32_t node[kMaxListedWindows][3] = {};
    uint32_t object[kMaxListedWindows] = {};  // resolved window object, 0 if unresolved
    uint32_t vtable[kMaxListedWindows] = {};
};

// Walks the list from the head along node[1]. Returns false if the list header cannot be read.
bool ReadWindowList(uintptr_t windows, WindowList* out);

// Resolves a node value to a ZGEOM object: a pointer to an object whose first dword is inside hitman2.exe's image is
// taken as is; otherwise the value is treated as a geometry reference and resolved like
// ZEngineDataBase::GeomRefToPtr (0x422B00 -> 0x426BF0 -> 0x426B80) for dynamic refs (bit 30 clear):
// db = [[0x6A6C5C]+0x98], entry = [[db+0xC] + 4*(ref & [[db+8]+0x10])], object = [entry+0x54].
// Static refs (bit 30 set): object = [[[db+0x14]+4] + (ref & 0x3FFFFFFF) + 0x54].
// Runtime 2026-11-11: mission list nodes hold value 0, so a node value does not always identify the window.
uint32_t ResolveGeom(uint32_t value);

}  // namespace h2e
