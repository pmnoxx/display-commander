#pragma once

#include <filesystem>

namespace display_commander::utils {

// Config: use_global_version (bool, default false). When false: load DC from game folder (same as .exe) if addon
// is present there, otherwise from global folder. When true: load only from global folder.

// Returns true if the given module was loaded from a file whose name ends with .dll (case-insensitive).
// Used to detect proxy-DLL loader mode (e.g. dxgi.dll, d3d11.dll) vs addon load (.addon64/.addon32).
bool IsLoadedWithDLLExtension(void* h_module);

// Returns the directory to load DC from (from config). Optional current_module: HMODULE of the loader (e.g. proxy
// DLL) for proxy-directory fallback when use_global_version is false.
// When use_global_version is false: (1) game folder (same as .exe) if addon present, (2) global folder.
// When use_global_version is true: global folder only (base, then stable/Debug subdirs).
std::filesystem::path GetDcDirectoryForLoading(void* current_module = nullptr);

// Local folder (%localappdata%\Programs\Display_Commander).
std::filesystem::path GetLocalDcDirectory();

// Process (game) directory only if it contains zzz_display_commander.addon64/.addon32; otherwise empty. For UI "Local
// DC version".
std::filesystem::path GetLocalDcAddonDirectory();
// Full path of the first loaded DC proxy module (e.g. dxgi.dll, winmm.dll), or empty. Only modules that export
// GetDisplayCommanderState are considered (avoids treating system version.dll etc. as DC). Version from this DLL is
// used for "Local Proxy DC version".
std::filesystem::path GetDcProxyModulePath();

// Config get/set (section DisplayCommander.DC). use_global_version: when true, load DC from global folder only.
bool GetUseGlobalDcVersionFromConfig();
void SetUseGlobalDcVersionInConfig(bool use_global);

// Installed DC versions: stable = subdirs of .../Display_Commander/stable/; debug = subdirs of .../Debug/.
const char* const* GetDcInstalledVersionListStable(size_t* out_count);
const char* const* GetDcInstalledVersionListDebug(size_t* out_count);

// Path to zzz_display_commander.addon64 (64-bit) or zzz_display_commander.addon32 (32-bit) in the given directory, or
// empty if not found.
std::filesystem::path GetDcAddonPathInDirectory(const std::filesystem::path& dir);

// Delete local DC addon files (zzz_display_commander.addon64, zzz_display_commander.addon32) from the given directory
// (e.g. game folder). Returns true on success.
bool DeleteLocalDcAddonFromDirectory(const std::filesystem::path& dir, std::string* out_error = nullptr);

}  // namespace display_commander::utils
