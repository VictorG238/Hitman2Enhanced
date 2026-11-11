# Agent tooling: skills, tool-selection policy, MCP status

Status as of 2026-11-10. Decision record: [D-007](DECISIONS.md). Procedures live in `.claude/skills/*/SKILL.md`;
always-loaded rules are short (`CLAUDE.md` imports `AGENTS.md`, section 7).

## 1. Which task triggers which skill

Claude Code lists every skill's description to the model and loads the body when a task matches, so ordinary
requests need no slash command. Descriptions are written to be mutually distinct.

| Typical request | Skill(s) |
|---|---|
| "Find the function that reads the resolution", "how does the game do X", "what is this vtable/global" | `reverse-engineering` → `binary-analysis` / `ghidra-analysis` |
| "List imports of RenderD3D", "is hitman2.exe packed", "convert this VA to a file offset", "hash this DLL" | `binary-analysis` |
| "Decompile the function that creates the D3D8 device", "xrefs to this address", "name this function" | `ghidra-analysis` |
| "What is this value at runtime", "which base is RenderD3D loaded at", "install the logging proxy" | `runtime-debugging` |
| "Why is the picture stretched at 16:9", "FOV is wrong", "HUD scale", "VSync doesn't work" | `graphics-research` |
| "Why does the game break above 60 FPS" | `reverse-engineering` + `runtime-debugging` (+ `graphics-research` for presentation) |
| "Document this function", "write up the experiment", "log this decision" | `research-documentation` |

User-level skills with overlapping keywords may exist (for example REA's own `reverse-engineer-anything` for
connection recovery). The project skills name the Hitman 2 context and the repo files, which is what separates them;
`ghidra-analysis` defers to `reverse-engineer-anything` only for REA connection problems.

## 2. Reverse engineering tool-selection policy

For every reverse engineering task, take the first row that gives the needed evidence:

1. **Documented, hash-matching result** (`docs/INDEX.md`, `docs/FUNCTION_REGISTRY.md`, EXP READMEs). Reuse it.
2. **Connected REA MCP (Ghidra)**: strings, xrefs, targeted pseudocode, assembly, bytes.
3. **Repository tools**: `pe_inspect`, `iat_annotate`, `runtime_dump`, `watch_values`, `analyze_d3d8_log`, logger proxy.
4. **LLVM utilities**: `llvm-objdump` bounded ranges, `llvm-readobj`.
5. **A new script**, only if 1-4 cannot give the evidence efficiently. State the gap, put it in `tools/`, add a test,
   register it in `docs/INDEX.md`. No custom disassembler, decompiler, PE parser or memory scanner.

Efficiency rules: check the binary hash and prior reports before analyzing; use xrefs instead of text searches
over dumps; decompile one function at a time; never export whole-binary disassembly into context; write findings
to the registry so names are not rediscovered. Do not trade correctness for fewer tool calls.

Safety rules (from AGENTS.md): read-only on binaries; Steam launch only; no DRM bypass or unpacked executable;
no edits to the install (`Hitman2.ini` only with a backup, see AGENTS.md section 3); no game files, dumps or private paths in commits.

## 3. REA MCP: verified capabilities

Connected server `rea` (npm `rea-agents` 6.4.0, `claude mcp list`: Connected). Tool names are `mcp__rea__<tool>`;
schemas are deferred, load them with ToolSearch (`select:mcp__rea__open_binary,...`). Provider report
(`binary_session`) on this host:

| Provider | Status |
|---|---|
| Ghidra 12.1.4 headless | **available** (the one used) |
| Hopper | unavailable: unsupported on Windows |
| IDA (MCP adapter) | unavailable: `REA_IDA_MCP_CONFIG` not configured |

Used by the skills (all require `open_binary` first, with an **absolute path**, except where noted):

