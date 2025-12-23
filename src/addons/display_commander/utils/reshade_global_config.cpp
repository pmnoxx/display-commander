#include "reshade_global_config.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <reshade.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace utils {

namespace {

// Define the additional settings to track (section -> keys)
// These are organized as pairs of (section, key) that we want to save/load
const std::map<std::string, std::vector<std::string>> TRACKED_SETTINGS = {
    {"INPUT", {
        "KeyEffects",
        "KeyFPS",
        "KeyFrametime",
        "KeyNextPreset",
        "KeyOverlay",
        "KeyPerformanceMode",
        "KeyPreviousPreset",
        "KeyReload",
        "KeyScreenshot"
    }},
    {"GENERAL", {
        "EffectSearchPaths",
        "TextureSearchPaths",
        "NoEffectCache",
        "NoReloadOnInit",
        "PerformanceMode",
        "NoDebugInfo",
        "LoadFromDllMain"
    }},
    {"OVERLAY", {
        "ClockFormat",
        "ShowClock",
        "ShowFrameTime",
        "ShowFPS",
        "ShowForceLoadEffectsButton",
        "FPSPosition"
    }}
};

// Helper function to parse INI line
bool ParseIniLine(const std::string& line, std::string& key, std::string& value) {
    // Trim whitespace
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return false;

    size_t end = line.find_last_not_of(" \t\r\n");
    std::string trimmed = line.substr(start, end - start + 1);

    // Skip comments
    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
        return false;
    }

    // Find '=' separator
    size_t eq_pos = trimmed.find('=');
    if (eq_pos == std::string::npos) return false;

    key = trimmed.substr(0, eq_pos);
    value = trimmed.substr(eq_pos + 1);

    // Trim key and value
    key.erase(key.find_last_not_of(" \t") + 1);
    key.erase(0, key.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));

    return !key.empty();
}

// Helper function to split comma-separated paths
std::vector<std::string> SplitPaths(const std::string& paths_str) {
    std::vector<std::string> paths;
    std::stringstream ss(paths_str);
    std::string path;

    while (std::getline(ss, path, ',')) {
        // Trim whitespace
        path.erase(0, path.find_first_not_of(" \t"));
        path.erase(path.find_last_not_of(" \t") + 1);

        if (!path.empty()) {
            paths.push_back(path);
        }
    }

    return paths;
}

// Helper function to join paths with comma
std::string JoinPaths(const std::vector<std::string>& paths) {
    std::string result;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) result += ",";
        result += paths[i];
    }
    return result;
}

} // anonymous namespace

std::filesystem::path GetDisplayCommanderConfigPath() {
    // Get user's home directory using Windows API
    wchar_t* user_profile = nullptr;
    size_t len = 0;

    // Try to get USERPROFILE environment variable
    if (_wdupenv_s(&user_profile, &len, L"USERPROFILE") == 0 && user_profile != nullptr) {
        std::filesystem::path config_path = std::filesystem::path(user_profile) / L"DisplayCommander.ini";
        free(user_profile);
        return config_path;
    }

    // Fallback: use SHGetFolderPath
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, path))) {
        return std::filesystem::path(path) / L"DisplayCommander.ini";
    }

    // Last resort: return empty path
    return std::filesystem::path();
}

bool ReadCurrentReShadeSettings(ReShadeGlobalSettings& settings) {
    settings.additional_settings.clear();

    char buffer[4096] = {0};
    size_t buffer_size = sizeof(buffer);

    // Read all tracked settings
    for (const auto& [section, keys] : TRACKED_SETTINGS) {
        for (const auto& key : keys) {
            buffer_size = sizeof(buffer);
            memset(buffer, 0, sizeof(buffer));

            if (reshade::get_config_value(nullptr, section.c_str(), key.c_str(), buffer, &buffer_size)) {
                // Special handling for EffectSearchPaths and TextureSearchPaths
                // These are stored as null-terminated string arrays in ReShade
                if (key == "EffectSearchPaths" || key == "TextureSearchPaths") {
                    std::vector<std::string> paths;
                    const char* ptr = buffer;
                    while (*ptr != '\0' && ptr < buffer + buffer_size) {
                        std::string path(ptr);
                        if (!path.empty()) {
                            paths.push_back(path);
                        }
                        ptr += path.length() + 1;
                    }
                    // Convert to comma-separated string
                    settings.additional_settings[section][key] = JoinPaths(paths);
                } else {
                    // Regular string value
                    settings.additional_settings[section][key] = std::string(buffer);
                }
            }
        }
    }

    return !settings.additional_settings.empty();
}

