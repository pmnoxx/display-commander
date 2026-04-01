
// Source Code <Display Commander> // follow this order for includes in all files
#include "default_overrides.hpp"
#include "display_commander_config.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>

#undef what

namespace display_commander::config {

namespace {

// exe_name_lower -> (section -> (key -> value))
using OverrideMap = std::map<std::string, std::map<std::string, std::map<std::string, std::string>>>;

const OverrideMap& GetOverrideMap() {
    static const OverrideMap overrides = {
        {"re2.exe",
         {{"DisplayCommander",
           {{"AutoColorspace", "1"}, {"ContinueRendering", "1"}, {"WindowMode", "1"}}}}},
        {"re3.exe",
         {{"DisplayCommander",
           {{"AutoColorspace", "1"}, {"ContinueRendering", "1"}, {"WindowMode", "1"}}}}},
        {"re7.exe",
         {{"DisplayCommander",
           {{"AutoColorspace", "1"}, {"ContinueRendering", "1"}, {"WindowMode", "1"}}}}},
        {"re8.exe",
         {{"DisplayCommander",
           {{"AutoColorspace", "1"}, {"ContinueRendering", "1"}, {"WindowMode", "1"}}}}},
        {"sekiro.exe",
         {{"DisplayCommander",
           {{"AutoColorspace", "1"}, {"ContinueRendering", "1"}, {"WindowMode", "1"}}}}},
        {"eldenring.exe",
         {{"DisplayCommander",
           {{"AutoColorspace", "1"}, {"ContinueRendering", "1"}, {"WindowMode", "1"}}}}},
        {"armoredcore6.exe",
         {{"DisplayCommander",
           {{"AutoColorspace", "1"}, {"ContinueRendering", "1"}, {"WindowMode", "1"}}}}},
        {"hitman3.exe",
         {{"DisplayCommander",
           {{"AutoColorspace", "1"}, {"ContinueRendering", "1"}, {"WindowMode", "1"}}}}},
        {"devilmaycry5.exe",
         {{"DisplayCommander",
           {{"AutoColorspace", "1"}, {"ContinueRendering", "1"}, {"WindowMode", "1"}}}}},
    };
    return overrides;
}

std::atomic<bool> g_exe_name_initialized{false};
std::string g_current_exe_lower;
SRWLOCK g_exe_name_srwlock = SRWLOCK_INIT;
// (section, key) pairs for which we returned an override during Load
std::set<std::pair<std::string, std::string>> g_active_overrides;
SRWLOCK g_srwlock = SRWLOCK_INIT;

// Key -> human-readable name for UI tooltip
const std::map<std::string, std::string>& GetKeyDisplayNames() {
    static const std::map<std::string, std::string> names = {
        {"ContinueRendering", "Continue Rendering in Background (Fake Fullscreen)"},
        {"PreventMinimize", "Prevent Minimize"},
        {"PreventAlwaysOnTop", "Prevent Always on Top"},
        {"HideHDRCapabilities", "Hide HDR Capabilities"},
        {"EnableFlipChain", "Enable Flip Chain"},
        {"ForceFlipDiscardUpgrade", "Force Flip Discard upgrade"},
        {"AutoColorspace", "Auto color space"},
        {"WindowMode", "Window Mode"},
    };
    return names;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void EnsureLoaded() {
    if (g_exe_name_initialized.load(std::memory_order_acquire)) return;

    utils::SRWLockExclusive lock(g_exe_name_srwlock);
    if (g_exe_name_initialized.load(std::memory_order_relaxed)) return;

    g_current_exe_lower = GetCurrentExeNameLower();

    // Log exe check and result only once per process to avoid log spam.
    const std::string& exe = g_current_exe_lower;
    LogInfo("Game default overrides: checking against exe %.260s", exe.empty() ? "(unknown)" : exe.c_str());

    const auto& override_map = GetOverrideMap();
    if (!exe.empty() && override_map.count(exe)) {
        LogInfo("Game default override found for %.260s", exe.c_str());
    } else {
        LogInfo("No game default override for %.260s", exe.empty() ? "(unknown)" : exe.c_str());
    }

    g_exe_name_initialized.store(true, std::memory_order_release);
}

}  // namespace

std::string GetCurrentExeNameLower() {
    char exe_path[MAX_PATH];
    DWORD path_length = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    if (path_length == 0) {
        GetCurrentDirectoryA(MAX_PATH, exe_path);
    }
    std::string path(exe_path);
    size_t slash = path.find_last_of("/\\");
    std::string filename = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    return ToLower(filename);
}

bool GetDefaultOverride(const char* section, const char* key, std::string& out_value) {
    EnsureLoaded();
    if (section == nullptr || key == nullptr) return false;
    if (g_current_exe_lower.empty()) return false;

    const auto& override_map = GetOverrideMap();
    auto it_exe = override_map.find(g_current_exe_lower);
    if (it_exe == override_map.end()) return false;
    auto it_sec = it_exe->second.find(section);
    if (it_sec == it_exe->second.end()) return false;
    auto it_key = it_sec->second.find(key);
    if (it_key == it_sec->second.end()) return false;
    out_value = it_key->second;
    return true;
}

void MarkUsedOverride(const char* section, const char* key) {
    if (section == nullptr || key == nullptr) return;
    utils::SRWLockExclusive lock(g_srwlock);
    g_active_overrides.insert({section, key});
}

bool HasActiveOverrides() {
    utils::SRWLockShared lock(g_srwlock);
    return !g_active_overrides.empty();
}

std::vector<DefaultOverrideEntry> GetActiveOverrideEntries() {
    EnsureLoaded();
    std::vector<DefaultOverrideEntry> result;
    const std::string exe = g_current_exe_lower;
    const auto& override_map = GetOverrideMap();
    auto it_exe = override_map.find(exe);
    if (it_exe == override_map.end()) return result;

    utils::SRWLockShared lock(g_srwlock);
    const auto& display_names = GetKeyDisplayNames();
    for (const auto& [section, key] : g_active_overrides) {
        auto it_sec = it_exe->second.find(section);
        if (it_sec == it_exe->second.end()) continue;
        auto it_key = it_sec->second.find(key);
        if (it_key == it_sec->second.end()) continue;
        DefaultOverrideEntry e;
        e.section = section;
        e.key = key;
        e.value = it_key->second;
        auto dn = display_names.find(key);
        e.display_name = (dn != display_names.end()) ? dn->second : key;
        result.push_back(std::move(e));
    }
    return result;
}

void ApplyDefaultOverridesToConfig() {
    std::vector<DefaultOverrideEntry> entries = GetActiveOverrideEntries();
    for (const auto& e : entries) {
        set_config_value(e.section.c_str(), e.key.c_str(), e.value);
    }
    if (!entries.empty()) {
        save_config("Apply default overrides to config");
        utils::SRWLockExclusive lock(g_srwlock);
        g_active_overrides.clear();
    }
}

}  // namespace display_commander::config
