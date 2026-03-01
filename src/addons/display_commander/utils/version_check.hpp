#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace display_commander::utils::version_check {

// Version comparison result
enum class VersionComparison {
    UpToDate,         // Current version is up to date
    UpdateAvailable,  // Newer version available
    CheckFailed,      // Failed to check for updates
    Checking          // Currently checking
};

// Version check state
struct VersionCheckState {
    std::atomic<VersionComparison> status{VersionComparison::CheckFailed};
    std::atomic<std::string*> latest_version{nullptr};   // e.g., "0.10.1"
    std::atomic<std::string*> build_number{nullptr};     // Build number from release (e.g., "1162")
    std::atomic<std::string*> download_url_64{nullptr};  // URL for 64-bit download
    std::atomic<std::string*> download_url_32{nullptr};  // URL for 32-bit download
    std::atomic<bool> checking{false};
    std::atomic<std::string*> error_message{nullptr};
};

// Get the global version check state
VersionCheckState& GetVersionCheckState();

// Check for updates (runs in background thread)
void CheckForUpdates();

// Download the latest version to %localappdata%\Programs\Display_Commander
// Returns true on success, false on failure
// The filename will be zzz_display_commander_BUILD.addon64/32 where BUILD is 6-digit build number
bool DownloadUpdate(bool is_64bit, const std::string& build_number = "");

// Compare version strings (e.g., "0.10.0" vs "0.10.1")
// Returns: -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
int CompareVersions(const std::string& v1, const std::string& v2);

// Parse version string (handles formats like "0.10.0" or "v0.10.0")
std::string ParseVersionString(const std::string& version_str);

// Normalize version to X.Y.Z (strip fourth component if present, e.g. 6.7.3.12345 -> 6.7.3).
std::string NormalizeVersionToXyz(const std::string& version_str);

// Get the Display Commander base directory (%localappdata%\Programs\Display_Commander)
std::filesystem::path GetDownloadDirectory();

// Download a binary file from URL to the given path (for ReShade update, etc.)
bool DownloadBinaryFromUrl(const std::string& url, const std::filesystem::path& file_path);

// Fetch ReShade versions from GitHub (crosire/reshade tags). Returns all tags >= 6.6.2, sorted descending.
// Call at most once per app start; on failure returns false and out_versions is empty.
bool FetchReShadeVersionsFromGitHub(std::vector<std::string>& out_versions, std::string* out_error = nullptr);

// Fetch latest ReShade version from https://reshade.me (once per process, then cached).
// Parses version from download links e.g. ReShade_Setup_6.7.3.exe. Returns true and sets out_version on success.
bool FetchReShadeLatestFromReshadeMe(std::string* out_version, std::string* out_error = nullptr);

// Extract build number from version string
std::string ExtractBuildNumber(const std::string& version_str);

// Format build number as 6 digits with leading zeros
std::string FormatBuildNumber(const std::string& build_str);

// Fetch Display Commander release versions from GitHub (pmnoxx/display-commander). Returns tag names, sorted
// descending.
bool FetchDisplayCommanderReleasesFromGitHub(std::vector<std::string>& out_versions, std::string* out_error = nullptr);

// Download a specific DC version to %localappdata%\Programs\Display_Commander\Dll\<version>\. Returns true on success.
bool DownloadDcVersionToDll(const std::string& version, std::string* out_error = nullptr);

// Fetch the latest_debug release (https://github.com/pmnoxx/display-commander/releases/tag/latest_debug).
// Returns true if the release exists and has addon64/addon32 assets.
bool FetchLatestDebugRelease(std::string* out_error = nullptr);

// Download latest_debug release to Dll\X.Y.Z (version read from downloaded binaries). Call after
// FetchLatestDebugRelease (or any time).
bool DownloadDcLatestDebugToDll(std::string* out_error = nullptr);

// Download latest_debug release to Debug\X.Y.Z (version read from downloaded binaries). Use for DC selector
// debug mode; keeps debug builds separate from stable Dll\.
bool DownloadDcLatestDebugToDebugFolder(std::string* out_error = nullptr);

// Copy the given DC addon file to Dll\X.Y.Z (version read from the file) if that folder does not already exist.
// current_addon_path is typically the path to the running addon (e.g. from GetModuleFileNameW(g_hmodule)).
bool CopyCurrentVersionToDll(const std::filesystem::path& current_addon_path, std::string* out_error = nullptr);

// Copy the given DC addon file to the global base folder so "global" selector mode uses this version.
bool CopyCurrentVersionToGlobal(const std::filesystem::path& current_addon_path, std::string* out_error = nullptr);

}  // namespace display_commander::utils::version_check
