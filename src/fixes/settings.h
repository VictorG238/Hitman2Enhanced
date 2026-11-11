// Fix "settings": an in-game settings page drawn by the proxy (F10), for the options the game's own menus lack.
//
// The game's options menus are scene data (MainMenu.ZIP, RENDERING.md "Options menus are scene data"), so new entries
// cannot be added there without changing game files. The page is drawn over the frame before Present; while it is
// open the game's DirectInput keyboard/mouse data is blanked (dinput8 device vtable hook) so the menus underneath do
// not react.
//
// Items: resolution and display mode (written to Hitman2.ini, applied after a restart), field of view (scene camera
// projection, live), frame-rate cap (framelimit, live). User choices are stored in h2e_settings.ini next to h2e.ini.
#pragma once
#include <string>
#include <vector>

#include "../common/d3d8_min.h"

namespace h2e {
namespace settings {

// Frame-rate cap choices: 0 = monitor refresh rate (default), -1 = unlimited, otherwise fps.
extern const int kFpsChoices[];
extern const int kFpsChoiceCount;
int FpsChoiceIndex(int maxFps);  // index of the closest choice (default 0)

// Field of view in percent of the original horizontal angle (tangent scale). 100 = original.
constexpr int kFovMin = 70, kFovMax = 130, kFovStep = 5, kFovDefault = 100;
constexpr double kBaseFovXDeg = 67.4;  // gameplay camera, horizontal (run-002), constant across aspect (Vert-)
double FovDegrees(int percent);
// Scales a scene projection (centred LH perspective, zf > 4000) for `percent`; false = not a scene camera / 100 %.
bool ApplyFov(d3d8::Matrix& m, int percent);

struct Resolution {
    unsigned width = 0, height = 0;
    bool operator==(const Resolution& o) const { return width == o.width && height == o.height; }
    bool operator<(const Resolution& o) const {
        return width != o.width ? width < o.width : height < o.height;
    }
};
// Parses "1920x1080" (spaces allowed around x). False if malformed or below 640x480.
bool ParseResolution(const std::string& text, Resolution* out);
// Sorted unique list of `modes` (>= 640x480), with `current` added if missing.
std::vector<Resolution> ResolutionChoices(std::vector<Resolution> modes, Resolution current);

// Hitman2.ini is "Key Value" per line (some lines "Key=Value", flags are a bare "Key"). Keys match
// case-insensitively on the first token (up to space, tab or '=').
bool FindIniLine(const std::string& text, const char* key, std::string* value);
// Sets a "Key Value" line (value empty: bare "Key"), or removes the key's lines when `remove`. A missing key is
// appended. Line endings of the file (CRLF) are kept.
std::string SetIniLine(const std::string& text, const char* key, const std::string& value, bool remove = false);

// Page rows.
enum Row { kRowResolution, kRowDisplayMode, kRowFov, kRowFrameLimit, kRowDefaults, kRowClose, kRowCount };
const char* RowLabel(int row);
const char* RowHelp(int row);

// Key auto-repeat for held navigation keys: true on the press and then every `rate` ms after `delay` ms.
struct KeyRepeat {
    bool down = false;
    unsigned long since = 0, last = 0;
    bool Update(bool isDown, unsigned long nowMs, unsigned long delay = 400, unsigned long rate = 70);
};

}  // namespace settings
}  // namespace h2e
