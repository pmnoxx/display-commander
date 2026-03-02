#pragma once

#include <string>
#include <vector>

namespace display_commander::config {

// One entry for UI tooltip: section, key, value string, optional display name
struct DefaultOverrideEntry {
    std::string section;
    std::string key;
    std::string value;
    std::string display_name;
};

// Get current process exe filename in lowercase (e.g. "hitman3.exe")
std::string GetCurrentExeNameLower();

// Load overrides from DLL resource (lazy). Returns true if override exists for (section, key) for current exe.
bool GetDefaultOverride(const char* section, const char* key, std::string& out_value);

// Call when a setting was resolved using an override (so we can show "active" overrides and Apply)
void MarkUsedOverride(const char* section, const char* key);

// True if at least one setting was loaded via override for the current exe
bool HasActiveOverrides();

// List of (section, key, value, display_name) for active overrides (for tooltip)
std::vector<DefaultOverrideEntry> GetActiveOverrideEntries();

// Persist active override values to game's DisplayCommander.toml (clears active list so banner hides)
void ApplyDefaultOverridesToConfig();

}  // namespace display_commander::config
