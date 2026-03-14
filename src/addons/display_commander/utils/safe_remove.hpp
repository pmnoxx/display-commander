#pragma once

// Centralized whitelist for directory removal. Use SafeRemoveAll instead of
// std::filesystem::remove_all to avoid accidentally deleting wrong paths.

#include <filesystem>
#include <system_error>

namespace display_commander::utils {

// Returns true only for a path that is .../tmp/<numeric_pid>. Used to avoid
// ever deleting from Display_Commander root or arbitrary paths.
bool IsSafeTempSubdirPath(const std::filesystem::path& dir);

// Returns true if path is an absolute path (e.g. C:\...), not the Display Commander
// AppData root (e.g. ...\AppData\Local\Programs\Display_Commander), and on the whitelist
// for recursive directory removal:
// - .../tmp/<numeric_pid> (post-ReShade addon temp)
// - <GetTempPath()>/dc_reshade_update
// - <GetTempPath()>/dc_reshade_download
// - <GetDownloadDirectory()>/Debug/_staging_latest_debug
bool IsAllowedForRemoveAll(const std::filesystem::path& path);

// If path is an absolute path and allowed (IsAllowedForRemoveAll), removes the
// directory and all contents; otherwise does nothing. Returns true if removal
// was attempted and succeeded (or path did not exist); false if not allowed or
// remove_all failed.
bool SafeRemoveAll(const std::filesystem::path& path, std::error_code& ec);

}  // namespace display_commander::utils
