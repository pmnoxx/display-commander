#pragma once

#include <filesystem>

namespace display_commander::utils {

// Reshade load source: where to load ReShade DLL from when DC runs as proxy.
enum class ReshadeLoadSource : int {
    Local = 0,           // %localappdata%\Programs\Display_Commander\Reshade (flat)
    SharedPath = 1,      // User-provided directory
    SpecificVersion = 2,  // e.g. .../Reshade/Dll/6.7.3/
    NoReshade = 3        // Do not load ReShade (DC runs without ReShade when used as proxy)
};

// Returns the directory that should contain Reshade64.dll / Reshade32.dll for the
// current load source (read from Display Commander config). Used at ProcessAttach
// and in the Main tab. Does not depend on ReShade runtime.
std::filesystem::path GetReshadeDirectoryForLoading();

// Local folder path (%localappdata%\Programs\Display_Commander\Reshade). For UI version display.
std::filesystem::path GetLocalReshadeDirectory();

// Version string of Reshade64.dll in the local folder, or empty if not found.
std::string GetLocalReshadeVersion();

// Version string of Reshade64.dll in the configured shared path, or empty if path missing / DLL not found.
std::string GetSharedReshadeVersion();

// Get/set load source and related values from DC config (section DisplayCommander.ReShade).
// Used by ReShade tab UI and by GetReshadeDirectoryForLoading().
ReshadeLoadSource GetReshadeLoadSourceFromConfig();
void SetReshadeLoadSourceInConfig(ReshadeLoadSource value);

// True when load source is NoReshade (user chose not to load ReShade). Used to skip load without showing an error.
bool IsReshadeLoadDisabledByConfig();
std::string GetReshadeSharedPathFromConfig();
void SetReshadeSharedPathInConfig(const std::string& path);
std::string GetReshadeSelectedVersionFromConfig();
void SetReshadeSelectedVersionInConfig(const std::string& version);

// Supported ReShade versions for the "Specific version" dropdown (all known: fallback + GitHub).
const char* const* GetReshadeVersionList(size_t* out_count);

// Installed ReShade versions only (subdirs of .../Reshade/Dll/ that contain both DLLs). Use for version selector.
const char* const* GetReshadeInstalledVersionList(size_t* out_count);

// When load source is SpecificVersion but the selected version was not installed, GetReshadeDirectoryForLoading
// falls back to the highest available version. This returns true in that case and fills the two version strings
// (selected from config, and actually loaded). Use to show a UI warning. out_selected/out_loaded may be null.
bool GetReshadeLoadFallbackVersionInfo(std::string* out_selected_version, std::string* out_loaded_version);

}  // namespace display_commander::utils
