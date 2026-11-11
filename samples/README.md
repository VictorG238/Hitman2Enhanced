# samples/

Game files are **never** committed. `samples/local/` is git-ignored.

Create a working snapshot of an install (read-only on the source; verifies each copy and writes
`MANIFEST.sha256` + `manifest.json`):

```bash
python -I tools/snapshot_game.py "<path to Hitman 2 Silent Assassin>" samples/local/snapshot-YYYYMMDD
```

Files copied: top-level `*.exe *.dll *.ini *.cfg *.dat *.vdf *.txt`. Scene packages (`Scenes/`)
and `streams.wav` are not copied yet (large; not needed for executable analysis).

Current reference snapshot: `samples/local/snapshot-20261110` — hashes listed in
[docs/EXECUTABLE_ANALYSIS.md](../docs/EXECUTABLE_ANALYSIS.md#binary-identity).
To check a snapshot or another install against it: `sha256sum -c MANIFEST.sha256` inside the snapshot dir.
