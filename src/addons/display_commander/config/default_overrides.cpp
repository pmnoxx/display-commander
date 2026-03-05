// Source Code <Display Commander> // follow this order for includes in all files
#include "default_overrides.hpp"
#include "display_commander_config.hpp"
#include "../globals.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <Windows.h>

namespace display_commander::config {

namespace {

constexpr int IDR_GAME_DEFAULT_OVERRIDES = 304;

// exe_name_lower -> (section -> (key -> value))
using OverrideMap = std::map<std::string, std::map<std::string, std::map<std::string, std::string>>>;

OverrideMap g_override_map;
bool g_loaded = false;
std::string g_current_exe_lower;
// (section, key) pairs for which we returned an override during Load
std::set<std::pair<std::string, std::string>> g_active_overrides;
SRWLOCK g_srwlock = SRWLOCK_INIT;

// Key -> human-readable name for UI tooltip
static const std::map<std::string, std::string>& GetKeyDisplayNames() {
    static const std::map<std::string, std::string> names = {
        {"ContinueRendering", "Continue Rendering in Background"},
        {"PreventMinimize", "Prevent Minimize"},
        {"PreventAlwaysOnTop", "Prevent Always on Top"},
        {"HideHDRCapabilities", "Hide HDR Capabilities"},
        {"EnableFlipChain", "Enable Flip Chain"},
        {"AutoColorspace", "Auto color space"},
        {"window_mode", "Window Mode"},
    };
    return names;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "hitman3.exe.DisplayCommander" -> ("hitman3.exe", "DisplayCommander"). Section is after last dot.
std::pair<std::string, std::string> SplitTableName(const std::string& table_name) {
    size_t last_dot = table_name.rfind('.');
    if (last_dot == std::string::npos) {
        return {ToLower(table_name), ""};
    }
    std::string exe_part = table_name.substr(0, last_dot);
    std::string section_part = table_name.substr(last_dot + 1);
    return {ToLower(exe_part), section_part};
}

static std::string NodeToString(const toml::node& node) {
    if (node.is_string()) return std::string(node.as_string()->get());
    if (node.is_integer()) return std::to_string(node.as_integer()->get());
    if (node.is_floating_point()) return std::to_string(node.as_floating_point()->get());
    if (node.is_boolean()) return node.as_boolean()->get() ? "1" : "0";
    return "";
}

// TOML parses [hitman3.exe.DisplayCommander] as nested tables: tbl["hitman3"]["exe"]["DisplayCommander"].
// Recursively find leaf tables (tables that contain at least one non-table value) and add them by
// joining path to "exe.section" and splitting on last dot.
static void CollectLeafTables(const toml::table& tbl, std::vector<std::string>& path, OverrideMap& out_map) {
    bool has_non_table = false;
    for (auto&& [k, v] : tbl) {
        if (!v.is_table()) {
            has_non_table = true;
            break;
        }
    }
    if (has_non_table) {
        std::string table_name;
        for (size_t i = 0; i < path.size(); ++i) {
            if (i > 0) table_name += '.';
            table_name += path[i];
        }
        auto [exe_lower, section] = SplitTableName(table_name);
        if (section.empty()) return;
        auto& sec_map = out_map[exe_lower][section];
        for (auto&& [k2, v2] : tbl) {
            std::string key(k2.str());
            std::string value = NodeToString(v2);
            if (!value.empty()) sec_map[key] = value;
        }
        return;
    }
    for (auto&& [k, v] : tbl) {
        if (v.is_table()) {
            path.push_back(std::string(k.str()));
            CollectLeafTables(*v.as_table(), path, out_map);
            path.pop_back();
        }
    }
}

void LoadFromResource() {
    if (g_hmodule == nullptr) {
        LogWarn("Game default overrides: g_hmodule is null, cannot load embedded resource");
        return;
    }
    HRSRC hRes = FindResourceA(g_hmodule, MAKEINTRESOURCEA(IDR_GAME_DEFAULT_OVERRIDES), RT_RCDATA);
    if (hRes == nullptr) {
        LogWarn("Game default overrides: resource %d not found (rebuild addon so game_default_overrides.toml is embedded)", IDR_GAME_DEFAULT_OVERRIDES);
        return;
    }
    HGLOBAL hLoaded = LoadResource(g_hmodule, hRes);
    if (hLoaded == nullptr) {
        LogWarn("Game default overrides: LoadResource failed for resource %d", IDR_GAME_DEFAULT_OVERRIDES);
        return;
    }
    const void* pData = LockResource(hLoaded);
    const DWORD size = SizeofResource(g_hmodule, hRes);
    if (pData == nullptr || size == 0) {
        LogWarn("Game default overrides: LockResource/size failed for resource %d", IDR_GAME_DEFAULT_OVERRIDES);
        return;
    }

    std::string content(static_cast<const char*>(pData), size);
    try {
        toml::table tbl = toml::parse(content);
        std::vector<std::string> path;
        CollectLeafTables(tbl, path, g_override_map);
        g_loaded = true;
        LogInfo("Game default overrides: loaded from resource (%zu exe entries)", g_override_map.size());
    } catch (const toml::parse_error& e) {
        LogWarn("Game default overrides: parse error %s", e.what());
        return;
    }
}

void EnsureLoaded() {
    if (g_loaded) return;
    LoadFromResource();
    g_current_exe_lower = GetCurrentExeNameLower();
    // Bounded format to avoid log garbage if exe string were ever non-null-terminated
    const std::string& exe = g_current_exe_lower;
    LogInfo("Game default overrides: checking against exe %.260s", exe.empty() ? "(unknown)" : exe.c_str());
    if (g_loaded && !g_override_map.empty()) {
        if (g_override_map.count(g_current_exe_lower)) {
            LogInfo("Game default override found for %.260s", exe.c_str());
        } else {
            LogInfo("No game default override for %.260s", exe.c_str());
        }
    } else if (!g_loaded) {
        LogInfo("No game default override for %.260s (resource not loaded)", exe.c_str());
    }
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
    if (!g_loaded || section == nullptr || key == nullptr) return false;
    auto it_exe = g_override_map.find(g_current_exe_lower);
    if (it_exe == g_override_map.end()) return false;
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
    auto it_exe = g_override_map.find(exe);
    if (it_exe == g_override_map.end()) return result;

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
