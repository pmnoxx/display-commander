#pragma once

#include <string>
#include <atomic>
#include <filesystem>

namespace display_commander::utils::version_check {

// Version comparison result
enum class VersionComparison {
    UpToDate,      // Current version is up to date
    UpdateAvailable, // Newer version available
    CheckFailed,   // Failed to check for updates
    Checking       // Currently checking
};

// Version check state
struct VersionCheckState {
    std::atomic<VersionComparison> status{VersionComparison::CheckFailed};
    std::atomic<std::string*> latest_version{nullptr};  // e.g., "0.10.1"
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

// Download the latest version to Documents\Display Commander
// Returns true on success, false on failure
// The filename will be zzz_display_commander_BUILD.addon64/32 where BUILD is 6-digit build number
bool DownloadUpdate(bool is_64bit, const std::string& build_number = "");

// Compare version strings (e.g., "0.10.0" vs "0.10.1")
// Returns: -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
int CompareVersions(const std::string& v1, const std::string& v2);

// Parse version string (handles formats like "0.10.0" or "v0.10.0")
std::string ParseVersionString(const std::string& version_str);

// Get the download directory path (Documents\Display Commander)
std::filesystem::path GetDownloadDirectory();

// Extract build number from version string
std::string ExtractBuildNumber(const std::string& version_str);

// Format build number as 6 digits with leading zeros
std::string FormatBuildNumber(const std::string& build_str);

}  // namespace display_commander::utils::version_check
