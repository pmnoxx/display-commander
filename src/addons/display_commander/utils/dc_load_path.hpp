#pragma once

#include <filesystem>

namespace display_commander::utils {

// Display Commander update/load source: where to consider DC addon files (Local, Shared folder, or Specific version).
enum class DcUpdateSource : int {
    Local = 0,           // %localappdata%\Programs\Display_Commander (root with zzz_display_commander*.addon64/32)
    SharedPath = 1,      // User-provided directory
    SpecificVersion = 2  // ...\Display_Commander\Dll\X.Y.Z
};

// Returns the directory that contains Display Commander addon files for the current source (from config).
std::filesystem::path GetDcDirectoryForLoading();

// Local folder (%localappdata%\Programs\Display_Commander). For UI version display.
std::filesystem::path GetLocalDcDirectory();

// Version string from addon DLL in the given directory, or empty if not found.
std::string GetDcVersionInDirectory(const std::filesystem::path& dir);
std::string GetLocalDcVersion();
std::string GetSharedDcVersion();

// Config get/set (section DisplayCommander.DC).
DcUpdateSource GetDcUpdateSourceFromConfig();
void SetDcUpdateSourceInConfig(DcUpdateSource value);
std::string GetDcSharedPathFromConfig();
void SetDcSharedPathInConfig(const std::string& path);
std::string GetDcSelectedVersionFromConfig();
void SetDcSelectedVersionInConfig(const std::string& version);

// Installed DC versions only (subdirs of .../Display_Commander/Dll/ that contain both addon64 and addon32).
const char* const* GetDcInstalledVersionList(size_t* out_count);

}  // namespace display_commander::utils
