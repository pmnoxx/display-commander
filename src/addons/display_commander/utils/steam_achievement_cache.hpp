#pragma once

#include "steam_achievements.hpp"

#include <cstddef>
#include <cstdint>

namespace display_commander::utils {

// Returns Steam achievement count from a cache updated by a background thread at most once per second.
// Use this instead of GetSteamAchievementCount() from overlay or UI to avoid per-frame Steam API calls.
SteamAchievementCount GetSteamAchievementCountCached();

// Bump state: overlay shows "Achievement unlocked! X / Y" for 30s after a real unlock or test trigger.
constexpr int64_t kSteamAchievementBumpDurationSec = 30;

// Call when overlay detects unlocked count increased (only if show_steam_achievement_counter_increased is on).
void SetSteamAchievementBumpFromUnlock(int64_t now_ns, int unlocked, int total);
// Call when Steam achievements are unavailable (e.g. module not loaded) to reset last-unlocked tracking.
void ClearSteamAchievementLastUnlocked();
// Call from Advanced tab "Test achievement" button to show the 30s notification with current count.
void TriggerSteamAchievementTestBump();

// For overlay: only draw Steam achievement overlay when this returns true.
bool IsSteamAchievementBumpActive(int64_t now_ns);
void GetSteamAchievementBumpDisplay(int* out_unlocked, int* out_total);
// Optional: copy last-unlocked display name and debug lines (newline-separated). Buffers may be null; sizes ignored then.
void GetSteamAchievementBumpText(char* out_display_name, size_t display_name_size,
                                 char* out_debug, size_t debug_size);

// Play the achievement notification sound (system sound). Safe to call from any thread. Used on new achievement
// when play_sound_on_achievement is on, and for the "Test sound" button. No-op if winmm is unavailable.
void PlayAchievementSound();

}  // namespace display_commander::utils
