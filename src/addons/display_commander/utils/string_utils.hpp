// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Libraries <standard C++>
#include <string>
#include <string_view>

namespace display_commander::utils {

std::wstring Utf8ToWide(std::string_view input);
std::string WideToUtf8(std::wstring_view input);
std::string TrimAsciiWhitespace(std::string_view value);

}  // namespace display_commander::utils
