---
name: ghidra-analysis
description: "Targeted disassembly and decompilation of Hitman 2 code through the connected REA MCP (Ghidra 12.1.4 headless) with llvm-objdump as fallback. Use when asked to decompile or disassemble a specific function (for example the one that creates the Direct3D 8 device), follow calls, find cross-references, read assembly, recognize a vtable or class layout, rename or comment functions, or reconstruct what a routine does."
---

# Ghidra analysis via REA

Check `docs/FUNCTION_REGISTRY.md` for the function first; a hash-matching `CONFIRMED` entry may already
answer the question. Procedure for connection failures: global skill `reverse-engineer-anything`
(`references/connection-and-recovery.md`). Tool names and arguments below were verified against the
connected REA 6.4.0 server; see `docs/AGENT_TOOLING.md`.

## Open the right target

`mcp__rea__open_binary` with an **absolute path** to a snapshot copy (relative paths are rejected). Confirm the
returned `sha256` equals the documented hash. The session is bound to one target; opening another switches it.

| Code of interest | Open | Notes |
|---|---|---|
| RenderD3D (`RenderD3D.dll`, hash `e9aff449…871d`) | `samples/local/snapshot-*/RenderD3D.dll` | Ghidra addresses are static VAs (base 0x10000000) |
| Engine (`hitman2.exe` code) | `samples/local/dumps/<capture>/main.analysis.img` | the on-disk exe is a stub; image keeps the stub entry point, so define functions yourself (KI-003) |
| Other DLLs | snapshot copy | |

## Targeted operations (use in this order, smallest first)

- `search_strings` / `trace_feature` to find a config key or message, then `xrefs` on the string address.
- `xrefs` (address in, list of reference addresses out; kinds not exposed), `procedure_callers`,
  `procedure_callees`, `procedure_references` for kinds.
- `procedure_pseudo_code` for one function; `procedure_assembly` or `read_function_instructions` when instruction
  precision matters (pseudocode may be `null`, and Ghidra can cut a body at an assumed no-return call).
- `analyze_function` for a dossier (code, callees, strings, CFG) of one function.
- `read_bytes` to quote raw bytes as evidence. Use `address_to_file_offset` for file positions.
- Never `batch_decompile` the whole program, never list thousands of procedures into context. If you must
  enumerate, filter with `search_procedures` (regex) first.

## When Ghidra has no function there

Vtable-dispatched code and most hitman2.exe code are not auto-created (KI-003). Use
`llvm-objdump -d -M intel --start-address=0x… --stop-address=0x… <image>` for the bounded range, decode the
prologue, and state in the finding that the function boundary is manual.

## Naming and comments

- Names: `Module::Role` style that matches the registry (`RenderD3D::CreateDeviceSite`), or the exported symbol.
  Check `docs/FUNCTION_REGISTRY.md` so one address never gets two names.
- `set_address_name` and `set_comment` edit the **session** database only; REA programs are ephemeral and
  annotations vanish on close or cancel. They are a convenience, not storage. Durable names go to
  `docs/FUNCTION_REGISTRY.md` via `research-documentation`.
- Put the evidence in the comment (hash prefix, runtime confirmation) when it helps the next reader.

## Confirmed versus inferred

`CONFIRMED`: observed in bytes/pseudocode and, for behavior, at runtime. `INFERRED`: purpose deduced from
callers, strings or arguments (state the reasoning). Decompiler output is a view, not source: field names,
types and "this is a camera" labels are `INFERRED` until runtime-checked (`runtime-debugging`).

## Read-only

REA never writes the binary. Do not export patched images. Patching decisions need the hash gate in AGENTS.md.
