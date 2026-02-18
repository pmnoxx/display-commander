#pragma once

#include <string>

namespace display_commander::config {

// Path to chords.toml in Display Commander folder (Local App Data). Shared across all games.
std::string GetChordsFilePath();

// True if this (section, key) is stored in chords.toml (gamepad/chord settings shared globally).
bool IsChordConfigKey(const char* section, const char* key);

// Load chords from file into cache. Ensure directory exists. Returns true if file was read (or created empty).
bool LoadChordsFile();

// Save current cache to chords.toml. Returns true on success.
bool SaveChordsFile();

// Get value from cache (loads file on first use). Returns true if key exists.
bool GetChordValue(const char* section, const char* key, std::string& value);

// Set value in cache and save to file. (section, key) must be a chord config key.
void SetChordValue(const char* section, const char* key, const std::string& value);

}  // namespace display_commander::config
