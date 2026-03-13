#include "global_settings_file.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace display_commander::config {

namespace {

const char* const GLOBAL_KEYS[] = {
    "AutoEnableReshadeConfigBackup",
    "SuppressWgiGlobally",
};
constexpr size_t NUM_GLOBAL_KEYS = sizeof(GLOBAL_KEYS) / sizeof(GLOBAL_KEYS[0]);

std::map<std::string, std::string> g_global_cache;
bool g_global_loaded = false;

std::string NormalizeBoolValue(const std::string& value) {
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "true" || v == "1") return "1";
    if (v == "false" || v == "0") return "0";
    return value;
}

bool ParseTomlLine(const std::string& line, std::string& out_key, std::string& out_value) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    out_key = line.substr(0, eq);
    out_value = line.substr(eq + 1);
    out_key.erase(0, out_key.find_first_not_of(" \t"));
    out_key.erase(out_key.find_last_not_of(" \t") + 1);
    out_value.erase(0, out_value.find_first_not_of(" \t"));
    out_value.erase(out_value.find_last_not_of(" \t") + 1);
    if (out_value.size() >= 2
        && ((out_value.front() == '"' && out_value.back() == '"')
            || (out_value.front() == '\'' && out_value.back() == '\''))) {
        out_value = out_value.substr(1, out_value.size() - 2);
    }
    return !out_key.empty();
}

}  // namespace

std::string GetGlobalSettingsFilePath() {
    std::filesystem::path dir = GetDisplayCommanderAppDataFolder();
    if (dir.empty()) return {};
    return (dir / "global_settings.toml").string();
}

bool IsGlobalConfigKey(const char* key) {
    if (!key) return false;
    for (size_t i = 0; i < NUM_GLOBAL_KEYS; ++i) {
        if (strcmp(key, GLOBAL_KEYS[i]) == 0) return true;
    }
    return false;
}

bool LoadGlobalSettingsFile() {
    std::string path = GetGlobalSettingsFilePath();
    if (path.empty()) return false;

    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(dir, ec)) {
            LogError("Global settings file: failed to create directory %s: %s", dir.string().c_str(),
                     ec.message().c_str());
            return false;
        }
    }

    g_global_cache.clear();
    std::ifstream file(path);
    if (!file.is_open()) {
        g_global_loaded = true;
        return true;
    }

    std::string line;
    bool in_section = false;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            std::string sec = line.substr(1, line.size() - 2);
            in_section = (sec == "DisplayCommander");
            continue;
        }
        if (!in_section) continue;
        std::string k, v;
        if (ParseTomlLine(line, k, v)) {
            if (!IsGlobalConfigKey(k.c_str())) continue;
            if (k == "AutoEnableReshadeConfigBackup" || k == "SuppressWgiGlobally") v = NormalizeBoolValue(v);
            g_global_cache[k] = v;
        }
    }
    g_global_loaded = true;
    return true;
}

bool SaveGlobalSettingsFile() {
    std::string path = GetGlobalSettingsFilePath();
    if (path.empty()) return false;

    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(dir, ec)) {
            LogError("Global settings file: failed to create directory %s: %s", dir.string().c_str(),
                     ec.message().c_str());
            return false;
        }
    }

    std::string temp_path = path + ".temp";
    std::ofstream file(temp_path);
    if (!file.is_open()) {
        LogError("Global settings file: failed to open for write: %s", path.c_str());
        return false;
    }

    file << "[DisplayCommander]\n";
    for (const auto& kv : g_global_cache) {
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
        LogError("Global settings file: failed to rename temp to %s: %s", path.c_str(), e.what());
        std::filesystem::remove(temp_path);
        return false;
    }
}

bool GetGlobalSettingValue(const char* key, std::string& value) {
    if (!key) return false;
    if (!g_global_loaded && !LoadGlobalSettingsFile()) return false;
    auto it = g_global_cache.find(key);
    if (it == g_global_cache.end()) return false;
    value = it->second;
    return true;
}

void SetGlobalSettingValue(const char* key, const std::string& value) {
    if (!key) return;
    if (!IsGlobalConfigKey(key)) return;
    if (!g_global_loaded) LoadGlobalSettingsFile();
    g_global_cache[key] = value;
    SaveGlobalSettingsFile();
}

}  // namespace display_commander::config
