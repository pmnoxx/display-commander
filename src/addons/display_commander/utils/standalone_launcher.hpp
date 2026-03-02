// Source Code <Display Commander>
#pragma once

// Group 3 — Standard C++
#include <string>

namespace display_commander::utils {

// Copies or hardlinks the running addon (.addon64/.addon32) to %LocalAppData%\Programs\Display_Commander,
// then launches the Games-only UI via rundll32.exe "<path>,Launcher". When running as the addon (ReShade),
// uses g_hmodule; when running as the standalone exe, uses addon next to exe (zzz_display_commander.addon64/32).
// Returns true on success. On failure, if out_error is non-null, sets *out_error to a short message.
bool TryInstallAddonToAppDataAndLaunchGamesUI(std::string* out_error = nullptr);

}  // namespace display_commander::utils
