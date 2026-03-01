#pragma once

#include <filesystem>

namespace display_commander::utils {

// Selector mode: local (injection DLL), global (base path), debug (Debug\X.Y.Z), stable (Dll\X.Y.Z).
// Config: dc_selector_mode ("local"|"global"|"debug"|"stable"), dc_version_for_debug, dc_version_for_stable
// ("latest" or X.Y.Z). Migration from legacy DcSelectedVersion is done on first read.

// Returns the directory to load DC from (from config). When mode is "local", returns base so loader does not load
// from central. Otherwise: global -> base; debug -> Debug\version; stable -> Dll\version; with fallbacks.
std::filesystem::path GetDcDirectoryForLoading();

// Local folder (%localappdata%\Programs\Display_Commander).
std::filesystem::path GetLocalDcDirectory();

// Version string from addon DLL in the given directory, or empty if not found.
std::string GetDcVersionInDirectory(const std::filesystem::path& dir);
std::string GetLocalDcVersion();

// Config get/set (section DisplayCommander.DC).
// dc_selector_mode: "local" | "global" | "debug" | "stable". Default "local".
// dc_version_for_debug / dc_version_for_stable: "latest" | X.Y.Z.
std::string GetDcSelectorModeFromConfig();
void SetDcSelectorModeInConfig(const std::string& mode);
std::string GetDcVersionForDebugFromConfig();
void SetDcVersionForDebugInConfig(const std::string& version);
std::string GetDcVersionForStableFromConfig();
void SetDcVersionForStableInConfig(const std::string& version);

// Legacy: for migration only. Prefer GetDcSelectorModeFromConfig / GetDcVersionForStableFromConfig.
std::string GetDcSelectedVersionFromConfig();
void SetDcSelectedVersionInConfig(const std::string& version);

// Installed DC versions: stable = subdirs of .../Display_Commander/Dll/; debug = subdirs of .../Debug/.
const char* const* GetDcInstalledVersionListStable(size_t* out_count);
const char* const* GetDcInstalledVersionListDebug(size_t* out_count);
// Legacy alias: same as GetDcInstalledVersionListStable.
const char* const* GetDcInstalledVersionList(size_t* out_count);

// Path to the DC addon DLL for current architecture in the given directory, or empty if not found.
std::filesystem::path GetDcAddonPathInDirectory(const std::filesystem::path& dir);

}  // namespace display_commander::utils
