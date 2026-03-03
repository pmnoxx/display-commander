#pragma once

#include <cstdint>

// Persist list of Steam app IDs marked as favorites in the Games tab "Launch Steam game" search.
// Uses HKCU\Software\Display Commander\SteamFavorites.

namespace display_commander::steam_favorites {

// Add app_id to favorites (persisted to registry). No-op if app_id is 0.
void AddSteamGameToFavorites(uint32_t app_id);

// Remove app_id from favorites. No-op if app_id is 0.
void RemoveSteamGameFromFavorites(uint32_t app_id);

// Return true if app_id is in the favorites list.
bool IsSteamGameFavorite(uint32_t app_id);

}  // namespace display_commander::steam_favorites
