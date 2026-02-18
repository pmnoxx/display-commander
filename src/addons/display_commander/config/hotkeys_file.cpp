#include "hotkeys_file.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <toml++/toml.hpp>

#include <windows.h>

namespace display_commander::config {

namespace {

const char* const HOTKEY_KEYS[] = {
    "EnableHotkeys",
    "HotkeyMuteUnmute",
    "HotkeyBackgroundToggle",
    "HotkeyTimeslowdown",
    "HotkeyAdhdToggle",
    "HotkeyAutoclick",
    "HotkeyInputBlocking",
    "HotkeyDisplayCommanderUi",
    "HotkeyPerformanceOverlay",
    "HotkeyStopwatch",
    "HotkeyVolumeUp",
    "HotkeyVolumeDown",
    "HotkeySystemVolumeUp",
    "HotkeySystemVolumeDown",
    "ExclusiveKeysADEnabled",
    "ExclusiveKeysWSEnabled",
    "ExclusiveKeysAWSDEnabled",
    "ExclusiveKeysCustomGroups",
};
constexpr size_t NUM_HOTKEY_KEYS = sizeof(HOTKEY_KEYS) / sizeof(HOTKEY_KEYS[0]);

std::map<std::string, std::string> g_hotkeys_cache;
bool g_hotkeys_loaded = false;

// Normalize bool for storage: "true"/"false" -> "1"/"0" for consistency with INI/ReShade
std::string NormalizeBoolValue(const std::string& value) {
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "true" || v == "1") return "1";
    if (v == "false" || v == "0") return "0";
    return value;
}

// Parse a single TOML line: key = "value" or key = value (unquoted). Returns true if parsed.
bool ParseTomlLine(const std::string& line, std::string& out_key, std::string& out_value) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    out_key = line.substr(0, eq);
    out_value = line.substr(eq + 1);
    // Trim key
    out_key.erase(0, out_key.find_first_not_of(" \t"));
    out_key.erase(out_key.find_last_not_of(" \t") + 1);
    // Trim value
    out_value.erase(0, out_value.find_first_not_of(" \t"));
    out_value.erase(out_value.find_last_not_of(" \t") + 1);
    // Remove quotes from value
    if (out_value.size() >= 2 &&
        ((out_value.front() == '"' && out_value.back() == '"') || (out_value.front() == '\'' && out_value.back() == '\''))) {
        out_value = out_value.substr(1, out_value.size() - 2);
    }
    return !out_key.empty();
}

// Helper: migrate hotkey keys from a [DisplayCommander] key-value map into g_hotkeys_cache. Returns count migrated.
static int MigrateHotkeyKeysFromMap(const std::map<std::string, std::string>& kv_map) {
    int migrated = 0;
    for (const auto& [k, v] : kv_map) {
        if (!IsHotkeyConfigKey(k.c_str())) continue;
        std::string val = v;
        if (k == "EnableHotkeys" || k == "ExclusiveKeysADEnabled" || k == "ExclusiveKeysWSEnabled" ||
            k == "ExclusiveKeysAWSDEnabled") {
            val = NormalizeBoolValue(v);
        }
        g_hotkeys_cache[k] = val;
        ++migrated;
    }
    return migrated;
}

// One-time migration: when hotkeys.toml doesn't exist, copy hotkey keys from game's DisplayCommander.ini or .toml
void TryMigrateFromGameIni() {
    char exe_path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) == 0) return;
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    std::filesystem::path ini_path = exe_dir / "DisplayCommander.ini";
    std::filesystem::path toml_path = exe_dir / "DisplayCommander.toml";

    // Try .ini first (legacy)
    if (std::filesystem::exists(ini_path)) {
        std::ifstream file(ini_path);
        if (!file.is_open()) return;
        bool in_display_commander = false;
        std::string line;
        int migrated = 0;
        while (std::getline(file, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) line = line.substr(start);
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']') {
                std::string section = line.substr(1, line.size() - 2);
                in_display_commander = (section == "DisplayCommander");
                continue;
            }
            if (!in_display_commander) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            k.erase(0, k.find_first_not_of(" \t"));
            k.erase(k.find_last_not_of(" \t") + 1);
            v.erase(0, v.find_first_not_of(" \t"));
            v.erase(v.find_last_not_of(" \t") + 1);
            if (!IsHotkeyConfigKey(k.c_str())) continue;
            if (k == "EnableHotkeys" || k == "ExclusiveKeysADEnabled" || k == "ExclusiveKeysWSEnabled" ||
                k == "ExclusiveKeysAWSDEnabled") {
                v = NormalizeBoolValue(v);
            }
            g_hotkeys_cache[k] = v;
            ++migrated;
        }
        if (migrated > 0) {
            LogInfo("Hotkeys: migrated %d keys from %s to hotkeys.toml (shared)", migrated, ini_path.string().c_str());
            SaveHotkeysFile();
        }
        return;
    }

    // Try .toml (current format)
    if (std::filesystem::exists(toml_path)) {
        try {
            toml::table tbl = toml::parse_file(toml_path.string());
            auto* dc = tbl.get("DisplayCommander");
            if (dc && dc->is_table()) {
                std::map<std::string, std::string> kv_map;
                for (auto&& [k, v] : *dc->as_table()) {
                    std::string key = std::string(k.str());
                    if (!v.is_value()) continue;
                    std::string val;
                    if (v.is_string()) val = std::string(v.as_string()->get());
                    else if (v.is_integer()) val = std::to_string(v.as_integer()->get());
                    else if (v.is_boolean()) val = v.as_boolean()->get() ? "1" : "0";
                    else continue;
                    kv_map[key] = val;
                }
                int migrated = MigrateHotkeyKeysFromMap(kv_map);
                if (migrated > 0) {
                    LogInfo("Hotkeys: migrated %d keys from %s to hotkeys.toml (shared)", migrated, toml_path.string().c_str());
                    SaveHotkeysFile();
                }
            }
        } catch (const toml::parse_error&) {
            // Ignore parse errors
        }
    }
}

}  // namespace

