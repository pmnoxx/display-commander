#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nvapi.h>

namespace display_commander::nvapi {

struct ImportantProfileSetting {
    std::string label;                // e.g. "Smooth Motion (591 or below 4000 series)", "DLSS-SR override"
    std::string value;                // Human-readable value (e.g. "On", "Preset K")
    std::uint32_t setting_id = 0;     // DRS setting ID (0 = not editable)
    std::uint32_t value_id = 0;       // Current or default raw DWORD value
    std::uint32_t default_value = 0;  // NVIDIA default (for reset button)
    bool is_bit_field = false;        // If true, value_id is a bitmask; UI shows checkboxes per flag.
    bool known_to_driver =
        true;  // If false, setting is in profile but not in driver's recognized list (show key + value, Delete only).
    bool requires_admin = false;      // If true, UI shows label in warning color; changing may need admin.
    unsigned min_required_driver_version = 0;  // e.g. 43000 for 430.00, 57186 for 571.86; 0 = not specified. Shown in tooltip.
};

// Per-profile application entry data (one row in "Matching profile(s)" list).
struct MatchedProfileEntry {
    std::string profile_name;
    std::string app_name;  // Executable path/name in profile
    std::string user_friendly_name;
    std::string launcher;
    std::string file_in_folder;  // "File in folder" requirement (':' separated if multiple)
    bool is_metro = false;
    bool is_command_line = false;
    std::string command_line;
    int score = 0;  // Number of non-empty app-entry fields; higher = more specific match. Used for sorting.
};

struct NvidiaProfileSearchResult {
    bool success = false;                                // DRS query succeeded (even if no match)
    std::string current_exe_path;                        // Full path of current process exe
    std::string current_exe_name;                        // Base name (e.g. game.exe)
    std::vector<MatchedProfileEntry> matching_profiles;  // Matching profiles with full app entry data
    std::vector<std::string> matching_profile_names;     // Profile names only (derived; for backward compat)
    std::vector<ImportantProfileSetting>
        important_settings;  // Key settings from first matching profile (fixed list, "Not set" if missing)
    std::vector<ImportantProfileSetting>
        advanced_settings;  // Extra useful settings (Ansel, FXAA, etc.) when "show advanced" is enabled
    std::vector<ImportantProfileSetting>
        all_settings;   // All settings actually present in first matching profile (from EnumSettings)
    std::string error;  // If success is false
};

// Finds profile for current process exe by full path (single NvAPI_DRS_FindApplicationByName call).
// Caller owns hSession (must be created and loaded). Returns true if profile and app found.
bool FindApplicationByPathForCurrentExe(NvDRSSessionHandle hSession, NvDRSProfileHandle* phProfile,
                                        NVDRS_APPLICATION* pApp);

// Returns cached result for the current exe. Fills cache on first call or after InvalidateProfileSearchCache().
// Use this in UI to avoid searching every frame. Call InvalidateProfileSearchCache() on user "Refresh".
NvidiaProfileSearchResult GetCachedProfileSearchResult();

// Invalidates the profile search cache. Next GetCachedProfileSearchResult() will run a fresh search.
void InvalidateProfileSearchCache();

// Result of querying the NVIDIA profile FPS limit (FRL_FPS / FPS Limiter V3) for the current exe.
// Uses the same cached result as GetCachedProfileSearchResult(); safe to call from UI each frame.
struct ProfileFpsLimitResult {
    bool has_profile = false;  // true if at least one profile matches the current exe
    std::uint32_t value = 0;   // 0 = Off, 20-1000 = FPS limit
    std::string profile_name;  // first matching profile name when has_profile; else empty
    std::string error;         // non-empty if DRS failed (e.g. no NVIDIA GPU)
};

// Returns the current FPS limit from the NVIDIA driver profile for the current exe.
// Use for Main tab "NVIDIA Profile" FPS limiter mode. Value is 0 (Off) or 20-1000 (FPS).
ProfileFpsLimitResult GetProfileFpsLimit();

// Sets the NVIDIA profile FPS limit (FRL_FPS) for the current exe. Value 0 = Off, 20-1000 = FPS.
// Returns (true, "") on success; (false, error_message) on failure. Invalidates profile cache on success.
std::pair<bool, std::string> SetProfileFpsLimit(std::uint32_t value);

// Returns available (value, label) pairs for the profile FPS limit setting (Off + 20-1000 FPS). For UI combo.
std::vector<std::pair<std::uint32_t, std::string>> GetProfileFpsLimitOptions();

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

// Status of DLSS render preset overrides in the NVIDIA driver profile for the current exe.
// Uses the same cached result as GetCachedProfileSearchResult(); safe to call from UI each frame.
struct DlssDriverPresetStatus {
    bool has_profile = false;            // true if DRS succeeded and at least one profile matches
    std::string profile_error;           // non-empty if DRS failed (e.g. no NVIDIA GPU)
    std::string profile_names;           // comma-separated matching profile names when has_profile
    std::string sr_preset_value;         // human-readable DLSS-SR preset (e.g. "Not set", "Preset C", "Off", "Latest")
    bool sr_preset_is_override = false;  // true if profile sets a non-default DLSS-SR preset
    std::string rr_preset_value;         // human-readable DLSS-RR preset
    bool rr_preset_is_override = false;  // true if profile sets a non-default DLSS-RR preset
};

// Returns DLSS driver preset status for the current exe from the cached profile search result.
// Use on the DLSS Information page to show whether the driver profile has DLSS-SR/RR preset overrides.
DlssDriverPresetStatus GetDlssDriverPresetStatus();

// Clears the driver profile DLSS Render Profile override for the current exe: sets both DLSS-SR and DLSS-RR
// preset settings to default on the first matching profile. Invalidates cache on success.
// Returns (true, "") on success; (false, error_message) on failure.
std::pair<bool, std::string> ClearDriverDlssPresetOverride();

// Sets or deletes a DWORD setting for a profile that contains the given executable.
// Used by RunDLL_NvAPI_SetDWORD (rundll32). exePath is the full path to the executable (e.g. "C:\Games\game.exe").
// Logic assumes full exe path; NvAPI_DRS_FindApplicationByName is used with that path to find the profile.
// If deleteSetting is true, the setting is removed (reset to driver default); valueIfSet is ignored.
// If deleteSetting is false, the setting is set to valueIfSet. Requires NVAPI initialized.
// Returns (true, "") on success, (false, error_message) on failure.
std::pair<bool, std::string> SetOrDeleteProfileSettingForExe(const std::wstring& exePath, std::uint32_t settingId,
                                                             bool deleteSetting, std::uint32_t valueIfSet);

// One setting recognized by the current driver (from NvAPI_DRS_EnumAvailableSettingIds + GetSettingNameFromId).
// Use to show only settings valid for this driver version, or to dump the full list.
struct DriverAvailableSetting {
    std::uint32_t setting_id = 0;
    std::string name;  // Official name from driver (UTF-8)
};

// Enumerates all setting IDs recognized by the current NVIDIA driver and their official names.
// Does not require a DRS session. Empty on error (e.g. NVAPI not initialized, no NVIDIA GPU).
std::vector<DriverAvailableSetting> GetDriverAvailableSettings();

// Dumps all driver-recognized settings to a text file: one line per setting with ID (hex), name, type, and allowed
// values. filePath: full path for the output file (e.g. addon dir + "nvidia_driver_settings_dump.txt"). Returns (true,
// "") on success; (false, error_message) on failure.
std::pair<bool, std::string> DumpDriverSettingsToFile(const std::string& filePath);

// Returns all driver-recognized settings with current profile value (or "Not set") and driver default.
// Uses the same list as GetDriverAvailableSettings(); for each setting, reads value from the first
// profile matching the current exe (if any) and default from EnumAvailableSettingValues.
// Use for "All driver settings" UI with edit capability. Requires a DRS session internally.
std::vector<ImportantProfileSetting> GetDriverSettingsWithProfileValues();

// Removes a setting from the profile for the current exe (reset to driver default).
// Returns (true, "") on success; (false, error_message) on failure. Invalidates profile cache on success.
std::pair<bool, std::string> DeleteProfileSettingForCurrentExe(std::uint32_t settingId);

// Debug tooltip: returns a multi-line string with Key (hex), GetSettingNameFromId result, and
// GetSettingIdFromName(displayNameUtf8) result. Use when hovering over a setting name in the UI.
// displayNameUtf8 is the label shown in the UI (e.g. s.label). Empty string on NVAPI/init failure.
std::string GetSettingDriverDebugTooltip(std::uint32_t settingId, const std::string& displayNameUtf8);

// Returns the list of profile setting IDs shown in the Main tab "NVIDIA Control" section: Smooth Motion
// (Allowed APIs, Enable), RTX HDR (excluding admin-only Debanding/Allow), then Latency - Max Pre-Rendered Frames.
std::vector<std::uint32_t> GetRtxHdrSettingIds();

}  // namespace display_commander::nvapi
