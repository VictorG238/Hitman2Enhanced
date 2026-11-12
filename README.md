# Hitman 2 Enhanced — widescreen, high-FPS and menu fix for Hitman 2: Silent Assassin (PC)

By **Aquiles / Victor Baeza** ([@VictorG238](https://github.com/VictorG238)).

**Hitman 2 Enhanced** is a free, open-source fix pack for **Hitman 2: Silent Assassin** (2002, IO Interactive,
Glacier engine) on modern Windows 10/11 PCs — Steam version 1.0.0.277 and retail/disc version 1.0.0.265. It makes the game run at **1080p, 1440p,
4K and widescreen (16:9, 16:10) resolutions in fullscreen**, scales the tiny **menus, briefing and pause menu** to
the screen, fixes the **high frame rate (FPS) bugs** (frozen game, broken animations, cutscenes out of sync), keeps
the **game speed correct** at any FPS, adds **sharp HD menu fonts** and an **in-game settings page (F10)** for
resolution, field of view (FOV) and FPS limit. One-click installer, fully uninstallable.

Nothing in the game is modified on disk: the fixes run in memory through a `d3d8.dll` proxy (Direct3D 8 wrapper)
while the game is running.

## What it fixes

| Fix | Effect |
|---|---|
| `resolution` | Fullscreen keeps any resolution (1920x1080, 2560x1440, ...); the game otherwise forces 4:3/5:4 modes or fails |
| `menuscale` | Menus, briefing and pause menu fill the screen instead of a small 640x480 box; the cursor follows |
| `framelimit` | Caps the frame rate (default: your monitor's refresh rate). Above ~1024 FPS the game's timing breaks |
| `timer` | The game clock runs at the correct speed at any frame rate (it ran slow at high FPS) |
| `settings` | In-game settings page (**F10**): resolution, display mode, field of view, frame-rate limit |
| `hdfonts` | Sharp menu text; the installer builds the font pack from your own game files (nothing from the game is shipped) |

Status of each fix and the evidence behind it: [docs/FIXES.md](docs/FIXES.md). Known problems:
[docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).

## Install (players)

1. Download `Hitman2Enhanced-Setup-<version>.exe` from the [Releases](../../releases) page.
2. Run it. It explains what it changes, lets you choose the fixes, finds the Steam install of Hitman 2 by itself
   (or lets you pick the folder that contains `hitman2.exe`), copies `d3d8.dll` and `h2e.ini` there and, if chosen,
   builds the HD fonts from your own game files (about a minute).
3. Start the game from Steam and press **F10** in the main menu to choose your resolution and frame-rate limit.
   Resolution and display mode apply after restarting the game.

Uninstall from Windows "Installed apps" (Hitman 2 Enhanced), or delete `d3d8.dll`, `h2e.ini`, `h2e.log` and
`h2e_settings.ini` from the game folder. A manual zip (`Hitman2Enhanced-<version>.zip`) is also published; see
[docs/INSTALL.md](docs/INSTALL.md).

No game files are distributed. The fixes check the exact game version (SHA-256) and do nothing on an unknown one.

## FAQ

**How do I play Hitman 2: Silent Assassin in 1920x1080 / 2560x1440 / 4K fullscreen?**
Install Hitman 2 Enhanced, start the game, press F10 and pick your resolution; restart the game. The original game
snaps fullscreen to old 4:3 modes and fails to start above 1600 pixels wide (`Direct3D: Unable to create device`).

**Why are the menus so small at 1080p?** The game draws its menus as a fixed 640x480 box. The `menuscale` fix
enlarges them to the screen height and keeps the mouse cursor inside.

**The game freezes, Esc does nothing, or 47 runs strangely at high FPS.** The engine converts frame time with a
rounding that breaks above about 1024 FPS. `framelimit` caps the frame rate at your monitor's refresh rate and
`timer` keeps the game clock exact.

**Can I change the field of view (FOV)?** Yes, in the F10 page (70-130 %), in missions.

**The menu text looks a bit soft.** Set Options > Graphics > Texture filter to Trilinear (in `Hitman2.ini`:
`DisableTrilinearFiltering 0`); with it off the sharp HD font pages are sampled from their smaller copies.

**The arrow keys do nothing.** That comes from the game's own `keyboard.cfg` (some installs map the arrows only to
zoom); rebind them in Options > Controls.

**Does it work with the GOG or disc version?** Supported and tested: Steam 1.0.0.277 and the retail/disc 1.0.0.265.
**The GOG version has not been tested**, but it should work: GOG ships the 1.0 game, and if its `hitman2.exe` and
`RenderD3D.dll` are the same files as one of the supported versions, every fix applies. If they differ, the fixes
detect it and stay inactive (safe), except the frame-rate limit and the settings page; `h2e.log` in the game folder
says which version was detected. Reports from GOG players are welcome (open an issue with your `h2e.log`).

**Is it safe? Does it touch Steam or the copy protection?** No game file is changed and Steam's DRM is not
touched; everything runs in memory and is removed by the uninstaller. The source and the research behind every fix
are in this repository.

## For developers

This is also a reverse-engineering project: every fix is backed by documented evidence. Start at
[docs/INDEX.md](docs/INDEX.md) and read [CONTRIBUTING.md](CONTRIBUTING.md). AI coding agents: [AGENTS.md](AGENTS.md)
and the project skills in `.claude/skills/` (same files in `.agents/skills/`).

```bash
# Python tools and tests (standard library only)
python -m unittest discover -s tests

# Native proxy (Win32 - the game is 32-bit), with tests
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

# Release zip and installer (PyInstaller for the font-pack builder, Inno Setup 6)
python -I tools/package_release.py
python -m PyInstaller --onefile --name hdfonts_build --distpath dist/payload tools/hdfonts_build.py
iscc /DAppVersion=<version> installer/Hitman2Enhanced.iss
```

Developer installs into a game folder use `tools/d3d8_logger_install.py install|uninstall|status`. The optional
`hdfonts` pack is built from your own install with `python -I tools/hdfonts_build.py <game folder>`.

Pushing a tag `vX.Y.Z` runs the [release workflow](.github/workflows/build.yml): build, all tests, zip and
installer attached to a GitHub release.

## Legal

Code and documentation: [MIT License](LICENSE). Not affiliated with IO Interactive or Eidos. Hitman is a trademark of its owners. You need your own copy of the
game. This project does not circumvent or modify the game's copy protection.
