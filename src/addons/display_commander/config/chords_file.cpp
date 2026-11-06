
#include "chords_file.hpp"
#include "toml_line_parser.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

#include <string_view>
#include <utility>

#include <windows.h>

namespace display_commander::config {

namespace {

// Chord config keys stored as "Section.Key" for lookup (same as used in config API).
const char* const CHORD_KEYS[] = {
    "DisplayCommander.DefaultChordsEnabled",        "DisplayCommander.GuideButtonSoloUiToggleOnly",
    "DisplayCommander.InputRemapping.Enabled",       "DisplayCommander.InputRemapping.BlockInputOnHomeButton",
    "DisplayCommander.InputRemapping.DefaultMethod",
};
constexpr size_t NUM_CHORD_KEYS = sizeof(CHORD_KEYS) / sizeof(CHORD_KEYS[0]);

// TODO(remove after 2027-02-06): snake_case -> PascalCase key migration (v1.0.6), see display_commander_config.cpp.
void RenameLegacyChordKey(std::string& key) {
    constexpr std::string_view kOldDefault = "enable_default_chords";
    constexpr std::string_view kOldGuide = "guide_button_solo_ui_toggle_only";
    for (const auto& [old_name, new_name] : {std::pair<std::string_view, std::string_view>{kOldDefault, "DefaultChordsEnabled"},
                                             {kOldGuide, "GuideButtonSoloUiToggleOnly"}}) {
        if (key == old_name) {
            key = new_name;
        } else if (key.size() > old_name.size() && key.compare(key.size() - old_name.size(), old_name.size(), old_name) == 0
                   && key[key.size() - old_name.size() - 1] == '.') {
            key.replace(key.size() - old_name.size(), old_name.size(), new_name);
        }
    }
}

std::map<std::string, std::string> g_chords_cache;
bool g_chords_loaded = false;

std::string MakeCompositeKey(const char* section, const char* key) {
    if (!section || !key) return {};
    std::string s = section;
    if (s.empty()) return {};
    return s + "." + key;
}

// Normalize bool for storage: "true"/"false" -> "1"/"0"
std::string NormalizeBoolValue(const std::string& value) {
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (v == "true" || v == "1") return "1";
    if (v == "false" || v == "0") return "0";
    return value;
}

// One-time migration: when chords.toml doesn't exist, copy chord keys from game's DisplayCommander.ini or .toml
void TryMigrateFromGameConfig() {
    char exe_path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) == 0) return;
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    std::filesystem::path ini_path = exe_dir / "DisplayCommander.ini";
    std::filesystem::path toml_path = exe_dir / "DisplayCommander.toml";

    // Try .ini first (legacy) - only [DisplayCommander] keys
    if (std::filesystem::exists(ini_path)) {
        std::ifstream file(ini_path);
        if (!file.is_open()) return;
        bool in_display_commander = false;
        bool in_input_remapping = false;
        std::string line;
        int migrated = 0;
        while (std::getline(file, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) line = line.substr(start);
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']') {
                const std::string section(line.begin() + 1, line.end() - 1);
                in_display_commander = (section == "DisplayCommander");
                in_input_remapping = (section == "DisplayCommander.InputRemapping");
                continue;
            }
            if (!in_display_commander && !in_input_remapping) continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            k.erase(0, k.find_first_not_of(" \t"));
            k.erase(k.find_last_not_of(" \t") + 1);
            v.erase(0, v.find_first_not_of(" \t"));
            v.erase(v.find_last_not_of(" \t") + 1);

            RenameLegacyChordKey(k);
            if (in_display_commander) {
                if (!IsChordConfigKey("DisplayCommander", k.c_str())) continue;
                if (k == "DefaultChordsEnabled" || k == "GuideButtonSoloUiToggleOnly") {
                    v = NormalizeBoolValue(v);
                }
                std::string composite = "DisplayCommander." + k;
                g_chords_cache[composite] = v;
                ++migrated;
            } else if (in_input_remapping) {
                if (!IsChordConfigKey("DisplayCommander.InputRemapping", k.c_str())) continue;
                if (k == "Enabled" || k == "BlockInputOnHomeButton") {
                    v = NormalizeBoolValue(v);
                }
                std::string composite = "DisplayCommander.InputRemapping." + k;
                g_chords_cache[composite] = v;
                ++migrated;
            }
        }
        if (migrated > 0) {
            LogInfo("Chords: migrated %d keys from %s to chords.toml (shared)", migrated, ini_path.string().c_str());
            SaveChordsFile();
        }
        return;
    }

    // Try .toml (old/current formats were simple [section] + key = "value" lines).
    if (!std::filesystem::exists(toml_path)) return;

    std::ifstream file(toml_path);
    if (!file.is_open()) return;

    bool in_display_commander = false;
    bool in_input_remapping = false;
    std::string line;
    int migrated = 0;

    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) line = line.substr(start);
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            const std::string section(line.begin() + 1, line.end() - 1);
            in_display_commander = (section == "DisplayCommander");
            in_input_remapping = (section == "DisplayCommander.InputRemapping");
            continue;
        }

        if (!in_display_commander && !in_input_remapping) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        k.erase(0, k.find_first_not_of(" \t"));
        k.erase(k.find_last_not_of(" \t") + 1);
        v.erase(0, v.find_first_not_of(" \t"));
        v.erase(v.find_last_not_of(" \t") + 1);

        if (v.size() >= 2
            && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
            v = v.substr(1, v.size() - 2);
        }

        RenameLegacyChordKey(k);
        if (in_display_commander) {
            if (!IsChordConfigKey("DisplayCommander", k.c_str())) continue;
            if (k == "DefaultChordsEnabled" || k == "GuideButtonSoloUiToggleOnly") {
                v = NormalizeBoolValue(v);
            }
            std::string composite = "DisplayCommander." + k;
            g_chords_cache[composite] = v;
            ++migrated;
        } else if (in_input_remapping) {
            if (!IsChordConfigKey("DisplayCommander.InputRemapping", k.c_str())) continue;
            if (k == "Enabled" || k == "BlockInputOnHomeButton") {
                v = NormalizeBoolValue(v);
            }
            std::string composite = "DisplayCommander.InputRemapping." + k;
            g_chords_cache[composite] = v;
            ++migrated;
        }
    }

    if (migrated > 0) {
        LogInfo("Chords: migrated %d keys from %s to chords.toml (shared)", migrated,
                toml_path.string().c_str());
        SaveChordsFile();
    }
}

}  // namespace

