---
name: reverse-engineering
description: "Coordinates a Hitman 2 / Glacier 1 reverse engineering investigation from question to recorded finding. Use when asked how an engine feature works internally, to find the function, structure, vtable, global or data flow behind a game behavior (for example where the resolution, FOV or frame timing is read or used), or to understand an unknown function, DLL or interface. Picks the tool, enforces the hash and prior-research check, and hands off to binary-analysis, ghidra-analysis, runtime-debugging, graphics-research and research-documentation."
---

# Reverse engineering coordinator (Hitman 2 / Glacier 1)

You are answering "how does X work inside the game?". This skill decides *what to do first*; the
specialized skills hold the procedures. Do not duplicate them here.

## Workflow

1. **Prior research first.** Read `docs/INDEX.md`, then `docs/FUNCTION_REGISTRY.md` and only the topic
   document that applies (`RENDERING.md`, `TIMING_AND_FPS.md`, `EXECUTABLE_ANALYSIS.md`, the matching
   `docs/experiments/EXP-*/README.md`). Grep docs for the address, name or key before analyzing anything.
   If a `CONFIRMED` result already answers the question, cite it and stop.
2. **State one objective** in a sentence: the question, the binary, the expected evidence
   ("which function reads `[obj+0x64]`?"). Reject objectives like "analyze the exe".
3. **Pin the binary.** Name the file and SHA-256 and compare with
   `docs/EXECUTABLE_ANALYSIS.md#binary-identity`. Unknown hash means no reuse of documented addresses.
   Work on `samples/local/snapshot-*` or runtime captures, never the live install (D-003).
4. **Choose the tool** with the policy in `docs/AGENT_TOOLING.md` (order below). Say in one or two
   lines which tool you chose and why before running it.
5. **Investigate the smallest area**: one function, one xref set, one structure. Prefer
   xrefs / targeted decompile / bounded `llvm-objdump --start-address --stop-address` over whole-binary dumps.
6. **Collect reproducible evidence**: tool + binary SHA-256 + address/RVA + raw bytes or log lines.
   REA Evidence IDs are session-scoped; always add the hash and bytes.
7. **Record** with the `research-documentation` skill before moving on. Tag every claim
   `CONFIRMED` / `INFERRED` / `HYPOTHESIS` / `OPEN`.

## Tool order (stop at the first that works)

1. Existing documented, hash-matching result.
2. REA MCP (Ghidra provider): `open_binary`, `search_strings`, `xrefs`, `procedure_pseudo_code`,
   `analyze_function`, `read_bytes`. Details in `ghidra-analysis`.
3. Repository tools: `tools/pe_inspect.py`, `tools/iat_annotate.py`, `tools/analyze_d3d8_log.py`,
   `tools/watch_values.py`, `tools/runtime_dump.py`.
4. LLVM: `llvm-objdump`, `llvm-readobj`.
5. A new script, only for a genuine gap. Say why 1-4 cannot do it and make it reusable (`tools/` + test).

Never write your own disassembler, decompiler, PE parser or memory scanner.

## Routing

| Need | Skill |
|---|---|
| PE headers, imports/exports, strings, VA/RVA/offset | `binary-analysis` |
| Decompile, xrefs, vtables, naming | `ghidra-analysis` |
| Live process values, loaded modules, timing, D3D calls | `runtime-debugging` |
| Resolution, aspect, FOV, projection, viewport, HUD scale | `graphics-research` |
| Writing the finding down | `research-documentation` |

## Pitfalls specific to this project

- `hitman2.exe` on disk is a DRM-wrapped stub: Ghidra on the file sees only the stub (KI-003). Engine code is
  analyzed from a read-only capture of a Steam-launched process (`main.analysis.img`). Never unpack or rebuild it (D-005).
- Ghidra does not create functions for vtable-dispatched code; define or disassemble the range explicitly.
- Do not assume Hitman Contracts / Blood Money / Codename 47 addresses or structures apply.
- Tool failure (REA down, provider unavailable, capture refused) is reported to the user, not silently replaced.
