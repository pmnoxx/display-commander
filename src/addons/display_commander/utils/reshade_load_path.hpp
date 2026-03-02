#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace display_commander::utils {

// Location type for ReShade: Local (game folder), Global (fixed base), or SpecificVersion (Dll\X.Y.Z).
enum class ReshadeLocationType {
    Local,            // game folder (Reshade32/64 in same folder as game exe)
    Global,           // one fixed location: %LocalAppData%\...\Display_Commander\Reshade (default when no local)
    SpecificVersion   // Reshade\Dll\X.Y.Z (versioned subfolders)
};

// One tracked ReShade location (directory containing Reshade64.dll / Reshade32.dll).
struct ReshadeLocation {
    ReshadeLocationType type;
    std::string version;   // normalized X.Y.Z
    std::filesystem::path directory;
};

// ReShade load config: single string ReshadeSelectedVersion.
// "global" = load from base folder. "local" = game folder if present else global. "latest" = Dll\<highest>.
// "X.Y.Z" = Dll\X.Y.Z. "no" = do not load ReShade.

// Result of choosing one ReShade location from the list and settings.
struct ChooseReshadeVersionResult {
    std::filesystem::path directory;  // empty if "no" or no valid location
    std::string fallback_selected;    // set when user chose X.Y.Z but we loaded another
    std::string fallback_loaded;      // version actually used in that case
};

// Returns all tracked ReShade locations (Local + Global + SpecificVersion).
std::vector<ReshadeLocation> GetReshadeLocations(const std::filesystem::path& game_directory);

// Picks one directory from locations based on selected_setting ("no" | "local" | "latest" | "global" | "X.Y.Z").
ChooseReshadeVersionResult ChooseReshadeVersion(const std::vector<ReshadeLocation>& locations,
                                                const std::string& selected_setting);

// Returns the directory that should contain Reshade64.dll / Reshade32.dll for the current config and game.
// Empty path when ReShade load is disabled ("no"). Pass game_directory (e.g. exe parent path).
std::filesystem::path GetReshadeDirectoryForLoading(const std::filesystem::path& game_directory);

// Overload for UI/callers without game context: uses empty game_directory (no Local entry).
std::filesystem::path GetReshadeDirectoryForLoading();

// Local folder path (%localappdata%\Programs\Display_Commander\Reshade). For UI version display.
std::filesystem::path GetLocalReshadeDirectory();

// Version string of Reshade64.dll in the given directory, or empty if not found. For UI (loaded version, etc.).
std::string GetReshadeVersionInDirectory(const std::filesystem::path& dir);

// Version string of Reshade64.dll in the local base folder, or empty if not found.
std::string GetLocalReshadeVersion();

// Config get/set. Value: "global", "local", "latest", "X.Y.Z", or "no".
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

// Delete local ReShade DLLs (Reshade64.dll, Reshade32.dll) from the given directory (e.g. game exe folder).
// Safe because we never load the game-folder copy directly; we copy to temp then load. Returns true on success.
bool DeleteLocalReshadeFromDirectory(const std::filesystem::path& dir, std::string* out_error = nullptr);

}  // namespace display_commander::utils
