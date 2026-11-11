# EXP-002 — Packed executable investigation

- **Date**: 2026-11-10
- **Status**: **done** (static + runtime, under the DRM boundary of D-005, option A)
- **Binary**: `hitman2.exe` SHA-256 `f85765f1b36734165fc22122050ca39ca1b8873f9b2d430b50a8271cde5d5136`
  (baseline `samples/local/baseline-20261110-repaired`)
- **Confidence**: high for everything tagged CONFIRMED below (observed in memory of a Steam-launched process
  and/or Ghidra listing); names of engine functions are inferred unless they come from the export table.

## Goal / hypotheses

Understand the packing layer, confirm runtime unpacking, find the OEP, get an analyzable memory image and
identify engine functions useful for rendering work.
H1: the stub stores the OEP at VA 0x6EB903. H2: the packer is a generic compressor.

## A. Static results (REA/Ghidra 12.1.4, packed file)

`CONFIRMED` — entry stub at VA 0x006EB86C (REA `ev_ddf0015e…`):
```
mov eax,0xF06E7CCD ; push ebp/ebx/ecx/edi/esi/edx
lea ebx,[eax+0x10003B7B]        ; ebx = 0x006EB848 (parameter block)
push 0x40 ; push 0x1000 ; push [ebx+4] ; push 0
call [ [ebx+0x10] + imagebase ] ; VirtualAlloc(0, 0x1CC8, MEM_COMMIT, PAGE_EXECUTE_READWRITE)
call [ebx+0x0C] + imagebase     ; decompress second stage (src RVA 0x2EAA18) into the buffer
call edi                        ; run second stage
mov [ebp+0x10003C36],eax        ; store return value at VA 0x006EB903
... pop regs ; jmp eax
```
Parameter block at VA 0x6EB848: `+00` 0x2EAA18 (compressed 2nd stage), `+04` 0x1CC8, `+08` 0xA4C,
`+0C` 0x2EB79D (decompressor), `+10` VirtualAlloc IAT, `+14` VirtualFree IAT, `+18` 0x00400000 (image base),
`+1C` LoadLibraryA IAT, `+20` GetProcAddress IAT. No `steam`/`drm`/`.bind` strings in the packed file.

## B. Runtime results

### B1. Direct launch (history; superseded method)

Runs 001/probe/002 launched the exe directly from the copy `samples/local/gamecopy`:
- `CONFIRMED` runtime unpacking (DINPUT8/EAX/DSOUND loaded within ~0.07 s; `.text` entropy 8.00 → 6.19;
  679 exports appear in memory).
- `CONFIRMED` the game shows **"Application error — Failed to find Steam."** and exits (code 0xFFFFFFFF) after
  ~10.6 s; the OEP slot stays 0. The string is absent from the packed file *and* from the unpacked image
  ⇒ `INFERRED` (high): the check lives in the second-stage loader outside the image — the packing layer is a
  **Steam DRM wrapper** (product/version not identified, not named).
- This method was **stopped** (it captures memory while the DRM layer runs). The run-002 capture was deleted.
  See [DECISIONS D-005](../../DECISIONS.md).

### B2. Steam launch + read-only attach (current method)

Procedure (reproducible):
```bash
# 1. start the capture tool; it waits for the process
python -I tools/runtime_dump.py samples/local/dumps/steam-001 --marker RenderD3D.dll --wait 240
# 2. start the game normally through Steam (the user, or: start steam://rungameid/6850)
# 3. name the IAT and produce a disassembler-friendly image
python -I tools/iat_annotate.py samples/local/dumps/steam-001 --analysis-image
python -I tools/pe_inspect.py --mapped --summary samples/local/dumps/steam-001/main.bin
# 4. disassemble / analyze main.analysis.img (llvm-objdump, REA/Ghidra)
llvm-objdump -d -M intel --start-address=0x416fd0 --stop-address=0x417110 samples/local/dumps/steam-001/main.analysis.img
```
`runtime_dump.py` opens the process with `PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ` only and
refuses unknown exe hashes. Capture `steam-001` (2026-11-10 00:35, pid 21060): `main.bin` SHA-256 recorded in its
`meta.json`; analysis image `main.analysis.img` SHA-256 `f59d353e3ca632cda6eed391ecca99242d534d69af81222078d2102d4e05266a`.

Results:
1. `CONFIRMED` **OEP = VA 0x005A184E (RVA 0x1A184E)**. With a legitimate Steam launch the slot VA 0x6EB903 holds
   0x005A184E (H1 confirmed for successful launches; in B1 the check failed before the stub returned). Bytes at the
   OEP are the Visual C++ 6 CRT entry prologue: `55 8B EC 6A FF 68 30 CB 65 00 68 9C FF 59 00 64 A1 00 00 00 00 …
   FF 15 04 91 63 00` — the first call goes through IAT 0x639104 = `KERNEL32!GetVersion`, as in `WinMainCRTStartup`.
