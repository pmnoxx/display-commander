#pragma once

#include <filesystem>

namespace display_commander::utils {

// Selector mode: local (injection DLL), global (base path), debug (Debug\X.Y.Z), stable (stable\X.Y.Z).
// Config: dc_selector_mode ("local"|"global"|"debug"|"stable"), dc_version_for_debug, dc_version_for_stable
// ("latest" or X.Y.Z). Migration from legacy DcSelectedVersion is done on first read.

// Returns the directory to load DC from (from config). Optional current_module: HMODULE of the loader (e.g. proxy
// DLL) for proxy-directory fallback when mode is "local".
// When mode is "local", resolution order: (1) local zzz_display_commander.addon64/.addon32, (2) global same,
// (3) proxy .dll dir (dxgi.dll/winmm.dll/d3d11.dll/etc.). When mode is global/debug/stable: unchanged.
std::filesystem::path GetDcDirectoryForLoading(void* current_module = nullptr);

// Local folder (%localappdata%\Programs\Display_Commander).
std::filesystem::path GetLocalDcDirectory();

// Process (game) directory only if it contains zzz_display_commander.addon64/.addon32; otherwise empty. For UI "Local
// DC version".
std::filesystem::path GetLocalDcAddonDirectory();
// Directory of the first loaded DC proxy module (e.g. dxgi.dll, winmm.dll), or empty.
std::filesystem::path GetDcProxyDirectory();
// Full path of the first loaded DC proxy module (e.g. dxgi.dll, winmm.dll), or empty. Version from this DLL is used for
// "Local Proxy DC version".
std::filesystem::path GetDcProxyModulePath();

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

// Installed DC versions: stable = subdirs of .../Display_Commander/stable/; debug = subdirs of .../Debug/.
const char* const* GetDcInstalledVersionListStable(size_t* out_count);
const char* const* GetDcInstalledVersionListDebug(size_t* out_count);
// Legacy alias: same as GetDcInstalledVersionListStable.
const char* const* GetDcInstalledVersionList(size_t* out_count);

// Path to zzz_display_commander.addon64 (64-bit) or zzz_display_commander.addon32 (32-bit) in the given directory, or
// empty if not found.
std::filesystem::path GetDcAddonPathInDirectory(const std::filesystem::path& dir);

// Delete local DC addon files (zzz_display_commander.addon64, zzz_display_commander.addon32) from the given directory
// (e.g. game folder). Returns true on success.
bool DeleteLocalDcAddonFromDirectory(const std::filesystem::path& dir, std::string* out_error = nullptr);

}  // namespace display_commander::utils
