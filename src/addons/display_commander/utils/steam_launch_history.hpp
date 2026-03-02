#pragma once

#include <cstdint>

// Persist Steam game launch timestamps for "most recently launched" ordering in Games tab.
// Uses HKCU\Software\Display Commander\SteamLaunchHistory.

namespace display_commander::steam_launch_history {

// Record that the given Steam app was launched from the Games tab UI (stores current time).
void RecordSteamLaunch(uint32_t app_id);

// Return Unix timestamp of last launch for app_id, or 0 if never launched from this UI.
int64_t GetSteamLaunchTimestamp(uint32_t app_id);

}  // namespace display_commander::steam_launch_history
