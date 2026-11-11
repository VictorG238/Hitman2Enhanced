# Decision records

## D-001 — PE inspector in pure Python stdlib (2026-11-10)

- **Context**: `pefile` and `pytest` are not installed; the rules say not to install unnecessary dependencies.
- **Decision**: write `tools/pe_inspect.py` with `struct` only; tests use `unittest`.
- **Consequences**: zero setup, fully reviewable parser. Correctness is guarded by synthetic-PE unit
  tests plus an oracle test against `llvm-readobj` on real system DLLs and the game binaries.

## D-002 — Repo location (2026-11-10)

- **Context**: the session only had access to the game folder.
- **Decision**: the repo lives at `<game>/Hitman2Enhanced/`. Tools take the game dir as an argument, so
  the repo can be moved anywhere later without code changes.
- **Consequences**: see KNOWN_ISSUES KI-005.

## D-003 — Analyze snapshots, not the live install (2026-11-10)

- **Context**: game files changed during the first session (KI-001).
- **Decision**: all analysis runs on `samples/local/snapshot-YYYYMMDD` created by `tools/snapshot_game.py`,
  which verifies the copy and writes a SHA-256 manifest.

## D-004 — Two independent analysis paths (2026-11-10)

- **Decision**: identification facts (hashes, imports, exports) come from our tested `pe_inspect`
  cross-checked with `llvm-readobj`; code-level analysis uses REA (Ghidra). A claim about code
  quotes raw bytes + binary hash so it can be re-verified without the REA session.

## D-005 — DRM boundary for hitman2.exe (2026-11-10)

- **Context**: EXP-002 showed the packing layer performs a Steam check ("Failed to find Steam." on direct launch)
  from code outside the game image ⇒ very likely a Steam DRM wrapper. Project rule: do not bypass licensing or DRM.
- **Decision** (owner chose option A): the game is started normally through Steam; research tools only *attach*
  read-only (`PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ`) to the running process. No standalone/unpacked
  executable is produced: import names go to a sidecar file (`iat.json`/`iat.csv`); the disassembler image only has
  its section headers adjusted and keeps the stub entry point and stub import directory. Captures stay in
  `samples/local/` (git-ignored) and are never distributed. The direct-launch capture mode was removed, the
  PE-rebuild tool deleted, and the one capture taken that way (run-002) deleted.
- **Consequences**: engine analysis works from memory images; anything that would modify the engine must also be
  applied in memory after a legitimate Steam start.

## D-006 — Runtime observation through a d3d8 logging proxy (2026-11-10)

- **Context**: RenderD3D reaches Direct3D only via `Direct3DCreate8` (EXP-001); Steam runs the game from the install
  folder, so a proxy must be placed there.
- **Decision**: a minimal C++ proxy (`src/d3d8_logger`) that forwards to the system `d3d8.dll`, wraps 8 vtable slots
  (verified layout) without changing arguments or results, and logs. Placement/removal only through
  `tools/d3d8_logger_install.py` (never overwrites an existing `d3d8.dll`; hash marker; log moved out of the game
  folder). `H2E_D3D8LOG=0` disables all hooking.
- **Consequences**: the install is modified only while a test runs; after `uninstall` the binaries are verified
  against the baseline manifest.

## D-007 — Reverse engineering tool-selection policy (2026-11-10)

- **Context**: investigations repeated work (throwaway scripts, re-analysis, names lost between sessions).
- **Decision**: tool order: documented result first, then Ghidra, repository tools, LLVM utilities, and a new script
  only for a real gap. Named addresses are stored in `docs/FUNCTION_REGISTRY.md` because Ghidra annotations of the
  ephemeral analysis programs are lost between sessions.
- **Consequences**: no new dependency.

## D-008 — Resolution and Hor+ fixes live in the d3d8 proxy, off by default (2026-11-10)

- **Decision**: implement the fixes as option B of `docs/RENDERING.md`: the existing d3d8 proxy (D-006) gains
  optional fixes, each enabled separately in `h2e_d3d8.ini` next to the DLL (written/removed by
  `tools/d3d8_logger_install.py --fix`). Resolution = in-memory patch of RenderD3D (hash + byte checked);
  Hor+ = adjusted scene projection in `SetTransform`.
- **Why**: no file on disk changes, `hitman2.exe` is never touched (D-005), removal is deleting two files, and the
  proxy already sees every device call. A d3d8-only fix (option A) cannot correct the height because the engine's
  own state (`+0x64/+0x68`) is already wrong before CreateDevice.
- **Evidence**: EXP-003 run-004 (writer), run-005/006 (windowed validation), run-007 (resolution fix confirmed;
  first Hor+ version broke the 2D layer and was corrected in 0.5.1).
- **Open**: long-term packaging (separate fix DLL, launcher, versioned releases) to be decided with the owner.
  -> decided in D-009. (Note: 0.5.1's Hor+ was later rejected in run-008.)

## D-009 — Fix framework, configuration and releases (2026-11-11, owner accepted)

- **Decision**:
  - **Code layout**: `src/common/` shared helpers (SHA-256, verified byte patches `code_patch`), `src/fixes/` one
    module per fix plus `registry.cpp`, `src/proxy/` the `d3d8.dll` that hosts fixes and the research logger,
    `src/version.h` the single version. A fix is a `Fix` record (`src/fixes/fix.h`) with optional callbacks for
    the proxy's events (`onAttach` for code patches, `onProjection`); new events are added only when a fix needs them.
  - **One DLL**: the shipped `d3d8.dll` contains the research logger too, switched by `[log] Level` (0 off, 1 setup
    and fix outcomes, 2 research). Research hooks are installed only at level 2.
  - **Configuration**: `h2e.ini` next to `d3d8.dll`, `[<fix id>] Enabled=0/1` per fix (all off unless enabled) and
    `[log] Level`. Same format for the developer installer and the release.
  - **Versions and releases**: semantic versioning (0.x while the config format may change), `CHANGELOG.md` updated
    with every release, `tools/package_release.py` builds `dist/Hitman2Enhanced-<version>.zip` (DLL, default
    `h2e.ini` with only mission-confirmed fixes on, `docs/INSTALL.md` as README, changelog). Developers keep using
    `tools/d3d8_logger_install.py` (hash-checked install/uninstall into the game folder).
  - **Documentation**: every fix has an entry in `docs/FIXES.md` (problem, change, safety, verification, limits).
- **Why**: the owner plans many more fixes (frame rate, AI, ...). One registry + one config format keeps each fix
  self-contained and testable, and a player install stays "copy two files / delete two files". A separate research
  DLL was rejected: two builds of the same hooks would drift; the log level gives the same separation.
- **Rules kept**: fixes act in memory only; code patches are pinned to a file hash and exact bytes; `hitman2.exe` is
  never patched (D-005); every fix is off unless enabled.
- **Consistency guard**: `tests/test_fix_registry.py` (C++ fix ids == installer ids, registered, documented; release
  fixes exist; versions agree) and `tests/native/fixes_test.cpp`.
- **Not done yet**: a launcher with per-fix checkboxes; a DLL version resource.
