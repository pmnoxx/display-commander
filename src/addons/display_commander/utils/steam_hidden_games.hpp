#pragma once

#include <cstdint>

// Persist list of Steam app IDs to hide from the Games tab "Launch Steam game" search results.
// Uses HKCU\Software\Display Commander\SteamHiddenGames.

namespace display_commander::steam_hidden_games {

// Add app_id to the hidden list (persisted to registry). No-op if app_id is 0.
void AddSteamGameToHidden(uint32_t app_id);

// Return true if app_id is in the hidden list.
bool IsSteamGameHidden(uint32_t app_id);

}  // namespace display_commander::steam_hidden_games
