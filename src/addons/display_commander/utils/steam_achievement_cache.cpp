// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "steam_achievement_cache.hpp"
#include "steam_achievements.hpp"
#include "srwlock_wrapper.hpp"
#include "timing.hpp"

#include <cstring>

#include "../globals.hpp"

// Libraries <standard C++>
#include <atomic>
#include <cstdio>

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::utils {

namespace {

SRWLOCK g_cache_lock = SRWLOCK_INIT;
SteamAchievementCount g_cached{};

std::atomic<int64_t> g_bump_show_until_ns{0};
std::atomic<int> g_bump_unlocked{0};
std::atomic<int> g_bump_total{0};
std::atomic<int> g_last_unlocked{-1};

constexpr size_t kBumpDisplayNameSize = 256;
constexpr size_t kBumpDescriptionSize = 512;
constexpr size_t kBumpDebugSize = 1024;
SRWLOCK g_bump_text_lock = SRWLOCK_INIT;
char g_bump_display_name[kBumpDisplayNameSize] = {};
char g_bump_description[kBumpDescriptionSize] = {};
char g_bump_debug[kBumpDebugSize] = {};

bool IsSteamAchievementNotificationsEnabled() {
    return settings::g_advancedTabSettings.show_steam_achievement_notifications.GetValue();
}

// PlaySoundW from winmm.dll (dynamic load to avoid static link). Flags: SND_ALIAS=0x10000, SND_ASYNC=1, SND_NODEFAULT=2.
void PlayAchievementSoundImpl() {
    using PlaySoundW_t = BOOL(WINAPI*)(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound);
    static PlaySoundW_t s_play = nullptr;
    static std::atomic<bool> s_tried_load{false};
    if (!s_tried_load.exchange(true)) {
        HMODULE winmm = LoadLibraryW(L"winmm.dll");
        if (winmm != nullptr) {
            s_play = reinterpret_cast<PlaySoundW_t>(GetProcAddress(winmm, "PlaySoundW"));
        }
    }
    if (s_play != nullptr) {
        constexpr DWORD kSndAlias = 0x00010000;
        constexpr DWORD kSndAsync = 0x0001;
        constexpr DWORD kSndNoDefault = 0x0002;
        s_play(L"SystemAsterisk", nullptr, kSndAlias | kSndAsync | kSndNoDefault);
    }
}

}  // namespace

SteamAchievementCount GetSteamAchievementCountCachedNonBlocking() {
    if (!IsSteamAchievementNotificationsEnabled()) {
        return SteamAchievementCount{};
    }
    SteamAchievementCount copy;
    {
        ::utils::SRWLockShared lock(g_cache_lock);
        copy = g_cached;
    }
    return copy;
}

SteamAchievementCount GetSteamAchievementCountCached() {
    return GetSteamAchievementCountCachedNonBlocking();
}

void RefreshSteamAchievementCacheFromBackground() {
    if (!IsSteamAchievementNotificationsEnabled()) {
        return;
    }
    SteamAchievementCount fresh = GetSteamAchievementCountBlocking();
    {
        ::utils::SRWLockExclusive lock(g_cache_lock);
        g_cached = fresh;
    }
    // Update bump state only from this background thread (never from overlay/main thread).
    if (!fresh.available) {
        ClearSteamAchievementLastUnlocked();
    } else if (settings::g_advancedTabSettings.show_steam_achievement_counter_increased.GetValue()) {
        const int64_t now_ns = ::utils::get_now_ns();
        SetSteamAchievementBumpFromUnlock(now_ns, fresh.unlocked, fresh.total);
    }
}

void SetSteamAchievementBumpFromUnlock(int64_t now_ns, int unlocked, int total) {
    int prev = g_last_unlocked.load(std::memory_order_relaxed);
    if (unlocked <= prev) {
        return;
    }
    if (!g_last_unlocked.compare_exchange_strong(prev, unlocked, std::memory_order_relaxed)) {
        return;
    }
    // Only play sound when we had a real previous count (prev >= 0), not on first run when establishing baseline
    if (prev >= 0 && settings::g_advancedTabSettings.play_sound_on_achievement.GetValue()) {
        PlayAchievementSoundImpl();
    }
    g_bump_show_until_ns.store(now_ns + kSteamAchievementBumpDurationSec * ::utils::SEC_TO_NS,
                               std::memory_order_relaxed);
    g_bump_unlocked.store(unlocked, std::memory_order_relaxed);
    g_bump_total.store(total, std::memory_order_relaxed);
    SteamLastUnlockedInfo info;
    GetLastUnlockedAchievementInfoBlocking(unlocked, total, &info);
    {
        ::utils::SRWLockExclusive lock(g_bump_text_lock);
        snprintf(g_bump_display_name, kBumpDisplayNameSize, "%s", info.display_name);
        snprintf(g_bump_description, kBumpDescriptionSize, "%s", info.description);
        snprintf(g_bump_debug, kBumpDebugSize, "%s", info.debug);
    }
}

void ClearSteamAchievementLastUnlocked() {
    g_last_unlocked.store(-1, std::memory_order_relaxed);
}

void TriggerSteamAchievementTestBump() {
    if (settings::g_advancedTabSettings.play_sound_on_achievement.GetValue()) {
        PlayAchievementSoundImpl();
    }
    SteamAchievementCount ac = GetSteamAchievementCountCachedNonBlocking();
    const int64_t now_ns = ::utils::get_now_ns();
    g_bump_show_until_ns.store(now_ns + kSteamAchievementBumpDurationSec * ::utils::SEC_TO_NS,
                               std::memory_order_relaxed);
    g_bump_unlocked.store(ac.unlocked, std::memory_order_relaxed);
    g_bump_total.store(ac.total, std::memory_order_relaxed);
    SteamLastUnlockedInfo info;
    GetLastUnlockedAchievementInfoBlocking(ac.unlocked, ac.total, &info);
    {
        ::utils::SRWLockExclusive lock(g_bump_text_lock);
        snprintf(g_bump_display_name, kBumpDisplayNameSize, "%s", info.display_name);
        snprintf(g_bump_description, kBumpDescriptionSize, "%s", info.description);
        snprintf(g_bump_debug, kBumpDebugSize, "%s", info.debug);
    }
}

bool IsSteamAchievementBumpActiveNonBlocking(int64_t now_ns) {
    return now_ns < g_bump_show_until_ns.load(std::memory_order_relaxed)
           && g_bump_total.load(std::memory_order_relaxed) > 0;
}

void GetSteamAchievementBumpDisplayNonBlocking(int* out_unlocked, int* out_total) {
    if (out_unlocked != nullptr) {
        *out_unlocked = g_bump_unlocked.load(std::memory_order_relaxed);
    }
    if (out_total != nullptr) {
        *out_total = g_bump_total.load(std::memory_order_relaxed);
    }
}

void GetSteamAchievementBumpTextNonBlocking(char* out_display_name, size_t display_name_size,
                                            char* out_description, size_t description_size,
                                            char* out_debug, size_t debug_size) {
    ::utils::SRWLockShared lock(g_bump_text_lock);
    if (out_display_name != nullptr && display_name_size > 0) {
        snprintf(out_display_name, display_name_size, "%s", g_bump_display_name);
    }
    if (out_description != nullptr && description_size > 0) {
        snprintf(out_description, description_size, "%s", g_bump_description);
    }
    if (out_debug != nullptr && debug_size > 0) {
        snprintf(out_debug, debug_size, "%s", g_bump_debug);
    }
}

void PlayAchievementSound() {
    PlayAchievementSoundImpl();
}

}  // namespace display_commander::utils
