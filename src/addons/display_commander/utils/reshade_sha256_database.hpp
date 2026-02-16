#pragma once

#include <string>

namespace display_commander::utils {

// Known ReShade DLL SHA256 hashes (ReShade64.dll and ReShade32.dll per version).
// Populate by running scripts/download_reshade_hashes.ps1 (downloads 6.7.2, 6.7.1, 6.6.2, extracts, hashes).
// Returns expected SHA256 (64-char lowercase hex) for the given version and bitness, or nullptr if not in database.
// version: e.g. "6.7.2" (first 3 components of DLL version).
const char* GetReShadeExpectedSha256(const std::string& version, bool is_64bit);

// Normalize DLL version string to database key (e.g. "6.7.2.12345" -> "6.7.2").
std::string NormalizeReShadeVersionForLookup(const std::string& version);

}  // namespace display_commander::utils
