// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

#include <string>

namespace display_commander::utils {

// Replaces the Windows profile folder name after \Users\ or \Documents and Settings\ (case-insensitive,
// both \ and / separators) with "<user>" so log files do not contain the account directory name.
std::string SanitizeLogUserPaths(std::string message);

}  // namespace display_commander::utils
