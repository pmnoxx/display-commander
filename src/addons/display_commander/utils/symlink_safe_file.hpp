// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Libraries <standard C++>
#include <filesystem>
#include <string>
#include <string_view>

namespace display_commander::utils {

// True when `path` itself is a symbolic link. On symlink_status error, returns false.
bool IsPathSymlink(const std::filesystem::path& path);

// Replace file contents with `text`.
// - Symlink destination: truncate and write in place (no .temp, no rename).
// - Missing or regular file: write `path` + ".temp", then rename over `path`.
// `log_tag` is a short bracket prefix for failure lines (e.g. "Config", "Hotkeys").
bool WriteTextFileReplace(const std::filesystem::path& path, std::string_view text, std::string_view log_tag);

}  // namespace display_commander::utils
