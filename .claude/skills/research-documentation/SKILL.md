---
name: research-documentation
description: "Records Hitman 2 Enhanced research permanently in docs/. Use when you have just discovered or named a function, confirmed or corrected an address, completed or started an experiment, found a file format, added a reverse engineering tool, or made an architectural decision, and whenever asked to document, write up or log a finding. Applies the CONFIRMED / INFERRED / HYPOTHESIS / OPEN convention and updates existing documents instead of creating duplicates."
---

# Research documentation

Chat history is not memory. Write the finding to `docs/` before moving to the next task.

## Where things go (update the existing file, do not create parallel ones)

| Finding | Location |
|---|---|
| Function, global, vtable slot, structure field (any binary) | `docs/FUNCTION_REGISTRY.md` (one row per address) |
| Binary identity, PE facts, DRM/packing | `docs/EXECUTABLE_ANALYSIS.md` |
| Renderer, projection, FOV, viewports | `docs/RENDERING.md` |
| Time source, fps behavior | `docs/TIMING_AND_FPS.md` |
| Game data formats | `docs/FILE_FORMATS.md` |
| Experiment (goal, procedure, results) | `docs/experiments/EXP-NNN_<name>/README.md` |
| Tool or environment problem, observed game bug | `docs/KNOWN_ISSUES.md` (KI-/GB- ids) |
| Decision with consequences | `docs/DECISIONS.md` (next D-NNN) |
| Chronology | `docs/RESEARCH_LOG.md` (newest first, one short entry linking details) |
| Navigation | `docs/INDEX.md` (new experiment, tool or document; keep "Current state" accurate) |
| Phase status | `docs/ROADMAP.md` |

## Required content of a significant finding

- **Binary and SHA-256** (full hash in the registry or experiment header; short form `f85765f1…5136` elsewhere).
- **Function or subsystem**, with **address** labelled as VA / RVA / `module+offset` (see `binary-analysis`).
- **Evidence**: tool, raw bytes or log lines, REA Evidence ID if any (session-scoped, so also keep bytes + hash).
- **Reproduction**: the commands or steps that give the same result.
- **Confidence**: `CONFIRMED` (reproducible evidence) / `INFERRED` (state reasoning) / `HYPOTHESIS` / `OPEN`.
- **Related** experiments or documents, and **remaining unknowns**.

## Rules

- A correction does not delete history: change the claim, mark the old value as corrected and say why
  (see the 0x10001750 correction in `docs/RENDERING.md` for the style).
- Never write an address without its binary hash nearby. Never guess a name: if the purpose is inferred, the
  registry name is provisional and the status says `INFERRED`.
- Promote tags only with new evidence; demote when contradicted.
- One fact, one home; elsewhere link to it.
- Never commit game binaries, memory dumps, or absolute machine paths (use repo-relative paths and
  `samples/local/...`). Small derived JSON/log excerpts are fine.
- English only. Keep entries short; link instead of pasting long listings.
- After editing docs, re-check that every relative link in `docs/INDEX.md` resolves
  (`python -m unittest tests.test_agent_skills`).

## Experiment skeleton

`docs/experiments/EXP-NNN_<name>/README.md`: goal, hypothesis, date, status, files + SHA-256, tools,
reproducible procedure, results (tagged), conclusions, confidence, next steps. Link from INDEX and RESEARCH_LOG.

## Closing

End a documentation step by saying which files changed and which tags were set or changed. Commit only when asked,
with only the files belonging to the finding.