2. `CONFIRMED` **Runtime IAT at VA 0x639000**: 144 slots in 7 groups, all resolved (iat_annotate, 0 unexplained):
   ADVAPI32 (4, 0x639000), DINPUT8 (1, 0x639014), DSOUND (1, 0x63901C), EAX.DLL (1, 0x639024), GDI32 (1, 0x63902C),
   KERNEL32 (103, 0x639034), USER32 (33, 0x6391D4). An identical copy of the 144 resolved values exists at
   VA 0x673A90 (purpose `OPEN`). No d3d8/winmm/msvcrt imports in the engine image.
3. `CONFIRMED` export table in memory: 679 exports (DLL name `hitman2.exe`), all 28 RenderD3D imports present.
4. `CONFIRMED` RenderD3D.dll is **relocated at runtime** (0x07A00000 in steam-001, 0x07950000 in EXP-003 run-001;
   its preferred base 0x10000000 is taken by EAX.DLL). Always convert with `runtime_va = base + RVA`.

### B3. Engine functions identified (analysis image, VA = runtime VA, base 0x400000, no relocations)

| VA | What | Evidence | Tag |
|---|---|---|---|
| 0x005A184E | OEP, CRT `WinMainCRTStartup` | slot value + prologue bytes | CONFIRMED |
| 0x006A6C5C | `?g_pSysInterface@@3PAVZSysInterface@@A` | export table | CONFIRMED |
| 0x006A6C50/54/58/60 | `g_pSysMem`, `g_pSysCom`, `g_pSysFile`, `g_pSysInput` | export table | CONFIRMED |
| `g_pSysInterface->vtbl[0x54]` | config getter `(key, &value) -> bool` ("GetConfigValue", name inferred) | used for every ini key, also by RenderD3D | CONFIRMED use / INFERRED name |
| 0x0041700B–0x00417062 | **`Resolution` parser**: `GetConfigValue("Resolution")`; width = `abs(atoi(s))` → `[esi+0x64]`; skip digits/'-' and one separator; height = `atoi(rest)` → `[esi+0x68]` | llvm-objdump listing | CONFIRMED |
| 0x00416FEE–0x004171xx | same function reads `PathfinderInfo`, `ScriptDebug`, `ScriptDll`, `ScriptIfDll`, … into the same object | listing | CONFIRMED |
| 0x00418307–0x004183FB | settings writer: `sprintf("%dx%d", [esi+0x64], [esi+0x68])`; table of `Window`, `Resolution`, `DrawDll`, `ColorDepth`, `ShadowDetail`… | listing | CONFIRMED |
| 0x00418156 | plug-in DLL loader: `GetProcAddress(hMod,"Create")` → call; fallback `new(0xC)` + ctor 0x5B02F0; then `obj->vtbl[0x30](hMod,…)`; error path on `GetLastError()==0x7E` | listing | CONFIRMED (generic-loader role INFERRED) |

`INFERRED`: no range check on the parsed width/height in the parser itself; validation downstream is `OPEN`.
Combined with EXP-003 (no mode enumeration in RenderD3D), arbitrary `Resolution WxH` values probably reach
`CreateDevice` unchanged — to be tested.

### Address spaces

Image base 0x00400000 with `RELOCS_STRIPPED` ⇒ VA = 0x400000 + RVA in every run, and `main.bin` offset == RVA.
`main.analysis.img` differs from `main.bin` only in its section headers (raw pointer = VA). Packed-file offsets only
apply to the stub (`.rsrc`: VA 0x6DC000 ↔ file 0xC5C00). RenderD3D: preferred base 0x10000000, runtime base varies.

## Limitations

- Captures are not atomic (process keeps running); code pages are stable.
- Ghidra auto-analysis of the analysis image starts at the stub entry, so many engine functions are not defined
  automatically; listings above come from llvm-objdump. Function start addresses for the parser/loader: `OPEN`.
- The second-stage (DRM) code outside the image is out of scope and was not analyzed.

## Next steps

- Define functions from the OEP / exports in Ghidra (apply `iat.csv` labels) to recover call graphs.
- Follow `[esi+0x64]/[esi+0x68]` to where the renderer receives them (feeds RenderD3D's `D3DPRESENT_PARAMETERS`).
- Locate the main loop and the `QueryPerformanceCounter` callers (TIMING_AND_FPS.md).
