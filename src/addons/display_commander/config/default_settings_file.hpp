#pragma once

#include <string>

namespace display_commander::config {

// Path to default_settings.toml in Display Commander folder (Local App Data). User-editable defaults for all games.
std::string GetDefaultSettingsFilePath();

// Load default settings from file into cache. Creates file with template if missing. Returns true if load succeeded (or file created empty).
bool LoadDefaultSettingsFile();

// Get value from cache for [DisplayCommander] key (loads file on first use). Returns true if key exists. Section must be "DisplayCommander".
bool GetDefaultSettingValue(const char* section, const char* key, std::string& value);

}  // namespace display_commander::config
