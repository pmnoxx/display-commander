#include "display_commander_config.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <toml++/toml.hpp>
#include "../globals.hpp"
#include "../utils.hpp"
#include "../utils/display_commander_logger.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "chords_file.hpp"
#include "hotkeys_file.hpp"

namespace display_commander::config {

// Shared section representation for INI (migration) and TOML
struct ConfigSection {
    std::string name;
    std::vector<std::pair<std::string, std::string>> key_values;
};

// Simple INI file parser used only for migration from .ini to .toml
class IniFile {
   public:
    bool LoadFromFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        sections_.clear();
        std::string line;
        ConfigSection* current_section = nullptr;

        while (std::getline(file, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line[0] == '[' && line.back() == ']') {
                std::string section_name = line.substr(1, line.length() - 2);
                sections_.push_back({section_name, {}});
                current_section = &sections_.back();
            } else if (current_section != nullptr) {
                size_t equal_pos = line.find('=');
                if (equal_pos != std::string::npos) {
                    std::string key = line.substr(0, equal_pos);
                    std::string value = line.substr(equal_pos + 1);
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);
                    current_section->key_values.push_back({key, value});
                }
            }
        }

        return true;
    }

    template <typename F>
    void ForEachKeyValue(F&& fn) const {
        for (const auto& s : sections_) {
            for (const auto& kv : s.key_values) {
                fn(s.name, kv.first, kv.second);
            }
        }
    }

   private:
    std::vector<ConfigSection> sections_;
};

