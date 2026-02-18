#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace display_commander::nvapi {

struct ImportantProfileSetting {
    std::string label;   // e.g. "Smooth Motion", "DLSS-SR override"
    std::string value;   // Human-readable value (e.g. "On", "Preset K")
    std::uint32_t setting_id = 0;   // DRS setting ID (0 = not editable)
    std::uint32_t value_id = 0;     // Current or default raw DWORD value
    std::uint32_t default_value = 0; // NVIDIA default (for reset button)
    bool is_bit_field = false;      // If true, value_id is a bitmask; UI shows checkboxes per flag.
};

struct NvidiaProfileSearchResult {
    bool success = false;           // DRS query succeeded (even if no match)
    std::string current_exe_path;   // Full path of current process exe
    std::string current_exe_name;   // Base name (e.g. game.exe)
    std::vector<std::string> matching_profile_names;  // Profiles that list this exe
    std::vector<ImportantProfileSetting> important_settings;  // Key settings from first matching profile (fixed list, "Not set" if missing)
    std::vector<ImportantProfileSetting> all_settings;        // All settings actually present in first matching profile (from EnumSettings)
    std::string error;              // If success is false
};

// Searches all NVIDIA driver profiles for any that contain the current process executable.
// Enumerates profiles via DRS, then each profile's applications; matches by exe path or name.
// Requires NVAPI to be available (NVIDIA GPU). Does not call NvAPI_Initialize/Unload.
NvidiaProfileSearchResult SearchAllProfilesForCurrentExe();

// Returns cached result for the current exe. Fills cache on first call or after InvalidateProfileSearchCache().
// Use this in UI to avoid searching every frame. Call InvalidateProfileSearchCache() on user "Refresh".
NvidiaProfileSearchResult GetCachedProfileSearchResult();

// Invalidates the profile search cache. Next GetCachedProfileSearchResult() will run a fresh search.
void InvalidateProfileSearchCache();

// Returns available (value, label) pairs for a DWORD setting. Cached per settingId. Empty on error.
std::vector<std::pair<std::uint32_t, std::string>> GetSettingAvailableValues(std::uint32_t settingId);

// Sets a DWORD setting on the first profile matching the current exe. Saves settings and invalidates cache.
// Returns (true, "") on success; (false, error_message) on failure. Error message includes step and NVAPI status.
std::pair<bool, std::string> SetProfileSetting(std::uint32_t settingId, std::uint32_t value);

// Creates an NVIDIA driver profile for the current process executable and adds the exe to it.
// Profile name will be "Display Commander - <exe base name>". If a profile already exists
// for this exe, does nothing and returns success. Invalidates cache on success.
// Returns (true, "") on success, (false, error_message) on failure.
std::pair<bool, std::string> CreateProfileForCurrentExe();

// Returns true if the result includes a profile created by Display Commander (name starts with "Display Commander -").
bool HasDisplayCommanderProfile(const NvidiaProfileSearchResult& r);

// Deletes the NVIDIA profile named "Display Commander - <current exe base name>" if it exists.
// Only removes profiles we created; requires admin if driver enforces privilege. Invalidates cache on success.
// Returns (true, "") on success, (false, error_message) on failure.
std::pair<bool, std::string> DeleteDisplayCommanderProfileForCurrentExe();

// Sets or deletes a DWORD setting for a profile that contains the given executable name.
// Used by RunDLL_NvAPI_SetDWORD (rundll32). exeName is the executable name or path (e.g. "game.exe").
// If deleteSetting is true, the setting is removed (reset to driver default); valueIfSet is ignored.
// If deleteSetting is false, the setting is set to valueIfSet. Requires NVAPI initialized.
// Returns (true, "") on success, (false, error_message) on failure.
std::pair<bool, std::string> SetOrDeleteProfileSettingForExe(const std::wstring& exeName, std::uint32_t settingId,
                                                            bool deleteSetting, std::uint32_t valueIfSet);

}  // namespace display_commander::nvapi
