// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "global_overrides_file.hpp"
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

std::map<std::string, std::string> g_global_overrides_cache;
bool g_global_overrides_loaded = false;

const char* const GLOBAL_OVERRIDES_TEMPLATE = R"(# Display Commander — Global overrides
# Location: %LocalAppData%\Programs\Display_Commander\global_overrides.toml
#
# Same format as default_settings.toml, but values here override the game's DisplayCommander.ini
# even when the game config already has the key. Use keys that match what they override.
#
# [DisplayCommander]
# SuppressWgiEnabled = false
)";

bool EnsureGlobalOverridesFileExists(const std::string& path) {
    if (path.empty()) return false;
    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(dir, ec)) {
            LogError("Global overrides file: failed to create directory %s: %s", dir.string().c_str(),
                     ec.message().c_str());
            return false;
        }
    }
    if (std::filesystem::exists(path)) return true;
    std::ofstream file(path);
    if (!file.is_open()) {
        LogError("Global overrides file: failed to create %s", path.c_str());
        return false;
    }
    file << GLOBAL_OVERRIDES_TEMPLATE;
    file.close();
    LogInfo("Global overrides file: created template at %s", path.c_str());
    return true;
}

}  // namespace

std::string GetGlobalOverridesFilePath() {
    std::filesystem::path dir = GetDisplayCommanderAppDataFolder();
    if (dir.empty()) return {};
    return (dir / "global_overrides.toml").string();
}

bool LoadGlobalOverridesFile() {
    std::string path = GetGlobalOverridesFilePath();
    if (path.empty()) return false;

    if (!EnsureGlobalOverridesFileExists(path)) return false;


    g_global_overrides_cache.clear();
    std::ifstream file(path);
    if (!file.is_open()) {
        g_global_overrides_loaded = true;
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
            g_global_overrides_cache[k] = v;
        }
    }
    g_global_overrides_loaded = true;
    return true;
}

bool SaveGlobalOverridesFile() {
    std::string path = GetGlobalOverridesFilePath();
    if (path.empty()) return false;
    if (!EnsureGlobalOverridesFileExists(path)) return false;

    std::ofstream file(path);
    if (!file.is_open()) {
        LogError("Global overrides file: failed to save %s", path.c_str());
        return false;
    }
    file << "# Display Commander — Global overrides\n";
    file << "# Keys match Display Commander config; values override game config.\n\n";
    file << "[DisplayCommander]\n";
    for (const auto& kv : g_global_overrides_cache) {
        file << kv.first << " = " << kv.second << "\n";
    }
    return true;
}

bool GetGlobalOverrideValue(const char* key, std::string& value) {
    if (key == nullptr) return false;
    if (!g_global_overrides_loaded && !LoadGlobalOverridesFile()) return false;
    auto it = g_global_overrides_cache.find(key);
    if (it == g_global_overrides_cache.end()) return false;
    value = it->second;
    return true;
}

void SetGlobalOverrideValue(const char* key, const std::string& value) {
    if (key == nullptr) return;
    if (!g_global_overrides_loaded) (void)LoadGlobalOverridesFile();
    g_global_overrides_cache[key] = value;
    (void)SaveGlobalOverridesFile();
}

}  // namespace display_commander::config
