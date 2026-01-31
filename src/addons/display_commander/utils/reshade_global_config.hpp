#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace utils {

// Structure to hold ReShade global settings
// All settings are stored as strings, organized by section and key
struct ReShadeGlobalSettings {
    // Map structure: section_name -> (key_name -> value)
    std::map<std::string, std::map<std::string, std::string>> additional_settings;
};

// Get the path to DisplayCommander.ini in user's home folder
std::filesystem::path GetDisplayCommanderConfigPath();

// Read ReShade global settings from the current ReShade.ini
bool ReadCurrentReShadeSettings(ReShadeGlobalSettings& settings);

// Write ReShade global settings to the current ReShade.ini
bool WriteCurrentReShadeSettings(const ReShadeGlobalSettings& settings);

// Load global settings from DisplayCommander.ini in user folder
bool LoadGlobalSettings(ReShadeGlobalSettings& settings);

// Save global settings to DisplayCommander.ini in user folder
bool SaveGlobalSettings(const ReShadeGlobalSettings& settings);

// Set LoadFromDllMain setting in ReShade
bool SetLoadFromDllMain(bool enabled);

}  // namespace utils
