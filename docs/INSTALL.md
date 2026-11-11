Hitman 2 Enhanced - installation
================================

Unofficial fixes for Hitman 2: Silent Assassin (Steam version 1.0.0.277). Nothing in the game is
modified on disk: the fixes run in memory while the game is running.

Easiest: run Hitman2Enhanced-Setup-<version>.exe (it finds the game and can be uninstalled from
Windows "Installed apps"). Manual install from the zip:

Install
-------
1. Open the game folder (Steam: right-click the game > Manage > Browse local files).
2. Copy d3d8.dll and h2e.ini from this zip into that folder (next to hitman2.exe).
   If a d3d8.dll is already there it belongs to another mod: do not overwrite it.
3. Start the game from Steam. In the main menu press F10 to open the settings page and pick your
   resolution, display mode, field of view and frame-rate limit. Resolution and display mode apply
   after restarting the game.

Configure (h2e.ini)
-------------------
  [resolution] Enabled=1   fullscreen uses your Resolution exactly
  [menuscale]  Enabled=1   menus, briefing and pause menu fill the screen
  [framelimit] Enabled=1   frame-rate cap (default: monitor refresh rate; the F10 page changes it)
  [timer]      Enabled=1   correct game clock speed at any frame rate
  [settings]   Enabled=1   the F10 settings page
  [hdfonts]    Enabled=0   sharp menu text; needs h2e_hdfonts.bin built from your own game files
  [horplus]    Enabled=0   experimental wider field of view - breaks the HUD
  [log]        Level=1     0 = no log, 1 = short log in h2e.log, 2 = developer logging (large)

Uninstall
---------
Delete d3d8.dll, h2e.ini, h2e.log and h2e_settings.ini from the game folder. If Hitman2.ini still
asks for a resolution the original game cannot use in fullscreen (wider than 1600 pixels), add a
line "Window" (windowed) or set a 4:3 resolution again.

Known limitations (see CHANGELOG.md and docs/KNOWN_ISSUES.md)
-------------------------------------------------------------
- Menus keep their 4:3 layout (side bars at widescreen); menu art is 2002 resolution.
- Changing the field of view also moves parts of the HUD.
- Rare crashes while loading a save also happen in the original game (GB-008/GB-009).