// TOML config file backend (primary storage)
class TomlFile {
   public:
    static std::string NodeToString(const toml::node& node) {
        if (node.is_string()) {
            return std::string(node.as_string()->get());
        }
        if (node.is_integer()) {
            return std::to_string(node.as_integer()->get());
        }
        if (node.is_floating_point()) {
            return std::to_string(node.as_floating_point()->get());
        }
        if (node.is_boolean()) {
            return node.as_boolean()->get() ? "1" : "0";
        }
        if (node.is_array()) {
            std::string result;
            const auto& arr = *node.as_array();
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i > 0) result += '\0';
                const auto& el = arr[i];
                if (el.is_string()) {
                    result += std::string(el.as_string()->get());
                } else if (el.is_integer()) {
                    result += std::to_string(el.as_integer()->get());
                } else if (el.is_boolean()) {
                    result += el.as_boolean()->get() ? "1" : "0";
                }
            }
            return result;
        }
        return "";
    }

    bool LoadFromFile(const std::string& filepath) {
        try {
            toml::table tbl = toml::parse_file(filepath);
            sections_.clear();

            for (auto&& [k, v] : tbl) {
                std::string section_name = std::string(k.str());
                if (!v.is_table()) {
                    continue;
                }
                ConfigSection sec;
                sec.name = section_name;
                for (auto&& [k2, v2] : *v.as_table()) {
                    std::string key = std::string(k2.str());
                    std::string value = NodeToString(v2);
                    sec.key_values.push_back({key, value});
                }
                if (!sec.key_values.empty()) {
                    sections_.push_back(std::move(sec));
                }
            }
            return true;
        } catch (const toml::parse_error& e) {
            (void)e;
            return false;
        }
    }

    bool SaveToFile(const std::string& filepath) {
        std::string temp_filepath = filepath + ".temp";
        try {
            toml::table root;
            for (const auto& section : sections_) {
                toml::table sec_table;
                for (const auto& kv : section.key_values) {
                    // Store as string; TOML will quote if needed
                    sec_table.insert_or_assign(kv.first, std::string(kv.second));
                }
                root.insert_or_assign(section.name, std::move(sec_table));
            }

            std::ofstream file(temp_filepath);
            if (!file.is_open()) {
                return false;
            }
            file << root;
            file.close();

            std::filesystem::rename(temp_filepath, filepath);
            return true;
        } catch (const std::exception&) {
            std::filesystem::remove(temp_filepath);
            return false;
        }
    }

    bool GetValue(const std::string& section, const std::string& key, std::string& value) const {
        for (const auto& s : sections_) {
            if (s.name == section) {
                for (const auto& kv : s.key_values) {
                    if (kv.first == key) {
                        value = kv.second;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void SetValue(const std::string& section, const std::string& key, const std::string& value) {
        for (auto& s : sections_) {
            if (s.name == section) {
                for (auto& kv : s.key_values) {
                    if (kv.first == key) {
                        kv.second = value;
                        return;
                    }
                }
                s.key_values.push_back({key, value});
                return;
            }
        }
        sections_.push_back({section, {{key, value}}});
    }

    bool GetValue(const std::string& section, const std::string& key, std::vector<std::string>& values) const {
        values.clear();
        std::string value_str;
        if (GetValue(section, key, value_str)) {
            std::stringstream ss(value_str);
            std::string item;
            while (std::getline(ss, item, '\0')) {
                if (!item.empty()) {
                    values.push_back(item);
                }
            }
            return !values.empty();
        }
        return false;
    }

    void SetValue(const std::string& section, const std::string& key, const std::vector<std::string>& values) {
        std::string value_str;
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) value_str += '\0';
            value_str += values[i];
        }
        SetValue(section, key, value_str);
    }

   private:
    std::vector<ConfigSection> sections_;
};

// DisplayCommanderConfigManager implementation
DisplayCommanderConfigManager& DisplayCommanderConfigManager::GetInstance() {
    static DisplayCommanderConfigManager instance;
    return instance;
}

void DisplayCommanderConfigManager::Initialize() {
    utils::SRWLockExclusive lock(config_mutex_);

    if (initialized_) {
        return;
    }

    const std::string toml_path = GetConfigFilePath();
    const std::string ini_path = GetConfigFilePathIni();
    config_path_ = toml_path;
    config_file_ = std::make_unique<TomlFile>();

    // Initialize logger with DisplayCommander.log in the main executable directory
    char exe_path[MAX_PATH];
    DWORD path_length = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::filesystem::path exe_dir;
    if (path_length > 0) {
        exe_dir = std::filesystem::path(exe_path).parent_path();
    } else {
        // Fallback to config directory if we can't get exe path
        exe_dir = std::filesystem::path(config_path_).parent_path();
    }
    std::string log_path = (exe_dir / "DisplayCommander.log").string();
    display_commander::logger::Initialize(log_path);

    // Test the logger
    display_commander::logger::LogInfo("DisplayCommander config system initializing - logger test successful");

    EnsureConfigFileExists();

    // Prefer .toml; migrate from .ini if only .ini exists
    const bool toml_exists = std::filesystem::exists(config_path_);
    const bool ini_exists = std::filesystem::exists(ini_path);

    if (toml_exists) {
        if (config_file_->LoadFromFile(config_path_)) {
            LogInfo("DisplayCommanderConfigManager: Loaded config from %s", config_path_.c_str());
        } else {
            LogInfo("DisplayCommanderConfigManager: Opened config file at %s (load failed, using empty)",
                    config_path_.c_str());
        }
    } else if (ini_exists) {
        IniFile ini;
        if (ini.LoadFromFile(ini_path)) {
            ini.ForEachKeyValue([this](const std::string& section, const std::string& key, const std::string& value) {
                config_file_->SetValue(section, key, value);
            });
            if (config_file_->SaveToFile(config_path_)) {
                std::error_code ec;
                std::filesystem::remove(ini_path, ec);
                LogInfo("DisplayCommanderConfigManager: Migrated config from %s to %s and removed .ini",
                        ini_path.c_str(), config_path_.c_str());
            } else {
                LogInfo(
                    "DisplayCommanderConfigManager: Migrated config from %s to memory; save to %s failed (will retry "
                    "on next save)",
                    ini_path.c_str(), config_path_.c_str());
            }
        } else {
            LogInfo("DisplayCommanderConfigManager: Created new config file at %s", config_path_.c_str());
        }
    } else {
        LogInfo("DisplayCommanderConfigManager: Created new config file at %s", config_path_.c_str());
    }

    initialized_ = true;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, std::string& value) {
    // Hotkeys are stored in hotkeys.toml (Display Commander folder) for sharing across games
    if (section != nullptr && strcmp(section, "DisplayCommander") == 0 && key != nullptr && IsHotkeyConfigKey(key)) {
        return GetHotkeyValue(key, value);
    }
    // Chords / gamepad remap settings are stored in chords.toml for sharing across games
    if (section != nullptr && key != nullptr && IsChordConfigKey(section, key)) {
        return GetChordValue(section, key, value);
    }
    utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    if (!config_file_->GetValue(section != nullptr ? section : "", key != nullptr ? key : "", value)) {
        return false;
    }
    return true;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, int& value) {
    std::string str_value;
    if (GetConfigValue(section, key, str_value)) {
        try {
            value = std::stoi(str_value);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, uint32_t& value) {
    std::string str_value;
    if (GetConfigValue(section, key, str_value)) {
        try {
            value = static_cast<uint32_t>(std::stoul(str_value));
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, float& value) {
    std::string str_value;
    if (GetConfigValue(section, key, str_value)) {
        try {
            value = std::stof(str_value);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, double& value) {
    std::string str_value;
    if (GetConfigValue(section, key, str_value)) {
        try {
            value = std::stod(str_value);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, bool& value) {
    int int_value;
    if (GetConfigValue(section, key, int_value)) {
        value = (int_value != 0);
        return true;
    }
    return false;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key,
                                                   std::vector<std::string>& values) {
    utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    return config_file_->GetValue(section != nullptr ? section : "", key != nullptr ? key : "", values);
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, std::string& value,
                                                               const std::string& default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        SaveConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, int& value,
                                                               int default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        SaveConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, uint32_t& value,
                                                               uint32_t default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        SaveConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, float& value,
                                                               float default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        SaveConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, double& value,
                                                               double default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        SaveConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::GetConfigValueEnsureExists(const char* section, const char* key, bool& value,
                                                               bool default_value) {
    if (!GetConfigValue(section, key, value)) {
        // Value doesn't exist, set default and save
        SetConfigValue(section, key, default_value);
        SaveConfig("get_config_value_ensure_exists");
        value = default_value;
    }
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, const std::string& value) {
    if (section != nullptr && strcmp(section, "DisplayCommander") == 0 && key != nullptr && IsHotkeyConfigKey(key)) {
        SetHotkeyValue(key, value);
        return;
    }
    if (section != nullptr && key != nullptr && IsChordConfigKey(section, key)) {
        SetChordValue(section, key, value);
        return;
    }
    utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    config_file_->SetValue(section != nullptr ? section : "", key != nullptr ? key : "", value);
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, const char* value) {
    if (section != nullptr && strcmp(section, "DisplayCommander") == 0 && key != nullptr && IsHotkeyConfigKey(key)) {
        SetHotkeyValue(key, value != nullptr ? value : "");
        return;
    }
    if (section != nullptr && key != nullptr && IsChordConfigKey(section, key)) {
        SetChordValue(section, key, value != nullptr ? value : "");
        return;
    }
    utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    config_file_->SetValue(section != nullptr ? section : "", key != nullptr ? key : "", value != nullptr ? value : "");
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, int value) {
    SetConfigValue(section, key, std::to_string(value));
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, uint32_t value) {
    SetConfigValue(section, key, std::to_string(value));
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, float value) {
    SetConfigValue(section, key, std::to_string(value));
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, double value) {
    SetConfigValue(section, key, std::to_string(value));
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key, bool value) {
    SetConfigValue(section, key, value ? 1 : 0);
}

void DisplayCommanderConfigManager::SetConfigValue(const char* section, const char* key,
                                                   const std::vector<std::string>& values) {
    utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    config_file_->SetValue(section != nullptr ? section : "", key != nullptr ? key : "", values);
}

void DisplayCommanderConfigManager::SaveConfig(const char* reason) {
    utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        return;
    }

    EnsureConfigFileExists();
    if (config_file_->SaveToFile(config_path_)) {
        // Clear any previous save failure state
        g_config_save_failure_path.store(nullptr);

        if (reason != nullptr && reason[0] != '\0') {
            LogInfo("DisplayCommanderConfigManager: Saved config to %s (reason: %s)", config_path_.c_str(), reason);
        } else {
            LogInfo("DisplayCommanderConfigManager: Saved config to %s", config_path_.c_str());
        }
    } else {
        // Set save failure state for UI display
        g_config_save_failure_path.store(std::make_shared<const std::string>(config_path_));

        if (reason != nullptr && reason[0] != '\0') {
            LogError("DisplayCommanderConfigManager: Failed to save config to %s (reason: %s)", config_path_.c_str(),
                     reason);
        } else {
            LogError("DisplayCommanderConfigManager: Failed to save config to %s", config_path_.c_str());
        }
    }
}

std::string DisplayCommanderConfigManager::GetConfigPath() const { return config_path_; }

void DisplayCommanderConfigManager::SetAutoFlushLogs(bool enabled) {
    auto_flush_logs_.store(enabled);
    if (enabled) {
        // Immediately flush logs when auto-flush is enabled
        display_commander::logger::FlushLogs();
    }
}

bool DisplayCommanderConfigManager::GetAutoFlushLogs() const { return auto_flush_logs_.load(); }

void DisplayCommanderConfigManager::EnsureConfigFileExists() {
    if (config_path_.empty()) {
        config_path_ = GetConfigFilePath();
    }

    // Create directory if it doesn't exist
    std::filesystem::path config_dir = std::filesystem::path(config_path_).parent_path();
    if (!config_dir.empty() && !std::filesystem::exists(config_dir)) {
        std::filesystem::create_directories(config_dir);
    }
}

std::string DisplayCommanderConfigManager::GetConfigFilePath() {
    char exe_path[MAX_PATH];
    DWORD path_length = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    if (path_length == 0) {
        GetCurrentDirectoryA(MAX_PATH, exe_path);
    }
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    return (exe_dir / "DisplayCommander.toml").string();
}

std::string DisplayCommanderConfigManager::GetConfigFilePathIni() {
    char exe_path[MAX_PATH];
    DWORD path_length = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    if (path_length == 0) {
        GetCurrentDirectoryA(MAX_PATH, exe_path);
    }
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    return (exe_dir / "DisplayCommander.ini").string();
}

// Global function implementations
bool get_config_value(const char* section, const char* key, std::string& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, int& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, uint32_t& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, float& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, double& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, bool& value) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value);
}

bool get_config_value(const char* section, const char* key, std::vector<std::string>& values) {
    return DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, values);
}

bool get_config_value(const char* section, const char* key, char* buffer, size_t* buffer_size) {
    std::string value;
    if (DisplayCommanderConfigManager::GetInstance().GetConfigValue(section, key, value)) {
        if (buffer != nullptr && buffer_size != nullptr) {
            size_t copy_size = (value.length() < (*buffer_size - 1)) ? value.length() : (*buffer_size - 1);
            strncpy_s(buffer, *buffer_size, value.c_str(), copy_size);
            buffer[copy_size] = '\0';
            *buffer_size = copy_size + 1;
            return true;
        }
    }
    return false;
}

void set_config_value(const char* section, const char* key, const std::string& value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, const char* value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, int value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, uint32_t value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, float value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, double value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, bool value) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, value);
}

void set_config_value(const char* section, const char* key, const std::vector<std::string>& values) {
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(section, key, values);
}

void save_config(const char* reason) { DisplayCommanderConfigManager::GetInstance().SaveConfig(reason); }

void get_config_value_ensure_exists(const char* section, const char* key, std::string& value,
                                    const std::string& default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_ensure_exists(const char* section, const char* key, int& value, int default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_ensure_exists(const char* section, const char* key, uint32_t& value, uint32_t default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_ensure_exists(const char* section, const char* key, float& value, float default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_ensure_exists(const char* section, const char* key, double& value, double default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

void get_config_value_ensure_exists(const char* section, const char* key, bool& value, bool default_value) {
    DisplayCommanderConfigManager::GetInstance().GetConfigValueEnsureExists(section, key, value, default_value);
}

}  // namespace display_commander::config
