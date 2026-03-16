#pragma once

// Source Code <Display Commander>
// Windows SDR content brightness (nits) when HDR is on. Uses undocumented DwmpSDRToHDRBoost.

#include <windows.h>

namespace display_commander::display::sdr_white_level {

constexpr int kSdrNitsMin = 80;
constexpr int kSdrNitsMax = 480;

// Monitor for the game window, or primary if unknown.
HMONITOR GetGameMonitorForSdrBrightness();

// Set Windows SDR content brightness for the given monitor (when HDR is on). nits in [kSdrNitsMin, kSdrNitsMax].
bool SetSdrWhiteLevelNits(HMONITOR monitor, float nits);

}  // namespace display_commander::display::sdr_white_level