| Need | Tools |
|---|---|
| Open / identify target | `open_binary` (returns sha256), `binary_session`, `list_documents`, `close_binary` |
| Strings and features | `search_strings`, `trace_feature`, `list_strings` |
| Cross-references and call graph | `xrefs`, `find_xrefs_to_name`, `procedure_callers`, `procedure_callees`, `procedure_references`, `build_call_path` |
| Functions | `search_procedures`, `procedure_info`, `procedure_pseudo_code`, `procedure_assembly`, `read_function_instructions`, `analyze_function` |
| Bytes and layout | `read_bytes`, `address_to_file_offset`, `list_segments`, `inspect_pe_resources` (target-free, takes a path) |
| Session annotations | `set_address_name`, `set_comment`, `set_inline_comment` (session only) |
| Evidence | `export_evidence_bundle`, `get_evidence_bundle` (IDs are session-scoped) |

Live verification (2026-11-10): `open_binary` on `samples/local/snapshot-20261110/RenderD3D.dll` returned sha256
`e9aff449…871d` (matches the documented hash); `procedure_pseudo_code 0x10001750` returned a decompilation;
`xrefs 0x10001750` returned `0x100033b0`, the caller already documented in RENDERING.md.

Behavioral limits observed or reported by REA:
- Annotations and the imported program are ephemeral (deleted on close/cancel). Durable names go to
  [FUNCTION_REGISTRY.md](FUNCTION_REGISTRY.md).
- Ghidra does not auto-create functions for vtable-dispatched code or most hitman2.exe engine code (KI-003); use
  `llvm-objdump` for the range or define the function.
- `search_strings` covers defined strings only; empty is not proof of absence.

## 4. Unavailable capabilities and manual configuration

| Capability | State on this machine | What would be needed |
|---|---|---|
| Dynamic tracing / breakpoints (`observe_native_calls`, `capture_process_scenario`) | REA reports `unsupported_host` (macOS/Linux only) | A Windows debugger (x64dbg or WinDbg) installed and approved by the owner; today runtime work is read-only memory capture + D3D8 logging |
| Hopper provider | unsupported on Windows | none |
| IDA provider | not configured | owner installs IDA and its MCP adapter, sets `REA_IDA_MCP_CONFIG`; not required |
| Codex / other agents | no skill auto-loading is configured | They read `AGENTS.md` section 7, which points at the `SKILL.md` files; a Codex-specific skills directory is not set up (unverified how the installed Codex discovers skills) |

No MCP server was installed or reconfigured for this work.

## 5. Validation

| Check | Result |
|---|---|
| `python -m unittest tests.test_agent_skills` (frontmatter, unique names/descriptions, no `disable-model-invocation`, doc links, hashes, private paths, `CLAUDE.md` import) | passes (see commit) |
| Full suite `python -m unittest discover -s tests` | passes, existing tests untouched |
| Skill registration: `claude -p` in the repo, `system/init` event lists the skills | **verified**: all six project skills listed |
| Model selects the right skill for the four sample prompts | **unverified** in a headless run (no model turn happened) |
| Skill loadable in the session that created the directory | not available: that session reported `Unknown skill`; `.claude/skills/` created mid-session needs a restart |

Manual validation (run in a **new** Claude Code session opened in this repo; each expected skill should appear as a
`Skill` tool call before analysis starts, and prior docs should be read first):

1. "Find the function responsible for reading the game's resolution." → `reverse-engineering`, then `ghidra-analysis`/`binary-analysis`; cites `docs/EXECUTABLE_ANALYSIS.md` (parser 0x41700B) before re-analysis; uses `mcp__rea__*`, not a new script.
2. "Decompile the function that creates the Direct3D 8 device." → `ghidra-analysis`; opens the snapshot RenderD3D (hash check), uses the documented site RenderD3D+0x1D8D/0x1DB6 and `procedure_pseudo_code`/`procedure_assembly` on it.
3. "Investigate why the game behaves incorrectly above 60 FPS." → `reverse-engineering` + `runtime-debugging`; reads `docs/TIMING_AND_FPS.md`; proposes QPC-xref analysis and fixed-frame-rate comparison; does not suggest a limiter as the fix.
4. "Document the function we just discovered." → `research-documentation`; edits `docs/FUNCTION_REGISTRY.md` (plus the topic doc, `RESEARCH_LOG.md`) with hash, address kind, evidence and tag.

If a skill is not chosen, tighten its `description` (keep the trigger phrases) and re-run; if it is never listed, check
that `.claude/skills/<name>/SKILL.md` exists and restart the session.
