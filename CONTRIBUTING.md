# Contributing

Hitman 2 Enhanced is research-driven: every fix is backed by documented evidence. The knowledge base starts at
[`docs/INDEX.md`](docs/INDEX.md); check [`docs/RESEARCH_LOG.md`](docs/RESEARCH_LOG.md) and
[`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md) before starting.

## Knowledge rules

- **Never invent technical data.** Every claim in `docs/` carries a status tag:
  - `CONFIRMED` — verified with reproducible evidence (tool + binary hash + procedure).
  - `INFERRED` — deduced from evidence but not directly verified (state the reasoning).
  - `HYPOTHESIS` — idea without sufficient evidence.
  - `OPEN` — open question / pending research.
- Addresses, offsets and vtable slots are only written next to the **SHA-256 of the binary** they belong to.
  RenderD3D.dll is relocated at runtime: record its addresses as RVAs / module+offset.
- Do not assume compatibility with Hitman: Codename 47, Contracts, Blood Money or Freedom Fighters.
- Each experiment lives in `docs/experiments/EXP-NNN_<name>/README.md` with goal, hypothesis, date, status, files +
  SHA-256, tools, reproducible procedure, results, conclusions, confidence and next steps.

## Game files

- **Never modify the original install.** Analyse a snapshot:
  `python -I tools/snapshot_game.py <game_dir> samples/local/snapshot-YYYYMMDD`.
- `samples/local/` is git-ignored. Game binaries, assets, dumps or data derived from them (such as the `hdfonts`
  pack) are **never** committed or distributed. Small derived reports (header/import JSON) are fine.
- Before any in-memory patch the binary's SHA-256 is checked (`docs/EXECUTABLE_ANALYSIS.md#binary-identity`);
  unknown version => no patch.
- `hitman2.exe` is wrapped by Steam DRM: start it only through Steam and analyse it only with read-only captures
  (`tools/runtime_dump.py`). Never produce, rebuild or commit an unpacked executable (D-005).
- During development the proxy is added to and removed from a game folder only with
  `tools/d3d8_logger_install.py install|uninstall|status`.

## Build and test

Python 3.10+ (standard library only) and, for the native parts, CMake 3.20+ with MSVC (Win32 — the game is 32-bit).

```bash
python -m unittest discover -s tests
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Conventions

- Everything in English: docs, code, identifiers, comments, commit messages. Small, descriptive commits.
- C++17, Win32 (x86) target. Layout and fix framework: [D-009](docs/DECISIONS.md).
- **Adding a fix**: module `src/fixes/<id>.{h,cpp}` defining `extern const Fix k<Name>Fix = {"<id>", ...}`, add it to
  `src/fixes/registry.cpp` and `FIX_IDS` in `tools/d3d8_logger_install.py`, unit tests in
  `tests/native/fixes_test.cpp`, an entry in `docs/FIXES.md`, a line in `CHANGELOG.md`. Off by default; enable it in
  the release (`RELEASE_FIXES` in `tools/package_release.py`) only after a mission test.
- **Releasing**: bump `src/version.h` + `CMakeLists.txt`, add the `CHANGELOG.md` section, then push a tag `vX.Y.Z`:
  the GitHub Actions workflow builds, tests and publishes the zip and the installer.