std::string GetHotkeysFilePath() {
    std::filesystem::path dir = GetDisplayCommanderAppDataFolder();
    if (dir.empty()) return {};
    return (dir / "hotkeys.toml").string();
}

bool IsHotkeyConfigKey(const char* key) {
    if (!key) return false;
    for (size_t i = 0; i < NUM_HOTKEY_KEYS; ++i) {
        if (strcmp(key, HOTKEY_KEYS[i]) == 0) return true;
    }
    return false;
}

bool LoadHotkeysFile() {
    std::string path = GetHotkeysFilePath();
    if (path.empty()) return false;

    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(dir, ec)) {
            LogError("Hotkeys file: failed to create directory %s: %s", dir.string().c_str(), ec.message().c_str());
            return false;
        }
    }

    g_hotkeys_cache.clear();
    std::ifstream file(path);
    if (!file.is_open()) {
        g_hotkeys_loaded = true;
        TryMigrateFromGameIni();  // One-time migration from game's DisplayCommander.ini if it exists
        return true;  // No file yet, use defaults (or migrated cache)
    }

    std::string line;
    bool in_hotkeys = false;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            std::string section = line.substr(1, line.size() - 2);
            in_hotkeys = (section == "hotkeys");
            continue;
        }
        if (!in_hotkeys) continue;
        std::string k, v;
        if (ParseTomlLine(line, k, v)) {
            if (k == "EnableHotkeys" || k == "ExclusiveKeysADEnabled" || k == "ExclusiveKeysWSEnabled" ||
                k == "ExclusiveKeysAWSDEnabled") {
                v = NormalizeBoolValue(v);
            }
            g_hotkeys_cache[k] = v;
        }
    }
    g_hotkeys_loaded = true;
    return true;
}

bool SaveHotkeysFile() {
    std::string path = GetHotkeysFilePath();
    if (path.empty()) return false;

    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(dir, ec)) {
            LogError("Hotkeys file: failed to create directory %s: %s", dir.string().c_str(), ec.message().c_str());
            return false;
        }
    }

    std::string temp_path = path + ".temp";
    std::ofstream file(temp_path);
    if (!file.is_open()) {
        LogError("Hotkeys file: failed to open for write: %s", path.c_str());
        return false;
    }

    file << "[hotkeys]\n";
    for (const auto& kv : g_hotkeys_cache) {
        const std::string& v = kv.second;
        bool is_bool = (v == "0" || v == "1");
        if (is_bool) {
            file << kv.first << " = " << (v == "1" ? "true" : "false") << "\n";
        } else {
            file << kv.first << " = \"" << v << "\"\n";
        }
    }
    file.close();

    try {
        std::filesystem::rename(temp_path, path);
        return true;
    } catch (const std::exception& e) {
        LogError("Hotkeys file: failed to rename temp to %s: %s", path.c_str(), e.what());
        std::filesystem::remove(temp_path);
        return false;
    }
}

bool GetHotkeyValue(const char* key, std::string& value) {
    if (!key) return false;
    if (!g_hotkeys_loaded && !LoadHotkeysFile()) return false;
    auto it = g_hotkeys_cache.find(key);
    if (it == g_hotkeys_cache.end()) return false;
    value = it->second;
    return true;
}

void SetHotkeyValue(const char* key, const std::string& value) {
    if (!key) return;
    if (!g_hotkeys_loaded) LoadHotkeysFile();
    g_hotkeys_cache[key] = value;
    SaveHotkeysFile();
}

}  // namespace display_commander::config