bool WriteCurrentReShadeSettings(const ReShadeGlobalSettings& settings) {
    // Write all settings
    for (const auto& [section, keys_values] : settings.additional_settings) {
        for (const auto& [key, value] : keys_values) {
            // Special handling for EffectSearchPaths and TextureSearchPaths
            // ReShade expects null-terminated string arrays for these
            if (key == "EffectSearchPaths" || key == "TextureSearchPaths") {
                std::vector<std::string> paths = SplitPaths(value);
                std::string combined;
                for (const auto& path : paths) {
                    combined += path;
                    combined += '\0';
                }
                reshade::set_config_value(nullptr, section.c_str(), key.c_str(), combined.c_str());
            } else {
                // Regular string value
                reshade::set_config_value(nullptr, section.c_str(), key.c_str(), value.c_str());
            }
        }
    }

    return true;
}

bool LoadGlobalSettings(ReShadeGlobalSettings& settings) {
    settings.additional_settings.clear();

    std::filesystem::path config_path = GetDisplayCommanderConfigPath();
    if (config_path.empty() || !std::filesystem::exists(config_path)) {
        LogInfo("DisplayCommander.ini not found at: %ls", config_path.c_str());
        return false;
    }

    std::ifstream file(config_path);
    if (!file.is_open()) {
        LogInfo("Failed to open DisplayCommander.ini at: %ls", config_path.c_str());
        return false;
    }

    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        // Check for section header
        if (!line.empty() && line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos) {
                current_section = line.substr(1, end - 1);
            }
            continue;
        }

        if (current_section.empty()) continue;

        std::string key;
        std::string value;
        if (ParseIniLine(line, key, value)) {
            // Check if this is a tracked setting
            auto section_it = TRACKED_SETTINGS.find(current_section);
            if (section_it != TRACKED_SETTINGS.end()) {
                const auto& tracked_keys = section_it->second;
                if (std::find(tracked_keys.begin(), tracked_keys.end(), key) != tracked_keys.end()) {
                    settings.additional_settings[current_section][key] = value;
                }
            }
        }
    }

    file.close();

    LogInfo("Loaded global settings from: %ls", config_path.c_str());

    size_t total_settings = 0;
    for (const auto& [section, keys_values] : settings.additional_settings) {
        total_settings += keys_values.size();
    }
    LogInfo("  Loaded %zu settings across %zu sections",
            total_settings, settings.additional_settings.size());

    return true;
}

bool SaveGlobalSettings(const ReShadeGlobalSettings& settings) {
    std::filesystem::path config_path = GetDisplayCommanderConfigPath();
    if (config_path.empty()) {
        LogInfo("Failed to get DisplayCommander.ini path");
        return false;
    }

    // Write to temp file first for atomic operation
    std::filesystem::path temp_path = config_path;
    temp_path += L".temp";

    std::ofstream file(temp_path);
    if (!file.is_open()) {
        LogInfo("Failed to create/open DisplayCommander.ini.temp at: %ls", temp_path.c_str());
        return false;
    }

    // Write sections in a specific order: INPUT, GENERAL, OVERLAY
    const std::vector<std::string> section_order = {"INPUT", "GENERAL", "OVERLAY"};

    for (const auto& section_name : section_order) {
        auto section_it = settings.additional_settings.find(section_name);
        if (section_it != settings.additional_settings.end() && !section_it->second.empty()) {
            file << "[" << section_name << "]\n";
            for (const auto& [key, value] : section_it->second) {
                file << key << "=" << value << "\n";
            }
            file << "\n";
        }
    }

    file.close();

    // Atomically move temp file to final location
    try {
        std::filesystem::rename(temp_path, config_path);
        LogInfo("Saved global settings to: %ls", config_path.c_str());

        size_t total_settings = 0;
        for (const auto& [section, keys_values] : settings.additional_settings) {
            total_settings += keys_values.size();
        }
        LogInfo("  Saved %zu settings across %zu sections",
                total_settings, settings.additional_settings.size());

        return true;
    } catch (const std::exception& e) {
        // Clean up temp file on failure
        std::filesystem::remove(temp_path);
        LogInfo("Failed to rename temp file to DisplayCommander.ini: %s", e.what());
        return false;
    }
}

bool SetLoadFromDllMain(bool enabled) {
    const char* value = enabled ? "1" : "0";

    try {
        //reshade::set_config_value(nullptr, "ADDON", "LoadFromDllMain", value);
        LogInfo("Set LoadFromDllMain to %s in ReShade configuration", value);
        return true;
    } catch (...) {
        LogInfo("Failed to set LoadFromDllMain to %s in ReShade configuration", value);
        return false;
    }
}

} // namespace utils

