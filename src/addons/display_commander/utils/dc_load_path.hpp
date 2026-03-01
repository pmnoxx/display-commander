#pragma once

#include <filesystem>

namespace display_commander::utils {

// Config: single string. "" = no override (load current library). "latest" = latest in Dll. "X.Y.Z" = specific.
// Only versions under AppData\Local\Programs\Display_Commander\Dll\X.Y.Z are considered.

// Returns the directory to load DC from (from config). "" -> base; "latest" -> Dll\<highest>; "X.Y.Z" -> Dll\X.Y.Z or base if not found.
std::filesystem::path GetDcDirectoryForLoading();

// Local folder (%localappdata%\Programs\Display_Commander).
std::filesystem::path GetLocalDcDirectory();

// Version string from addon DLL in the given directory, or empty if not found.
std::string GetDcVersionInDirectory(const std::filesystem::path& dir);
std::string GetLocalDcVersion();

// Config get/set (section DisplayCommander.DC). Value: "", "latest", or "X.Y.Z".
std::string GetDcSelectedVersionFromConfig();
void SetDcSelectedVersionInConfig(const std::string& version);

// Installed DC versions only (subdirs of .../Display_Commander/Dll/ that contain both addon64 and addon32).
const char* const* GetDcInstalledVersionList(size_t* out_count);

// Path to the DC addon DLL for current architecture in the given directory, or empty if not found.
std::filesystem::path GetDcAddonPathInDirectory(const std::filesystem::path& dir);

}  // namespace display_commander::utils
