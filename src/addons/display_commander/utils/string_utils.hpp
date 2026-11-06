// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Libraries <standard C++>
#include <filesystem>
#include <string>
#include <string_view>

namespace display_commander::utils {

std::wstring Utf8ToWide(std::string_view input);
std::string WideToUtf8(std::wstring_view input);
std::string TrimAsciiWhitespace(std::string_view value);

// On Windows, std::filesystem::path::string() uses the ANSI code page and can throw for Unicode paths.
// Use these helpers for persisted / logged UTF-8 paths instead.
std::string FilesystemPathToUtf8(const std::filesystem::path& path);
std::filesystem::path Utf8ToFilesystemPath(std::string_view utf8_path);

}  // namespace display_commander::utils
