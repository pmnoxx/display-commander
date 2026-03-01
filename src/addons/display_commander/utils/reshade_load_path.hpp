#pragma once

#include <filesystem>

namespace display_commander::utils {

// Reshade load source: where to load ReShade DLL from when DC runs as proxy.
enum class ReshadeLoadSource : int {
    Local = 0,           // %localappdata%\Programs\Display_Commander\Reshade (flat)
    SharedPath = 1,      // User-provided directory
    SpecificVersion = 2  // e.g. .../Reshade/6.7.3/
};

// Returns the directory that should contain Reshade64.dll / Reshade32.dll for the
// current load source (read from Display Commander config). Used at ProcessAttach
// and in the ReShade tab. Does not depend on ReShade runtime.
std::filesystem::path GetReshadeDirectoryForLoading();

// Get/set load source and related values from DC config (section DisplayCommander.ReShade).
// Used by ReShade tab UI and by GetReshadeDirectoryForLoading().
ReshadeLoadSource GetReshadeLoadSourceFromConfig();
void SetReshadeLoadSourceInConfig(ReshadeLoadSource value);
std::string GetReshadeSharedPathFromConfig();
void SetReshadeSharedPathInConfig(const std::string& path);
std::string GetReshadeSelectedVersionFromConfig();
void SetReshadeSelectedVersionInConfig(const std::string& version);

// Supported ReShade versions for the "Specific version" dropdown.
const char* const* GetReshadeVersionList(size_t* out_count);

}  // namespace display_commander::utils
