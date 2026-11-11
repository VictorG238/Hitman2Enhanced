---
name: binary-analysis
description: "Static analysis of the Windows PE files of Hitman 2 (hitman2.exe, RenderD3D.dll, EAX.DLL, config.exe and other game DLLs). Use when inspecting PE headers, sections, imports, exports, the runtime IAT, resources or strings, hashing a binary, converting between VA, RVA, file offset and module-relative offsets, or checking whether an exe is packed. Prefers tools/pe_inspect.py, llvm-readobj and REA; never modifies game binaries."
---

# PE binary analysis (static)

## Rules

- Read-only. Never modify, rename or write into the game install or `samples/local/*` binaries.
- Analyze `samples/local/snapshot-*` copies (D-003). Create one with `python -I tools/snapshot_game.py`.
- Compute or read the **SHA-256 first** and compare with `docs/EXECUTABLE_ANALYSIS.md#binary-identity`.
  An address copied from another build or hash is `HYPOTHESIS` until re-verified on this file.
- Run scripts that read game data with `python -I`.

## Address vocabulary (always label which one you mean)

| Term | Meaning | Convert |
|---|---|---|
| VA | virtual address at the preferred base (hitman2.exe base 0x400000, RenderD3D 0x10000000) | VA = ImageBase + RVA |
| RVA | offset from the loaded image base | RVA = VA - ImageBase |
| File offset | position in the on-disk file | via section table: raw_ptr + (RVA - section_RVA); REA `address_to_file_offset` or `pe_inspect` |
| Module-relative offset | `module+0xNNN`, how runtime logs report callers | same number as the RVA |
| Runtime relocated address | actual address in a process | base + RVA; **RenderD3D relocates** (observed 0x07A00000, 0x07950000; KI-009) |

hitman2.exe has stripped relocations, so runtime VA equals 0x400000 + RVA in every run. In docs write RenderD3D
locations as RVA or `module+offset`, with the static VA in parentheses when helpful.

## Tool choice

1. Prior results: `docs/EXECUTABLE_ANALYSIS.md`, `docs/experiments/EXP-001_pe_survey/reports/*.json`
   (headers/imports/exports of every game binary already exist).
2. `python -I tools/pe_inspect.py [--summary|--json] <file>`; `--mapped` for memory images from runtime captures.
3. `llvm-readobj` (headers, sections, imports, exports, resources) and `llvm-objdump` for bounded ranges.
4. REA (after `open_binary`): `search_strings`, `xrefs`, `read_bytes`, `address_to_file_offset`, `list_segments`,
   `inspect_pe_resources` (target-free, takes a path). Strings tool lists only defined strings; an empty result
   does not prove absence, so confirm with raw bytes.
5. Only then a new script.

## Packed binaries

`hitman2.exe` (SHA-256 `f85765f1…5136`) is a Steam-DRM-wrapped stub (entry 0x6EB86C, runtime OEP 0x5A184E).
Static analysis of the file shows the stub only. Use the read-only runtime capture and its `iat.csv` sidecar and
`main.analysis.img` (see `runtime-debugging`). Do not produce an unpacked or standalone executable (D-005).
Entropy ~8.0 on `.text` of the file versus ~6.2 in memory is the packing indicator already recorded.

## Output

Report: file, SHA-256, the specific fact, address in the labelled form, tool, tag. Hand durable results to
`research-documentation`. Do not paste whole import tables into the conversation; link the JSON report.
