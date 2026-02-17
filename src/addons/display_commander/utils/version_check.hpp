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

// Get the Display Commander base directory (%localappdata%\Programs\Display_Commander)
std::filesystem::path GetDownloadDirectory();

// Download a binary file from URL to the given path (for ReShade update, etc.)
bool DownloadBinaryFromUrl(const std::string& url, const std::filesystem::path& file_path);

// Get supported ReShade versions (hardcoded list: "latest", "6.7.2", "6.7.1", "6.6.2"). Fills out_versions.
// Kept name for API compatibility. Always returns true; out_error is unused.
bool FetchReShadeVersionsFromGitHub(std::vector<std::string>& out_versions, std::string* out_error = nullptr);

// Extract build number from version string
std::string ExtractBuildNumber(const std::string& version_str);

// Format build number as 6 digits with leading zeros
std::string FormatBuildNumber(const std::string& build_str);

}  // namespace display_commander::utils::version_check
