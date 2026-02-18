#pragma once

#include <string>

namespace display_commander::config {

// Path to hotkeys.toml in Display Commander folder (Local App Data). Shared across all games.
std::string GetHotkeysFilePath();

// True if this config key is stored in hotkeys.toml (DisplayCommander section only).
bool IsHotkeyConfigKey(const char* key);

// Load hotkeys from file into cache. Ensure directory exists. Returns true if file was read (or created empty).
bool LoadHotkeysFile();

// Save current cache to hotkeys.toml. Returns true on success.
bool SaveHotkeysFile();

// Get value from cache (loads file on first use). Returns true if key exists.
bool GetHotkeyValue(const char* key, std::string& value);

// Set value in cache and save to file. Key must be a hotkey config key.
void SetHotkeyValue(const char* key, const std::string& value);

}  // namespace display_commander::config
