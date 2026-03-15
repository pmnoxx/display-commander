// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "default_settings_file.hpp"
#include "toml_line_parser.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace display_commander::config {

namespace {

std::map<std::string, std::string> g_default_settings_cache;
bool g_default_settings_loaded = false;

const char* const DEFAULT_SETTINGS_TEMPLATE = R"(# Display Commander — User default settings
# Location: %LocalAppData%\Programs\Display_Commander\default_settings.toml
#
# This file is optional. When a game's DisplayCommander.toml does not contain a setting,
# Display Commander can use the value from here as the default for that game.
# Edit this file to set your preferred defaults for all games (they apply only when
# the game config does not already have the key).
#
# Format: use the [DisplayCommander] section and keys that match Display Commander config.
# Example (uncomment and edit as needed):
#
# [DisplayCommander]
# ContinueRendering = true
# window_mode = 1
)";

bool EnsureDefaultSettingsFileExists(const std::string& path) {
    if (path.empty()) return false;
    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(dir, ec)) {
            LogError("Default settings file: failed to create directory %s: %s", dir.string().c_str(),
                     ec.message().c_str());
            return false;
        }
    }
    if (std::filesystem::exists(path)) return true;
    std::ofstream file(path);
    if (!file.is_open()) {
        LogError("Default settings file: failed to create %s", path.c_str());
        return false;
    }
    file << DEFAULT_SETTINGS_TEMPLATE;
    file.close();
    LogInfo("Default settings file: created template at %s", path.c_str());
    return true;
}

}  // namespace

std::string GetDefaultSettingsFilePath() {
    std::filesystem::path dir = GetDisplayCommanderAppDataFolder();
    if (dir.empty()) return {};
    return (dir / "default_settings.toml").string();
}

bool LoadDefaultSettingsFile() {
    std::string path = GetDefaultSettingsFilePath();
    if (path.empty()) return false;

    if (!EnsureDefaultSettingsFileExists(path)) return false;

    g_default_settings_cache.clear();
    std::ifstream file(path);
    if (!file.is_open()) {
        g_default_settings_loaded = true;
        return true;
    }

    std::string line;
    bool in_display_commander = false;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            std::string sec = line.substr(1, line.size() - 2);
            in_display_commander = (sec == "DisplayCommander");
            continue;
        }
        if (!in_display_commander) continue;
        std::string k;
        std::string v;
        if (ParseTomlLine(line, k, v)) {
            g_default_settings_cache[k] = v;
        }
    }
    g_default_settings_loaded = true;
    return true;
}

bool GetDefaultSettingValue(const char* section, const char* key, std::string& value) {
    if (section == nullptr || key == nullptr) return false;
    if (strcmp(section, "DisplayCommander") != 0) return false;
    if (!g_default_settings_loaded && !LoadDefaultSettingsFile()) return false;
    auto it = g_default_settings_cache.find(key);
    if (it == g_default_settings_cache.end()) return false;
    value = it->second;
    return true;
}

}  // namespace display_commander::config
