#pragma once

#include <filesystem>
#include <string>

namespace display_commander::utils::version_check {

// Compare version strings (e.g., "0.10.0" vs "0.10.1")
// Returns: -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
int CompareVersions(const std::string& v1, const std::string& v2);

// Parse version string (handles formats like "0.10.0" or "v0.10.0")
std::string ParseVersionString(const std::string& version_str);

// Normalize version to X.Y.Z (strip fourth component if present, e.g. 6.7.3.12345 -> 6.7.3).
std::string NormalizeVersionToXyz(const std::string& version_str);

// Get the Display Commander base directory (%localappdata%\Programs\Display_Commander)
std::filesystem::path GetDownloadDirectory();

}  // namespace display_commander::utils::version_check
