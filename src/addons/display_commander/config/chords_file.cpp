#include "chords_file.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <toml++/toml.hpp>

#include <windows.h>

namespace display_commander::config {

namespace {

// Chord config keys stored as "Section.Key" for lookup (same as used in config API).
const char* const CHORD_KEYS[] = {
    "DisplayCommander.enable_default_chords",        "DisplayCommander.guide_button_solo_ui_toggle_only",
    "DisplayCommander.InputRemapping.Enabled",       "DisplayCommander.InputRemapping.BlockInputOnHomeButton",
    "DisplayCommander.InputRemapping.DefaultMethod",
};
constexpr size_t NUM_CHORD_KEYS = sizeof(CHORD_KEYS) / sizeof(CHORD_KEYS[0]);

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
        std::string line;
        int migrated = 0;
        while (std::getline(file, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) line = line.substr(start);
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line.front() == '[' && line.back() == ']') {
                in_display_commander = (std::string(line.begin() + 1, line.end() - 1) == "DisplayCommander");
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
            std::string composite = "DisplayCommander." + k;
            if (!IsChordConfigKey("DisplayCommander", k.c_str())) continue;
            if (k == "enable_default_chords" || k == "guide_button_solo_ui_toggle_only") {
                v = NormalizeBoolValue(v);
            }
            g_chords_cache[composite] = v;
            ++migrated;
        }
        if (migrated > 0) {
            LogInfo("Chords: migrated %d keys from %s to chords.toml (shared)", migrated, ini_path.string().c_str());
            SaveChordsFile();
        }
        return;
    }

    // Try .toml - DisplayCommander table and optionally InputRemapping
    if (std::filesystem::exists(toml_path)) {
        try {
            toml::table tbl = toml::parse_file(toml_path.string());
            int migrated = 0;
            auto* dc = tbl.get("DisplayCommander");
            if (dc && dc->is_table()) {
                for (auto&& [k, v] : *dc->as_table()) {
                    std::string key = std::string(k.str());
                    std::string composite = "DisplayCommander." + key;
                    if (!IsChordConfigKey("DisplayCommander", key.c_str())) continue;
                    std::string val;
                    if (v.is_string())
                        val = std::string(v.as_string()->get());
                    else if (v.is_integer())
                        val = std::to_string(v.as_integer()->get());
                    else if (v.is_boolean())
                        val = v.as_boolean()->get() ? "1" : "0";
                    else
                        continue;
                    if (key == "enable_default_chords" || key == "guide_button_solo_ui_toggle_only") {
                        val = NormalizeBoolValue(val);
                    }
                    g_chords_cache[composite] = val;
                    ++migrated;
                }
            }
            auto* input_remap = tbl.get("DisplayCommander.InputRemapping");
            if (input_remap && input_remap->is_table()) {
                for (auto&& [k, v] : *input_remap->as_table()) {
                    std::string key = std::string(k.str());
                    std::string composite = "DisplayCommander.InputRemapping." + key;
                    if (!IsChordConfigKey("DisplayCommander.InputRemapping", key.c_str())) continue;
                    std::string val;
                    if (v.is_string())
                        val = std::string(v.as_string()->get());
                    else if (v.is_integer())
                        val = std::to_string(v.as_integer()->get());
                    else if (v.is_boolean())
                        val = v.as_boolean()->get() ? "1" : "0";
                    else
                        continue;
                    if (key == "Enabled" || key == "BlockInputOnHomeButton") {
                        val = NormalizeBoolValue(val);
                    }
                    g_chords_cache[composite] = val;
                    ++migrated;
                }
            }
            if (migrated > 0) {
                LogInfo("Chords: migrated %d keys from %s to chords.toml (shared)", migrated,
                        toml_path.string().c_str());
                SaveChordsFile();
            }
        } catch (const toml::parse_error&) {
            // Ignore
        }
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
            if (k.find("enable_default_chords") != std::string::npos
                || k.find("guide_button_solo_ui_toggle_only") != std::string::npos
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
