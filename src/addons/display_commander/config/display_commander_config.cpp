#include "display_commander_config.hpp"
#include "hotkeys_file.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../globals.hpp"
#include "../utils.hpp"
#include "../utils/display_commander_logger.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"

namespace display_commander::config {

// Simple INI file parser/writer
class IniFile {
   public:
    struct Section {
        std::string name;
        std::vector<std::pair<std::string, std::string>> key_values;
    };

    bool LoadFromFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return false;
        }

        sections_.clear();
        std::string line;
        Section* current_section = nullptr;

        while (std::getline(file, line)) {
            // Remove leading/trailing whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            // Skip empty lines and comments
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            // Check for section header [section_name]
            if (line[0] == '[' && line.back() == ']') {
                std::string section_name = line.substr(1, line.length() - 2);
                sections_.push_back({section_name, {}});
                current_section = &sections_.back();
            }
            // Check for key=value pair
            else if (current_section != nullptr) {
                size_t equal_pos = line.find('=');
                if (equal_pos != std::string::npos) {
                    std::string key = line.substr(0, equal_pos);
                    std::string value = line.substr(equal_pos + 1);

                    // Remove leading/trailing whitespace from key and value
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

    bool SaveToFile(const std::string& filepath) {
        // Write to temp file first for atomic operation
        std::string temp_filepath = filepath + ".temp";
        std::ofstream file(temp_filepath);
        if (!file.is_open()) {
            return false;
        }

        for (const auto& section : sections_) {
            file << "[" << section.name << "]\n";
            for (const auto& kv : section.key_values) {
                file << kv.first << "=" << kv.second << "\n";
            }
            file << "\n";
        }

        file.close();

        // Atomically move temp file to final location
        try {
            std::filesystem::rename(temp_filepath, filepath);
            return true;
        } catch (const std::exception&) {
            // Clean up temp file on failure
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

                        // Migration: Check if this is a device ID setting that was saved as an integer
                        if ((key.find("device_id") != std::string::npos
                             || key.find("display_device_id") != std::string::npos || key == "target_display")
                            && !value.empty() && std::all_of(value.begin(), value.end(), ::isdigit)) {
                            // This is likely an old integer value, return empty string to trigger default
                            value = "";
                            // Mark for removal from config file
                            const_cast<IniFile*>(this)->SetValue(section, key, "");
                        }

                        return true;
                    }
                }
            }
        }
        return false;
    }

    void SetValue(const std::string& section, const std::string& key, const std::string& value) {
        // Find existing section
        for (auto& s : sections_) {
            if (s.name == section) {
                // Find existing key in section
                for (auto& kv : s.key_values) {
                    if (kv.first == key) {
                        kv.second = value;
                        return;
                    }
                }
                // Key not found, add it
                s.key_values.push_back({key, value});
                return;
            }
        }
        // Section not found, create it
        sections_.push_back({section, {{key, value}}});
    }

    bool GetValue(const std::string& section, const std::string& key, std::vector<std::string>& values) const {
        values.clear();
        std::string value_str;
        if (GetValue(section, key, value_str)) {
            // Split by null character (ReShade format)
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
            if (i > 0) {
                value_str += '\0';
            }
            value_str += values[i];
        }
        SetValue(section, key, value_str);
    }

   private:
    std::vector<Section> sections_;
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

    config_path_ = GetConfigFilePath();
    config_file_ = std::make_unique<IniFile>();

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

    // Load existing config if it exists
    if (!config_file_->LoadFromFile(config_path_)) {
        LogInfo("DisplayCommanderConfigManager: Created new config file at %s", config_path_.c_str());
    } else {
        LogInfo("DisplayCommanderConfigManager: Loaded existing config from %s", config_path_.c_str());
    }

    initialized_ = true;
}

bool DisplayCommanderConfigManager::GetConfigValue(const char* section, const char* key, std::string& value) {
    // Hotkeys are stored in hotkeys.toml (Display Commander folder) for sharing across games
    if (section != nullptr && strcmp(section, "DisplayCommander") == 0 && key != nullptr && IsHotkeyConfigKey(key)) {
        return GetHotkeyValue(key, value);
    }
    utils::SRWLockExclusive lock(config_mutex_);
    if (!initialized_) {
        Initialize();
    }
    return config_file_->GetValue(section != nullptr ? section : "", key != nullptr ? key : "", value);
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
    // Get the directory where the main executable is located
    char exe_path[MAX_PATH];

    // Get the main executable path (nullptr = current process)
    DWORD path_length = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    if (path_length == 0) {
        // Fallback to current directory
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
