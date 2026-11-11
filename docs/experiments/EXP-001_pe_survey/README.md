# EXP-001 — PE survey of the installed game binaries

- **Date**: 2026-11-09/10
- **Status**: done (static, read-only)
- **Confidence**: high for header/import/export facts (two independent parsers agree);
  medium for the RenderD3D code interpretation (manual decode + Ghidra, no runtime confirmation).

## Goal / hypotheses

Identify every executable/DLL, its version and hash, its imports/exports, and find how rendering is
wired (RenderD3D ↔ hitman2.exe ↔ d3d8). Hypotheses going in: RenderD3D uses Direct3D 8; resolution
is configured via `Hitman2.ini`.

## Tools

- `tools/snapshot_game.py` (copy + SHA-256 manifest), `tools/pe_inspect.py` 0.1.0 (stdlib PE parser).
- `llvm-readobj` (LLVM, independent cross-check, see `tests/test_llvm_oracle.py`).
- REA MCP with Ghidra 12.1.4 headless (strings, xrefs, raw bytes of RenderD3D.dll).
- Python `re` for string/byte scans.

## Procedure (reproducible)

```bash
cd Hitman2Enhanced
python -I tools/snapshot_game.py .. samples/local/snapshot-20261110
S=samples/local/snapshot-20261110
python -I tools/pe_inspect.py --summary --output-dir docs/experiments/EXP-001_pe_survey/reports \
    $S/*.exe $S/*.dll $S/*.EXE > docs/experiments/EXP-001_pe_survey/summary.txt
HITMAN2_SNAPSHOT=$S python -m unittest tests.test_llvm_oracle   # pe_inspect == llvm-readobj on all game files
```
REA: `open_binary` on `$S/RenderD3D.dll`, then `search_strings` (config keys), `xrefs` on
0x10048F6C ("RefreshRate"), 0x10042160 (Direct3DCreate8 IAT), 0x100487E0 (device error string),
`read_bytes` at 0x10015040, 0x10001D80, 0x100033A0.

## Results

Outputs: [`summary.txt`](summary.txt), per-file JSON in [`reports/`](reports/).
Findings are consolidated in [EXECUTABLE_ANALYSIS.md](../../EXECUTABLE_ANALYSIS.md) and
[RENDERING.md](../../RENDERING.md). Key points:

1. `hitman2.exe` is packed (entropy 8.00, 4 imports, entry `mov eax,0x6EB86C; jmp eax`, exports only in memory).
2. `testapp.exe` == `hitman2.exe` (same SHA-256).
3. RenderD3D: D3D8 via `Direct3DCreate8` only; exports `Create`; imports 28 engine symbols from `hitman2.exe`.
4. RenderD3D reads `RefreshRate/GammaValue/DisableVSync/Antialias/Anisotropy` through
   `g_pSysInterface->vtbl[0x54]`; `Resolution` is not referenced there.
5. Device creation site (0x10001D8D) and projection `SetTransform` candidate (0x100033C8) located.
6. Ghidra auto-analysis did not create functions at these sites (927 procedures found; vtable-dispatched
   code is not discovered automatically) — functions must be defined manually for decompilation.

### Files rewritten during the session (cause: owner's manual install repair)

> Update 2026-11-10: the owner confirmed these files changed because they manually verified and
> repaired the installation. The "after" hashes are the repaired, correct state; the snapshot used
> by this experiment equals the reference baseline `baseline-20261110-repaired`.

First `sha256sum` (2026-11-09 ~23:55, before the rewrite) vs snapshot:

| File | Before (mtime) | Before SHA-256 | After SHA-256 |
|---|---|---|---|
| RenderD3D.dll | 2016-03-24 | `11661c7bf92af79ed7affd3736b4d9d7b2d19c8c998d3437d378f1b3cf611518` | `e9aff449…c871d` |
| RenderOpenGL.dll | 2011-12-06 | `42b9fa076a22c53b6dbad592e7c4b035d672209a7df5f0dbfaf2b2c56447df4c` | `d81d5bc9…5688` |
| config.exe | 2016-03-24 | `03f014657d8ce92569322d99b4ae2ca513d3b886d64cc3c90b8d3f7569a08701` | `8ad654db…cd67` |
| jpegdll.dll | 2002-08-05 | `a59dd0bd9a4425a6bd8ab09d11e59685081c652d8fc73b52522c97d9109e3cc8` | `8f4047c1…51f` |
| msvcrt.dll | 2001-10-30 | `eda09e2a61c1f9b9a4ec7bdfd8f4deaac7505855ef9a26ea367e5e23611c5b73` | `11b2e109…d105` |
| xmlparse.dll | 2002-08-05 | `e4b80069b407988c63ede20ab4bd3f5758cadddd9f4146c4c286a2a1929c69eb` | `d48c3494…ffb9` |

(After-hashes in full: `samples/local/snapshot-20261110/MANIFEST.sha256` and EXECUTABLE_ANALYSIS.md.)
The "before" binaries were not preserved, so they cannot be analyzed.

## Limitations

- No runtime observation. Vtable slot identities for D3D8 are inferred from d3d8.h ordering + argument
  shape; no d3d8.h on this machine to verify.
- Packed `hitman2.exe`: nothing about resolution parsing, FOV source or timing can be read statically yet.
- REA Evidence IDs are session-scoped; the raw bytes are quoted in RENDERING.md so claims stay checkable.

## Next steps

- EXP-002: obtain an unpacked `hitman2.exe` image (memory dump after the stub) and re-run pe_inspect/Ghidra.
- Define functions at 0x10001D8D's container and 0x1000174F in Ghidra and decompile. *(Correction 2026-11-10: the helper is 0x10001750, called at 0x100033B0; the device load starts at 0x100033C7 — see RENDERING.md.)*
- Get a d3d8.h (MinGW-w64 headers or DirectX 8 SDK) to confirm vtable slots 15 and 37.