std::string GetChordsFilePath() {
    std::filesystem::path dir = GetDisplayCommanderAppDataFolder();
    if (dir.empty()) return {};
    return (dir / "chords.toml").string();
}

bool IsChordConfigKey(const char* section, const char* key) {
    if (!section || !key) return false;
    std::string composite = MakeCompositeKey(section, key);
    if (composite.empty()) return false;
    for (size_t i = 0; i < NUM_CHORD_KEYS; ++i) {
        if (composite == CHORD_KEYS[i]) return true;
    }
    return false;
}

bool LoadChordsFile() {
    std::string path = GetChordsFilePath();
    if (path.empty()) return false;

    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(dir, ec)) {
            LogError("Chords file: failed to create directory %s: %s", dir.string().c_str(), ec.message().c_str());
            return false;
        }
    }

    g_chords_cache.clear();
    std::ifstream file(path);
    if (!file.is_open()) {
        g_chords_loaded = true;
        TryMigrateFromGameConfig();
        return true;
    }

    std::string line;
    bool in_chords = false;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            std::string sec = line.substr(1, line.size() - 2);
            in_chords = (sec == "chords");
            continue;
        }
        if (!in_chords) continue;
        std::string k, v;
        if (ParseTomlLine(line, k, v)) {
            RenameLegacyChordKey(k);
            if (k.find("DefaultChordsEnabled") != std::string::npos
                || k.find("GuideButtonSoloUiToggleOnly") != std::string::npos
                || k.find("Enabled") != std::string::npos || k.find("BlockInputOnHomeButton") != std::string::npos) {
                v = NormalizeBoolValue(v);
            }
            g_chords_cache[k] = v;
        }
    }
    g_chords_loaded = true;
    return true;
}

bool SaveChordsFile() {
    std::string path = GetChordsFilePath();
    if (path.empty()) return false;

    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(dir, ec)) {
            LogError("Chords file: failed to create directory %s: %s", dir.string().c_str(), ec.message().c_str());
            return false;
        }
    }

    std::string temp_path = path + ".temp";
    std::ofstream file(temp_path);
    if (!file.is_open()) {
        LogError("Chords file: failed to open for write: %s", path.c_str());
        return false;
    }

    file << "[chords]\n";
    for (const auto& kv : g_chords_cache) {
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
        LogError("Chords file: failed to rename temp to %s: %s", path.c_str(), e.what());
        std::filesystem::remove(temp_path);
        return false;
    }
}

bool GetChordValue(const char* section, const char* key, std::string& value) {
    if (!section || !key) return false;
    std::string composite = MakeCompositeKey(section, key);
    if (composite.empty() || !IsChordConfigKey(section, key)) return false;
    if (!g_chords_loaded && !LoadChordsFile()) return false;
    auto it = g_chords_cache.find(composite);
    if (it == g_chords_cache.end()) return false;
    value = it->second;
    return true;
}

void SetChordValue(const char* section, const char* key, const std::string& value) {
    if (!section || !key || !IsChordConfigKey(section, key)) return;
    std::string composite = MakeCompositeKey(section, key);
    if (composite.empty()) return;
    if (!g_chords_loaded) LoadChordsFile();
    g_chords_cache[composite] = value;
    SaveChordsFile();
}

}  // namespace display_commander::config
