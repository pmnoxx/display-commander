#pragma once

#include <cstddef>
#include <cstdint>

namespace display_commander::utils {

// Result of querying Steam for achievement counts (read-only; no mutex).
// Uses game's Steam API (SteamUserStats export or, if only SteamClient present, SteamClient path).
struct SteamAchievementCount {
    bool available = false;
    int unlocked = 0;
    int total = 0;
};

// Single achievement entry for list display (API name, localized display name, description, unlocked, unlock time).
struct SteamAchievementEntry {
    static constexpr size_t kMaxApiName = 128;
    static constexpr size_t kMaxDisplayName = 256;
    static constexpr size_t kMaxDescription = 512;
    char api_name[kMaxApiName] = {};
    char display_name[kMaxDisplayName] = {};
    char description[kMaxDescription] = {};  // Localized description from GetAchievementDisplayAttribute(..., "desc").
    bool unlocked = false;
    uint32_t unlock_time = 0;  // Unix time when unlocked; 0 if locked (used to sort by last unlocked).
};

SteamAchievementCount GetSteamAchievementCountBlocking();

// Fills entries[0..return-1] with achievement list. Returns count filled, or -1 on error.
// Uses same resolution as GetSteamAchievementCountBlocking (SteamUserStats direct or SteamClient path).
int GetSteamAchievementListBlocking(SteamAchievementEntry* entries, size_t max_entries);

// Writes one-line debug: which exports are present for achievements (Special K uses SteamUserStats).
void GetSteamAchievementExportsDebugBlocking(char* buf, size_t buf_size);

// Result of querying the "last unlocked" achievement (for notification display).
// debug contains newline-separated lines indicating which query failed (e.g. "GetAchievementAndUnlockTime not in
// vtable").
struct SteamLastUnlockedInfo {
    static constexpr size_t kMaxDisplayName = 256;
    static constexpr size_t kMaxDescription = 512;
    static constexpr size_t kMaxDebug = 1024;
    bool has_display_name = false;
    char display_name[kMaxDisplayName] = {};
    char description[kMaxDescription] = {};  // Localized description from GetAchievementDisplayAttribute(..., "desc").
    char debug[kMaxDebug] = {};
};

// Fills out_info with the display name of the most recently unlocked achievement (by unlock time), if available.
// Always appends debug lines to out_info->debug describing which step failed (vtable missing, null return, etc.).
void GetLastUnlockedAchievementInfoBlocking(int unlocked_count, int total, SteamLastUnlockedInfo* out_info);

}  // namespace display_commander::utils
