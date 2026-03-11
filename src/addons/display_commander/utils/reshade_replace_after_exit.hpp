#pragma once

#include <filesystem>
#include <string>

namespace display_commander::utils {

// Full path to the DLL module where ReShade is currently loaded (e.g. game dir\dxgi.dll), or empty if not loaded.
// When DC runs as addon (not proxy), this is the ReShade DLL the game loaded; useful to show "loaded as" and as
// replace target.
std::filesystem::path GetReshadeLoadedModulePath();

// Start a background .cmd script that waits for the current process to exit, then copies global ReShade to target
// in a loop until successful. No config is saved; the script runs immediately. Returns true if the script was
// created and started; false on error (out_error set). Optionally returns the script path for debug (out_script_path).
bool StartReplaceWithGlobalAfterExitScript(std::string* out_error = nullptr, std::string* out_script_path = nullptr);

}  // namespace display_commander::utils
