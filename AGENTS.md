# AGENTS.md — Persistent instructions for agents (Codex, Claude, etc.)

This file is the project's operating memory. Read it at the start of every session.
Technical knowledge lives in `docs/`; the entry point is [`docs/INDEX.md`](docs/INDEX.md).

## 1. Before working

1. Read `docs/INDEX.md` (do not reread all of `docs/`: the index says which document holds what).
2. Read only the documents relevant to the task.
3. Check the latest entries of `docs/RESEARCH_LOG.md` and `docs/KNOWN_ISSUES.md`.

## 2. Knowledge rules (mandatory)

- **Never invent technical data.** Every claim in `docs/` carries a status tag:
  - `CONFIRMED` — verified with reproducible evidence (tool + hash + procedure).
  - `INFERRED` — deduced from evidence but not directly verified (state the reasoning).
  - `HYPOTHESIS` — idea without sufficient evidence.
  - `OPEN` — open question / pending research.
- Addresses, offsets and vtable slots are only written next to the **SHA-256 of the binary** they belong to.
- Do not assume compatibility with Hitman: Codename 47, Contracts, Blood Money or Freedom Fighters.
- Chat history is **not** memory: every discovery is written to `docs/` before moving on.
- Each experiment lives in `docs/experiments/EXP-NNN_<name>/README.md` with: goal, hypothesis,
  date, status, files + SHA-256, tools, reproducible procedure, results, conclusions,
  confidence and next steps. Link it from `docs/INDEX.md` and `docs/RESEARCH_LOG.md`.
- REA/Ghidra findings cite the REA Evidence ID; Evidence IDs are session-scoped, so also record
  the binary SHA-256 and the raw bytes/addresses that support the claim.

## 3. Game file rules

- **Never modify the original install.** Work on a snapshot:
  `python -I tools/snapshot_game.py <game_dir> samples/local/snapshot-YYYYMMDD`.
- `samples/local/` is git-ignored. Game binaries or assets are **never** committed.
- Before any binary patch: check the SHA-256 against
  `docs/EXECUTABLE_ANALYSIS.md#binary-identity`. Unknown version ⇒ no patch.
- Small derived outputs (header/import JSON reports) may be committed.

- **DRM boundary (D-005)**: `hitman2.exe` is Steam-DRM wrapped. Start it only through Steam; analyze it only with
  read-only captures (`tools/runtime_dump.py`). Never produce, rebuild or commit an unpacked executable.
- The game folder may only be changed through `tools/d3d8_logger_install.py` (install/uninstall/status).
  After every runtime test: `uninstall`, then verify the install against the baseline manifest
  (`tr -d '
' < samples/local/baseline-20261110-repaired/MANIFEST.sha256 | sha256sum -c` from the game folder).
- **`Hitman2.ini`**: the owner allowed agents (2026-11-10) to edit `Hitman2.ini` for experiments: back it up first to `samples/local/ini-backups/`, change only the keys the test needs, keep the owner's keys (e.g. `DefaultScene=AllLevels/levelmenu.gms`) and the CRLF line endings, and report every change. It is excluded from post-test baseline checks. (Until 2026-11-10 only the
  owner edited it.)
- RenderD3D.dll is relocated at runtime: record its addresses as RVAs / module+offset.

## 4. Tools and environment

- Python 3.14, standard library only for now (any new dependency must be justified in
  `docs/DECISIONS.md`). Run scripts that read game data with `python -I`.
- Available on the dev machine: git, CMake 4.x, LLVM (`llvm-readobj`, `llvm-objdump`),
  Visual Studio (MSVC 14.51, CMake generator "Visual Studio 18 2026"), REA MCP (Ghidra 12.1.4 headless).
  Not available: x64dbg, Process Monitor (none needed so far).
- Tests (from the repo root): `python -m unittest discover -s tests`.
  Including the game snapshot: `HITMAN2_SNAPSHOT=samples/local/baseline-20261110-repaired python -m unittest discover -s tests`.
- Native (Win32 only — the game is 32-bit): `cmake -S . -B build -G "Visual Studio 18 2026" -A Win32`,
  `cmake --build build --config Release`, `ctest --test-dir build -C Release --output-on-failure`.

## 5. Per-task workflow

1. Consult docs → 2. Define a verifiable goal → 3. Build the tool/experiment →
4. Run the checks → 5. Record results and limitations → 6. Update `docs/` and the index →
7. Summarize what was achieved and the next step.

Never declare a goal complete without tests/evidence. A frame limiter is not a simulation fix.
If something is blocked, document why and move to an independent task.

## 6. Conventions

- Everything in English: docs, code, identifiers, comments, commit messages.
- Small, descriptive commits authored by the owner only: no `Co-Authored-By` or other AI attribution trailers.
- Edit files with the editor tools (Edit/Write), not with scripts that rewrite files.
- C++: CMake, C++17, Win32 (x86) target — the game is 32-bit. Layout and fix framework: D-009.
- **Adding a fix**: module `src/fixes/<id>.{h,cpp}` defining `extern const Fix k<Name>Fix = {"<id>", ...}`, add it
  to `src/fixes/registry.cpp` and `FIX_IDS` in `tools/d3d8_logger_install.py`, unit tests in
  `tests/native/fixes_test.cpp`, an entry in `docs/FIXES.md`, a line in `CHANGELOG.md`. Off by default; enable it in
  the release `h2e.ini` (`RELEASE_FIXES` in `tools/package_release.py`) only after a mission test.
- **Releasing**: bump `src/version.h` + `CMakeLists.txt`, add the `CHANGELOG.md` section, build, run all tests,
  `python -I tools/package_release.py`.

## 7. Reverse engineering policy and skills

Policy (details in `docs/AGENT_TOOLING.md`):

- Consult existing research (`docs/INDEX.md`, `docs/FUNCTION_REGISTRY.md`) and the binary hash before analyzing.
- Use specialized tools before custom scripts: documented result, then REA/Ghidra MCP, repository tools, LLVM
  utilities, and only then a new script that fills a real gap (explain the gap, make it reusable, add a test).
- Investigate narrowly: one function, one xref set. No whole-binary dumps or throwaway scripts.
- Document every verified finding with hash, address kind, evidence, tag (section 2) before moving on.
- Never modify original game binaries or protected components (section 3). Report tool failures; do not hide them.
- Keep existing tests and experiments intact; new work extends them.

Task procedures live in skills. Claude Code loads `.claude/skills/` automatically; any other agent should read the
matching `SKILL.md` before starting that kind of task:

| Task | Skill file |
|---|---|
| Understand how an engine feature or unknown function works; coordinate | `.claude/skills/reverse-engineering/SKILL.md` |
| PE headers, imports/exports, strings, VA/RVA/offset | `.claude/skills/binary-analysis/SKILL.md` |
| Decompile, xrefs, vtables, naming (REA/Ghidra) | `.claude/skills/ghidra-analysis/SKILL.md` |
| Live process values, modules, timing, D3D logging | `.claude/skills/runtime-debugging/SKILL.md` |
| Resolution, FOV, projection, viewport, HUD, VSync | `.claude/skills/graphics-research/SKILL.md` |
| Recording findings in `docs/` | `.claude/skills/research-documentation/SKILL.md` |
