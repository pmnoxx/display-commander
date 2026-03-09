#pragma once

#include <string>

namespace display_commander::config {

// Path to global_settings.toml in Display Commander folder (Local App Data). Shared across all games.
std::string GetGlobalSettingsFilePath();

// True if this config key is stored in global_settings.toml (DisplayCommander section only).
bool IsGlobalConfigKey(const char* key);

// Load global settings from file into cache. Ensure directory exists. Returns true if file was read (or created empty).
bool LoadGlobalSettingsFile();

// Save current cache to global_settings.toml. Returns true on success.
bool SaveGlobalSettingsFile();

// Get value from cache (loads file on first use). Returns true if key exists.
bool GetGlobalSettingValue(const char* key, std::string& value);

// Set value in cache and save to file. Key must be a global config key.
void SetGlobalSettingValue(const char* key, const std::string& value);

}  // namespace display_commander::config
