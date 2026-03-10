// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "steam_achievements.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

namespace display_commander::utils {

namespace {

#ifdef _WIN64
HMODULE GetSteamModule() { return GetModuleHandleW(L"steam_api64.dll"); }
#else
HMODULE GetSteamModule() { return GetModuleHandleW(L"steam_api.dll"); }
#endif

// Interface reference: external-src/steamapi_sdk (read-only; do not import).
// ISteamUserStats — latest achievement by date: GetNumAchievements() then for each index i:
//   GetAchievementName(i) → API name; GetAchievementAndUnlockTime(name, &achieved, &unlockTime)
//   (unlockTime = seconds since 1970-01-01); take achievement with max unlockTime among achieved;
//   display name via GetAchievementDisplayAttribute(name, "name"). SDK version string: 013.
// ISteamUserStats vtable indices (0-based; matches isteamuserstats.h).
constexpr unsigned int k_get_achievement_index = 6;
constexpr unsigned int k_get_achievement_and_unlock_time_index = 9;
constexpr unsigned int k_get_achievement_display_attribute_index = 12;
constexpr unsigned int k_get_num_achievements_index = 14;
constexpr unsigned int k_get_achievement_name_index = 15;

// ISteamClient vtable indices.
constexpr unsigned int k_create_steam_pipe_index = 0;
constexpr unsigned int k_connect_to_global_user_index = 2;
constexpr unsigned int k_get_isteam_user_stats_index = 13;

using SteamUserStatsFn = void* (*)();
using SteamClientFn = void* (*)();

using GetNumAchievementsFn = unsigned int (*)(void* self);
using GetAchievementNameFn = const char* (*)(void* self, unsigned int index);
using GetAchievementFn = bool (*)(void* self, const char* name, bool* achieved);
using GetAchievementAndUnlockTimeFn = bool (*)(void* self, const char* name, bool* achieved, unsigned int* unlock_time);
using GetAchievementDisplayAttributeFn = const char* (*)(void* self, const char* name, const char* key);

using CreateSteamPipeFn = intptr_t (*)(void* self);
using ConnectToGlobalUserFn = intptr_t (*)(void* self, intptr_t h_pipe);
using GetISteamUserStatsFn = void* (*)(void* self, intptr_t h_user, intptr_t h_pipe, const char* version);

// Try 011 first (matches Special K / many games); then 012, 013 (SDK default).
constexpr const char* k_userstats_versions[] = {
    "STEAMUSERSTATS_INTERFACE_VERSION011",
    "STEAMUSERSTATS_INTERFACE_VERSION012",
    "STEAMUSERSTATS_INTERFACE_VERSION013",
};

bool GetAchievementCountFromUserStats(void* p_user_stats, SteamAchievementCount& result) {
    if (p_user_stats == nullptr) {
        return false;
    }
    void** vtable = *reinterpret_cast<void***>(p_user_stats);
    if (vtable == nullptr || vtable[k_get_num_achievements_index] == nullptr
        || vtable[k_get_achievement_name_index] == nullptr || vtable[k_get_achievement_index] == nullptr) {
        return false;
    }
    auto* get_num = reinterpret_cast<GetNumAchievementsFn>(vtable[k_get_num_achievements_index]);
    auto* get_name = reinterpret_cast<GetAchievementNameFn>(vtable[k_get_achievement_name_index]);
    auto* get_achieved = reinterpret_cast<GetAchievementFn>(vtable[k_get_achievement_index]);
    unsigned int total = get_num(p_user_stats);
    if (total > 65536) {
        return false;
    }
    int unlocked = 0;
    for (unsigned int i = 0; i < total; ++i) {
        const char* name = get_name(p_user_stats, i);
        if (name == nullptr) {
            continue;
        }
        bool achieved = false;
        if (get_achieved(p_user_stats, name, &achieved) && achieved) {
            ++unlocked;
        }
    }
    result.available = true;
    result.unlocked = unlocked;
    result.total = static_cast<int>(total);
    return true;
}

// Fills entries from p_user_stats. Fetches unlock time for sorting by last unlocked.
int FillAchievementListFromUserStats(void* p_user_stats, SteamAchievementEntry* entries, size_t max_entries) {
    if (p_user_stats == nullptr || entries == nullptr || max_entries == 0) {
        return -1;
    }
    void** vtable = *reinterpret_cast<void***>(p_user_stats);
    if (vtable == nullptr || vtable[k_get_num_achievements_index] == nullptr
        || vtable[k_get_achievement_name_index] == nullptr || vtable[k_get_achievement_index] == nullptr) {
        return -1;
    }
    auto* get_num = reinterpret_cast<GetNumAchievementsFn>(vtable[k_get_num_achievements_index]);
    auto* get_name = reinterpret_cast<GetAchievementNameFn>(vtable[k_get_achievement_name_index]);
    auto* get_achieved = reinterpret_cast<GetAchievementFn>(vtable[k_get_achievement_index]);
    GetAchievementAndUnlockTimeFn get_unlock_time = nullptr;
    if (vtable[k_get_achievement_and_unlock_time_index] != nullptr) {
        get_unlock_time =
            reinterpret_cast<GetAchievementAndUnlockTimeFn>(vtable[k_get_achievement_and_unlock_time_index]);
    }
    GetAchievementDisplayAttributeFn get_display_attr = nullptr;
    if (vtable[k_get_achievement_display_attribute_index] != nullptr) {
        get_display_attr =
            reinterpret_cast<GetAchievementDisplayAttributeFn>(vtable[k_get_achievement_display_attribute_index]);
    }
    unsigned int total = get_num(p_user_stats);
    if (total > 65536) {
        return -1;
    }
    const size_t n = (max_entries < total) ? max_entries : static_cast<size_t>(total);
    for (size_t i = 0; i < n; ++i) {
        SteamAchievementEntry& e = entries[i];
        e.api_name[0] = '\0';
        e.display_name[0] = '\0';
        e.unlocked = false;
        e.unlock_time = 0;
        const char* api_name = get_name(p_user_stats, static_cast<unsigned int>(i));
        if (api_name != nullptr) {
            snprintf(e.api_name, SteamAchievementEntry::kMaxApiName, "%s", api_name);
            get_achieved(p_user_stats, api_name, &e.unlocked);
            if (get_unlock_time != nullptr) {
                bool achieved = false;
                unsigned int ut = 0;
                if (get_unlock_time(p_user_stats, api_name, &achieved, &ut)) {
                    e.unlock_time = ut;
                }
            }
            if (get_display_attr != nullptr) {
                const char* display = get_display_attr(p_user_stats, api_name, "name");
                if (display != nullptr && display[0] != '\0') {
                    snprintf(e.display_name, SteamAchievementEntry::kMaxDisplayName, "%s", display);
                } else {
                    snprintf(e.display_name, SteamAchievementEntry::kMaxDisplayName, "%s", api_name);
                }
            } else {
                snprintf(e.display_name, SteamAchievementEntry::kMaxDisplayName, "%s", api_name);
            }
        }
    }
    return static_cast<int>(n);
}

// Returns pointer and optionally count; caller must not use pointer after other Steam API calls.
bool GetUserStatsPointer(void*& out_p_user_stats, SteamAchievementCount* out_count) {
    out_p_user_stats = nullptr;
    HMODULE steam = GetSteamModule();
    if (steam == nullptr) {
        return false;
    }
    auto* steam_user_stats_fn = reinterpret_cast<SteamUserStatsFn>(GetProcAddress(steam, "SteamUserStats"));
    if (steam_user_stats_fn != nullptr) {
        void* p = steam_user_stats_fn();
        if (p != nullptr) {
            SteamAchievementCount c;
            if (GetAchievementCountFromUserStats(p, c)) {
                out_p_user_stats = p;
                if (out_count != nullptr) {
                    *out_count = c;
                }
                return true;
            }
        }
    }
    auto* steam_client_fn = reinterpret_cast<SteamClientFn>(GetProcAddress(steam, "SteamClient"));
    if (steam_client_fn == nullptr) {
        return false;
    }
    void* p_client = steam_client_fn();
    if (p_client == nullptr) {
        return false;
    }
    void** client_vtable = *reinterpret_cast<void***>(p_client);
    if (client_vtable == nullptr || client_vtable[k_create_steam_pipe_index] == nullptr
        || client_vtable[k_connect_to_global_user_index] == nullptr
        || client_vtable[k_get_isteam_user_stats_index] == nullptr) {
        return false;
    }
    auto* create_pipe = reinterpret_cast<CreateSteamPipeFn>(client_vtable[k_create_steam_pipe_index]);
    auto* connect_user = reinterpret_cast<ConnectToGlobalUserFn>(client_vtable[k_connect_to_global_user_index]);
    auto* get_user_stats = reinterpret_cast<GetISteamUserStatsFn>(client_vtable[k_get_isteam_user_stats_index]);
    intptr_t h_pipe = create_pipe(p_client);
    if (h_pipe == 0) {
        return false;
    }
    intptr_t h_user = connect_user(p_client, h_pipe);
    if (h_user == 0) {
        return false;
    }
    for (const char* version : k_userstats_versions) {
        void* p = get_user_stats(p_client, h_user, h_pipe, version);
        if (p != nullptr) {
            SteamAchievementCount c;
            if (GetAchievementCountFromUserStats(p, c)) {
                out_p_user_stats = p;
                if (out_count != nullptr) {
                    *out_count = c;
                }
                return true;
            }
        }
    }
    return false;
}

void AppendDebugLine(SteamLastUnlockedInfo* out_info, const char* fmt, ...) {
    if (out_info == nullptr) {
        return;
    }
    size_t len = std::strlen(out_info->debug);
    if (len >= SteamLastUnlockedInfo::kMaxDebug - 2) {
        return;
    }
    int written = snprintf(out_info->debug + len, SteamLastUnlockedInfo::kMaxDebug - len, "%s", (len > 0) ? "\n" : "");
    if (written > 0) {
        len += static_cast<size_t>(written);
    }
    if (len >= SteamLastUnlockedInfo::kMaxDebug - 1) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    written = vsnprintf(out_info->debug + len, SteamLastUnlockedInfo::kMaxDebug - len, fmt, args);
    va_end(args);
}

// Returns false if caller should try another UserStats interface version (e.g. sanity fail on GetNumAchievements).
bool GetLastUnlockedFromUserStats(void* p_user_stats, int /*unlocked_count*/, int total,
                                  SteamLastUnlockedInfo* out_info) {
    if (p_user_stats == nullptr || out_info == nullptr || total <= 0) {
        if (out_info != nullptr && total <= 0) {
            AppendDebugLine(out_info, "total<=0, no achievements");
        }
        return true;
    }
    void** vtable = *reinterpret_cast<void***>(p_user_stats);
    if (vtable == nullptr) {
        AppendDebugLine(out_info, "UserStats vtable is null");
        return true;
    }
    if (vtable[k_get_num_achievements_index] == nullptr) {
        AppendDebugLine(out_info, "GetNumAchievements not in vtable (index %u)", k_get_num_achievements_index);
        return true;
    }
    if (vtable[k_get_achievement_name_index] == nullptr) {
        AppendDebugLine(out_info, "GetAchievementName not in vtable (index %u)", k_get_achievement_name_index);
        return true;
    }
    if (vtable[k_get_achievement_and_unlock_time_index] == nullptr) {
        AppendDebugLine(out_info, "GetAchievementAndUnlockTime not in vtable (index %u)",
                       k_get_achievement_and_unlock_time_index);
        return true;
    }
    auto* get_num = reinterpret_cast<GetNumAchievementsFn>(vtable[k_get_num_achievements_index]);
    auto* get_name = reinterpret_cast<GetAchievementNameFn>(vtable[k_get_achievement_name_index]);
    auto* get_achieved_time =
        reinterpret_cast<GetAchievementAndUnlockTimeFn>(vtable[k_get_achievement_and_unlock_time_index]);
    GetAchievementDisplayAttributeFn get_display_attr = nullptr;
    if (vtable[k_get_achievement_display_attribute_index] != nullptr) {
        get_display_attr =
            reinterpret_cast<GetAchievementDisplayAttributeFn>(vtable[k_get_achievement_display_attribute_index]);
    } else {
        AppendDebugLine(out_info, "GetAchievementDisplayAttribute not in vtable (index %u)",
                       k_get_achievement_display_attribute_index);
    }
    unsigned int num = get_num(p_user_stats);
    if (num > 65536) {
        AppendDebugLine(out_info,
                       "GetNumAchievements (vtable index %u) returned %u (sanity fail) — wrong interface version. "
                       "The 18/63 count comes from GetSteamAchievementCountBlocking (same API).",
                       k_get_num_achievements_index, num);
        return false;  // Caller may try another UserStats version
    }
    unsigned int max_unlock_time = 0;
    const char* best_api_name = nullptr;
    for (unsigned int i = 0; i < num; ++i) {
        const char* name = get_name(p_user_stats, i);
        if (name == nullptr || name[0] == '\0') {
            AppendDebugLine(out_info, "GetAchievementName(%u) returned null or empty", i);
            continue;
        }
        bool achieved = false;
        unsigned int unlock_time = 0;
        if (!get_achieved_time(p_user_stats, name, &achieved, &unlock_time)) {
            AppendDebugLine(out_info, "GetAchievementAndUnlockTime(\"%s\") failed", name);
            continue;
        }
        if (!achieved) {
            continue;
        }
        if (unlock_time > max_unlock_time) {
            max_unlock_time = unlock_time;
            best_api_name = name;
        }
    }
    if (best_api_name == nullptr) {
        AppendDebugLine(out_info, "no unlocked achievement with valid unlock time found");
        return true;
    }
    const char* display = best_api_name;
    if (get_display_attr != nullptr) {
        const char* attr = get_display_attr(p_user_stats, best_api_name, "name");
        if (attr != nullptr && attr[0] != '\0') {
            display = attr;
        } else {
            AppendDebugLine(out_info, "GetAchievementDisplayAttribute(\"%s\", \"name\") returned null or empty",
                            best_api_name);
        }
    }
    out_info->has_display_name = true;
    snprintf(out_info->display_name, SteamLastUnlockedInfo::kMaxDisplayName, "%s", display);
    return true;
}

}  // namespace

void GetLastUnlockedAchievementInfoBlocking(int unlocked_count, int total, SteamLastUnlockedInfo* out_info) {
    if (out_info == nullptr) {
        return;
    }
    out_info->debug[0] = '\0';
    out_info->has_display_name = false;
    out_info->display_name[0] = '\0';
    HMODULE steam = GetSteamModule();
    if (steam == nullptr) {
        AppendDebugLine(out_info, "Steam module not loaded (steam_api64.dll / steam_api.dll)");
        return;
    }
    void* p_user_stats = nullptr;
    auto* steam_user_stats_fn = reinterpret_cast<SteamUserStatsFn>(GetProcAddress(steam, "SteamUserStats"));
    if (steam_user_stats_fn != nullptr) {
        p_user_stats = steam_user_stats_fn();
        if (p_user_stats != nullptr) {
            if (GetLastUnlockedFromUserStats(p_user_stats, unlocked_count, total, out_info)) {
                return;
            }
            out_info->debug[0] = '\0';
            out_info->has_display_name = false;
            out_info->display_name[0] = '\0';
            AppendDebugLine(out_info, "SteamUserStats() pointer gave sanity fail, trying SteamClient path...");
        } else {
            AppendDebugLine(out_info, "SteamUserStats() returned null");
        }
    } else {
        AppendDebugLine(out_info, "SteamUserStats export not found");
    }
    auto* steam_client_fn = reinterpret_cast<SteamClientFn>(GetProcAddress(steam, "SteamClient"));
    if (steam_client_fn == nullptr) {
        AppendDebugLine(out_info, "SteamClient export not found (cannot try pipe path)");
        return;
    }
    void* p_client = steam_client_fn();
    if (p_client == nullptr) {
        AppendDebugLine(out_info, "SteamClient() returned null");
        return;
    }
    void** client_vtable = *reinterpret_cast<void***>(p_client);
    if (client_vtable == nullptr || client_vtable[k_create_steam_pipe_index] == nullptr
        || client_vtable[k_connect_to_global_user_index] == nullptr
        || client_vtable[k_get_isteam_user_stats_index] == nullptr) {
        AppendDebugLine(out_info, "SteamClient vtable missing CreateSteamPipe/ConnectToGlobalUser/GetISteamUserStats");
        return;
    }
    auto* create_pipe = reinterpret_cast<CreateSteamPipeFn>(client_vtable[k_create_steam_pipe_index]);
    auto* connect_user = reinterpret_cast<ConnectToGlobalUserFn>(client_vtable[k_connect_to_global_user_index]);
    auto* get_user_stats =
        reinterpret_cast<GetISteamUserStatsFn>(client_vtable[k_get_isteam_user_stats_index]);
    intptr_t h_pipe = create_pipe(p_client);
    if (h_pipe == 0) {
        AppendDebugLine(out_info, "CreateSteamPipe() returned 0");
        return;
    }
    intptr_t h_user = connect_user(p_client, h_pipe);
    if (h_user == 0) {
        AppendDebugLine(out_info, "ConnectToGlobalUser() returned 0");
        return;
    }
    for (const char* version : k_userstats_versions) {
        p_user_stats = get_user_stats(p_client, h_user, h_pipe, version);
        if (p_user_stats != nullptr) {
            if (GetLastUnlockedFromUserStats(p_user_stats, unlocked_count, total, out_info)) {
                return;
            }
            out_info->debug[0] = '\0';
            out_info->has_display_name = false;
            out_info->display_name[0] = '\0';
        }
    }
    AppendDebugLine(out_info, "GetISteamUserStats: all versions (013, 012, 011) gave sanity fail or null");
}

SteamAchievementCount GetSteamAchievementCountBlocking() {
    SteamAchievementCount result{};
    HMODULE steam = GetSteamModule();
    if (steam == nullptr) {
        return result;
    }

    // Path 1: SteamUserStats() export (normal steam_api).
    auto* steam_user_stats_fn = reinterpret_cast<SteamUserStatsFn>(GetProcAddress(steam, "SteamUserStats"));
    if (steam_user_stats_fn != nullptr) {
        void* p_user_stats = steam_user_stats_fn();
        if (GetAchievementCountFromUserStats(p_user_stats, result)) {
            return result;
        }
    }

    // Path 2: Only SteamClient() — get pipe, user, then GetISteamUserStats.
    auto* steam_client_fn = reinterpret_cast<SteamClientFn>(GetProcAddress(steam, "SteamClient"));
    if (steam_client_fn == nullptr) {
        return result;
    }
    void* p_client = steam_client_fn();
    if (p_client == nullptr) {
        return result;
    }
    void** client_vtable = *reinterpret_cast<void***>(p_client);
    if (client_vtable == nullptr || client_vtable[k_create_steam_pipe_index] == nullptr
        || client_vtable[k_connect_to_global_user_index] == nullptr
        || client_vtable[k_get_isteam_user_stats_index] == nullptr) {
        return result;
    }
    auto* create_pipe = reinterpret_cast<CreateSteamPipeFn>(client_vtable[k_create_steam_pipe_index]);
    auto* connect_user = reinterpret_cast<ConnectToGlobalUserFn>(client_vtable[k_connect_to_global_user_index]);
    auto* get_user_stats = reinterpret_cast<GetISteamUserStatsFn>(client_vtable[k_get_isteam_user_stats_index]);
    intptr_t h_pipe = create_pipe(p_client);
    if (h_pipe == 0) {
        return result;
    }
    intptr_t h_user = connect_user(p_client, h_pipe);
    if (h_user == 0) {
        return result;
    }
    for (const char* version : k_userstats_versions) {
        void* p_user_stats = get_user_stats(p_client, h_user, h_pipe, version);
        if (GetAchievementCountFromUserStats(p_user_stats, result)) {
            return result;
        }
    }
    return result;
}

int GetSteamAchievementListBlocking(SteamAchievementEntry* entries, size_t max_entries) {
    if (entries == nullptr || max_entries == 0) {
        return -1;
    }
    void* p_user_stats = nullptr;
    SteamAchievementCount count;
    if (!GetUserStatsPointer(p_user_stats, &count) || p_user_stats == nullptr || !count.available) {
        return -1;
    }
    const size_t limit = (max_entries < static_cast<size_t>(count.total)) ? max_entries
                                                                         : static_cast<size_t>(count.total);
    const int n = FillAchievementListFromUserStats(p_user_stats, entries, limit);
    if (n <= 0) {
        return n;
    }
    // Sort by last unlocked: highest unlock_time first, then locked (unlock_time 0) at the end.
    std::sort(entries, entries + n, [](const SteamAchievementEntry& a, const SteamAchievementEntry& b) {
        return a.unlock_time > b.unlock_time;
    });
    return n;
}

void GetSteamAchievementExportsDebugBlocking(char* buf, size_t buf_size) {
    if (buf == nullptr || buf_size == 0) {
        return;
    }
    buf[0] = '\0';
    HMODULE steam = GetSteamModule();
    if (steam == nullptr) {
        snprintf(buf, buf_size, "Steam module not loaded.");
        return;
    }
    const bool has_user_stats = (GetProcAddress(steam, "SteamUserStats") != nullptr);
    const bool has_steam_client = (GetProcAddress(steam, "SteamClient") != nullptr);
    const bool has_steam_user = (GetProcAddress(steam, "SteamUser") != nullptr);
    snprintf(buf, buf_size,
             "Exports for achievements: SteamUserStats=%s, SteamClient=%s, SteamUser=%s (Special K uses SteamUserStats).",
             has_user_stats ? "yes" : "no", has_steam_client ? "yes" : "no", has_steam_user ? "yes" : "no");
}

}  // namespace display_commander::utils
