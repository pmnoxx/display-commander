#pragma once

#include <filesystem>

namespace display_commander::utils {

// ReShade load config: single string ReshadeSelectedVersion.
// "" = no override (load from base folder). "latest" = Dll\<highest>. "X.Y.Z" = Dll\X.Y.Z. "no" = do not load ReShade.
// Only versions under %localappdata%\Programs\Display_Commander\Reshade\Dll\X.Y.Z are considered.

// Returns the directory that should contain Reshade64.dll / Reshade32.dll for the
// current config. Empty path when ReShade load is disabled ("no").
std::filesystem::path GetReshadeDirectoryForLoading();

// Local folder path (%localappdata%\Programs\Display_Commander\Reshade). For UI version display.
std::filesystem::path GetLocalReshadeDirectory();

// Version string of Reshade64.dll in the local base folder, or empty if not found.
std::string GetLocalReshadeVersion();

// Config get/set. Value: "", "latest", "X.Y.Z", or "no".
std::string GetReshadeSelectedVersionFromConfig();
void SetReshadeSelectedVersionInConfig(const std::string& version);

// True when selected version is "no" (do not load ReShade).
bool IsReshadeLoadDisabledByConfig();

// Supported ReShade versions for dropdown (all known: fallback + GitHub).
const char* const* GetReshadeVersionList(size_t* out_count);

// Installed ReShade versions only (subdirs of .../Reshade/Dll/ that contain both DLLs).
const char* const* GetReshadeInstalledVersionList(size_t* out_count);

// When selected is "X.Y.Z" but that version was not installed, we load the highest available and set fallback info.
// Returns true in that case and fills the two version strings. out_selected/out_loaded may be null.
bool GetReshadeLoadFallbackVersionInfo(std::string* out_selected_version, std::string* out_loaded_version);

// Copy currently loaded ReShade from loaded_reshade_directory to Reshade\Dll\X.Y.Z (version from DLL) if that
// folder does not already contain the DLLs. Skips if loaded_reshade_directory is already under Reshade\Dll\.
// Returns true on success or skip, false on error (out_error set).
bool CopyCurrentReshadeToDll(const std::filesystem::path& loaded_reshade_directory, std::string* out_error);

}  // namespace display_commander::utils
