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
#include <thread>

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::utils {

namespace {

constexpr unsigned int kPollIntervalMs = 1000;

SRWLOCK g_cache_lock = SRWLOCK_INIT;
SteamAchievementCount g_cached{};
std::atomic<bool> g_thread_started{false};

std::atomic<int64_t> g_bump_show_until_ns{0};
std::atomic<int> g_bump_unlocked{0};
std::atomic<int> g_bump_total{0};
std::atomic<int> g_last_unlocked{-1};

constexpr size_t kBumpDisplayNameSize = 256;
constexpr size_t kBumpDebugSize = 1024;
SRWLOCK g_bump_text_lock = SRWLOCK_INIT;
char g_bump_display_name[kBumpDisplayNameSize] = {};
char g_bump_debug[kBumpDebugSize] = {};

bool IsSteamAchievementNotificationsEnabled() {
    return settings::g_advancedTabSettings.show_steam_achievement_notifications.GetValue();
}

void SteamAchievementCacheThread() {
    while (!g_shutdown.load(std::memory_order_relaxed)) {
        if (!IsSteamAchievementNotificationsEnabled()) {
            g_thread_started.store(false);
            return;
        }
        Sleep(kPollIntervalMs);
        if (g_shutdown.load(std::memory_order_relaxed)) {
            break;
        }
        if (!IsSteamAchievementNotificationsEnabled()) {
            g_thread_started.store(false);
            return;
        }
        SteamAchievementCount fresh = GetSteamAchievementCount();
        {
            ::utils::SRWLockExclusive lock(g_cache_lock);
            g_cached = fresh;
        }
    }
}

}  // namespace

SteamAchievementCount GetSteamAchievementCountCached() {
    if (!IsSteamAchievementNotificationsEnabled()) {
        return SteamAchievementCount{};
    }
    if (!g_thread_started.exchange(true)) {
        SteamAchievementCount initial = GetSteamAchievementCount();
        {
            ::utils::SRWLockExclusive lock(g_cache_lock);
            g_cached = initial;
        }
        std::thread(SteamAchievementCacheThread).detach();
    }
    SteamAchievementCount copy;
    {
        ::utils::SRWLockShared lock(g_cache_lock);
        copy = g_cached;
    }
    return copy;
}

void SetSteamAchievementBumpFromUnlock(int64_t now_ns, int unlocked, int total) {
    int prev = g_last_unlocked.load(std::memory_order_relaxed);
    if (unlocked <= prev) {
        return;
    }
    if (!g_last_unlocked.compare_exchange_strong(prev, unlocked, std::memory_order_relaxed)) {
        return;
    }
    g_bump_show_until_ns.store(now_ns + kSteamAchievementBumpDurationSec * ::utils::SEC_TO_NS,
                               std::memory_order_relaxed);
    g_bump_unlocked.store(unlocked, std::memory_order_relaxed);
    g_bump_total.store(total, std::memory_order_relaxed);
    SteamLastUnlockedInfo info;
    GetLastUnlockedAchievementInfo(unlocked, total, &info);
    {
        ::utils::SRWLockExclusive lock(g_bump_text_lock);
        snprintf(g_bump_display_name, kBumpDisplayNameSize, "%s", info.display_name);
        snprintf(g_bump_debug, kBumpDebugSize, "%s", info.debug);
    }
}

void ClearSteamAchievementLastUnlocked() {
    g_last_unlocked.store(-1, std::memory_order_relaxed);
}

void TriggerSteamAchievementTestBump() {
    SteamAchievementCount ac = GetSteamAchievementCountCached();
    const int64_t now_ns = ::utils::get_now_ns();
    g_bump_show_until_ns.store(now_ns + kSteamAchievementBumpDurationSec * ::utils::SEC_TO_NS,
                               std::memory_order_relaxed);
    g_bump_unlocked.store(ac.unlocked, std::memory_order_relaxed);
    g_bump_total.store(ac.total, std::memory_order_relaxed);
    SteamLastUnlockedInfo info;
    GetLastUnlockedAchievementInfo(ac.unlocked, ac.total, &info);
    {
        ::utils::SRWLockExclusive lock(g_bump_text_lock);
        snprintf(g_bump_display_name, kBumpDisplayNameSize, "%s", info.display_name);
        snprintf(g_bump_debug, kBumpDebugSize, "%s", info.debug);
    }
}

bool IsSteamAchievementBumpActive(int64_t now_ns) {
    return now_ns < g_bump_show_until_ns.load(std::memory_order_relaxed)
           && g_bump_total.load(std::memory_order_relaxed) > 0;
}

void GetSteamAchievementBumpDisplay(int* out_unlocked, int* out_total) {
    if (out_unlocked != nullptr) {
        *out_unlocked = g_bump_unlocked.load(std::memory_order_relaxed);
    }
    if (out_total != nullptr) {
        *out_total = g_bump_total.load(std::memory_order_relaxed);
    }
}

void GetSteamAchievementBumpText(char* out_display_name, size_t display_name_size,
                                 char* out_debug, size_t debug_size) {
    ::utils::SRWLockShared lock(g_bump_text_lock);
    if (out_display_name != nullptr && display_name_size > 0) {
        snprintf(out_display_name, display_name_size, "%s", g_bump_display_name);
    }
    if (out_debug != nullptr && debug_size > 0) {
        snprintf(out_debug, debug_size, "%s", g_bump_debug);
    }
}

}  // namespace display_commander::utils
